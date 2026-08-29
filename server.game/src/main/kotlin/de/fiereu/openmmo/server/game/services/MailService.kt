package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.net.game.packets.MailComposeSendPacket
import de.fiereu.openmmo.net.game.packets.MailDeletePacket
import de.fiereu.openmmo.net.game.packets.MailDetail
import de.fiereu.openmmo.net.game.packets.MailDetailPacket
import de.fiereu.openmmo.net.game.packets.MailDetailRequestPacket
import de.fiereu.openmmo.net.game.packets.MailEntry
import de.fiereu.openmmo.net.game.packets.MailPageRequestPacket
import de.fiereu.openmmo.net.game.packets.MailResultPacket
import de.fiereu.openmmo.net.game.packets.MailboxCountsPacket
import de.fiereu.openmmo.net.game.packets.MailboxPagePacket
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.MAILBOX_CAP
import de.fiereu.openmmo.server.game.storage.MAIL_BOX_IN
import de.fiereu.openmmo.server.game.storage.MAIL_BOX_SENT
import de.fiereu.openmmo.server.game.storage.MailCopy
import de.fiereu.openmmo.server.game.storage.MailRepository
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/* sj1.Y90 as f/oP1 reads it. Every code prints string 5800 + code, which is
 * where these names come from: 5804 "The requested recipient could not be
 * found", 5819 "...has not been online recently", and so on. */
private const val MAIL_OK = 0
private const val MAIL_THEIR_BOX_FULL = 2
private const val MAIL_NO_RECIPIENT = 4
private const val MAIL_SUBJECT_LEN = 5
private const val MAIL_BODY_LEN = 6
private const val MAIL_ATTACH_ITEM = 8
private const val MAIL_SELF = 15
private const val MAIL_OWN_BOX_FULL = 18

/**
 * Rows on one page of the box. The official client's own pager is `f/rV0`, a `Cp0(10, 8)`: ten rows to a
 * page, eight page buttons.
 */
const val MAIL_PAGE_ROWS = 10

