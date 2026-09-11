package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.server.game.offline.OfflineImportService
import de.fiereu.openmmo.server.game.offline.UndoOutcome
import javax.inject.Inject
import javax.inject.Singleton

/** The player's own undo of their last save import. */
@Singleton
class UndoImportCommand @Inject constructor(private val service: OfflineImportService) :
    ChatCommand {
  override val name = "undo-import"
  override val usage = "/undo-import"
  override val description = "put your character back as it was before your last save import"

  override suspend fun run(ctx: CommandContext) {
    when (val outcome = service.undoLatest(ctx.characterId)) {
      is UndoOutcome.Restored -> {
        ctx.reply(
            "Put back as you were before that save landed: " +
                "${outcome.record.partyCount}+${outcome.record.boxCount} imported monsters gone," +
                " money back to ${outcome.record.moneyBefore}. Your save file is untouched.")
        // The window is still drawing the imported character. A fresh join is the only thing
        // that reads the restored one back, so the session ends and the client rejoins.
        service.endSessionOf(
            ctx.characterId,
            "Your character is back as it was. Rejoining so the game shows it.",
            "import ${outcome.record.id} undone by the player")
      }
      is UndoOutcome.Refused -> ctx.reply(outcome.why.replaceFirstChar { it.uppercase() } + ".")
    }
  }
}
