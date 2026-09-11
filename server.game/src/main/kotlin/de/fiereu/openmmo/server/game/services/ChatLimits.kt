package de.fiereu.openmmo.server.game.services

import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/**
 * The longest line this server passes on, which is `MMO_CHAT_TEXT_MAX`, the client's own ceiling.
 */
private const val MAX_CHAT_CHARS = 128

/** How long a line may be and how fast one may be sent, for every way of saying something. */
@Singleton
class ChatLimits @Inject constructor(private val violations: ViolationLog) {

  /** How fast a player may say things. */
  private val pace = PaceLimit(burst = 6.0, perSecond = 1.0)

  /** Whether this character may say something now. A refusal is recorded and nothing is sent. */
  fun allow(characterId: Long): Boolean {
    if (pace.allow(characterId)) return true
    violations.record(
        characterId, ViolationLog.Kind.IMPOSSIBLE_PACE, "is talking faster than anybody types")
    return false
  }

  /** The line, shortened to what a client could have typed. */
  fun cut(characterId: Long?, text: String): String {
    if (text.length <= MAX_CHAT_CHARS) return text
    log.warn { "char=$characterId sent a ${text.length} character line, cut to $MAX_CHAT_CHARS" }
    return text.take(MAX_CHAT_CHARS)
  }
}
