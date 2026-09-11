package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.offline.verify.ReplayLimits
import de.fiereu.openmmo.server.game.offline.verify.ReplayVerificationService
import de.fiereu.openmmo.server.game.offline.verify.RequestOutcome
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Asks for the play behind a save import to be replayed: for one monster in the party, or for all
 * of it.
 */
@Singleton
class VerifyCommand
@Inject
constructor(
    private val service: ReplayVerificationService,
    private val species: SpeciesRegistry,
) : ChatCommand {
  override val name = "verify"
  override val usage = "/verify <party slot|all>"
  override val description = "have the play behind your save import checked, for one monster or all"

  override suspend fun run(ctx: CommandContext) {
    val what = ctx.args.firstOrNull()?.lowercase()
    if (what == null) {
      ctx.reply("Usage: $usage")
      return
    }
    val pid: Int?
    val named: String
    if (what == "all") {
      pid = null
      named = "everything behind your save"
    } else {
      val slot = what.toIntOrNull()
      val party = ctx.character.pokemon
      if (slot == null || slot !in 1..party.size) {
        ctx.reply("Usage: $usage, your party has ${party.size} in it.")
        return
      }
      val mon = party[slot - 1]
      named = "${species.get(mon.dexId)?.name ?: "species ${mon.dexId}"} in slot $slot"
      if (!mon.offlineOrigin) {
        ctx.reply("$named is already trusted; nothing to check.")
        return
      }
      pid = mon.seed
    }
    when (val outcome = service.request(ctx.characterId, pid)) {
      is RequestOutcome.Queued ->
          ctx.reply(
              "Asked: $named will be checked by replaying up to ${hours(outcome.frames)} of your" +
                  " play" +
                  (if (outcome.fee > 0) ", for ¥${outcome.fee}; what is not needed comes back"
                  else ", for nothing") +
                  ". The answer lands whether or not you are here.")
      is RequestOutcome.Refused -> ctx.reply("Not asked: ${outcome.why}.")
      is RequestOutcome.CannotAfford ->
          ctx.reply("That check costs ¥${outcome.fee}, which you do not have.")
    }
  }

  /** Frames as a person reads them: the console's sixty a second. */
  private fun hours(frames: Long): String {
    val minutes = frames / (ReplayLimits.FRAMES_PER_SECOND * 60)
    return when {
      minutes < 1 -> "a moment"
      minutes < 90 -> "$minutes minute(s)"
      else -> "${(minutes + 30) / 60} hour(s)"
    }
  }
}
