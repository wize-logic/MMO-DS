package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.ImportRecord
import de.fiereu.openmmo.server.game.storage.ImportRepository
import java.time.format.DateTimeFormatter
import javax.inject.Inject
import javax.inject.Singleton

/** What a character has brought in from a save file, and what the door made of it. */
@Singleton
class ImportsCommand
@Inject
constructor(
    private val imports: ImportRepository,
    private val characters: CharacterStore,
) : ChatCommand {
  override val name = "imports"
  override val usage = "/imports <name> [count]"
  override val description = "the saves a character has brought online, newest first"
  override val role = AccountRole.MODERATOR

  override suspend fun run(ctx: CommandContext) {
    val who = ctx.args.firstOrNull()
    if (who == null) {
      ctx.reply("Usage: $usage")
      return
    }
    val wanted = ctx.args.getOrNull(1)?.toIntOrNull()?.coerceIn(1, 20) ?: 5
    val found = characters.findIdByName(who)
    if (found == null) {
      ctx.reply("Nobody here is called $who.")
      return
    }
    val (characterId, spelling) = found
    val rows = imports.listFor(characterId, wanted)
    if (rows.isEmpty()) {
      ctx.reply("$spelling has never brought a save online.")
      return
    }
    ctx.reply("$spelling, newest first:")
    for (row in rows) ctx.reply(line(row))
  }

  private fun line(row: ImportRecord): String {
    val at = STAMP.format(row.importedAt)
    val money =
        if (row.moneyGained > 0) "+${row.moneyGained}" else "${row.moneyAfter - row.moneyBefore}"
    val state =
        when {
          row.undone -> " ROLLED BACK by ${row.rolledBackBy}"
          row.sealedAt != null -> " sealed (${row.sealedReason})"
          else -> ""
        }
    val clamps = row.verdicts.count { it.contains(" brought to ") }
    val drops = row.verdicts.count { it.contains(" dropped: ") }
    val replay =
        row.replayVerdict?.let { verdict ->
          ", replay $verdict" + (row.replayFrame?.let { " at frame $it" } ?: "")
        } ?: ""
    return "$at ${row.partyCount}+${row.boxCount} mons, ${row.speciesCount} species," +
        " levels ${row.levelTotal} (top ${row.levelMax}), money $money," +
        " ${row.badgesBefore}->${row.badgesAfter} badges, played ${row.playTimeSeconds / 3600}h," +
        " $clamps clamped, $drops dropped, client ${row.clientRevision}$replay$state, id ${row.id}"
  }

  private companion object {
    val STAMP: DateTimeFormatter = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm")
  }
}
