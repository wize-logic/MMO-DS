package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*

/** One mart line: item, endless-or-count stock, and the price the clerk charges. */
data class ShopItem(
    val itemId: Short,
    val stock: Short,
    val price: Int,
)

object ShopItemCodec : PacketCodec<ShopItem>() {
  override fun CodecScope<ShopItem>.body(): ShopItem {
    val itemId = field(S16LE) { it.itemId }
    val stock = field(S16LE) { it.stock }
    val price = field(S32LE) { it.price }
    return ShopItem(itemId, stock, price)
  }
}

/** Opens or closes the mart. A close is the single byte [CLOSED]. */
data class ShopCatalogPacket(
    val kind: Byte,
    val flags: Byte = 0,
    val currencyKind: Byte = 0,
    val param: Int? = null,
    val titleId: Int? = null,
    val subtitleId: Int? = null,
    val npcEntityId: Long? = null,
    val items: List<ShopItem> = emptyList(),
) {
  companion object {
    const val OPEN: Byte = 0
    const val CLOSED: Byte = -1

    const val FLAG_TITLE: Int = 0x08
    const val FLAG_SUBTITLE: Int = 0x10
    const val FLAG_PARAM: Int = 0x20
    const val FLAG_NPC: Int = 0x40

    fun open(npcEntityId: Long, items: List<ShopItem>): ShopCatalogPacket =
        ShopCatalogPacket(
            kind = OPEN,
            flags = FLAG_NPC.toByte(),
            currencyKind = 0,
            npcEntityId = npcEntityId,
            items = items,
        )
  }
}

object ShopCatalogPacketCodec : PacketCodec<ShopCatalogPacket>() {
  override fun CodecScope<ShopCatalogPacket>.body(): ShopCatalogPacket {
    val kind = field(S8) { it.kind }
    if (kind == ShopCatalogPacket.CLOSED) {
      return ShopCatalogPacket(kind)
    }
    val flags = field(S8) { it.flags }
    val currencyKind = field(S8) { it.currencyKind }
    val f = flags.toInt() and 0xFF
    val param = optionalField(f and ShopCatalogPacket.FLAG_PARAM != 0, S32LE) { it.param }
    val titleId = optionalField(f and ShopCatalogPacket.FLAG_TITLE != 0, S32LE) { it.titleId }
    val subtitleId =
        optionalField(f and ShopCatalogPacket.FLAG_SUBTITLE != 0, S32LE) { it.subtitleId }
    val npcEntityId = optionalField(f and ShopCatalogPacket.FLAG_NPC != 0, S64LE) { it.npcEntityId }
    val items = field(ShopItemCodec.listPrefixed(U16LE)) { it.items }
    return ShopCatalogPacket(
        kind, flags, currencyKind, param, titleId, subtitleId, npcEntityId, items)
  }
}
