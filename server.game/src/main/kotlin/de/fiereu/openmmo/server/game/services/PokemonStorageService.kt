package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.MAX_PARTY_SIZE
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.net.game.packets.PokemonMove
import de.fiereu.openmmo.net.game.packets.PokemonMovePacket
import de.fiereu.openmmo.server.game.battle.BattleRegistry
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.Containers
import de.fiereu.openmmo.server.game.storage.DAYCARE_SIZE
import de.fiereu.openmmo.server.game.storage.PC_STORAGE_SIZE
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/**
 * Deposits, withdrawals and reorders, which are one gesture on the wire: a monster is picked up
 * from a slot and put down on another. A destination that is already taken swaps, which is what the
 * client's own paired source/destination arrays describe.
 */
@Singleton
class PokemonStorageService
@Inject
constructor(
    private val characterStore: CharacterStore,
    private val battles: BattleRegistry,
) {

  fun onMove(event: PacketEvent<PokemonMovePacket>) {
    val session = event.session
    val state = session.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val moves = event.packet.moves
    if (moves.isEmpty()) {
      // The client never frames an empty batch, so one here is a bug worth a line in the log,
      // and it returns before the resend below, which would otherwise leave no trace at all.
      log.warn { "char=$charId sent an empty monster move batch" }
      return
    }
    if (battles.byChar(charId) != null) {
      log.warn { "char=$charId tried to move a monster while in battle" }
      resend(session, charId)
      return
    }
    // A settlement is two single-character writes, one after the other, and this is the one
    // gesture that can land between them: the monster the first write just handed over is in
    // the party, and moving it to the box there leaves the second write nothing to give back.
    if (state.atTradeTable) {
      log.warn { "char=$charId tried to move a monster while at a trade table" }
      resend(session, charId)
      return
    }
    val applied =
        characterStore.rearrangeMonsters(charId) { party, pc, daycare ->
          apply(charId, moves, party, pc, daycare)
        }
    // Resend either way: the client has already redrawn the slots it dragged, so a refusal has to
    // be corrected on screen and not only in the log.
    resend(session, charId)
    if (applied) log.info { "char=$charId moved ${moves.size} monster(s) between containers" }
  }

  /** The three rearranged containers, or null if any pair in the batch is one we will not make. */
  private fun apply(
      charId: Long,
      moves: List<PokemonMove>,
      party: List<Pokemon>,
      pc: List<Pokemon>,
      daycare: List<Pokemon>,
  ): Containers? {
    val slots =
        mapOf(
            PokemonContainer.PARTY to
                party.withIndex().associate { (i, m) -> i to m }.toMutableMap(),
            PokemonContainer.PC to pc.associateBy { it.containerSlot.toInt() }.toMutableMap(),
            PokemonContainer.DAYCARE to
                daycare.associateBy { it.containerSlot.toInt() }.toMutableMap(),
        )
    for (move in moves) {
      val from = slots[move.fromContainer]
      val to = slots[move.toContainer]
      if (from == null || to == null) {
        log.warn {
          "char=$charId named ${move.fromContainer}->${move.toContainer}, not addressable"
        }
        return null
      }
      if (!inRange(move.fromContainer, move.fromSlot) || !inRange(move.toContainer, move.toSlot)) {
        log.warn { "char=$charId named slot ${move.fromSlot}->${move.toSlot}, out of range" }
        return null
      }
      val moved = from[move.fromSlot.toInt()]
      if (moved == null) {
        log.warn {
          "char=$charId moved ${move.fromContainer} slot ${move.fromSlot}, which is empty"
        }
        return null
      }
      // The occupant goes back where the moved monster came from, so a drop onto a taken slot is
      // the swap the client drew rather than a monster overwritten and lost.
      val displaced = to[move.toSlot.toInt()]
      from.remove(move.fromSlot.toInt())
      to[move.toSlot.toInt()] = moved
      if (displaced != null) from[move.fromSlot.toInt()] = displaced
    }
    val newParty = slots.getValue(PokemonContainer.PARTY)
    if (newParty.isEmpty()) {
      log.warn { "char=$charId tried to empty its party" }
      return null
    }
    // The party is contiguous: the engine and the game client both draw six slots in order, and a
    // hole in the middle has no representation in either.
    val partyList =
        newParty.entries
            .sortedBy { it.key }
            .mapIndexed { i, e -> e.value.reseat(PokemonContainer.PARTY, i) }
    val pcList =
        slots
            .getValue(PokemonContainer.PC)
            .entries
            .sortedBy { it.key }
            .map { it.value.reseat(PokemonContainer.PC, it.key) }
    // The day care keeps the slots it was given rather than being packed down. Its two slots are
    // the building's own, and the client reports them as the game holds them, including the shift
    // the game makes when the first one empties, which arrives here as an ordinary pair.
    val daycareList =
        slots
            .getValue(PokemonContainer.DAYCARE)
            .entries
            .sortedBy { it.key }
            .map { it.value.reseat(PokemonContainer.DAYCARE, it.key) }
    return Containers(partyList, pcList, daycareList)
  }

  private fun inRange(container: PokemonContainer, slot: Short): Boolean =
      when (container) {
        PokemonContainer.PARTY -> slot in 0 until MAX_PARTY_SIZE
        PokemonContainer.PC -> slot in 0 until PC_STORAGE_SIZE
        PokemonContainer.DAYCARE -> slot in 0 until DAYCARE_SIZE
        else -> false
      }

  private fun Pokemon.reseat(container: PokemonContainer, slot: Int): Pokemon =
      if (this.container == container && containerSlot.toInt() == slot) this
      else copy(container = container, containerSlot = slot.toShort())

  /** Send the three containers back as they now stand, so what the client shows is the server's. */
  fun resend(session: SessionContext, charId: Long) {
    val stored = characterStore.getCharacter(charId) ?: return
    session.sendContainer(PokemonContainer.PARTY, stored.pokemon)
    session.sendContainer(PokemonContainer.DAYCARE, stored.daycare)
    session.sendContainer(PokemonContainer.PC, stored.pcStorage)
  }
}
