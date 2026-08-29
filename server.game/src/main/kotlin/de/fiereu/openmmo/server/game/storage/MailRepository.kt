package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.db.game.tables.records.MailMessagesRecord
import de.fiereu.openmmo.db.game.tables.references.MAIL_MESSAGES
import java.time.LocalDateTime
import java.time.ZoneOffset
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicLong
import javax.inject.Inject
import javax.inject.Named
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import org.jooq.DSLContext

const val MAILBOX_CAP = 250

/** Which of the owner's two boxes a copy sits in. */
const val MAIL_BOX_IN = 0
const val MAIL_BOX_SENT = 1

/**
 * One side of a letter. The official client gives the sender's copy and the recipient's copy
 * separate ids and lets either be deleted alone, so this is a copy rather than a message.
 */
data class MailCopy(
    val id: Long,
    val ownerId: Long,
    val box: Int,
    val senderId: Long,
    val senderName: String,
    val recipientId: Long,
    val recipientName: String,
    val subject: String,
    val body: String,
    val sentAt: Int,
    val unread: Boolean,
)

interface MailRepository {
  suspend fun inboxOf(charId: Long): List<MailCopy>

  suspend fun sentOf(charId: Long): List<MailCopy>

  suspend fun inboxCount(charId: Long): Int

  suspend fun sentCount(charId: Long): Int

  /** How many of the inbox are still unread, which is the official client's second count. */
  suspend fun unreadCount(charId: Long): Int

  /** Writes both copies of one letter, or neither. */
  suspend fun deliver(inbox: MailCopy, sent: MailCopy)

  suspend fun find(charId: Long, mailId: Long): MailCopy?

  suspend fun markRead(charId: Long, mailId: Long)

  suspend fun remove(charId: Long, mailId: Long): Boolean
}

class JooqMailRepository
@Inject
constructor(
    private val dsl: DSLContext,
    @param:Named("db") private val dispatcher: CoroutineDispatcher,
) : MailRepository {

  override suspend fun inboxOf(charId: Long): List<MailCopy> = pageOf(charId, MAIL_BOX_IN)

  override suspend fun sentOf(charId: Long): List<MailCopy> = pageOf(charId, MAIL_BOX_SENT)

  override suspend fun inboxCount(charId: Long): Int = countOf(charId, MAIL_BOX_IN)

  override suspend fun sentCount(charId: Long): Int = countOf(charId, MAIL_BOX_SENT)

  override suspend fun unreadCount(charId: Long): Int =
      withContext(dispatcher) {
        dsl.fetchCount(
            MAIL_MESSAGES,
            MAIL_MESSAGES.OWNER_ID.eq(charId)
                .and(MAIL_MESSAGES.BOX.eq(MAIL_BOX_IN.toShort()))
                .and(MAIL_MESSAGES.UNREAD.isTrue))
      }

  override suspend fun deliver(inbox: MailCopy, sent: MailCopy) {
    withContext(dispatcher) {
      dsl.transaction { cfg ->
        cfg.dsl().insertInto(MAIL_MESSAGES).set(inbox.toRecord()).execute()
        cfg.dsl().insertInto(MAIL_MESSAGES).set(sent.toRecord()).execute()
      }
    }
  }

  override suspend fun find(charId: Long, mailId: Long): MailCopy? =
      withContext(dispatcher) {
        dsl.selectFrom(MAIL_MESSAGES)
            .where(MAIL_MESSAGES.ID.eq(mailId).and(MAIL_MESSAGES.OWNER_ID.eq(charId)))
            .fetchOne()
            ?.toCopy()
      }

  override suspend fun markRead(charId: Long, mailId: Long) {
    withContext(dispatcher) {
      dsl.update(MAIL_MESSAGES)
          .set(MAIL_MESSAGES.UNREAD, false)
          .where(MAIL_MESSAGES.ID.eq(mailId).and(MAIL_MESSAGES.OWNER_ID.eq(charId)))
          .execute()
    }
  }

  override suspend fun remove(charId: Long, mailId: Long): Boolean =
      withContext(dispatcher) {
        dsl.deleteFrom(MAIL_MESSAGES)
            .where(MAIL_MESSAGES.ID.eq(mailId).and(MAIL_MESSAGES.OWNER_ID.eq(charId)))
            .execute() > 0
      }

  /** Newest first, which is the order the official client's list is in. */
  private suspend fun pageOf(charId: Long, box: Int): List<MailCopy> =
      withContext(dispatcher) {
        dsl.selectFrom(MAIL_MESSAGES)
            .where(MAIL_MESSAGES.OWNER_ID.eq(charId).and(MAIL_MESSAGES.BOX.eq(box.toShort())))
            .orderBy(MAIL_MESSAGES.ID.desc())
            .limit(MAILBOX_CAP)
            .fetch()
            .map { it.toCopy() }
      }

  private suspend fun countOf(charId: Long, box: Int): Int =
      withContext(dispatcher) {
        dsl.fetchCount(
            MAIL_MESSAGES,
            MAIL_MESSAGES.OWNER_ID.eq(charId).and(MAIL_MESSAGES.BOX.eq(box.toShort())))
      }
}

