package de.fiereu.openmmo.server.game.offline.verify

import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.pokemon.EvolutionRegistry
import de.fiereu.openmmo.server.game.storage.ChainRepository
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.Containers
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.ExportRepository
import de.fiereu.openmmo.server.game.storage.ExportVerdict
import de.fiereu.openmmo.server.game.storage.ImportRepository
import de.fiereu.openmmo.server.game.storage.InMemoryExportRepository
import de.fiereu.openmmo.server.game.storage.InMemoryRequestRepository
import de.fiereu.openmmo.server.game.storage.RequestRepository
import de.fiereu.openmmo.server.game.storage.StoredChain
import de.fiereu.openmmo.server.game.storage.StoredLink
import de.fiereu.openmmo.server.game.storage.StoredRequest
import io.github.oshai.kotlinlogging.KotlinLogging
import java.time.Duration
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext

private val log = KotlinLogging.logger {}

/** A chain of offline sessions offered as the evidence behind one import. */
data class ChainOffer(
    val characterId: Long,
    val importId: Long,
    val chain: SessionChain,
    /** The hash of the save that actually landed. The chain has to end on it. */
    val uploadedSaveSha256: String,
)

sealed interface OfferOutcome {
  /** Kept as evidence. Nothing is replayed until somebody asks. */
  data class Kept(val chainId: Long, val frames: Long) : OfferOutcome

  /** Refused before a frame was run, with the sentence the player is shown. */
  data class Unverifiable(val why: String) : OfferOutcome
}

sealed interface RequestOutcome {
  /**
   * [frames] is the most the walk can run; [fee] was taken on it and the unused part comes back.
   */
  data class Queued(val requestId: Long, val fee: Int, val frames: Long) : RequestOutcome

  data class Refused(val why: String) : RequestOutcome

  /** The replay is affordable to this server and not to the player. Nothing was taken. */
  data class CannotAfford(val fee: Int) : RequestOutcome
}

sealed interface RequeueOutcome {
  data class Queued(val requestId: Long) : RequeueOutcome

  data class Refused(val why: String) : RequeueOutcome
}

/**
 * Replay verification, end to end: what is kept, what is asked, what it costs, what is run, and
 * what it changes.
 */
