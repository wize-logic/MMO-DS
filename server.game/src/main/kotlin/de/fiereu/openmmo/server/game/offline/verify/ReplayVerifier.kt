package de.fiereu.openmmo.server.game.offline.verify

import io.github.oshai.kotlinlogging.KotlinLogging

private val log = KotlinLogging.logger {}

/** How far this server has replayed a chain itself, and the image it ended holding. */
data class Frontier(val link: Int, val image: ByteArray?) {
  override fun equals(other: Any?): Boolean =
      this === other ||
          (other is Frontier &&
              link == other.link &&
              (image?.contentEquals(other.image) ?: (other.image == null)))

  override fun hashCode(): Int = 31 * link + (image?.size ?: 0)
}

/** What a walk of the chain found, and where it left the frontier. */
data class Verification(
    val verdict: ReplayVerdict,
    val frontier: Frontier,
    val framesRun: Long,
    val reason: String? = null,
    val divergedLink: Int? = null,
    val divergedFrame: Long? = null,
    val stoppedAt: Int? = null,
    val report: ByteArray? = null,
) {
  override fun equals(other: Any?): Boolean =
      this === other ||
          (other is Verification &&
              verdict == other.verdict &&
              frontier == other.frontier &&
              framesRun == other.framesRun &&
              reason == other.reason &&
              divergedLink == other.divergedLink &&
              divergedFrame == other.divergedFrame &&
              stoppedAt == other.stoppedAt &&
              (report?.contentEquals(other.report) ?: (other.report == null)))

  override fun hashCode(): Int = 31 * verdict.hashCode() + frontier.hashCode()
}

/** Replaying a chain forward from a trusted anchor. */
class ReplayVerifier(
    private val runner: ReplayRunner,
    private val limits: ReplayLimits,
) {

  /** Advance [from] up to and including session [target], and say what happened. */
  fun verify(
      chain: SessionChain,
      from: Frontier,
      target: Int,
      until: ((ByteArray) -> Boolean)? = null,
  ): Verification {
    require(target < chain.links.size) { "there is no session $target in this chain" }
    var frontier = from
    var ran = 0L
    var stopped: Int? = null
    var report: ByteArray? = null

    for (ordinal in (from.link + 1)..target) {
      val link = chain.links[ordinal]
      val recording = chain.recordings[ordinal]
      val reading = ReplayScript.read(recording, limits.recordingBytes)
      if (reading is ScriptReading.Refused) {
        // Admission already read this file once. Getting here means the bytes changed underneath
        // the request, which is a fault of this server's own store and not of the player's.
        return inconclusive(frontier, ran, stopped, "session ${ordinal + 1}: ${reading.why}")
      }
      val frames = ReplayAdmission.runLength(link, reading as ScriptReading.Ok, limits)

      val outcome =
          runner.run(
              ReplayRun(
                  revision =
                      link.revision
                          ?: return inconclusive(
                              frontier, ran, stopped, "session ${ordinal + 1} names no build"),
                  bootImage = frontier.image,
                  rtc = link.rtc,
                  recording = recording,
                  frames = frames,
              ))
      ran += frames

      when (outcome) {
        is ReplayOutcome.Broke ->
            return inconclusive(frontier, ran, stopped, "session ${ordinal + 1}: ${outcome.why}")
        is ReplayOutcome.Quit -> {
          if (outcome.sha256 != link.quitSha256) {
            log.info {
              "replay diverged at session ${ordinal + 1} of ${chain.links.size}: the recorded input" +
                  " produced ${outcome.sha256}, the record claims ${link.quitSha256}"
            }
            return Verification(
                verdict = ReplayVerdict.DIVERGED,
                frontier = frontier,
                framesRun = ran,
                reason = "session ${ordinal + 1} did not produce the save it says it did",
                divergedLink = ordinal,
                divergedFrame = frames,
                stoppedAt = stopped,
            )
          }
          frontier = Frontier(ordinal, outcome.image)
          stopped = ordinal
          report = outcome.report
          if (until != null && outcome.report != null && until(outcome.report)) break
        }
      }
    }
    return Verification(ReplayVerdict.VERIFIED, frontier, ran, stoppedAt = stopped, report = report)
  }

  private fun inconclusive(frontier: Frontier, ran: Long, stopped: Int?, why: String) =
      Verification(ReplayVerdict.INCONCLUSIVE, frontier, ran, why, stoppedAt = stopped)
}

/** What a verdict costs, which is the other half of the fee being honest. */
fun keepsTheFee(verdict: ReplayVerdict, saidWhatItHeld: Boolean): Boolean =
    when (verdict) {
      ReplayVerdict.VERIFIED -> saidWhatItHeld
      ReplayVerdict.DIVERGED -> true
      ReplayVerdict.INCONCLUSIVE,
      ReplayVerdict.UNVERIFIABLE -> false
      ReplayVerdict.HELD,
      ReplayVerdict.PENDING -> true
    }

/** What goes back of [paid], which is all of it or none. */
fun refundFor(verdict: ReplayVerdict, paid: Int, saidWhatItHeld: Boolean = true): Int =
    if (keepsTheFee(verdict, saidWhatItHeld)) 0 else paid

/** The one verdict that takes the provenance mark off anything. */
fun clearsTheMark(verdict: ReplayVerdict): Boolean = verdict == ReplayVerdict.VERIFIED
