package de.fiereu.openmmo.net.login.packets

import de.fiereu.bytecodec.Codec
import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S8
import de.fiereu.bytecodec.U16LE
import de.fiereu.bytecodec.bytesPrefixed
import de.fiereu.bytecodec.imap
import de.fiereu.openmmo.common.utils.gzipCompress
import de.fiereu.openmmo.common.utils.gzipDecompress
import java.nio.charset.StandardCharsets

data class ToSPacket(val confirmationKey: Byte, val tosText: String)

// The shared pair rather than a second one here, so the ceiling on what a gzip field may unpack to
// is written once and both fields on these protocols get it.
private val GzippedTextU16: Codec<String> =
    bytesPrefixed(U16LE)
        .imap(
            decode = { String(it.gzipDecompress(), StandardCharsets.UTF_8) },
            encode = { it.toByteArray(StandardCharsets.UTF_8).gzipCompress() },
        )

object ToSPacketCodec : PacketCodec<ToSPacket>() {
  override fun CodecScope<ToSPacket>.body(): ToSPacket {
    val confirmationKey = field(S8) { it.confirmationKey }
    val tosText = field(GzippedTextU16) { it.tosText }
    return ToSPacket(confirmationKey, tosText)
  }
}
