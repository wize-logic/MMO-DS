package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.db.game.tables.references.IMPORT_CHAINS
import de.fiereu.openmmo.db.game.tables.references.IMPORT_CHAIN_LINKS
import de.fiereu.openmmo.server.game.offline.verify.Frontier
import de.fiereu.openmmo.server.game.offline.verify.ReplayVerdict
import de.fiereu.openmmo.server.game.offline.verify.SessionChain
import de.fiereu.openmmo.server.game.offline.verify.SessionLink
import java.time.LocalDateTime
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Named
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import org.jooq.DSLContext

/**
 * A chain as a person reads it back: the evidence behind an import, and how far it has been
 * replayed.
 */
data class StoredChain(
    val id: Long,
    val characterId: Long,
    val importId: Long?,
    val uploadedAt: LocalDateTime,
    val anchorSha256: String?,
    val linkCount: Int,
    val frameTotal: Long,
    /** The last session replayed and agreed with, or -1 for none. Only ever moves forward. */
    val verifiedLink: Int,
    /**
     * HELD while sessions remain to be replayed, VERIFIED once the frontier is at the end, DIVERGED
     * once a session did not produce the save it says it did, and nothing past that is ever run.
     */
    val verdict: ReplayVerdict,
    val verdictReason: String? = null,
    val divergedLink: Int? = null,
    val divergedFrame: Long? = null,
)

/** One session's record and the recording it is. */
data class StoredLink(val link: SessionLink, val recording: ByteArray) {
  override fun equals(other: Any?): Boolean =
      this === other ||
          (other is StoredLink && link == other.link && recording.contentEquals(other.recording))

  override fun hashCode(): Int = 31 * link.hashCode() + recording.size
}

interface ChainRepository {

  /** Writes the chain and its sessions, or neither. */
  suspend fun record(chain: StoredChain, links: List<StoredLink>)

  suspend fun find(id: Long): StoredChain?

  /** The sessions and their recordings, in order, with the anchor the chain claimed. */
  suspend fun sessions(id: Long): SessionChain?

  /** How far the worker got, and the image it holds there. Null when the chain is gone. */
  suspend fun frontier(id: Long): Frontier?

  /** Moves the frontier forward. A frontier never goes back, so a lower ordinal is ignored. */
  suspend fun advance(id: Long, frontier: Frontier)

  /**
   * The chain's state: DIVERGED where a session disagreed, VERIFIED once the frontier is at the
   * end. Only a HELD chain changes; a chain that diverged is finished for good.
   */
  suspend fun mark(
      id: Long,
      verdict: ReplayVerdict,
      reason: String?,
      divergedLink: Int?,
      divergedFrame: Long?,
  ): Boolean

  /** A character's chains, newest upload first. */
  suspend fun listFor(characterId: Long, limit: Int): List<StoredChain>
}

