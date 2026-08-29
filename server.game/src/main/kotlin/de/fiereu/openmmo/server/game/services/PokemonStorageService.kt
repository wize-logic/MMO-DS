package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.MAX_PARTY_SIZE
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket
import de.fiereu.openmmo.net.game.packets.PokemonMove
import de.fiereu.openmmo.net.game.packets.PokemonMovePacket
import de.fiereu.openmmo.server.game.battle.BattleRegistry
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.PC_STORAGE_SIZE
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** One record per packet is capped at a byte, so a full PC crosses in several. */
private const val RECORDS_PER_PACKET = 255

/**
 * Deposits, withdrawals and reorders, which are one gesture on the wire: a monster is picked
 * up from a slot and put down on another. A destination that is already taken swaps, which is
 * what the client's own paired source/destination arrays describe.
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
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
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
    val applied =
        characterStore.rearrangeMonsters(charId) { party, pc -> apply(charId, moves, party, pc) }
    // Resend either way: the client has already redrawn the slots it dragged, so a refusal has to
    // be corrected on screen and not only in the log.
    resend(session, charId)
    if (applied) log.info { "char=$charId moved ${moves.size} monster(s) between containers" }
  }

  /** The rearranged party and PC, or null if any pair in the batch is one we will not make. */
  private fun apply(
      charId: Long,
      moves: List<PokemonMove>,
      party: List<Pokemon>,
      pc: List<Pokemon>,
  ): Pair<List<Pokemon>, List<Pokemon>>? {
    val slots =
        mapOf(
            PokemonContainer.PARTY to
                party.withIndex().associate { (i, m) -> i to m }.toMutableMap(),
            PokemonContainer.PC to pc.associateBy { it.containerSlot.toInt() }.toMutableMap(),
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
    return partyList to pcList
  }

  private fun inRange(container: PokemonContainer, slot: Short): Boolean =
      when (container) {
        PokemonContainer.PARTY -> slot in 0 until MAX_PARTY_SIZE
        PokemonContainer.PC -> slot in 0 until PC_STORAGE_SIZE
        else -> false
      }

  private fun Pokemon.reseat(container: PokemonContainer, slot: Int): Pokemon =
      if (this.container == container && containerSlot.toInt() == slot) this
      else copy(container = container, containerSlot = slot.toShort())

  /** Send the two containers back as they now stand, so what the client shows is the server's. */
  private fun resend(session: SessionContext, charId: Long) {
    val stored = characterStore.getCharacter(charId) ?: return
    session.send(
        PokemonContainerPacket(
            container = PokemonContainer.PARTY,
            hasChange = true,
            delete = false,
            pokemon = stored.pokemon,
        ))
    // The first packet replaces the container, the rest merge into it by monster id, which is how
    // the client applies a container that does not fit in one.
    val pc = stored.pcStorage.chunked(RECORDS_PER_PACKET).ifEmpty { listOf(emptyList()) }
    pc.forEachIndexed { i, chunk ->
      session.send(
          PokemonContainerPacket(
              container = PokemonContainer.PC,
              hasChange = i == 0,
              delete = false,
              pokemon = chunk,
          ))
    }
  }
}
