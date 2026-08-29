package de.fiereu.openmmo.server.game.services

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.decompressIVs
import de.fiereu.openmmo.items.ItemDef
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket
import de.fiereu.openmmo.net.game.packets.SocialListEntryAddPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleAddPokemon
import de.fiereu.openmmo.net.game.packets.battle.BattleSideAddPokemonPacket
import de.fiereu.openmmo.net.game.packets.battle.ItemStack
import de.fiereu.openmmo.net.game.packets.battle.itemStacksPacket
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.BattleRng
import de.fiereu.openmmo.server.game.battle.StatCalculator
import de.fiereu.openmmo.server.game.battle.WildMonFactory
import de.fiereu.openmmo.server.game.battle.acquiredMonsterDelta
import de.fiereu.openmmo.server.game.script.Pokedex
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.storage.CharacterStore
import javax.inject.Inject
import javax.inject.Singleton

/** Party, healing, and bag operations used by ROM-derived overworld scripts. */
@Singleton
class StoryPlayerService
@Inject
constructor(
    private val characters: CharacterStore,
    private val pokemonFactory: WildMonFactory,
    private val species: SpeciesRegistry,
    private val moves: MoveRegistry,
    private val items: ItemRegistry,
) {

  /**
   * Where a grant lands and what it looks like when it arrives. The decomp passes these to
   * `GiveMon` one at a time, but every scripted grant bar one wants the defaults, so they
   * travel together and the call sites that only name a species and a level stay one line.
   */
  data class Grant(
      val nickname: String = "",
      val hp: Int = -1,
      val container: PokemonContainer = PokemonContainer.PARTY,
      val slot: Int = -1,
      /**
       * The monster the client already holds, when the grant is a record of one rather than a
       * request for one. Null where the server is the one rolling it.
       */
      val individual: Individual? = null,
  )

  /**
   * An individual the engine rolled: its personality value, which is what a nature is read out
   * of, its six IVs packed five bits each, and whether it came out shiny.
   */
  data class Individual(val seed: Int, val ivBits: Int, val isShiny: Boolean)

  /** Gives a story Pokemon and syncs it. */
  suspend fun givePokemon(
      session: SessionContext,
      state: PlayerState,
      dexId: Int,
      level: Int,
      moveIds: List<Int>,
      grant: Grant = Grant(),
  ): Pokemon? {
    val (nickname, hp, container, slot) = grant
    val characterId = state.characterId ?: return null
    val stored = characters.getCharacter(characterId) ?: return null
    val rolled = pokemonFactory.create(dexId, level, BattleRng()) ?: return null
    val offered =
        rolled.copy(
            ownerId = characterId,
            container = container,
            ot = stored.info.name,
            moves = paddedMoves(moveIds),
            nickname = nickname.ifBlank { rolled.nickname },
            // The factory's hp is the rolled maximum; a capture keeps its battle damage.
            hp = if (hp in 1..rolled.hp.toInt()) hp.toShort() else rolled.hp,
            // A reported individual is the monster that exists; the roll above only stands in for
            // the fields it did not carry.
            seed = grant.individual?.seed ?: rolled.seed,
            iVs = grant.individual?.let { decompressIVs(it.ivBits) } ?: rolled.iVs,
            isShiny = grant.individual?.isShiny ?: rolled.isShiny,
            // Where the trainer is standing. A native catch reaches the server through this door
            // (the client reports the monster the ball caught), and so does a scripted gift, and
            // both were met in the Mystery Zone until the record had somewhere to say otherwise.
            caughtRegionId = (stored.info.positionRegionId.toInt() and 0xFF),
            caughtBankId = (stored.info.positionBankId.toInt() and 0xFF),
            caughtMapId = (stored.info.positionMapId.toInt() and 0xFF),
        )
    // Only tell the client about it once the database has it. Where it sits is the store's answer:
    // it takes the free slot inside the same update that writes the monster, so two grants that
    // arrive together cannot both be told the same one is free.
    val pokemon = characters.addPokemon(characterId, offered, preferredSlot = slot) ?: return null
    Pokedex.markCaught(characters, session, characterId, dexId)
    // Send the granted Pokemon's full record.
    session.send(SocialListEntryAddPacket(pokemon))
    species.get(dexId)?.let { session.send(acquiredMonsterDelta(pokemon, it)) }
    val fresh = characters.getCharacter(characterId)
    session.send(
        PokemonContainerPacket(
            container = container,
            hasChange = true,
            delete = false,
            pokemon =
                (if (container == PokemonContainer.PC) fresh?.pcStorage else fresh?.pokemon)
                    ?: listOf(pokemon),
        ))
    return pokemon
  }

  /**
   * The decomp's `GiveEgg`: the same grant as [givePokemon] with the egg bit set, which is the
   * one field that separates the two on the wire (`PokemonCodec`'s `eggBits`). It hatches into
   * a level 1 of its species, so that is the level it is rolled at.
   */
  suspend fun giveEgg(session: SessionContext, state: PlayerState, dexId: Int): Pokemon? {
    val characterId = state.characterId ?: return null
    val stored = characters.getCharacter(characterId) ?: return null
    val rolled = pokemonFactory.create(dexId, EGG_HATCH_LEVEL, BattleRng()) ?: return null
    val offered =
        rolled.copy(
            ownerId = characterId,
            container = PokemonContainer.PARTY,
            ot = stored.info.name,
            isEgg = true,
        )
    val egg = characters.addPokemon(characterId, offered) ?: return null
    session.send(SocialListEntryAddPacket(egg))
    session.send(
        PokemonContainerPacket(
            container = PokemonContainer.PARTY,
            hasChange = true,
            delete = false,
            pokemon = characters.getCharacter(characterId)?.pokemon ?: listOf(egg),
        ))
    return egg
  }

  fun healParty(session: SessionContext, state: PlayerState) {
    val characterId = state.characterId ?: return
    val stored = characters.getCharacter(characterId) ?: return
    val healed =
        stored.pokemon.map { pokemon ->
          val definition = species.get(pokemon.dexId) ?: return@map pokemon
          pokemon.copy(
              hp = StatCalculator.computeAll(definition, pokemon).hp.toShort(),
              moves =
                  pokemon.moves.map { move ->
                    val maxPp = moves.get(move.id.toInt())?.pp ?: move.pp.toInt()
                    PokemonMove(move.id, maxPp.toByte())
                  },
          )
        }
    healed.forEach { characters.updatePokemon(characterId, it) }
    session.send(
        PokemonContainerPacket(
            container = PokemonContainer.PARTY,
            hasChange = true,
            delete = false,
            pokemon = healed,
        ))
  }

  /** The decomp's `CheckItem`: whether the bag holds at least [quantity] of [item]. */
  fun hasItem(state: PlayerState, item: ItemDef, quantity: Int): Boolean {
    val characterId = state.characterId ?: return false
    val bag: Map<Int, Int> = characters.getCharacter(characterId)?.items ?: return false
    return (bag[items.idOf(item)] ?: 0) >= quantity
  }

  /** The decomp's `GetPartyCount`, which several scenes branch their words on. */
  fun partyCount(state: PlayerState): Int {
    val characterId = state.characterId ?: return 0
    return characters.getCharacter(characterId)?.pokemon?.size ?: 0
  }

  /** The decomp's `SetPartyGiratinaForm`. [form] 1 is Origin, 0 is Altered. */
  fun setPartyGiratinaForm(session: SessionContext, state: PlayerState, form: Int) {
    val characterId = state.characterId ?: return
    val stored = characters.getCharacter(characterId) ?: return
    val updated =
        stored.pokemon.map { mon ->
          if (!mon.isEgg && mon.dexId == SPECIES_GIRATINA) mon.copy(form = form) else mon
        }
    updated.forEach { characters.updatePokemon(characterId, it) }
    session.send(
        PokemonContainerPacket(
            container = PokemonContainer.PARTY,
            hasChange = true,
            delete = false,
            pokemon = updated,
        ))
  }

  suspend fun giveItem(
      session: SessionContext,
      state: PlayerState,
      item: ItemDef,
      quantity: Int
  ): Boolean {
    val characterId = state.characterId ?: return false
    if (!characters.addItem(characterId, items.idOf(item), quantity)) return false
    val bag: Map<Int, Int> = characters.getCharacter(characterId)?.items ?: return false
    session.send(storyItemStacksPacket(bag))
    return true
  }

  private fun paddedMoves(moveIds: List<Int>): List<PokemonMove> =
      moveIds.take(MAX_MOVES).map { id ->
        PokemonMove(id.toShort(), (moves.get(id)?.pp ?: 0).toByte())
      } + List((MAX_MOVES - moveIds.size).coerceAtLeast(0)) { PokemonMove(0, 0) }

  private companion object {
    const val MAX_MOVES = 4
    const val EGG_HATCH_LEVEL = 1
    const val SPECIES_GIRATINA = 487
  }
}

/** Builds a stable full bag snapshot. */
fun storyItemStacksPacket(items: Map<Int, Int>) =
    itemStacksPacket(
        items.entries
            .sortedBy { it.key }
            .map { (itemId, quantity) ->
              ItemStack(
                  objectId = (itemId.toLong() shl 16) or ITEM_ENTITY_TAG,
                  itemId = itemId.toShort(),
                  quantity = quantity.toShort(),
              )
            })

/**
 * A bag stack, not a monster. The open shop window only refreshes its count when the update arrives
 * as this single stack rather than as a whole new bag.
 */
fun itemStackUpdatePacket(itemId: Int, quantity: Int) =
    BattleSideAddPokemonPacket(
        side = 1,
        pokemon =
            BattleAddPokemon(
                entityId = (itemId.toLong() shl 16) or ITEM_ENTITY_TAG,
                frontSpriteId = itemId.toShort(),
                backSpriteId = quantity.toShort(),
                side = 1,
                slot = 0,
                partyIndex = -1,
                statusEffect = null,
            ),
    )

private const val ITEM_ENTITY_TAG = 0x5000L
