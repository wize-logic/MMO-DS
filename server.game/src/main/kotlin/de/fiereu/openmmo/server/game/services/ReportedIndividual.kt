package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.server.game.config.GameServerConfig
import java.nio.ByteBuffer
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec
import javax.inject.Inject
import javax.inject.Singleton

/** One in this many captures comes out shiny, which is the odds the games have always used. */
private const val SHINY_ODDS = 8192

/** Six IVs, five bits each, is how the record packs them. */
private const val IV_MAX = 31

/**
 * The individual behind a monster the client says it caught.
 *
 * A reported capture used to carry its own personality value, its six IVs and its shiny flag, all
 * written down as sent. That is the whole of what makes one monster worth more than another, so it
 * was also the whole of what a modified client had to edit.
 *
 * The engine really does roll these client side and no trainer id crosses the wire, so the roll
 * cannot be checked. It can be taken away. What the client sends is treated as a token, an opaque
 * name for "this capture rather than that one", and the individual is derived here from that token,
 * the character, the species and the server's own secret.
 *
 * A keyed hash rather than a fresh draw, for two reasons. The same token always names the same
 * monster, which the duplicate check depends on because a reporter re-sends a grant it has not seen
 * answered. And the client cannot steer it: without the secret there is nothing to search offline,
 * and searching online means taking each monster and paying for it out of [GrantBudget].
 */
@Singleton
class ReportedIndividual @Inject constructor(config: GameServerConfig) {

  private val key = SecretKeySpec(config.sessionSecret, "HmacSHA256")

  /**
   * The individual for [token], as reported by [characterId] for [dexId]. A token of zero is the
   * reporter saying it has no individual to name, which the caller answers before reaching here.
   */
  fun forToken(characterId: Long, dexId: Int, token: Int): Rolled {
    val mac = Mac.getInstance("HmacSHA256")
    mac.init(key)
    val message =
        ByteBuffer.allocate(Long.SIZE_BYTES + Int.SIZE_BYTES * 2)
            .putLong(characterId)
            .putInt(dexId)
            .putInt(token)
            .array()
    val buffer = ByteBuffer.wrap(mac.doFinal(message))
    // The personality value, which the nature and the gender are read out of. Taken whole.
    val seed = buffer.int
    var ivBits = 0
    for (slot in 0 until 6) {
      ivBits = ivBits or ((buffer.get().toInt() and IV_MAX) shl (slot * 5))
    }
    // The shiny check, at the odds the game rolls it at, from the same digest.
    val shinyRoll = buffer.short.toInt() and 0xFFFF
    return Rolled(seed = seed, ivBits = ivBits, isShiny = shinyRoll % SHINY_ODDS == 0)
  }

  /** A personality value, six IVs packed five bits each, and the shiny answer. */
  data class Rolled(val seed: Int, val ivBits: Int, val isShiny: Boolean)
}
