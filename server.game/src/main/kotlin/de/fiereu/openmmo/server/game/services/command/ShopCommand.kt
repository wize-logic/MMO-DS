package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.CharacterPermissions
import de.fiereu.openmmo.items.generated.Items
import de.fiereu.openmmo.server.game.services.ShopService
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class ShopCommand @Inject constructor(private val shopService: ShopService) : ChatCommand {
  override val name = "shop"
  override val usage = "/shop"
  override val description = "opens a mart on this tile"
  override val permission = CharacterPermissions.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    shopService.open(
        ctx.session,
        npcEntityId = 0L,
        // The Cherish Ball is here for a reason the other four are not: it is the item
        // mmo/mods/imports lands a foreign icon on, and a mart is the only way a bag gets one
        // without a story that hands it over.
        shelf =
            listOf(
                Items.POKE_BALL,
                Items.POTION,
                Items.ANTIDOTE,
                Items.PARLYZ_HEAL,
                Items.CHERISH_BALL,
                // The Poffin Case and one berry of each flavour.
                Items.POFFIN_CASE,
                Items.CHERI_BERRY,
                Items.CHESTO_BERRY,
                Items.PECHA_BERRY,
                Items.RAWST_BERRY,
                Items.ASPEAR_BERRY,
            ),
    )
  }
}
