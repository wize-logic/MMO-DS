package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.items.ItemDef
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.net.game.packets.ExchangeItemRequestPacket
import de.fiereu.openmmo.net.game.packets.LocalCharacterDeltaPacket
import de.fiereu.openmmo.net.game.packets.ShopCatalogPacket
import de.fiereu.openmmo.net.game.packets.ShopItem
import de.fiereu.openmmo.net.game.packets.ShopSellRequestPacket
import de.fiereu.openmmo.server.game.session.OPEN_SHOP
import de.fiereu.openmmo.server.game.session.OpenShop
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.OfflineItemRepository
import de.fiereu.openmmo.server.game.storage.sellableQuantity
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

// 0x7FFF is the stock the official client mart reader treats as an endless shelf.
private const val ENDLESS_SHELF: Short = 0x7FFF
// A bag stack id is the item id shifted up, with the item entity tag in the low bits.
private const val ITEM_ENTITY_SHIFT = 16

@Singleton
class ShopService
@Inject
constructor(
    private val characterStore: CharacterStore,
    private val items: ItemRegistry,
    private val offlineItems: OfflineItemRepository,
) {

  fun open(session: SessionContext, npcEntityId: Long, shelf: List<ItemDef>) {
    // Deliberately the item table's price, until we know where the real one comes from. The
    // mart_catalog capture charges 200 for a Potion where the table says 300.
    val offered =
        shelf.mapNotNull { item ->
          val id = items.idOrNull(item)
          if (id == null) {
            log.warn { "Shop offered ${item.name}, which has no id in this build" }
            null
          } else {
            item to ShopItem(id.toShort(), ENDLESS_SHELF, item.price)
          }
        }
    val state = session.attributes[PLAYER_STATE] ?: return
    session.attributes[OPEN_SHOP] =
        OpenShop(state.regionId, state.bankId, state.mapId, offered.toMap())
    session.send(ShopCatalogPacket.open(npcEntityId, offered.map { it.second }))
  }

  /** The shelf, if one is open and the player is still standing where they opened it. */
  private fun openShelf(session: SessionContext): Map<ItemDef, ShopItem>? {
    val open = session.attributes[OPEN_SHOP] ?: return null
    val state = session.attributes[PLAYER_STATE] ?: return null
    if (open.regionId != state.regionId ||
        open.bankId != state.bankId ||
        open.mapId != state.mapId) {
      session.attributes.remove(OPEN_SHOP)
      return null
    }
    return open.shelf
  }

  suspend fun onBuy(event: PacketEvent<ExchangeItemRequestPacket>) {
    val session = event.session
    val shelf = openShelf(session) ?: return
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    val quantity = event.packet.quantity.toInt()
    val item = items.get(event.packet.itemEntityRef.toInt())
    val offer = item?.let { shelf[it] }
    if (offer == null || quantity <= 0) {
      log.warn {
        "char=$charId asked for $quantity of ${event.packet.itemEntityRef}, not on this shelf"
      }
      return
    }

    val itemId = offer.itemId.toInt()
    // A quantity is a signed Short, so a dear enough item would wrap the product negative.
    val cost = offer.price.toLong() * quantity
    val stored = characterStore.getCharacter(charId) ?: return
    if (stored.info.money < cost) {
      log.info { "char=$charId cannot afford $quantity of ${item.name} at $cost" }
      return
    }

    // The item goes in first, so a bag that refuses it costs the player nothing.
    if (!characterStore.addItem(charId, itemId, quantity)) return
    if (!characterStore.addMoney(charId, -cost.toInt())) {
      if (!characterStore.addItem(charId, itemId, -quantity)) {
        log.error { "char=$charId keeps $quantity ${item.name} unpaid, the refund did not persist" }
      }
      return
    }

    log.info { "char=$charId bought $quantity ${item.name} for $cost" }
    sendBagAndMoney(session, charId, itemId)
  }

  suspend fun onSell(event: PacketEvent<ShopSellRequestPacket>) {
    val session = event.session
    if (openShelf(session) == null) return
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    val quantity = event.packet.quantity.toInt()
    val stored = characterStore.getCharacter(charId) ?: return
    // The client sells the bag stack by the id the bag packet gave it, not by item id.
    val itemId = (event.packet.itemEntityId shr ITEM_ENTITY_SHIFT).toInt()
    val held = stored.items[itemId] ?: 0
    if (quantity <= 0 || held < quantity) {
      log.warn { "char=$charId offered $quantity of stack $itemId, holding $held" }
      return
    }

    val item = items.get(itemId)
    // Anything halving to nothing, a key item above all, would be deleted and paid nothing for.
    if (item == null || item.price < 2) {
      log.warn { "char=$charId offered stack $itemId, which a mart does not buy" }
      return
    }

    /* What of that stack came out of a save file, which a mart will not turn into money. */
    val marked = offlineItems.load(charId)[itemId] ?: 0
    val sellable = sellableQuantity(held, marked)
    if (quantity > sellable) {
      log.info {
        "char=$charId offered $quantity of ${item.name} with only $sellable sellable" +
            " ($marked of $held came from a save file)"
      }
      session.send(
          notice(
              "$sellable of your ${item.name} can be sold here. The rest came in from an offline" +
                  " save, and a mart will not turn those into money."))
      return
    }

    val paid = item.price / 2 * quantity
    if (!characterStore.addItem(charId, itemId, -quantity)) return
    if (!characterStore.addMoney(charId, paid)) {
      if (!characterStore.addItem(charId, itemId, quantity)) {
        log.error { "char=$charId lost $quantity of stack $itemId, the refund did not persist" }
      }
      return
    }

    log.info { "char=$charId sold $quantity of ${item.name} for $paid" }
    sendBagAndMoney(session, charId, itemId)
  }

  private fun sendBagAndMoney(session: SessionContext, charId: Long, itemId: Int) {
    val after = characterStore.getCharacter(charId) ?: return
    session.send(LocalCharacterDeltaPacket(money = after.info.money))
    session.send(itemStackUpdatePacket(itemId, after.items[itemId] ?: 0))
  }
}
