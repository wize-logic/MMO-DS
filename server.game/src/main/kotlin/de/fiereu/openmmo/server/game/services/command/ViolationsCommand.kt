package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.server.game.services.ViolationLog
import de.fiereu.openmmo.server.game.storage.CharacterStore
import javax.inject.Inject
import javax.inject.Singleton

/**
 * What the server has refused lately, and who it refused. [ViolationLog] does not act on its own
 * counters, so this is what makes them worth keeping: an operator asks, reads a tally against a
 * name, and decides.
 */
@Singleton
class ViolationsCommand
@Inject
constructor(
    private val violations: ViolationLog,
    private val characters: CharacterStore,
) : ChatCommand {
  override val name = "violations"
  override val usage = "/violations [count]"
  override val description = "what the server has recently refused, and against whom"
  override val role = AccountRole.MODERATOR

  override suspend fun run(ctx: CommandContext) {
    val wanted = ctx.args.firstOrNull()?.toIntOrNull()?.coerceIn(1, 40) ?: 10
    val recent = violations.recent(wanted)
    if (recent.isEmpty()) {
      ctx.reply("Nothing refused since this server started.")
      return
    }
    ctx.reply("${violations.totalRecorded()} refusals since start. The last ${recent.size}:")
    for (entry in recent) {
      val who = entry.characterId?.let { nameOf(it) } ?: "an unnamed session"
      ctx.reply("${entry.kind} $who ${entry.detail}")
    }
    // The tally is the part worth acting on: one refusal is a race, a column of them is a habit.
    val byCharacter = recent.mapNotNull { it.characterId }.distinct()
    for (id in byCharacter) {
      val counts = violations.countsFor(id)
      if (counts.values.sum() < 2) continue
      ctx.reply(
          "${nameOf(id)}: " +
              counts.entries
                  .sortedByDescending { it.value }
                  .joinToString { "${it.key}=${it.value}" })
    }
  }

  private fun nameOf(characterId: Long): String =
      characters.getCharacter(characterId)?.info?.name ?: "char=$characterId"
}
