package de.fiereu.openmmo.net.game.packets.gtl

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S32LE
import de.fiereu.bytecodec.S64LE
import de.fiereu.bytecodec.S8
import de.fiereu.bytecodec.U8
import de.fiereu.bytecodec.listPrefixed

/**
 * Claim the funds a listing (or several) has settled (c2s 0x54, ours). The official client's claim rides an
 * opcode this registry already spends on a battle packet, so the slot is ours; the shape is
 * the official client's `Op`: a counted list of listing ids.
 */
data class GtlClaimPacket(val listingIds: List<Long>)

object GtlClaimPacketCodec : PacketCodec<GtlClaimPacket>() {
  override fun CodecScope<GtlClaimPacket>.body(): GtlClaimPacket {
    val ids = field(S64LE.listPrefixed(U8)) { it.listingIds }
    return GtlClaimPacket(ids)
  }
}

/** Lower a standing listing's price (c2s 0x55, ours; the official client's `Xc` shape). */
data class GtlPriceChangePacket(val listingId: Long, val newPrice: Int)

object GtlPriceChangePacketCodec : PacketCodec<GtlPriceChangePacket>() {
  override fun CodecScope<GtlPriceChangePacket>.body(): GtlPriceChangePacket {
    val id = field(S64LE) { it.listingId }
    val price = field(S32LE) { it.newPrice }
    return GtlPriceChangePacket(id, price)
  }
}

/**
 * One shelf verb's answer (s2c 0xAF, ours; the official client's `wn1` is a code the client maps to its own
 * toast strings, and this is the same idea).
 */
data class GtlResultPacket(val code: Byte, val a: Long, val b: Int) {
  companion object {
    const val CODE_LISTED: Byte = 1 // "You have successfully created a new listing."
    const val CODE_CAP: Byte = 2 // "You have reached the maximum amount of active listings."
    const val CODE_BOUGHT: Byte = 4 // "You have successfully made a purchase."
    const val CODE_GONE: Byte = 5 // "The requested listing could not be found."
    const val CODE_OWN: Byte = 6 // "You cannot purchase your own listing."
    const val CODE_CANCELED: Byte = 7 // "The requested listing has been canceled."
    const val CODE_UNSETTLED: Byte = 8 // "...has unsettled funds. Please claim first."
    const val CODE_PRICE_CHANGED: Byte = 9 // "You have successfully changed a listing price."
    const val CODE_PRICE_COOLDOWN: Byte = 10 // "You cannot change the price ... yet."
    const val CODE_CLAIMED: Byte = 11 // "You have successfully claimed listing funds/items."
    const val CODE_MIN_PRICE: Byte = 12 // "...cannot be less than ${00} for this item." b = floor
    const val CODE_NO_FUNDS: Byte = 13 // "You cannot afford it."
    const val CODE_NO_ROOM: Byte = 15 // "You have no room for it."
    const val CODE_SOLD_ONE: Byte = 20 // "Your {a=dex/item} listing has sold for ${b}."
    const val CODE_SOLD_MANY: Byte = 21 // "{a} of your listings have sold for ${b}."
    const val CODE_PRICE_FLOOR: Byte = 22 // "You cannot lower the price ... any further."
  }
}

object GtlResultPacketCodec : PacketCodec<GtlResultPacket>() {
  override fun CodecScope<GtlResultPacket>.body(): GtlResultPacket {
    val code = field(S8) { it.code }
    val a = field(S64LE) { it.a }
    val b = field(S32LE) { it.b }
    return GtlResultPacket(code, a, b)
  }
}