/* The id column is the database's: a new copy leaves it untouched. */
private fun MailCopy.toRecord(): MailMessagesRecord =
    MailMessagesRecord(
        id = null,
        ownerId = ownerId,
        box = box.toShort(),
        senderId = senderId,
        senderName = senderName,
        recipientId = recipientId,
        recipientName = recipientName,
        subject = subject,
        body = body,
        sentAt = LocalDateTime.ofEpochSecond(sentAt.toLong(), 0, ZoneOffset.UTC),
        unread = unread)

private fun MailMessagesRecord.toCopy(): MailCopy =
    MailCopy(
        id = id!!,
        ownerId = ownerId,
        box = box.toInt(),
        senderId = senderId,
        senderName = senderName,
        recipientId = recipientId,
        recipientName = recipientName,
        subject = subject,
        body = body,
        sentAt = sentAt.toEpochSecond(ZoneOffset.UTC).toInt(),
        unread = unread == true)

/** The graph a test builds without a database. Same ordering rule: newest first. */
class InMemoryMailRepository : MailRepository {
  private val nextId = AtomicLong(1)
  private val rows = ConcurrentHashMap<Long, MutableList<MailCopy>>()

  override suspend fun inboxOf(charId: Long): List<MailCopy> = boxOf(charId, MAIL_BOX_IN)

  override suspend fun sentOf(charId: Long): List<MailCopy> = boxOf(charId, MAIL_BOX_SENT)

  override suspend fun inboxCount(charId: Long): Int = boxOf(charId, MAIL_BOX_IN).size

  override suspend fun sentCount(charId: Long): Int = boxOf(charId, MAIL_BOX_SENT).size

  override suspend fun unreadCount(charId: Long): Int =
      boxOf(charId, MAIL_BOX_IN).count { it.unread }

  override suspend fun deliver(inbox: MailCopy, sent: MailCopy) {
    for (copy in listOf(inbox, sent)) {
      rows
          .getOrPut(copy.ownerId) { CopyOnWriteArrayList() }
          .add(0, copy.copy(id = nextId.getAndIncrement()))
    }
  }

  override suspend fun find(charId: Long, mailId: Long): MailCopy? =
      rows[charId]?.firstOrNull { it.id == mailId }

  override suspend fun markRead(charId: Long, mailId: Long) {
    val list = rows[charId] ?: return
    val at = list.indexOfFirst { it.id == mailId }
    if (at >= 0) list[at] = list[at].copy(unread = false)
  }

  override suspend fun remove(charId: Long, mailId: Long): Boolean =
      rows[charId]?.removeAll { it.id == mailId } == true

  private fun boxOf(charId: Long, box: Int): List<MailCopy> =
      rows[charId]?.filter { it.box == box } ?: emptyList()
}
