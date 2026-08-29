package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*

/** One option on a [GmPanelRow]: its label and the byte that selects its kind. */
data class GmPanelOption(
    val label: String,
    val kind: Byte,
)

/** One labelled row of the panel's menu body, with the options under it. */
data class GmPanelRow(
    val label: String,
    val options: List<GmPanelOption>,
)

/** One pair of bytes on the detail body's trailing list. */
data class GmPanelPair(
    val first: Byte,
    val second: Byte,
)

/**
 * s2c 0xA2. One discriminator byte and then the body that byte's own case takes; the official client matches
 * the byte against an enum and falls back to the 0 case, so a value outside the set below
 * carries no body rather than being a malformed packet.
 */
data class GmPanelVariantPacket(
    val variant: Byte,
    val entityId: Long? = null,
    val leadText: String? = null,
    val textA: String? = null,
    val textB: String? = null,
    val value: Int? = null,
    val leadByte: Byte? = null,
    val shortA: Short? = null,
    val shortB: Short? = null,
    val shortC: Short? = null,
    val byteA: Byte? = null,
    val intA: Int? = null,
    val flagA: Boolean? = null,
    val flagB: Boolean? = null,
    val byteB: Byte? = null,
    val byteC: Byte? = null,
    val byteD: Byte? = null,
    val pairs: List<GmPanelPair> = emptyList(),
    val rows: List<GmPanelRow> = emptyList(),
) {
  companion object {
    const val NOTE: Byte = 1
    const val TARGET: Byte = 2
    const val DETAIL: Byte = 40
    const val SUMMARY: Byte = 41
    const val TITLED_NOTE: Byte = 101
    const val MENU: Byte = 102
  }
}

private val GmPanelOptionCodec: Codec<GmPanelOption> =
    object : PacketCodec<GmPanelOption>() {
      override fun CodecScope<GmPanelOption>.body(): GmPanelOption {
        val label = field(Utf16LeNullTerminated) { it.label }
        val kind = field(S8) { it.kind }
        return GmPanelOption(label, kind)
      }
    }

private val GmPanelRowCodec: Codec<GmPanelRow> =
    object : PacketCodec<GmPanelRow>() {
      override fun CodecScope<GmPanelRow>.body(): GmPanelRow {
        val label = field(Utf16LeNullTerminated) { it.label }
        val count = field(U8) { it.options.size }
        val options = (0 until count).map { i -> field(GmPanelOptionCodec) { it.options[i] } }
        return GmPanelRow(label, options)
      }
    }

private val GmPanelPairCodec: Codec<GmPanelPair> =
    object : PacketCodec<GmPanelPair>() {
      override fun CodecScope<GmPanelPair>.body(): GmPanelPair {
        val first = field(S8) { it.first }
        val second = field(S8) { it.second }
        return GmPanelPair(first, second)
      }
    }

object GmPanelVariantPacketCodec : PacketCodec<GmPanelVariantPacket>() {
  override fun CodecScope<GmPanelVariantPacket>.body(): GmPanelVariantPacket {
    val variant = field(S8) { it.variant }
    return when (variant) {
      GmPanelVariantPacket.NOTE -> readNote(variant, null)
      GmPanelVariantPacket.TITLED_NOTE -> {
        val leadText =
            field(Utf16LeNullTerminated) {
              it.leadText ?: throw MalformedPacketException("leadText required")
            }
        readNote(variant, leadText)
      }

      GmPanelVariantPacket.TARGET -> {
        val entityId =
            field(S64LE) { it.entityId ?: throw MalformedPacketException("entityId required") }
        GmPanelVariantPacket(variant, entityId = entityId)
      }

      GmPanelVariantPacket.DETAIL,
      GmPanelVariantPacket.SUMMARY -> readBlock(variant)
      GmPanelVariantPacket.MENU -> {
        val count = field(U8) { it.rows.size }
        val rows = (0 until count).map { i -> field(GmPanelRowCodec) { it.rows[i] } }
        GmPanelVariantPacket(variant, rows = rows)
      }

      else -> GmPanelVariantPacket(variant)
    }
  }

  /** Case 1, which case 101 falls through into after its own leading string. */
  private fun CodecScope<GmPanelVariantPacket>.readNote(
      variant: Byte,
      leadText: String?
  ): GmPanelVariantPacket {
    val entityId =
        field(S64LE) { it.entityId ?: throw MalformedPacketException("entityId required") }
    val textA =
        field(Utf16LeNullTerminated) {
          it.textA ?: throw MalformedPacketException("textA required")
        }
    val textB =
        field(Utf16LeNullTerminated) {
          it.textB ?: throw MalformedPacketException("textB required")
        }
    val value = field(S32LE) { it.value ?: throw MalformedPacketException("value required") }
    return GmPanelVariantPacket(
        variant,
        entityId = entityId,
        leadText = leadText,
        textA = textA,
        textB = textB,
        value = value)
  }

  /** Cases 40 and 41. 41 is 40 without the lead byte, the two booleans or the pair list. */
  private fun CodecScope<GmPanelVariantPacket>.readBlock(variant: Byte): GmPanelVariantPacket {
    val detail = variant == GmPanelVariantPacket.DETAIL
    val leadByte =
        if (detail) field(S8) { it.leadByte ?: throw MalformedPacketException("leadByte required") }
        else null
    val shortA = field(S16LE) { it.shortA ?: throw MalformedPacketException("shortA required") }
    val shortB = field(S16LE) { it.shortB ?: throw MalformedPacketException("shortB required") }
    val shortC = field(S16LE) { it.shortC ?: throw MalformedPacketException("shortC required") }
    val byteA = field(S8) { it.byteA ?: throw MalformedPacketException("byteA required") }
    field(S8) { 0 }
    val intA = field(S32LE) { it.intA ?: throw MalformedPacketException("intA required") }
    val flagA = if (detail) field(U8) { if (it.flagA == true) 1 else 0 } == 1 else null
    val flagB = if (detail) field(U8) { if (it.flagB == true) 1 else 0 } == 1 else null
    val byteB = field(S8) { it.byteB ?: throw MalformedPacketException("byteB required") }
    val byteC = field(S8) { it.byteC ?: throw MalformedPacketException("byteC required") }
    val byteD = field(S8) { it.byteD ?: throw MalformedPacketException("byteD required") }
    val pairs =
        if (detail) {
          val count = field(U8) { it.pairs.size }
          (0 until count).map { i -> field(GmPanelPairCodec) { it.pairs[i] } }
        } else emptyList()
    return GmPanelVariantPacket(
        variant,
        leadByte = leadByte,
        shortA = shortA,
        shortB = shortB,
        shortC = shortC,
        byteA = byteA,
        intA = intA,
        flagA = flagA,
        flagB = flagB,
        byteB = byteB,
        byteC = byteC,
        byteD = byteD,
        pairs = pairs)
  }
}
