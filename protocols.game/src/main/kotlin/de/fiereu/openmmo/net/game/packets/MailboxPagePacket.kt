package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.Bool
import de.fiereu.bytecodec.Codec
import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.ReadBuffer
import de.fiereu.bytecodec.S16LE
import de.fiereu.bytecodec.S32LE
import de.fiereu.bytecodec.S64LE
import de.fiereu.bytecodec.S8
import de.fiereu.bytecodec.Utf16LeNullTerminated
import de.fiereu.bytecodec.WriteBuffer
import de.fiereu.bytecodec.imap
import de.fiereu.bytecodec.listPrefixed

/**
 * One `f/Xe0` as `the official client(sent, false)` reads a list row. `recipientId` is `EU1` (the
 * addressee); `senderId` is `Xg0` (0 is system mail).
 */
data class MailEntry(
    val mailId: Long,
    val recipientId: Long,
    val senderId: Long,
    val staffKind: Byte,
    val senderName: String,
    val recipientName: String,
    val sentAt: Int,
    val subject: String,
    val unread: Byte,
    val hasAttachments: Boolean,
)

data class MailboxPagePacket(
    val page: Short,
    val sent: Boolean,
    val entries: List<MailEntry>,
)

private val S16Count = S16LE.imap({ it.toInt() }, { it.toShort() })

private fun mailListRowCodec(sent: Boolean): Codec<MailEntry> =
    object : Codec<MailEntry> {
      override fun read(buf: ReadBuffer): MailEntry {
        val mailId = S64LE.read(buf)
        val recipientId = S64LE.read(buf)
        val senderId = S64LE.read(buf)
        val staffKind = S8.read(buf)
        val senderName = if (!sent) Utf16LeNullTerminated.read(buf) else ""
        val recipientName = if (sent) Utf16LeNullTerminated.read(buf) else ""
        val sentAt = S32LE.read(buf)
        val subject = Utf16LeNullTerminated.read(buf)
        val unread = S8.read(buf)
        val hasAttachments = Bool.read(buf)
        return MailEntry(
            mailId,
            recipientId,
            senderId,
            staffKind,
            senderName,
            recipientName,
            sentAt,
            subject,
            unread,
            hasAttachments,
        )
      }

      override fun write(buf: WriteBuffer, value: MailEntry) {
        S64LE.write(buf, value.mailId)
        S64LE.write(buf, value.recipientId)
        S64LE.write(buf, value.senderId)
        S8.write(buf, value.staffKind)
        if (!sent) Utf16LeNullTerminated.write(buf, value.senderName)
        if (sent) Utf16LeNullTerminated.write(buf, value.recipientName)
        S32LE.write(buf, value.sentAt)
        Utf16LeNullTerminated.write(buf, value.subject)
        S8.write(buf, value.unread)
        Bool.write(buf, value.hasAttachments)
      }
    }

object MailboxPagePacketCodec : PacketCodec<MailboxPagePacket>() {
  override fun CodecScope<MailboxPagePacket>.body(): MailboxPagePacket {
    val page = field(S16LE) { it.page }
    val sent = field(Bool) { it.sent }
    val entries = field(mailListRowCodec(sent).listPrefixed(S16Count)) { it.entries }
    return MailboxPagePacket(page, sent, entries)
  }
}