class ReplayVerificationService(
    private val chains: ChainRepository,
    private val imports: ImportRepository,
    private val characters: CharacterStore,
    private val runner: ReplayRunner,
    private val limits: ReplayLimits,
    private val entityIds: EntityIdService,
    /**
     * The offline copies sessions sent as they left, of which the newest CHECKED one is the anchor.
     */
    private val exports: ExportRepository = InMemoryExportRepository(),
    /** Where a replay's wait on the port runs, off the dispatcher the sessions are served on. */
    private val blocking: CoroutineDispatcher,
    private val requests: RequestRepository = InMemoryRequestRepository(),
    /** For the one thing about a monster honest play changes that a birth check has to allow. */
    private val evolutions: EvolutionRegistry = EvolutionRegistry(),
    private val clock: () -> LocalDateTime = LocalDateTime::now,
) {

  private val verifier = ReplayVerifier(runner, limits)

  /**
   * Whether this server would replay anything at all, which is what a client asks before uploading.
   */
  fun replays(): Boolean = limits.cores > 0 && runner.revisions().isNotEmpty()

  /** Keeps a chain as the evidence behind an import, if it could ever be replayed. */
  suspend fun offer(offer: ChainOffer): OfferOutcome {
    val now = clock()
    val state =
        VerifierState(
            // Every checked copy this character still has here, not only the newest: a game
            // carried to another machine comes home naming the copy it left on, and this character
            // may well have carried another one out in the meantime.
            trustedAnchors =
                exports
                    .listFor(offer.characterId, ExportAnchorService.KEEP)
                    .filter { it.verdict == ExportVerdict.CHECKED }
                    .map { it.sha256 }
                    .toSet(),
            pinnedRevisions = runner.revisions())
    val admission = ReplayAdmission.check(offer.chain, offer.uploadedSaveSha256, state, limits, now)
    if (admission is Admission.Unverifiable) {
      imports.markReplay(offer.importId, ReplayVerdict.UNVERIFIABLE.name, null)
      log.info { "chain for character ${offer.characterId} not kept: ${admission.why}" }
      return OfferOutcome.Unverifiable(admission.why)
    }
    val kept = admission as Admission.Kept

    val id = entityIds.newChainId()
    chains.record(
        StoredChain(
            id = id,
            characterId = offer.characterId,
            importId = offer.importId,
            uploadedAt = now,
            anchorSha256 = offer.chain.anchorSha256,
            linkCount = offer.chain.links.size,
            frameTotal = kept.frames,
            verifiedLink = -1,
            verdict = ReplayVerdict.HELD,
        ),
        offer.chain.links.mapIndexed { i, link -> StoredLink(link, offer.chain.recordings[i]) })
    imports.markReplay(offer.importId, ReplayVerdict.HELD.name, null)
    log.info {
      "chain $id for character ${offer.characterId} kept: ${offer.chain.links.size} session(s)," +
          " ${kept.frames} frames behind import ${offer.importId}"
    }
    return OfferOutcome.Kept(id, kept.frames)
  }

  /**
   * The chain a request of this character's would be on: the one behind their newest standing
   * import. An older import's chain is evidence about a character that no longer exists.
   */
  suspend fun chainFor(characterId: Long): StoredChain? {
    val newest = imports.newestStanding(characterId) ?: return null
    return chains.listFor(characterId, LOOK_BACK).firstOrNull { it.importId == newest.id }
  }

  /** Asks for one monster, by [pid], its personality, or, with null, for the whole chain. */
  suspend fun request(characterId: Long, pid: Int?): RequestOutcome {
    if (!replays()) return RequestOutcome.Refused("this server is not checking offline play")
    val chain =
        chainFor(characterId)
            ?: return RequestOutcome.Refused(
                "no sessions of yours are on file behind your newest save")
    if (chain.verdict == ReplayVerdict.DIVERGED) {
      return RequestOutcome.Refused(
          "the play behind your save did not produce it at session" +
              " ${(chain.divergedLink ?: 0) + 1}; nothing past that can be checked")
    }
    if (pid != null) {
      val monster =
          monsterOf(characterId, pid)
              ?: return RequestOutcome.Refused("no monster of yours has that personality")
      if (!monster.offlineOrigin) return RequestOutcome.Refused("that monster is already trusted")
    } else if (chain.verdict == ReplayVerdict.VERIFIED && !stillMarked(characterId)) {
      return RequestOutcome.Refused("the play behind your save has been checked to its end")
    }
    val sessions =
        chains.sessions(chain.id)
            ?: return RequestOutcome.Refused("the recordings behind your save are gone")
    val frames = framesLeft(sessions, chain.verifiedLink)

    val now = clock()
    val spent = requests.framesSince(characterId, now.minus(WEEK))
    val state =
        RequestState(
            framesSpentThisWeek = spent,
            freeFramesLeftThisWeek = (limits.freeFrames - spent).coerceAtLeast(0),
            requestsInFlightForCharacter = requests.inFlightFor(characterId),
            queueDepth = requests.depth(),
        )
    val admission = ReplayAdmission.request(frames, state, limits)
    if (admission is RequestAdmission.Refused) return RequestOutcome.Refused(admission.why)
    val queued = admission as RequestAdmission.Queued

    // Taken at the request, so a queue of asks nobody can pay for cannot form. It comes back on
    // every answer that was about this server rather than about the save.
    if (queued.fee > 0 && !characters.addMoney(characterId, -queued.fee)) {
      return RequestOutcome.CannotAfford(queued.fee)
    }
    val id = entityIds.newRequestId()
    requests.record(
        StoredRequest(
            id = id,
            chainId = chain.id,
            characterId = characterId,
            monsterPid = pid,
            requestedAt = now,
            frameBudget = frames,
            freeFramesLeft = state.freeFramesLeftThisWeek,
            feePaid = queued.fee,
        ))
    chain.importId?.let { imports.markReplay(it, ReplayVerdict.PENDING.name, null) }
    log.info {
      "request $id for character $characterId: " +
          (if (pid == null) "the whole chain" else "monster %08x".format(pid)) +
          " on chain ${chain.id}, up to $frames frames, fee ${queued.fee}, ${state.queueDepth + 1}" +
          " waiting"
    }
    return RequestOutcome.Queued(id, queued.fee, frames)
  }

  /** Age out what has waited too long, then hand over the oldest request left that nobody holds. */
  suspend fun claimNext(busy: Set<Long> = emptySet()): StoredRequest? {
    if (limits.cores == 0) return null
    val now = clock()
    val waiting = requests.pending(limits.queueDepth).filter { it.id !in busy }
    if (waiting.isEmpty()) return null

    val stale = now.minusDays(limits.queueDays.toLong())
    for (old in waiting.filter { it.waitingSince.isBefore(stale) }) {
      finish(old, ReplayVerdict.UNVERIFIABLE, "the queue", null, 0)
    }
    return waiting.firstOrNull { !it.waitingSince.isBefore(stale) }
  }

  /** One request to its verdict. */
  suspend fun replay(request: StoredRequest) {
    val chain =
        chains.find(request.chainId)
            ?: return finish(request, ReplayVerdict.INCONCLUSIVE, "the chain is gone", null, 0)
    if (chain.verdict == ReplayVerdict.DIVERGED) {
      return finish(
          request,
          ReplayVerdict.INCONCLUSIVE,
          "the play behind this save had already diverged when this was run",
          null,
          0)
    }
    val sessions =
        chains.sessions(chain.id)
            ?: return finish(
                request, ReplayVerdict.INCONCLUSIVE, "the recordings are gone", null, 0)
    val stored = chains.frontier(chain.id) ?: Frontier(-1, null)
    // A chain that starts from a copy starts from the copy's image, which the frontier holds
    // only once the first link has been replayed.
    val from =
        if (stored.link < 0 && stored.image == null && chain.anchorSha256 != null) {
          val image = exports.checkedImage(chain.characterId, chain.anchorSha256)
          if (image == null) {
            return finish(
                request,
                ReplayVerdict.INCONCLUSIVE,
                "the offline copy this chain starts from is no longer here",
                null,
                0)
          }
          Frontier(-1, image)
        } else stored

    // The whole walk is done with the character loaded: a birth is judged against the monster the
    // character holds, and the player is usually gone by the time a lane gets here.
    val reached =
        characters.withLoaded(request.characterId) { walk(request, chain, sessions, from) }
    if (reached == null) {
      finish(request, ReplayVerdict.UNVERIFIABLE, "the character is gone", null, 0)
    }
  }

  private suspend fun walk(
      request: StoredRequest,
      chain: StoredChain,
      sessions: SessionChain,
      from: Frontier,
  ) {
    val pid = request.monsterPid
    try {
      if (pid != null && from.image != null) {
        // Born at or before the frontier: the copy, or a session already agreed with. Nothing needs
        // replaying; the monster is read off the image this server already trusts.
        val report = withContext(blocking) { reportOf(from.image, sessions) }
        val born = report?.let { MonsterBirth.find(it, pid) }
        if (born != null) {
          return judgeBirth(request, chain, born, from.link, 0)
        }
      }
      if (from.link >= sessions.links.lastIndex) {
        // Nothing left to replay and the monster was not at the frontier.
        if (pid != null) {
          return finish(
              request,
              ReplayVerdict.UNVERIFIABLE,
              "the play behind this save holds no such monster in its party or boxes",
              from.link,
              0)
        }
        // The whole chain, with every session already agreed with: what the play ended holding is
        // read off the image the frontier stands on.
        val report =
            from.image?.let { image -> withContext(blocking) { reportOf(image, sessions) } }
        return finish(
            request,
            ReplayVerdict.VERIFIED,
            null,
            from.link,
            0,
            report?.let { MonsterBirth.pids(it) })
      }
      val until: ((ByteArray) -> Boolean)? =
          pid?.let { { report -> MonsterBirth.holds(report, it) } }
      val result =
          withContext(blocking) { verifier.verify(sessions, from, sessions.links.lastIndex, until) }
      if (result.frontier.link > from.link) chains.advance(chain.id, result.frontier)
      when (result.verdict) {
        ReplayVerdict.DIVERGED -> {
          chains.mark(
              chain.id,
              ReplayVerdict.DIVERGED,
              result.reason,
              result.divergedLink,
              result.divergedFrame)
          chain.importId?.let {
            imports.markReplay(it, ReplayVerdict.DIVERGED.name, result.divergedFrame)
          }
          log.warn {
            "chain ${chain.id} for character ${chain.characterId}: the play behind import" +
                " ${chain.importId} does not produce the save it was imported from, at session" +
                " ${result.divergedLink?.plus(1)} frame ${result.divergedFrame}"
          }
          finish(request, ReplayVerdict.DIVERGED, result.reason, result.stoppedAt, result.framesRun)
        }
        ReplayVerdict.VERIFIED -> {
          if (result.frontier.link >= sessions.links.lastIndex) {
            chains.mark(chain.id, ReplayVerdict.VERIFIED, null, null, null)
          }
          if (pid == null) {
            // The last session's quit is the save that was imported, so the monsters it names are
            // the ones this play produced, and they are the only ones the verdict speaks for.
            return finish(
                request,
                ReplayVerdict.VERIFIED,
                null,
                result.stoppedAt,
                result.framesRun,
                result.report?.let { MonsterBirth.pids(it) })
          }
          val born = result.report?.let { MonsterBirth.find(it, pid) }
          if (born == null) {
            // Replayed to the end and never seen. The save is that last quit, so the monster was
            // not in a party or a box the report reads; the day care is the honest way there.
            return finish(
                request,
                ReplayVerdict.UNVERIFIABLE,
                "the play behind this save holds no such monster in its party or boxes",
                result.stoppedAt,
                result.framesRun)
          }
          judgeBirth(request, chain, born, result.stoppedAt, result.framesRun)
        }
        else -> finish(request, result.verdict, result.reason, result.stoppedAt, result.framesRun)
      }
    } catch (e: RuntimeException) {
      log.warn(e) {
        "request ${request.id} for character ${request.characterId}: the replay failed"
      }
      finish(request, ReplayVerdict.INCONCLUSIVE, "the replay itself failed: ${e.message}", null, 0)
    }
  }

  /**
   * One request's worth of work, for a caller with one lane. False when there was nothing to do.
   */
  suspend fun runNext(): Boolean {
    val next = claimNext() ?: return false
    replay(next)
    return true
  }

  /** A moderator putting a settled request back to be run again, once. */
  suspend fun requeue(request: StoredRequest, by: String): RequeueOutcome {
    if (!replays()) {
      return RequeueOutcome.Refused(
          "this server is not replaying offline play, so the check would only wait")
    }
    when (request.verdict) {
      ReplayVerdict.PENDING ->
          return RequeueOutcome.Refused("check ${request.id} is still waiting to be run")
      ReplayVerdict.VERIFIED ->
          return RequeueOutcome.Refused("check ${request.id} was verified; there is nothing to ask")
      else -> {}
    }
    if (request.requeuedAt != null) {
      return RequeueOutcome.Refused(
          "check ${request.id} was already put back once, by ${request.requeuedBy}")
    }
    // The admission rule, held here too: one check of a character's in flight at a time.
    if (requests.inFlightFor(request.characterId) > 0) {
      return RequeueOutcome.Refused("another check of that character's is already waiting")
    }
    if (!requests.requeue(request.id, by, clock())) {
      return RequeueOutcome.Refused("check ${request.id} could not be put back")
    }
    chains.find(request.chainId)?.importId?.let {
      imports.markReplay(it, ReplayVerdict.PENDING.name, null)
    }
    log.info {
      "request ${request.id} for character ${request.characterId} put back in the queue by $by" +
          " (was ${request.verdict}${request.verdictReason?.let { ": $it" } ?: ""})"
    }
    return RequeueOutcome.Queued(request.id)
  }

  /** Whether the character still wears the mark somewhere a verified chain could take it off. */
  private suspend fun stillMarked(characterId: Long): Boolean {
    val stored = characters.getOrLoadCharacter(characterId) ?: return false
    return (stored.pokemon + stored.pcStorage).any { it.offlineOrigin }
  }

  /** The monster of [pid] the character holds, wherever it is, or null. */
  private suspend fun monsterOf(characterId: Long, pid: Int): Pokemon? {
    val stored = characters.getOrLoadCharacter(characterId) ?: return null
    return (stored.pokemon + stored.pcStorage + stored.daycare).firstOrNull { it.seed == pid }
  }

  /** Frames between the frontier and the end: the most a walk from here can run. */
  private fun framesLeft(sessions: SessionChain, verifiedLink: Int): Long =
      ((verifiedLink + 1)..sessions.links.lastIndex).sumOf { ordinal ->
        val reading = ReplayScript.read(sessions.recordings[ordinal], limits.recordingBytes)
        if (reading is ScriptReading.Ok) {
          ReplayAdmission.runLength(sessions.links[ordinal], reading, limits)
        } else 0L
      }

  /**
   * What an image this server trusts holds, read the way the game itself reads it: booted for a few
   * frames with nothing pressed, so the exit reporter writes the loaded image down. Null when the
   * worker could not boot it.
   */
  private fun reportOf(image: ByteArray, sessions: SessionChain): ByteArray? {
    val revision =
        sessions.links.mapNotNull { it.revision }.maxOrNull()
            ?: runner.revisions().maxOrNull()
            ?: return null
    val outcome =
        runner.run(
            ReplayRun(
                revision = revision,
                bootImage = image,
                rtc = clock(),
                recording = ByteArray(0),
                frames = ExportAnchorService.CHECK_FRAMES,
            ))
    return (outcome as? ReplayOutcome.Quit)?.report
  }

  /** The monster was found born honestly; whether the one the character holds is still that one. */
  private suspend fun judgeBirth(
      request: StoredRequest,
      chain: StoredChain,
      born: de.fiereu.openmmo.server.game.offline.OfflineMonster,
      stoppedAt: Int?,
      framesRun: Long,
  ) {
    val now = monsterOf(chain.characterId, born.pid)
    if (now == null) {
      return finish(
          request,
          ReplayVerdict.UNVERIFIABLE,
          "the monster is no longer this character's",
          stoppedAt,
          framesRun)
    }
    val why = MonsterBirth.differences(now, born, evolutions)
    if (why != null) {
      return finish(
          request,
          ReplayVerdict.DIVERGED,
          "the monster in the save is not the one born at session ${(stoppedAt ?: -1) + 1}: $why",
          stoppedAt,
          framesRun)
    }
    finish(request, ReplayVerdict.VERIFIED, null, stoppedAt, framesRun)
  }

  /** The last word on a request: the verdict, the money back if it is owed, and the mark. */
  private suspend fun finish(
      request: StoredRequest,
      verdict: ReplayVerdict,
      reason: String?,
      stoppedLink: Int?,
      framesRun: Long,
      playHeld: Set<Int>? = null,
  ) {
    // Exactly what this verdict speaks for: the one monster that was asked about, or the monsters
    // the verified play ended holding.
    val born = request.monsterPid?.let { setOf(it) } ?: playHeld
    val kept =
        if (keepsTheFee(verdict, saidWhatItHeld = born != null))
            limits.fee(framesRun, request.freeFramesLeft)
        else 0
    val refund = (request.feeHeld - kept).coerceAtLeast(0)
    val settled =
        requests.settle(
            request.id,
            verdict,
            reason,
            stoppedLink,
            framesRun,
            request.feeRefunded + refund,
            clock())
    if (!settled) return
    val chain = chains.find(request.chainId)
    // The import row reads the chain's own state once the ask is answered: HELD while sessions
    // remain, VERIFIED or DIVERGED once the chain is finished either way.
    chain?.importId?.let { imports.markReplay(it, chain.verdict.name, chain.divergedFrame) }
    log.info {
      "request ${request.id} for character ${request.characterId}: $verdict" +
          (reason?.let { " ($it)" } ?: "") +
          " after $framesRun frames" +
          (if (refund > 0) ", $refund back" else "")
    }

    val clears = clearsTheMark(verdict) && chain != null && standsNewest(chain)
    if (clears && born == null) {
      log.warn {
        "request ${request.id}: the play behind character ${request.characterId}'s save verified," +
            " but what it ended holding could not be read, so no mark came off and the fee went" +
            " back"
      }
    }
    if (refund == 0 && !(clears && born != null)) return
    // The player is usually gone by the time a verdict lands, so the character is reached where it
    // is: loaded for this if it has to be, and let go again after.
    val reached =
        characters.withLoaded(request.characterId) {
          if (refund > 0 && !characters.addMoney(request.characterId, refund)) {
            log.warn {
              "request ${request.id}: $refund could not be given back to character" +
                  " ${request.characterId}"
            }
          }
          if (clears && born != null) {
            characters.rearrangeMonsters(request.characterId) { party, pc, daycare ->
              fun List<Pokemon>.cleared() = map {
                if (it.seed in born) it.copy(offlineOrigin = false) else it
              }
              Containers(party.cleared(), pc.cleared(), daycare.cleared())
            }
          }
        }
    if (reached == null) {
      log.warn {
        "request ${request.id}: character ${request.characterId} is gone, so nothing went back" +
            " or cleared"
      }
    }
  }

  /**
   * Only the newest standing import may be cleared by its own chain. A later import replaced the
   * character wholesale, so its monsters are marked for a reason an older chain says nothing about,
   * and clearing on ordinal alone would launder them.
   */
  private suspend fun standsNewest(chain: StoredChain): Boolean {
    val newest =
        chain.importId?.let { id ->
          imports.newestStanding(chain.characterId)?.takeIf { it.id == id }
        }
    if (newest == null) {
      log.info {
        "character ${chain.characterId}: play verified, but a later import stands, so the mark stays"
      }
      return false
    }
    return true
  }

  private companion object {
    val WEEK: Duration = Duration.ofDays(7)

    /** How far back in a character's chains the one behind the newest import is looked for. */
    const val LOOK_BACK = 50
  }
}
