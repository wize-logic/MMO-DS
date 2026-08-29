package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.CharacterPermissions
import de.fiereu.openmmo.server.game.services.DialogPresentation
import de.fiereu.openmmo.server.game.services.DialogService
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class MenuCommand @Inject constructor(private val dialogService: DialogService) : ChatCommand {
  override val name = "menu"
  override val usage = "/menu"
  override val description = "shows a Sinnoh starter species menu"
  override val permission = CharacterPermissions.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    /* Lock and send. Do not wait here: this runs on the chat handler,
     * and a wait would park the coroutine that has to read the reply. */
    dialogService.claimLock(ctx.session, ctx.state)
    dialogService.show(
        ctx.session,
        ctx.state,
        textId = 0,
        actionType = SPECIES_MENU,
        entityId = NO_ENTITY,
        presentation =
            DialogPresentation(
                detail =
                    byteArrayOf(
                        3,
                        (TURTWIG and 0xFF).toByte(),
                        (TURTWIG shr 8).toByte(),
                        (CHIMCHAR and 0xFF).toByte(),
                        (CHIMCHAR shr 8).toByte(),
                        (PIPLUP and 0xFF).toByte(),
                        (PIPLUP shr 8).toByte(),
                    ),
            ),
    )
  }

  private companion object {
    const val SPECIES_MENU = 0x23
    const val NO_ENTITY = -1L
    const val TURTWIG = 387
    const val CHIMCHAR = 390
    const val PIPLUP = 393
  }
}