class JooqChainRepository
@Inject
constructor(
    private val dsl: DSLContext,
    @param:Named("db") private val dispatcher: CoroutineDispatcher,
) : ChainRepository {

  override suspend fun record(chain: StoredChain, links: List<StoredLink>) {
    withContext(dispatcher) {
      dsl.transaction { tx ->
        val ctx = tx.dsl()
        ctx.insertInto(IMPORT_CHAINS)
            .set(IMPORT_CHAINS.ID, chain.id)
            .set(IMPORT_CHAINS.CHARACTER_ID, chain.characterId)
            .set(IMPORT_CHAINS.IMPORT_ID, chain.importId)
            .set(IMPORT_CHAINS.UPLOADED_AT, chain.uploadedAt)
            .set(IMPORT_CHAINS.ANCHOR_SHA256, chain.anchorSha256)
            .set(IMPORT_CHAINS.LINK_COUNT, chain.linkCount)
            .set(IMPORT_CHAINS.FRAME_TOTAL, chain.frameTotal)
            .set(IMPORT_CHAINS.VERIFIED_LINK, chain.verifiedLink)
            .set(IMPORT_CHAINS.VERDICT, chain.verdict.name)
            .set(IMPORT_CHAINS.VERDICT_REASON, chain.verdictReason)
            .set(IMPORT_CHAINS.DIVERGED_LINK, chain.divergedLink)
            .set(IMPORT_CHAINS.DIVERGED_FRAME, chain.divergedFrame)
            .execute()
        links.forEachIndexed { ordinal, stored ->
          ctx.insertInto(IMPORT_CHAIN_LINKS)
              .set(IMPORT_CHAIN_LINKS.CHAIN_ID, chain.id)
              .set(IMPORT_CHAIN_LINKS.ORDINAL, ordinal)
              .set(IMPORT_CHAIN_LINKS.REVISION, stored.link.revision)
              .set(IMPORT_CHAIN_LINKS.RTC, stored.link.rtc)
              .set(IMPORT_CHAIN_LINKS.BOOT_SHA256, stored.link.bootSha256)
              .set(IMPORT_CHAIN_LINKS.RECORDING_SHA256, stored.link.recordingSha256)
              .set(IMPORT_CHAIN_LINKS.QUIT_SHA256, stored.link.quitSha256)
              .set(IMPORT_CHAIN_LINKS.END_FRAME, stored.link.endFrame)
              .set(IMPORT_CHAIN_LINKS.RECORDING, stored.recording)
              .execute()
        }
      }
    }
  }

  override suspend fun find(id: Long): StoredChain? =
      withContext(dispatcher) {
        dsl.selectFrom(IMPORT_CHAINS).where(IMPORT_CHAINS.ID.eq(id)).fetchOne()?.toChain()
      }

  override suspend fun sessions(id: Long): SessionChain? =
      withContext(dispatcher) {
        val anchor =
            dsl.select(IMPORT_CHAINS.ANCHOR_SHA256)
                .from(IMPORT_CHAINS)
                .where(IMPORT_CHAINS.ID.eq(id))
                .fetchOne() ?: return@withContext null
        val rows =
            dsl.selectFrom(IMPORT_CHAIN_LINKS)
                .where(IMPORT_CHAIN_LINKS.CHAIN_ID.eq(id))
                .orderBy(IMPORT_CHAIN_LINKS.ORDINAL.asc())
                .fetch()
        SessionChain(
            anchorSha256 = anchor.value1(),
            links =
                rows.map {
                  SessionLink(
                      version =
                          de.fiereu.openmmo.server.game.offline.verify.SessionLinkFormat.VERSION,
                      revision = it.revision,
                      rtc = it.rtc,
                      bootSha256 = it.bootSha256,
                      recordingName = "session ${it.ordinal + 1}",
                      recordingSha256 = it.recordingSha256,
                      quitSha256 = it.quitSha256,
                      endFrame = it.endFrame,
                  )
                },
            recordings = rows.map { it.recording },
        )
      }

  override suspend fun frontier(id: Long): Frontier? =
      withContext(dispatcher) {
        dsl.select(IMPORT_CHAINS.VERIFIED_LINK, IMPORT_CHAINS.FRONTIER_IMAGE)
            .from(IMPORT_CHAINS)
            .where(IMPORT_CHAINS.ID.eq(id))
            .fetchOne()
            ?.let { (ordinal, image) -> Frontier(ordinal ?: -1, image) }
      }

  override suspend fun advance(id: Long, frontier: Frontier) {
    withContext(dispatcher) {
      dsl.update(IMPORT_CHAINS)
          .set(IMPORT_CHAINS.VERIFIED_LINK, frontier.link)
          .set(IMPORT_CHAINS.FRONTIER_IMAGE, frontier.image)
          .where(IMPORT_CHAINS.ID.eq(id))
          .and(IMPORT_CHAINS.VERIFIED_LINK.lt(frontier.link))
          .execute()
    }
  }

  override suspend fun mark(
      id: Long,
      verdict: ReplayVerdict,
      reason: String?,
      divergedLink: Int?,
      divergedFrame: Long?,
  ): Boolean =
      withContext(dispatcher) {
        dsl.update(IMPORT_CHAINS)
            .set(IMPORT_CHAINS.VERDICT, verdict.name)
            .set(IMPORT_CHAINS.VERDICT_REASON, reason?.take(REASON_MAX))
            .set(IMPORT_CHAINS.DIVERGED_LINK, divergedLink)
            .set(IMPORT_CHAINS.DIVERGED_FRAME, divergedFrame)
            .where(IMPORT_CHAINS.ID.eq(id))
            .and(IMPORT_CHAINS.VERDICT.eq(ReplayVerdict.HELD.name))
            .execute() == 1
      }

  override suspend fun listFor(characterId: Long, limit: Int): List<StoredChain> =
      withContext(dispatcher) {
        dsl.selectFrom(IMPORT_CHAINS)
            .where(IMPORT_CHAINS.CHARACTER_ID.eq(characterId))
            .orderBy(IMPORT_CHAINS.UPLOADED_AT.desc(), IMPORT_CHAINS.ID.desc())
            .limit(limit.coerceAtLeast(1))
            .fetch()
            .map { it.toChain() }
      }

  private fun de.fiereu.openmmo.db.game.tables.records.ImportChainsRecord.toChain(): StoredChain =
      StoredChain(
          id = id,
          characterId = characterId,
          importId = importId,
          uploadedAt = uploadedAt,
          anchorSha256 = anchorSha256,
          linkCount = linkCount,
          frameTotal = frameTotal,
          verifiedLink = verifiedLink,
          verdict = runCatching { ReplayVerdict.valueOf(verdict) }.getOrDefault(ReplayVerdict.HELD),
          verdictReason = verdictReason,
          divergedLink = divergedLink,
          divergedFrame = divergedFrame,
      )

  private companion object {
    /** The column's own width. A reason longer than this was going to be cut somewhere. */
    const val REASON_MAX = 256
  }
}

