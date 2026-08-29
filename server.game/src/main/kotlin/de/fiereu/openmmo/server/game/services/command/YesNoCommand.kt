package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.CharacterPermissions
import de.fiereu.openmmo.common.dialog.TextId
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.server.game.services.DialogPresentation
import de.fiereu.openmmo.server.game.services.DialogService
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class YesNoCommand @Inject constructor(private val dialogService: DialogService) : ChatCommand {
  override val name = "yesno"
  override val usage = "/yesno"
  override val description = "shows the Pokémon Center rest yes/no"
  override val permission = CharacterPermissions.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    /* Lock and send. Do not wait here: this runs on the chat handler,
     * and a wait would park the coroutine that has to read the reply. */
    dialogService.claimLock(ctx.session, ctx.state)
    dialogService.show(
        ctx.session,
        ctx.state,
        textId = TextId.ds(Region.SINNOH, COMMON_STRINGS_BANK, REST_QUESTION_ENTRY),
        actionType = YES_NO,
        entityId = NO_ENTITY,
        presentation = DialogPresentation(detail = ByteArray(0)),
    )
  }

  private companion object {
    const val COMMON_STRINGS_BANK = 213
    const val REST_QUESTION_ENTRY = 0
    const val YES_NO = 0x05
    const val NO_ENTITY = -1L
  }
}