@Singleton
class MailService
@Inject
constructor(
    private val mail: MailRepository,
    private val characterStore: CharacterStore,
    private val sessionRegistry: SessionRegistry,
) {

  suspend fun sendMailbox(ctx: SessionContext) {
    val charId = ctx.attributes[PLAYER_STATE]?.characterId ?: return
    ctx.send(buildCounts(charId))
    ctx.send(buildPage(charId, page = 0, sent = false))
  }

  suspend fun onCompose(event: PacketEvent<MailComposeSendPacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val stored = characterStore.getCharacter(charId) ?: return
    val packet = event.packet
    log.info { "MailCompose char=$charId to='${packet.recipientName}'" }

    if (packet.attachments.isNotEmpty()) {
      ctx.send(MailResultPacket(MAIL_ATTACH_ITEM))
      return
    }
    if (packet.subject.length !in 3..40) {
      ctx.send(MailResultPacket(MAIL_SUBJECT_LEN))
      return
    }
    if (packet.body.length !in 3..2000) {
      ctx.send(MailResultPacket(MAIL_BODY_LEN))
      return
    }
    if (packet.recipientName.equals(stored.info.name, ignoreCase = true)) {
      ctx.send(MailResultPacket(MAIL_SELF))
      return
    }
    if (mail.sentCount(charId) >= MAILBOX_CAP) {
      ctx.send(MailResultPacket(MAIL_OWN_BOX_FULL))
      return
    }

    /* The lookup reaches the database now, so a name nobody answers to is
     * "could not be found" (4) rather than the old "not online recently"
     * (19), 19 was only ever true because the cache was all we had. */
    val target = characterStore.findIdByName(packet.recipientName)
    if (target == null) {
      ctx.send(MailResultPacket(MAIL_NO_RECIPIENT))
      return
    }
    val (targetId, targetName) = target
    if (targetId == charId) {
      ctx.send(MailResultPacket(MAIL_SELF))
      return
    }
    if (mail.inboxCount(targetId) >= MAILBOX_CAP) {
      ctx.send(MailResultPacket(MAIL_THEIR_BOX_FULL))
      return
    }

    val now = (System.currentTimeMillis() / 1000L).toInt()
    val letter =
        MailCopy(
            id = 0,
            ownerId = targetId,
            box = MAIL_BOX_IN,
            senderId = charId,
            senderName = stored.info.name,
            recipientId = targetId,
            recipientName = targetName,
            subject = packet.subject,
            body = packet.body,
            sentAt = now,
            unread = true,
        )
    mail.deliver(letter, letter.copy(ownerId = charId, box = MAIL_BOX_SENT, unread = false))

    ctx.send(MailResultPacket(MAIL_OK))
    ctx.send(buildCounts(charId))
    ctx.send(buildPage(charId, page = 0, sent = true))

    sessionRegistry.getByCharacterId(targetId)?.let { other ->
      other.send(buildCounts(targetId))
      other.send(buildPage(targetId, page = 0, sent = false))
    }
  }

  suspend fun onPageRequest(event: PacketEvent<MailPageRequestPacket>) {
    val ctx = event.session
    val charId = ctx.attributes[PLAYER_STATE]?.characterId ?: return
    ctx.send(buildCounts(charId))
    ctx.send(buildPage(charId, event.packet.page, event.packet.sent))
  }

  suspend fun onDetailRequest(event: PacketEvent<MailDetailRequestPacket>) {
    val ctx = event.session
    val charId = ctx.attributes[PLAYER_STATE]?.characterId ?: return
    val copy = mail.find(charId, event.packet.mailId)
    if (copy == null) {
      ctx.send(MailDetailPacket(sent = false, mail = null))
      return
    }
    if (copy.unread) mail.markRead(charId, copy.id)
    /* The request carries no page (f/f4 is the id alone), so the box page is not resent: the
     * list already holds the row and clears its own unread mark. The counts carry the badge. */
    ctx.send(buildCounts(charId))
    ctx.send(
        MailDetailPacket(
            sent = copy.box == MAIL_BOX_SENT, mail = toDetail(copy.copy(unread = false))))
  }

  suspend fun onDelete(event: PacketEvent<MailDeletePacket>) {
    val ctx = event.session
    val charId = ctx.attributes[PLAYER_STATE]?.characterId ?: return
    val copy = mail.find(charId, event.packet.mailId)
    val sent = copy?.box == MAIL_BOX_SENT
    if (copy != null) mail.remove(charId, copy.id)
    ctx.send(buildCounts(charId))
    ctx.send(buildPage(charId, event.packet.page, sent))
  }

  private suspend fun buildCounts(charId: Long): MailboxCountsPacket =
      MailboxCountsPacket(
          inbox = mail.inboxCount(charId).toShort(),
          /* f/ia0's middle short is what raises the bar's mail badge (f/gz reads NU0 > 0)
           * and chimes when it climbs, so it is the unread count, not a seen count. */
          inboxSeen = mail.unreadCount(charId).toShort(),
          sent = mail.sentCount(charId).toShort())

  private suspend fun buildPage(charId: Long, page: Short, sent: Boolean): MailboxPagePacket {
    val rows = if (sent) mail.sentOf(charId) else mail.inboxOf(charId)
    val from = page.toInt().coerceAtLeast(0) * MAIL_PAGE_ROWS
    if (from >= rows.size) return MailboxPagePacket(page, sent, emptyList())
    val slice = rows.subList(from, minOf(from + MAIL_PAGE_ROWS, rows.size))
    return MailboxPagePacket(page, sent, slice.map { toEntry(it) })
  }

  private fun toEntry(copy: MailCopy): MailEntry =
      MailEntry(
          mailId = copy.id,
          recipientId = copy.recipientId,
          senderId = copy.senderId,
          staffKind = 0,
          senderName = copy.senderName,
          recipientName = copy.recipientName,
          sentAt = copy.sentAt,
          subject = copy.subject,
          unread = if (copy.unread) 1 else 0,
          hasAttachments = false,
      )

  private fun toDetail(copy: MailCopy): MailDetail =
      MailDetail(
          mailId = copy.id,
          recipientId = copy.recipientId,
          senderId = copy.senderId,
          staffKind = 0,
          senderName = copy.senderName,
          recipientName = copy.recipientName,
          sentAt = copy.sentAt,
          subject = copy.subject,
          body = copy.body,
          unread = if (copy.unread) 1 else 0,
          hasAttachments = false,
      )
}