/** For the checks that drive the verifier without a database under them. */
class InMemoryChainRepository : ChainRepository {
  private val chains = ConcurrentHashMap<Long, StoredChain>()
  private val links = ConcurrentHashMap<Long, List<StoredLink>>()
  private val anchors = ConcurrentHashMap<Long, String>()
  private val images = ConcurrentHashMap<Long, ByteArray>()

  override suspend fun record(chain: StoredChain, links: List<StoredLink>) {
    chains[chain.id] = chain
    this.links[chain.id] = links
    chain.anchorSha256?.let { anchors[chain.id] = it }
  }

  override suspend fun find(id: Long): StoredChain? = chains[id]

  override suspend fun sessions(id: Long): SessionChain? {
    val rows = links[id] ?: return null
    return SessionChain(anchors[id], rows.map { it.link }, rows.map { it.recording })
  }

  override suspend fun frontier(id: Long): Frontier? {
    val chain = chains[id] ?: return null
    return Frontier(chain.verifiedLink, images[id])
  }

  override suspend fun advance(id: Long, frontier: Frontier) {
    val chain = chains[id] ?: return
    if (frontier.link <= chain.verifiedLink) return
    chains[id] = chain.copy(verifiedLink = frontier.link)
    if (frontier.image != null) images[id] = frontier.image
  }

  override suspend fun mark(
      id: Long,
      verdict: ReplayVerdict,
      reason: String?,
      divergedLink: Int?,
      divergedFrame: Long?,
  ): Boolean {
    val chain = chains[id] ?: return false
    if (chain.verdict != ReplayVerdict.HELD) return false
    chains[id] =
        chain.copy(
            verdict = verdict,
            verdictReason = reason,
            divergedLink = divergedLink,
            divergedFrame = divergedFrame)
    return true
  }

  override suspend fun listFor(characterId: Long, limit: Int): List<StoredChain> =
      chains.values
          .filter { it.characterId == characterId }
          .sortedWith(compareByDescending<StoredChain> { it.uploadedAt }.thenByDescending { it.id })
          .take(limit.coerceAtLeast(1))
}
