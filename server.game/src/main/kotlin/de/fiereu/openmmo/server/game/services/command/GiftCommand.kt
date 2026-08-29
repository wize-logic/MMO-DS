package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.CharacterPermissions
import de.fiereu.openmmo.items.ItemDef
import de.fiereu.openmmo.items.generated.Items
import de.fiereu.openmmo.server.game.services.StoryPlayerService
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class GiftCommand @Inject constructor(private val storyPlayer: StoryPlayerService) : ChatCommand {
  override val name = "gift"
  override val usage = "/gift [item] [count]"
  override val description = "gives an item by name (default: one Potion)"
  override val permission = CharacterPermissions.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    val wanted = ctx.args.firstOrNull() ?: "potion"
    val count = (ctx.args.getOrNull(1)?.toIntOrNull() ?: 1).coerceIn(1, 99)
    val item = byName[normalize(wanted)]
    if (item == null) {
      ctx.reply("No item called '$wanted'. Try e.g. potion, pokeball, greatball.")
      return
    }
    if (!storyPlayer.giveItem(ctx.session, ctx.state, item, count)) {
      ctx.reply("Could not give ${item.name}.")
      return
    }
    ctx.reply("Gave $count × ${item.name}.")
  }

  private companion object {
    fun normalize(s: String) = s.filter { it.isLetterOrDigit() }.lowercase()

    /**
     * Every ItemDef the generated registry declares, keyed by its constant name with the
     * underscores dropped ("POKE_BALL" -> "pokeball"), so the command names items the way a person
     * types them.
     */
    val byName: Map<String, ItemDef> by lazy {
      Items.javaClass.methods
          .filter { it.parameterCount == 0 && it.returnType == ItemDef::class.java }
          .mapNotNull { m ->
            val def = m.invoke(Items) as? ItemDef ?: return@mapNotNull null
            normalize(m.name.removePrefix("get")) to def
          }
          .toMap()
    }
  }
}
