package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.common.dialog.TextId
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.server.game.services.DialogService
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class DialogCommand @Inject constructor(private val dialogService: DialogService) : ChatCommand {
  override val name = "dialog"
  override val usage = "/dialog"
  override val description = "shows the Twinleaf town sign"
  override val role = AccountRole.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    /* Lock and send. Do not wait here: this runs on the chat handler,
     * and a wait would park the coroutine that has to read the reply. */
    dialogService.claimLock(ctx.session, ctx.state)
    dialogService.show(
        ctx.session,
        ctx.state,
        textId = TextId.ds(Region.SINNOH, TWINLEAF_TOWN_BANK, MAP_SIGN_ENTRY),
        actionType = SIGN,
        entityId = NO_ENTITY,
    )
  }

  private companion object {
    const val TWINLEAF_TOWN_BANK = 554
    const val MAP_SIGN_ENTRY = 12
    const val SIGN = 3
    const val NO_ENTITY = -1L
  }
}
