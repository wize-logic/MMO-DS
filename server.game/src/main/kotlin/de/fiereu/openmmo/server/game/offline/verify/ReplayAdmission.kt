package de.fiereu.openmmo.server.game.offline.verify

import java.time.LocalDateTime

/** What replaying a chain would prove, once somebody has run it. */
enum class ReplayVerdict {
  /** Kept as evidence, and nothing asked about it yet. A chain's state, never a request's. */
  HELD,
  /** Asked for and not yet answered. A request's state. */
  PENDING,
  /** Every link replayed from a trusted anchor to the image the record claims. */
  VERIFIED,
  /**
   * A hash disagreed at a named link and frame: the engine says these inputs did not produce that
   * save. That is a moderator's evidence of an edit, and the only verdict a fee is kept on.
   */
  DIVERGED,
  /**
   * A crash, the wall-clock kill, or the port refusing a script it had already accepted. Evidence
   * of a port bug or a lost file and of nothing about the player, so the money goes back.
   */
  INCONCLUSIVE,
  /** Refused before a frame was replayed, or aged out of the queue. The reason is written down. */
  UNVERIFIABLE,
}

/** Whether a chain is kept as evidence at all. */
sealed interface Admission {
  /** [frames] is what replaying the whole chain would run, which is what the caps are on. */
  data class Kept(val frames: Long) : Admission

  data class Unverifiable(val why: String) : Admission
}

/** Whether a request is taken, and what the player pays for it. */
sealed interface RequestAdmission {
  /** [frames] is the most the walk can run; [fee] is taken on it and the unused part comes back. */
  data class Queued(val frames: Long, val fee: Int) : RequestAdmission

  data class Refused(val why: String) : RequestAdmission
}

/** What this server knows that the chain cannot tell it. */
data class VerifierState(val trustedAnchors: Set<String>, val pinnedRevisions: Set<Int>)

/** What this server knows about the character asking, at the moment they ask. */
data class RequestState(
    val framesSpentThisWeek: Long,
    val freeFramesLeftThisWeek: Long,
    val requestsInFlightForCharacter: Int,
    val queueDepth: Int,
)

/**
 * Rules 2 and 3: everything that can refuse a chain before a single frame is replayed, and then
 * what can refuse an ask about it.
 */
object ReplayAdmission {

  fun check(
      chain: SessionChain,
      uploadedSaveSha256: String,
      state: VerifierState,
      limits: ReplayLimits,
      now: LocalDateTime,
  ): Admission {
    if (limits.cores == 0) return no("verification is switched off on this server")
    if (chain.links.isEmpty()) return no("the chain has no sessions in it")
    if (chain.bytes > limits.chainBytes) {
      return no(
          "the chain is ${chain.bytes} bytes, over the ${limits.chainBytes} this server takes")
    }

    // The root. A chain whose first session booted from anything but the anchor it names has
    // nowhere honest to start, and no amount of replaying fixes that.
    val first = chain.links.first()
    val anchor = chain.anchorSha256
    if (anchor == null) {
      // No file at all, which is a New Game, honest only from a character that has never had a
      // copy handed to it here. One that has is claiming its play began before a game this server
      // itself wrote out.
      if (state.trustedAnchors.isNotEmpty()) {
        return no("the chain says it started from no saved game, and this character was handed one")
      }
    } else if (anchor !in state.trustedAnchors) {
      return no("the chain claims an anchor this server did not write")
    }
    if (first.bootSha256 != anchor) {
      return no(
          if (anchor == null)
              "the first session booted from a save that was here before it, so there is nothing to" +
                  " start from: play once online, or start a new file"
          else "the first session did not boot from the copy the chain says it started from")
    }

    var frames = 0L
    var previous: SessionLink? = null
    chain.links.forEachIndexed { ordinal, link ->
      val where = "session ${ordinal + 1}"

      if (link.revision == null) return no("$where does not say which build played it")
      if (link.revision !in state.pinnedRevisions) {
        return no("$where was played by build ${link.revision}, which this server cannot run")
      }

      val recording = chain.recordings[ordinal]
      when (val reading = ReplayScript.read(recording, limits.recordingBytes)) {
        is ScriptReading.Refused -> return no("$where: ${reading.why}")
        is ScriptReading.Ok -> {
          if (link.recordingSha256 != sha256(recording)) {
            return no("$where sent a recording that is not the one it wrote down")
          }
          val run = runLength(link, reading, limits)
          if (run <= 0) return no("$where has no frames to replay")
          if (link.endFrame != null && link.endFrame < reading.lastFrame) {
            return no("$where claims it ended at frame ${link.endFrame}, before its last button")
          }
          if (run > limits.linkFrames) {
            return no(
                "$where is $run frames, over the ${limits.linkFrames} one session may be; save and" +
                    " start a new one to keep it verifiable")
          }
          frames += run
        }
      }

      val before = previous
      if (before != null) {
        if (link.bootSha256 == null || link.bootSha256 != before.quitSha256) {
          return no("$where did not start from the save the one before it left")
        }
        if (!link.rtc.isAfter(before.rtc)) {
          return no("$where was played before the one in front of it, by its own clock")
        }
      }
      if (link.rtc.isAfter(now)) return no("$where was played later than this server's own clock")
      previous = link
    }

    val last = chain.links.last()
    if (last.quitSha256 == null) return no("the last session left no save at all")
    if (last.quitSha256 != uploadedSaveSha256) {
      return no("the last session's save is not the file that was imported")
    }

    if (frames > limits.chainFrames) {
      return no(
          "the chain is $frames frames, over the ${limits.chainFrames} this server replays at once")
    }
    return Admission.Kept(frames)
  }

  /** Whether an ask is taken: the queue, the character's own queue, the week, and the price. */
  fun request(frames: Long, state: RequestState, limits: ReplayLimits): RequestAdmission {
    if (limits.cores == 0) return RequestAdmission.Refused("verification is switched off here")
    if (state.requestsInFlightForCharacter > 0) {
      return RequestAdmission.Refused(
          "you already have a check waiting; it answers before another is taken")
    }
    if (state.queueDepth >= limits.queueDepth) return RequestAdmission.Refused("the queue is full")
    val left = limits.weeklyBudgetLeft(state.framesSpentThisWeek)
    if (frames > left) {
      return RequestAdmission.Refused(
          "you have asked for as much replay as a week allows; try again next week")
    }
    return RequestAdmission.Queued(frames, limits.fee(frames, state.freeFramesLeftThisWeek))
  }

  /** How many frames the worker runs one link for. */
  fun runLength(link: SessionLink, reading: ScriptReading.Ok, limits: ReplayLimits): Long =
      link.endFrame ?: (reading.lastFrame + limits.crashMargin)

  private fun no(why: String): Admission = Admission.Unverifiable(why)

  private fun sha256(bytes: ByteArray): String =
      java.security.MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") {
        "%02x".format(it)
      }
}
