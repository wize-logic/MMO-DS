package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.common.dialog.DialogLine
import de.fiereu.openmmo.common.dialog.TextId
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.dialog.generated.sinnoh.SafariGame
import de.fiereu.openmmo.maps.WarpTile
import de.fiereu.openmmo.server.game.services.DialogService
import de.fiereu.openmmo.server.game.services.WarpService
import de.fiereu.openmmo.server.game.session.SCRIPT_SCOPE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.PcRepository
import de.fiereu.openmmo.server.game.storage.PcVisit
import io.github.oshai.kotlinlogging.KotlinLogging
import java.time.LocalDateTime
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

private val log = KotlinLogging.logger {}

/** Travel between PCs. */
@Singleton
class PcCommand
@Inject
constructor(
    private val dialogService: DialogService,
    private val warpService: WarpService,
    private val characterStore: CharacterStore,
    private val pcs: PcRepository,
) : ChatCommand {
  override val name = "pc"
  override val usage = "/pc [here]"
  override val description = "travel to a PC you have logged into before"
  override val role = AccountRole.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    val s = ctx.state
    if (ctx.args.firstOrNull() == "here") {
      pcs.touch(
          PcVisit(
              characterId = ctx.characterId,
              // Masked at the door: a DS map id in the low byte reads negative off a signed Byte.
              region = s.regionId and 0xFF,
              bank = s.bankId and 0xFF,
              map = s.mapId and 0xFF,
              x = s.x.toInt(),
              y = s.y.toInt(),
              elevation = s.elevation,
              lastUsed = LocalDateTime.now(),
          ))
      return
    }
    val book = PcBook.of(pcs.list(ctx.characterId))
    val rows = book.map { LabelLine(TextId.ds(Region.SINNOH, MAP_LABEL_BANK, it.header)) }

    /* Lock and send on the script scope. Do not wait here: this runs on the
     * chat handler, and a wait would park the coroutine that has to read
     * the reply. */
    dialogService.claimLock(ctx.session, s)
    val scope =
        ctx.session.attributes.getOrPut(SCRIPT_SCOPE) {
          CoroutineScope(SupervisorJob() + Dispatchers.Default)
        }
    scope.launch {
      val pick =
          try {
            dialogService.askList(ctx.session, s, SafariGame.QuickTramPrompt, rows, NO_ENTITY)
          } finally {
            dialogService.close(ctx.session, s)
          }
      val to = book.getOrNull(pick) ?: return@launch
      log.info {
        "Character ${ctx.characterId} travels to the PC at ${to.region}:${to.bank}:${to.map}"
      }
      warpService.executeWarp(
          ctx.session,
          ctx.characterId,
          WarpTile(
              x = 0,
              y = 0,
              targetRegionId = to.region.toByte(),
              targetBankId = to.bank.toByte(),
              targetMapId = to.map.toByte(),
              targetX = to.x,
              targetY = to.y,
              targetElevation = to.elevation,
              exitFacing = Direction.UP,
          ),
      )
      characterStore.flushCharacterAsync(ctx.characterId)
    }
  }

  private class LabelLine(override val textId: Int) : DialogLine

  companion object {
    /** The text bank that means "each entry is a map header; name it by its label". */
    const val MAP_LABEL_BANK = 0xFFF
    private const val NO_ENTITY = -1L
  }
}
