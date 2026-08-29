package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.net.game.packets.DialogOptionPacket
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.StatCalculator
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton
import kotlin.math.min

private val log = KotlinLogging.logger {}

@Singleton
class ItemUseService
@Inject
constructor(
    private val characterStore: CharacterStore,
    private val items: ItemRegistry,
    private val species: SpeciesRegistry,
) {

  /**
   * Overworld bag use. The body is item + target monster + a trailer the one Potion capture
   * carries; the trailer is ignored until another sample names it.
   */
  suspend fun onUse(event: PacketEvent<DialogOptionPacket>) {
    val session = event.session
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    val packet = event.packet
    val item = items.get(packet.itemId)
    if (item == null || !item.healsHp) {
      log.info { "char=$charId used item ${packet.itemId}, which this build does not apply" }
      return
    }

    val stored = characterStore.getCharacter(charId) ?: return
    if ((stored.items[packet.itemId] ?: 0) < 1) {
      log.info { "char=$charId used ${item.name} without holding one" }
      return
    }
    val mon = stored.pokemon.firstOrNull { it.id == packet.targetEntityId }
    if (mon == null) {
      log.info { "char=$charId used ${item.name} on ${packet.targetEntityId}, not in the party" }
      return
    }
    val definition = species.get(mon.dexId)
    if (definition == null) {
      log.warn { "char=$charId used ${item.name} on unknown species ${mon.dexId}" }
      return
    }

    val maxHp = StatCalculator.computeAll(definition, mon).hp
    if (mon.hp <= 0 || mon.hp >= maxHp) {
      log.info { "char=$charId used ${item.name} on ${mon.id} at hp ${mon.hp}/$maxHp" }
      return
    }

    val healed = min(maxHp, mon.hp + item.healAmount(maxHp)).toShort()
    if (!characterStore.addItem(charId, packet.itemId, -1)) return
    characterStore.updatePokemon(charId, mon.copy(hp = healed))

    val after = characterStore.getCharacter(charId) ?: return
    log.info { "char=$charId used ${item.name} on ${mon.id}: hp ${mon.hp} -> $healed" }
    session.send(itemStackUpdatePacket(packet.itemId, after.items[packet.itemId] ?: 0))
    session.send(
        PokemonContainerPacket(
            container = PokemonContainer.PARTY,
            hasChange = true,
            delete = false,
            pokemon = after.pokemon,
        ),
    )
  }
}
