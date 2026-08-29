package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.server.game.script.MovementStep
import de.fiereu.openmmo.server.game.services.DialogService
import de.fiereu.openmmo.server.game.services.ScriptMovementService
import de.fiereu.openmmo.server.game.session.SCRIPT_SCOPE
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

@Singleton
class MoveCommand
@Inject
constructor(
    private val dialogService: DialogService,
    private val movement: ScriptMovementService,
) : ChatCommand {
  override val name = "move"
  override val usage = "/move"
  override val description = "turns the avatar with a scripted facing sequence"
  override val role = AccountRole.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    /* Lock and send on the script scope. Do not wait here: this runs
     * on the chat handler. Faces only, so the committed tile does not
     * move if a wall would have blocked a walk. */
    dialogService.claimLock(ctx.session, ctx.state)
    val scope =
        ctx.session.attributes.getOrPut(SCRIPT_SCOPE) {
          CoroutineScope(SupervisorJob() + Dispatchers.Default)
        }
    scope.launch {
      try {
        movement.moveSelf(
            ctx.session,
            ctx.state,
            listOf(MovementStep.FACE_RIGHT, MovementStep.FACE_DOWN),
        )
      } finally {
        dialogService.close(ctx.session, ctx.state)
      }
    }
  }
}
