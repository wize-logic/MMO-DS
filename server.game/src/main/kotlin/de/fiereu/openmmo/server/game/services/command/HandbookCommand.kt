package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.CharacterPermissions
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.MapManager
import javax.inject.Inject
import javax.inject.Singleton

/** Everywhere you can go and everything you can run, in the window you are playing in. */
@Singleton
class HandbookCommand @Inject constructor(private val mapManager: MapManager) : ChatCommand {
  override val name = "handbook"
  override val usage = "/handbook [what], try a place name, or 'commands'"
  override val description = "the GM handbook: search every map by name, or list what you can run"
  override val permission = CharacterPermissions.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    when (val what = ctx.args.firstOrNull()?.lowercase()) {
      null -> contents(ctx)
      "commands",
      "command",
      "cmds" -> commands(ctx)
      else -> places(ctx, ctx.args.joinToString(" "))
    }
  }

  private fun contents(ctx: CommandContext) {
    ctx.reply("GM handbook, ${mapManager.size()} maps are loaded.")
    ctx.reply("/handbook <name>    every map whose name contains it, with the warp for each")
    ctx.reply("/handbook commands  everything you may run here")
    ctx.reply("/warp <name>        go there by name; /warp <bank> <map> still takes numbers")
    ctx.reply("/flag <name>        search a story flag, or flip one: /flag <name> on|off")
    ctx.reply("/kit                the Explorer Kit, on or off")
    ctx.reply("You are on ${here(ctx)}, /pos prints that any time.")
  }

  private fun commands(ctx: CommandContext) {
    ctx.reply("You may run ${ctx.commands.size} command(s):")
    ctx.commands.sortedBy { it.name }.forEach { ctx.reply(" ${it.usage}, ${it.description}") }
  }

  private fun places(ctx: CommandContext, query: String) {
    /* Only maps a /warp can reach. Every region's maps are named, so an unscoped search buried
     * Sinnoh under a thousand GBA names whose warp the client then refused. */
    val anywhere = mapManager.search(query)
    val found = anywhere.filter { (it.regionId.toInt() and 0xFF) == WarpCommand.DRAWABLE_REGION }
    if (found.isEmpty()) {
      if (anywhere.isEmpty()) ctx.reply("No map's name contains '$query'. Try a town: $EXAMPLES")
      else ctx.reply("'$query' only names maps outside Sinnoh, which no warp can reach.")
      return
    }
    ctx.reply(
        "${found.size} map(s) match '$query'${if (found.size > PAGE) ", first $PAGE" else ""}:")
    found.take(PAGE).forEach { ctx.reply(" ${it.name}, /warp ${it.name}") }
    if (found.size > PAGE) ctx.reply("Narrow it down to see the rest.")
  }

  private fun here(ctx: CommandContext): String {
    val s = ctx.state
    val map: MapDef? = mapManager.getMap(s.regionId, s.bankId, s.mapId)
    val named = map?.name?.takeIf { it.isNotEmpty() }
    return "${named ?: "an unnamed map"} (${s.bankId}:${s.mapId})"
  }

  private companion object {
    /** As many as a chat window shows without scrolling away what was asked. */
    const val PAGE = 12
    const val EXAMPLES = "jubilife, oreburgh, veilstone, underground"
  }
}
