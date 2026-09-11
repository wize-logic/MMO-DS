package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.MAX_PARTY_SIZE
import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.pokemon.SpeciesDef
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.BattleRng
import de.fiereu.openmmo.server.game.battle.WildMonFactory
import de.fiereu.openmmo.server.game.services.PokemonStorageService
import de.fiereu.openmmo.server.game.storage.CharacterStore
import javax.inject.Inject
import javax.inject.Singleton
import kotlin.random.Random

/** Put monsters in the party, to fight with. */
@Singleton
class PartyCommand
@Inject
constructor(
    private val species: SpeciesRegistry,
    private val factory: WildMonFactory,
    private val characterStore: CharacterStore,
    private val storage: PokemonStorageService,
) : ChatCommand {
  override val name = "party"
  override val usage = "/party <species|random> [count] [level]"
  override val description = "puts a monster in the party; random picks Gen 5 species"
  override val role = AccountRole.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    if (ctx.args.isEmpty()) {
      ctx.reply("$usage, for example: /party random 6 50, or /party snivy 25")
      return
    }
    val wanted = ctx.args[0]
    val random = normalize(wanted) == "random"
    val numbers = ctx.args.drop(1).mapNotNull { it.toIntOrNull() }
    val count = if (random) (numbers.getOrNull(0) ?: 1).coerceIn(1, MAX_PARTY_SIZE) else 1
    val level =
        (if (random) numbers.getOrNull(1) else numbers.getOrNull(0) ?: DEFAULT_LEVEL)?.coerceIn(
            1, MAX_LEVEL) ?: DEFAULT_LEVEL

    val picks: List<SpeciesDef> =
        if (random) {
          val pool = species.all().filter { it.id > LAST_SPECIES_THE_ENGINE_SHIPPED }
          if (pool.isEmpty()) {
            ctx.reply("This server has no Gen 5 species to pick from.")
            return
          }
          List(count) { pool[rng.nextInt(pool.size)] }
        } else {
          val def = find(wanted)
          if (def == null) {
            ctx.reply(
                "No species called '$wanted'. Try a name (pikachu, victini), a dex number, or random.")
            return
          }
          listOf(def)
        }

    val seatedNames = mutableListOf<String>()
    for (def in picks) {
      val rolled = factory.create(def.id, level, BattleRng())
      if (rolled == null) {
        ctx.reply("Could not roll a ${def.name}.")
        break
      }
      val seated =
          characterStore.addPokemon(
              ctx.characterId,
              rolled.copy(container = PokemonContainer.PARTY, ot = ctx.character.info.name),
          )
      if (seated == null) {
        ctx.reply(
            if (seatedNames.isEmpty()) "The party is full."
            else "The party filled up after ${seatedNames.size}.")
        break
      }
      seatedNames.add("${def.name} (slot ${seated.containerSlot + 1})")
    }
    if (seatedNames.isEmpty()) return
    storage.resend(ctx.session, ctx.characterId)
    ctx.reply("Put level $level in the party: ${seatedNames.joinToString(", ")}.")
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

    /** Arceus; every species past it came out of a Gen 5 cartridge (see BoxCommand). */
    const val LAST_SPECIES_THE_ENGINE_SHIPPED = 493

    val rng = Random.Default

    fun normalize(s: String) = s.filter { it.isLetterOrDigit() }.lowercase()
  }
}
