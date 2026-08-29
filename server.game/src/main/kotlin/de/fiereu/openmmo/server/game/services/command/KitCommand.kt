package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.CharacterPermissions
import de.fiereu.openmmo.items.generated.Items
import de.fiereu.openmmo.server.game.services.StoryPlayerService
import javax.inject.Inject
import javax.inject.Singleton

/** The Explorer Kit, on or off. */
@Singleton
class KitCommand @Inject constructor(private val storyPlayer: StoryPlayerService) : ChatCommand {
  override val name = "kit"
  override val usage = "/kit [on|off]"
  override val description = "gives or takes the Explorer Kit (no argument toggles)"
  override val permission = CharacterPermissions.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    val had = storyPlayer.hasItem(ctx.state, Items.EXPLORER_KIT, 1)
    val want =
        when (ctx.args.firstOrNull()?.lowercase()) {
          null -> !had
          "on",
          "give" -> true
          "off",
          "take" -> false
          else -> {
            ctx.reply("Usage: $usage")
            return
          }
        }

    if (want == had) {
      ctx.reply(
          if (had) "You already have the Explorer Kit."
          else "You have no Explorer Kit to take away.")
      return
    }

    if (!storyPlayer.giveItem(ctx.session, ctx.state, Items.EXPLORER_KIT, if (want) 1 else -1)) {
      ctx.reply("Could not ${if (want) "give" else "take"} the Explorer Kit.")
      return
    }

    ctx.reply(if (want) "Gave the Explorer Kit." else "Took the Explorer Kit.")
  }
}
