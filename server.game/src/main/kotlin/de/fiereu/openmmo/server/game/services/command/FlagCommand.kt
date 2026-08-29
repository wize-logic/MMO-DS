package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.CharacterPermissions
import de.fiereu.openmmo.server.game.services.StoryClientState
import de.fiereu.openmmo.server.game.services.StoryService
import de.fiereu.openmmo.story.generated.kanto.KantoFlags
import de.fiereu.openmmo.story.generated.sinnoh.SinnohFlags
import javax.inject.Inject
import javax.inject.Singleton

/** Read, search and flip one story flag. */
@Singleton
class FlagCommand @Inject constructor(private val story: StoryService) : ChatCommand {
  override val name = "flag"
  override val usage = "/flag <name> [on|off], no state toggles, no name searches"
  override val description = "shows, searches or flips a story flag"
  override val permission = CharacterPermissions.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    val wanted = ctx.args.firstOrNull()
    if (wanted == null) {
      ctx.reply("usage: $usage")
      ctx.reply("e.g. /flag explorer_kit, then /flag <the full name> off")
      return
    }

    val matches = search(wanted)
    val exact =
        matches.firstOrNull { it.equals(wanted, ignoreCase = true) || key(it) == key(wanted) }
    if (matches.isEmpty()) {
      ctx.reply("No flag's name contains '$wanted'.")
      return
    }

    val state = ctx.args.getOrNull(1)?.lowercase()
    if (state == null && exact == null) {
      ctx.reply(
          "${matches.size} flag(s) match '$wanted'${if (matches.size > PAGE) ", first $PAGE" else ""}:")
      matches.take(PAGE).forEach { ctx.reply("  ${mark(ctx, it)} ${short(it)}") }
      if (matches.size > PAGE) ctx.reply("Narrow it down to see the rest.")
      return
    }

    val flag = exact ?: matches.single()
    val had = story.isFlagSet(ctx.characterId, flag)
    val want =
        when (state) {
          null -> !had
          "on",
          "set",
          "1" -> true
          "off",
          "clear",
          "0" -> false
          else -> {
            ctx.reply("usage: $usage")
            return
          }
        }

    if (want) story.setFlag(ctx.characterId, flag) else story.clearFlag(ctx.characterId, flag)
    StoryClientState.flagUpdate(ctx.state.regionId.toByte(), flag, enabled = want)
        ?.let(ctx.session::send)
    ctx.reply(
        "${short(flag)} is ${if (want) "set" else "clear"} (was ${if (had) "set" else "clear"}).")
  }

  private fun mark(ctx: CommandContext, flag: String) =
      if (story.isFlagSet(ctx.characterId, flag)) "[x]" else "[ ]"

  private companion object {
    const val PAGE = 12

    fun key(s: String) = s.filter { it.isLetterOrDigit() }.lowercase()

    /** Without the region prefix, which is the same on every line of a listing. */
    fun short(flag: String) = flag.substringAfter('/')

    /**
     * Every flag key both regions declare. Reflection over the generated objects because each is
     * one const per flag and carries no collection of itself, the same shape /gift reads items out
     * of.
     */
    val ALL: List<String> by lazy {
      listOf(SinnohFlags, KantoFlags).flatMap { obj ->
        obj.javaClass.declaredFields
            .filter { it.type == String::class.java }
            .mapNotNull {
              it.isAccessible = true
              it.get(obj) as? String
            }
      }
    }

    fun search(text: String): List<String> {
      val wanted = key(text)
      return ALL.filter { key(it).contains(wanted) }.sortedBy { it }
    }
  }
}
