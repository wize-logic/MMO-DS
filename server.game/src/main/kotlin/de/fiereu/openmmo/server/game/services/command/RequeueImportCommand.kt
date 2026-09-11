package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.server.game.offline.verify.ReplayVerdict
import de.fiereu.openmmo.server.game.offline.verify.ReplayVerificationService
import de.fiereu.openmmo.server.game.offline.verify.RequeueOutcome
import de.fiereu.openmmo.server.game.storage.ChainRepository
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.RequestRepository
import de.fiereu.openmmo.server.game.storage.StoredRequest
import javax.inject.Inject
import javax.inject.Singleton

/** Runs a settled check of the play behind a save import again, once. */
@Singleton
class RequeueImportCommand
@Inject
constructor(
    private val service: ReplayVerificationService,
    private val requests: RequestRepository,
    private val chains: ChainRepository,
    private val characters: CharacterStore,
) : ChatCommand {
  override val name = "requeue-import"
  override val usage = "/requeue-import <name> [id]"
  override val description = "run a check of the play behind a save import again, once"
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
    val rows = requests.listFor(characterId, LOOK_BACK)
    val target =
        if (asked == null) rows.firstOrNull { it.canGoBack() }
        else {
          val chainOfImport =
              chains.listFor(characterId, LOOK_BACK).firstOrNull { it.importId == asked }
          rows.firstOrNull { it.id == asked }
              ?: rows.firstOrNull {
                (it.chainId == asked || it.chainId == chainOfImport?.id) && it.canGoBack()
              }
        }
    if (target == null) {
      ctx.reply(
          if (asked != null) "No check of $spelling's is $asked, or behind import or chain $asked."
          else "$spelling has no check to run again. Run /imports $spelling to see the list.")
      return
    }
    when (val outcome = service.requeue(target, ctx.character.info.name)) {
      is RequeueOutcome.Queued -> {
        val chain = chains.find(target.chainId)
        ctx.reply(
            "Check ${outcome.requestId} of $spelling's (" +
                (target.monsterPid?.let { "monster %08x".format(it) } ?: "the whole chain") +
                ", was ${target.verdict}) is back in the queue" +
                (chain?.let { " from session ${it.verifiedLink + 2}" } ?: "") +
                ".")
      }
      is RequeueOutcome.Refused -> ctx.reply("Not requeued: ${outcome.why}")
    }
  }

  private fun StoredRequest.canGoBack(): Boolean =
      verdict != ReplayVerdict.PENDING && verdict != ReplayVerdict.VERIFIED && requeuedAt == null

  private companion object {
    /** How far back in a character's checks an id is looked for. */
    const val LOOK_BACK = 50
  }
}
