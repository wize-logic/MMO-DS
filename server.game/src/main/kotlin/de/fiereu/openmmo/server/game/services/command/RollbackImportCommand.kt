package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.server.game.offline.OfflineImportService
import de.fiereu.openmmo.server.game.offline.UndoOutcome
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.ImportRepository
import javax.inject.Inject
import javax.inject.Singleton

/** Puts a character back the way it was before a save landed on it. */
@Singleton
class RollbackImportCommand
@Inject
constructor(
    private val service: OfflineImportService,
    private val imports: ImportRepository,
    private val characters: CharacterStore,
) : ChatCommand {
  override val name = "rollback-import"
  override val usage = "/rollback-import <name> [id]"
  override val description = "undo a save import, putting the character back as it was"
  override val role = AccountRole.MODERATOR

  override suspend fun run(ctx: CommandContext) {
    val who = ctx.args.firstOrNull()
    if (who == null) {
      ctx.reply("Usage: $usage")
      return
    }
    val found = characters.findIdByName(who)
    if (found == null) {
      ctx.reply("Nobody here is called $who.")
      return
    }
    val (characterId, spelling) = found
    val asked = ctx.args.getOrNull(1)?.toLongOrNull()
    val target =
        if (asked != null) imports.find(asked)
        else imports.listFor(characterId, 20).firstOrNull { !it.undone }
    if (target == null) {
      ctx.reply("$spelling has no import to undo. Run /imports $spelling to see the list.")
      return
    }
    if (target.characterId != characterId) {
      ctx.reply("Import ${target.id} is not $spelling's.")
      return
    }
    if (target.sealedAt != null) {
      ctx.reply(
          "Warning: ${target.sealedReason} since that import, so what goes back may be a second" +
              " copy of something somebody else now holds.")
    }
    when (val outcome = service.rollBack(target.id, ctx.character.info.name)) {
      is UndoOutcome.Restored -> {
        ctx.reply(
            "$spelling is back as it was before import ${outcome.record.id}: " +
                "${outcome.record.partyCount}+${outcome.record.boxCount} imported monsters gone," +
                " money back to ${outcome.record.moneyBefore}.")
        // If they are playing right now, their window is still the imported character's; a fresh
        // join is what reads the restored one back.
        service.endSessionOf(
            characterId,
            "A moderator put your character back as it was before your save import. Rejoining" +
                " so the game shows it.",
            "import ${outcome.record.id} rolled back by ${ctx.character.info.name}")
      }
      is UndoOutcome.Refused -> ctx.reply("Not undone: ${outcome.why}")
    }
  }
}
