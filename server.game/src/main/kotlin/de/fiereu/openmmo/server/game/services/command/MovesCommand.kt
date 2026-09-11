package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.MAX_MOVE_SLOTS
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.moves.MoveDef
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.services.PokemonStorageService
import de.fiereu.openmmo.server.game.storage.CharacterStore
import javax.inject.Inject
import javax.inject.Singleton
import kotlin.random.Random

/** Rewrite a party monster's four moves, to fight with. */
@Singleton
class MovesCommand
@Inject
constructor(
    private val species: SpeciesRegistry,
    private val moves: MoveRegistry,
    private val learnsets: LearnsetRegistry,
    private val characterStore: CharacterStore,
    private val storage: PokemonStorageService,
) : ChatCommand {
  override val name = "moves"
  override val usage = "/moves <slot|all> <gen5|move...>"
  override val description = "sets a party monster's moves; gen5 picks moves Platinum never had"
  override val role = AccountRole.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    if (ctx.args.size < 2) {
      ctx.reply("$usage, for example: /moves all gen5, or /moves 1 scald surf")
      return
    }
    val party = ctx.character.pokemon
    val targets: List<Pokemon> =
        if (ctx.args[0].lowercase() == "all") party
        else {
          val slot = ctx.args[0].toIntOrNull()
          if (slot == null || slot !in 1..party.size) {
            ctx.reply("Slot must be 1 to ${party.size}.")
            return
          }
          listOf(party[slot - 1])
        }
    if (targets.isEmpty()) {
      ctx.reply("The party is empty.")
      return
    }
    val gen5 = ctx.args[1].lowercase() == "gen5"
    val named: List<MoveDef> =
        if (gen5) emptyList()
        else {
          val found = ctx.args.drop(1).map { it to find(it) }
          val missing = found.filter { it.second == null }.map { it.first }
          if (missing.isNotEmpty()) {
            ctx.reply("No move called ${missing.joinToString(", ") { "'$it'" }}.")
            return
          }
          found.mapNotNull { it.second }.distinctBy { it.id }.take(MAX_MOVE_SLOTS)
        }

    val lines = mutableListOf<String>()
    for (mon in targets) {
      val chosen = if (gen5) pickGen5(mon) else named
      if (chosen.isEmpty()) {
        ctx.reply("Nothing to teach.")
        return
      }
      val slots =
          List(MAX_MOVE_SLOTS) { i ->
            chosen.getOrNull(i)?.let { PokemonMove(it.id.toShort(), it.pp.toByte()) }
                ?: PokemonMove(0, 0)
          }
      characterStore.updatePokemon(ctx.characterId, mon.copy(moves = slots))
      val who = species.get(mon.dexId)?.name ?: "#${mon.dexId}"
      lines.add("$who: ${chosen.joinToString(", ") { it.name }}")
    }
    storage.resend(ctx.session, ctx.characterId)
    lines.forEach { ctx.reply(it) }
  }

  /** Four of the 92, the species' own first. */
  private fun pickGen5(mon: Pokemon): List<MoveDef> {
    val own =
        learnsets
            .get(mon.dexId)
            .map { it.moveId }
            .filter { it > LAST_PLATINUM_MOVE }
            .distinct()
            .mapNotNull { moves.get(it) }
            .shuffled(rng)
    val def = species.get(mon.dexId)
    val pool = moves.all().filter { it.id > LAST_PLATINUM_MOVE }
    val typed =
        pool
            .filter {
              it.power > 0 && def != null && (it.type == def.type1 || it.type == def.type2)
            }
            .shuffled(rng)
    val rest = pool.shuffled(rng)
    return (own + typed + rest).distinctBy { it.id }.take(MAX_MOVE_SLOTS)
  }

  private fun find(token: String): MoveDef? {
    token.toIntOrNull()?.let {
      return moves.get(it)
    }
    val wanted = normalize(token)
    return moves.all().firstOrNull { normalize(it.name) == wanted }
  }

  private companion object {
    /** Shadow Force: the last move Platinum had a row for; 468 and up are the cartridge's. */
    const val LAST_PLATINUM_MOVE = 467

    val rng = Random.Default

    fun normalize(s: String) = s.filter { it.isLetterOrDigit() }.lowercase()
  }
}
