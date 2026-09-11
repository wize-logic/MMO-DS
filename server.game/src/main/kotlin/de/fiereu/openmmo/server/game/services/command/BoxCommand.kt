package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.pokemon.SpeciesDef
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.BattleRng
import de.fiereu.openmmo.server.game.battle.WildMonFactory
import de.fiereu.openmmo.server.game.services.PokemonStorageService
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.PC_BOX_COUNT
import de.fiereu.openmmo.server.game.storage.PC_BOX_SIZE
import javax.inject.Inject
import javax.inject.Singleton

/** Put one monster in one named PC slot, to look at. */
@Singleton
class BoxCommand
@Inject
constructor(
    private val species: SpeciesRegistry,
    private val factory: WildMonFactory,
    private val characterStore: CharacterStore,
    private val storage: PokemonStorageService,
) : ChatCommand {
  override val name = "box"
  override val usage = "/box <box> <slot> <species> [level]"
  override val description = "puts a monster in a PC slot, by box and slot counted from one"
  override val role = AccountRole.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    if (ctx.args.size < 3) {
      ctx.reply("$usage, for example: /box 1 1 victini 50")
      return
    }
    val box = ctx.args[0].toIntOrNull()
    val position = ctx.args[1].toIntOrNull()
    if (box == null || box !in 1..PC_BOX_COUNT) {
      ctx.reply("Box must be 1 to $PC_BOX_COUNT.")
      return
    }
    if (position == null || position !in 1..PC_BOX_SIZE) {
      ctx.reply("Slot must be 1 to $PC_BOX_SIZE.")
      return
    }
    val wanted = ctx.args[2]
    val def = find(wanted)
    if (def == null) {
      ctx.reply("No species called '$wanted'. Try a name (pikachu, victini) or a dex number.")
      return
    }
    val level = (ctx.args.getOrNull(3)?.toIntOrNull() ?: DEFAULT_LEVEL).coerceIn(1, MAX_LEVEL)

    val asked = (box - 1) * PC_BOX_SIZE + (position - 1)
    val rolled = factory.create(def.id, level, BattleRng())
    if (rolled == null) {
      ctx.reply("Could not roll a ${def.name}.")
      return
    }
    val seated =
        characterStore.addPokemon(
            ctx.characterId,
            rolled.copy(container = PokemonContainer.PC, ot = ctx.character.info.name),
            preferredSlot = asked,
        )
    if (seated == null) {
      ctx.reply("The PC is full.")
      return
    }
    storage.resend(ctx.session, ctx.characterId)

    val slot = seated.containerSlot.toInt()
    val where = "box ${slot / PC_BOX_SIZE + 1} slot ${slot % PC_BOX_SIZE + 1}"
    val moved = if (slot == asked) "" else " (box $box slot $position was taken)"
    val unseen =
        if (def.id > LAST_SPECIES_THE_ENGINE_SHIPPED)
            " That one came out of a Gen 5 cartridge: it needs one imported to have a name, an" +
                " icon and a battle sprite."
        else ""
    ctx.reply("Put a level $level ${def.name} in $where.$moved$unseen")
  }

  /** By dex number, or by name with the spacing and punctuation a person would not type. */
  private fun find(token: String): SpeciesDef? {
    token.toIntOrNull()?.let {
      return species.get(it)
    }
    val wanted = normalize(token)
    return species.all().firstOrNull { normalize(it.name) == wanted }
  }

  private companion object {
    const val DEFAULT_LEVEL = 50
    const val MAX_LEVEL = 100

    /**
     * Arceus, the last species the engine this client renders with shipped any data for. Past it
     * every archive the client indexes by species is one a cartridge import has to fill, so this is
     * what the reply above warns about.
     */
    const val LAST_SPECIES_THE_ENGINE_SHIPPED = 493

    fun normalize(s: String) = s.filter { it.isLetterOrDigit() }.lowercase()
  }
}
