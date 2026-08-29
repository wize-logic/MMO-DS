package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.CharacterPermissions
import de.fiereu.openmmo.dialog.generated.sinnoh.JubilifeCity
import de.fiereu.openmmo.dialog.generated.sinnoh.MenuEntries
import de.fiereu.openmmo.server.game.services.DialogService
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class ListCommand @Inject constructor(private val dialogService: DialogService) : ChatCommand {
  override val name = "list"
  override val usage = "/list"
  override val description = "shows Jubilife's type-preference list"
  override val permission = CharacterPermissions.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    /* Lock and send. Do not wait here: this runs on the chat handler,
     * and a wait would park the coroutine that has to read the reply. */
    dialogService.claimLock(ctx.session, ctx.state)
    dialogService.showList(
        ctx.session,
        ctx.state,
        JubilifeCity.CanYouTellTypePreference,
        listOf(
            MenuEntries.TypePreferenceFire,
            MenuEntries.TypePreferenceWater,
            MenuEntries.TypePreferenceGrass,
        ),
        NO_ENTITY,
    )
  }

  private companion object {
    const val NO_ENTITY = -1L
  }
}
