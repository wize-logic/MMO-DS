package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.CharacterPermissions
import de.fiereu.openmmo.server.game.matchmaking.MatchmakingService
import de.fiereu.openmmo.server.game.matchmaking.QueueRules
import de.fiereu.openmmo.server.game.storage.CharacterStore
import javax.inject.Inject
import javax.inject.Singleton

/** Look at the matchmaking queue, and make a round happen now. */
@Singleton
class QueueCommand
@Inject
constructor(
    private val matchmaking: MatchmakingService,
    private val store: CharacterStore,
) : ChatCommand {
  override val name = "queue"
  override val usage = "/queue [round|window]"
  override val description = "shows who is waiting, runs a round, or sends the window"
  override val permission = CharacterPermissions.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    when (ctx.args.firstOrNull()?.lowercase()) {
      "round" -> {
        val made = matchmaking.runAllRounds()
        ctx.reply(if (made == 0) "The round paired nobody." else "The round made $made match(es).")
        return
      }

      // What opens the matchmaking window is not established: nothing in the specification's own
      // c2s set is obviously the request for it, and its client is handed the window rather than
      // asking for one. Until that is measured, this is how a real 0x47 gets in front of a client.
      "window" -> {
        ctx.session.send(matchmaking.window())
        ctx.reply("Sent the matchmaking window.")
        return
      }

      else -> {}
    }
    for (rules in QueueRules.ALL) {
      val waiting = matchmaking.waitingIn(rules.queue)
      val who =
          if (waiting.isEmpty()) "nobody"
          else waiting.joinToString(", ") { store.getCharacter(it)?.info?.name ?: "char=$it" }
      ctx.reply("${rules.queue}: $who")
    }
    val mine = matchmaking.signupOf(ctx.characterId)
    ctx.reply(
        if (mine.isEmpty()) "You are not signed up."
        else "You are signed up for ${mine.joinToString(", ")}.")
  }
}
