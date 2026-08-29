package de.fiereu.openmmo.net.game.codecs

import de.fiereu.bytecodec.*
import de.fiereu.openmmo.common.Skin
import de.fiereu.openmmo.common.enums.SkinSlot
import java.util.*

// Type and color share one 16 bit word.
private const val TYPE_MASK = 0x3FF
private const val COLOR_SHIFT = 10
private const val COLOR_MASK = 0x3F

// There are twelve slots, so the slot mask's top bit is free to carry a flag: when it is set,
// every populated slot's word is followed by one more byte, an override the client prefers to
// the word's own type.
private const val PER_SLOT_OVERRIDE = 0x8000

class SkinSet(var regionSelectionIndex: Int = 0, skins: Map<SkinSlot, Skin> = emptyMap()) :
    EnumMap<SkinSlot, Skin>(SkinSlot::class.java) {

  init {
    putAll(skins)
  }

  fun put(skin: Skin) {
    this[skin.slot] = skin
  }

  override fun put(key: SkinSlot, value: Skin): Skin? {
    require(key == value.slot) { "Slot mismatch: $key != ${value.slot}" }
    return super.put(key, value)
  }
}

class SkinSetCodec(
    private val slots: List<SkinSlot> = SkinSlot.entries,
    private val withLeadingByte: Boolean = true,
) : PacketCodec<SkinSet>() {
  override fun CodecScope<SkinSet>.body(): SkinSet {
    val regionSelectionIndex = if (withLeadingByte) field(U8) { it.regionSelectionIndex } else 0
    val mask =
        field(U16LE) { skins ->
          skins.keys.filter { it in slots }.fold(0) { acc, slot -> acc or (1 shl slot.ordinal) }
        }
    val skins = SkinSet(regionSelectionIndex)
    slots.forEach { slot ->
      if ((mask and (1 shl slot.ordinal)) != 0) {
        val compressed =
            field(U16LE) {
              val skin = it[slot] ?: Skin(slot, 0u, 0u)
              val type = (skin.type ?: TYPE_MASK.toUShort()).toInt()
              val color = (skin.color ?: COLOR_MASK.toUByte()).toInt()
              require(type <= TYPE_MASK) { "Skin type too large: ${skin.type}" }
              require(color <= COLOR_MASK) { "Skin color too large: ${skin.color}" }
              type or (color shl COLOR_SHIFT)
            }
        if (mask and PER_SLOT_OVERRIDE != 0) field(U8) { 0 }
        val type = (compressed and TYPE_MASK).toUShort()
        val color = ((compressed shr COLOR_SHIFT) and COLOR_MASK).toUByte()
        skins.put(Skin(slot, type, color))
      }
    }
    return skins
  }
}

val DefaultSkinSetCodec: Codec<SkinSet> = SkinSetCodec()
val SkinSetCodecNoLeading: Codec<SkinSet> = SkinSetCodec(withLeadingByte = false)
