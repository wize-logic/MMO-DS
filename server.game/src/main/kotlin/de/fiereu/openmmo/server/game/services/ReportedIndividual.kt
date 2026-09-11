package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.server.game.config.GameServerConfig
import java.nio.ByteBuffer
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec
import javax.inject.Inject
import javax.inject.Singleton

/** One in this many captures comes out shiny, which is the odds the games have used since Gen 2. */
private const val SHINY_ODDS = 8192

/** Six IVs, five bits each, is how the record packs them. */
private const val IV_MAX = 31

/** The individual behind a monster the client says it caught. */
@Singleton
class ReportedIndividual @Inject constructor(config: GameServerConfig) {

  private val key = SecretKeySpec(config.sessionSecret, "HmacSHA256")

  /** The individual for [token], as reported by [characterId] for [dexId]. */
  fun forToken(characterId: Long, dexId: Int, token: Int): Rolled {
    val mac = Mac.getInstance("HmacSHA256")
    mac.init(key)
    val message =
        ByteBuffer.allocate(Long.SIZE_BYTES + Int.SIZE_BYTES * 2)
            .putLong(characterId)
            .putInt(dexId)
            .putInt(token)
            .array()
    val digest = mac.doFinal(message)
    val buffer = ByteBuffer.wrap(digest)
    // The personality value, which is what the nature and the gender are read out of. Taken whole
    // rather than reduced, because every one of its bits is a field somewhere.
    val seed = buffer.int
    var ivBits = 0
    for (slot in 0 until 6) {
      ivBits = ivBits or ((buffer.get().toInt() and IV_MAX) shl (slot * 5))
    }
    // The shiny check, at the odds the game rolls it at. Sixteen bits is enough to say one in
    // 8,192 exactly, and taking them from the same digest keeps the whole individual one draw.
    val shinyRoll = buffer.short.toInt() and 0xFFFF
    return Rolled(seed = seed, ivBits = ivBits, isShiny = shinyRoll % SHINY_ODDS == 0)
  }

  /** A personality value, six IVs packed five bits each, and the shiny answer. */
  data class Rolled(val seed: Int, val ivBits: Int, val isShiny: Boolean)
}
