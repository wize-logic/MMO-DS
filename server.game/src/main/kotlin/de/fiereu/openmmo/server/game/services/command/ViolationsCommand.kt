package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.server.game.services.ViolationLog
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.ViolationRepository
import javax.inject.Inject
import javax.inject.Singleton

/** What the server has refused lately, and who it refused. */
@Singleton
class ViolationsCommand
@Inject
constructor(
    private val violations: ViolationLog,
    private val durable: ViolationRepository,
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
    // And the part the ring cannot answer. Everything above turns over in ten minutes and was
    // empty at the last restart; this is the same refusals counted since the account first made
    // one, which is the question anybody asking about a player is actually asking.
    val heaviest = durable.worst(wanted)
    if (heaviest.isEmpty()) return
    ctx.reply("Since the beginning, heaviest first:")
    for (row in heaviest) {
      ctx.reply("${nameOf(row.characterId)} ${row.kind} x${row.total}, last: ${row.lastDetail}")
    }
  }

  private fun nameOf(characterId: Long): String =
      characters.getCharacter(characterId)?.info?.name ?: "char=$characterId"
}
