package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.Bool
import de.fiereu.bytecodec.Codec
import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.MalformedPacketException
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.ReadBuffer
import de.fiereu.bytecodec.S32LE
import de.fiereu.bytecodec.S64LE
import de.fiereu.bytecodec.S8
import de.fiereu.bytecodec.U8
import de.fiereu.bytecodec.Utf16LeNullTerminated
import de.fiereu.bytecodec.WriteBuffer

/**
 * the official client `the official client`: a present byte, then the sent-box flag, then `the
 * official client(sent, true)`, the list row plus the body and a U8 attachment count. Attachments
 * are not seated; a non-zero count is refused rather than guessed.
 */
data class MailDetail(
    val mailId: Long,
    val recipientId: Long,
    val senderId: Long,
    val staffKind: Byte,
    val senderName: String,
    val recipientName: String,
    val sentAt: Int,
    val subject: String,
    val body: String,
    val unread: Byte,
    val hasAttachments: Boolean,
)

data class MailDetailPacket(
    val sent: Boolean,
    val mail: MailDetail?,
)

private fun mailDetailCodec(sent: Boolean): Codec<MailDetail> =
    object : Codec<MailDetail> {
      override fun read(buf: ReadBuffer): MailDetail {
        val mailId = S64LE.read(buf)
        val recipientId = S64LE.read(buf)
        val senderId = S64LE.read(buf)
        val staffKind = S8.read(buf)
        val senderName = if (!sent) Utf16LeNullTerminated.read(buf) else ""
        val recipientName = if (sent) Utf16LeNullTerminated.read(buf) else ""
        val sentAt = S32LE.read(buf)
        val subject = Utf16LeNullTerminated.read(buf)
        val body = Utf16LeNullTerminated.read(buf)
        val unread = S8.read(buf)
        val hasAttachments = Bool.read(buf)
        val n = U8.read(buf)
        if (n != 0) throw MalformedPacketException("mail attachments are not seated")
        return MailDetail(
            mailId,
            recipientId,
            senderId,
            staffKind,
            senderName,
            recipientName,
            sentAt,
            subject,
            body,
            unread,
            hasAttachments,
        )
      }

      override fun write(buf: WriteBuffer, value: MailDetail) {
        S64LE.write(buf, value.mailId)
        S64LE.write(buf, value.recipientId)
        S64LE.write(buf, value.senderId)
        S8.write(buf, value.staffKind)
        if (!sent) Utf16LeNullTerminated.write(buf, value.senderName)
        if (sent) Utf16LeNullTerminated.write(buf, value.recipientName)
        S32LE.write(buf, value.sentAt)
        Utf16LeNullTerminated.write(buf, value.subject)
        Utf16LeNullTerminated.write(buf, value.body)
        S8.write(buf, value.unread)
        Bool.write(buf, value.hasAttachments)
        U8.write(buf, 0)
      }
    }

object MailDetailPacketCodec : PacketCodec<MailDetailPacket>() {
  override fun CodecScope<MailDetailPacket>.body(): MailDetailPacket {
    val present = field(Bool) { it.mail != null }
    if (!present) return MailDetailPacket(sent = false, mail = null)
    val sent = field(Bool) { it.sent }
    val mail = field(mailDetailCodec(sent)) { it.mail!! }
    return MailDetailPacket(sent, mail)
  }
}
