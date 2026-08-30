package de.fiereu.openmmo.server.game.battle

import de.fiereu.openmmo.items.ItemDef
import kotlin.math.floor
import kotlin.math.sqrt

/**
 * Whether a thrown ball holds, by the games' own arithmetic.
 *
 * Every throw used to catch. The ball had to be in the bag and it left the bag, so this was never
 * free in items, but it was free in luck: a Poke Ball took a full health Garchomp first go, every
 * go. That is the wrong game, and it is also the cheapest bot there is, because the reply to a
 * battle starting is one packet and the answer is always a monster.
 *
 * Gen 4's formula. `a` is the modified catch rate: the species' own rate scaled by health left and
 * by the ball. At 255 or above the ball holds with no shake; otherwise `b` is the shake probability
 * and the ball has to pass it four times.
 *
 * Status doubles or halves `a` in the games. Nothing here inflicts status yet, so that term is
 * absent rather than guessed at, and belongs here the day a monster can be put to sleep.
 */
object CatchRoll {

  /** Above this the ball holds with no shakes at all. */
  private const val CERTAIN = 255.0

  private const val SHAKE_NUMERATOR = 1_048_576.0
  private const val SHAKE_DENOMINATOR = 16_711_680.0
  private const val SHAKE_ROLL_MAX = 65536

  /** Whether [ball] holds a wild monster of [catchRate] on [currentHp] of [maxHp]. */
  fun holds(ball: ItemDef, catchRate: Int, currentHp: Int, maxHp: Int, rng: BattleRng): Boolean {
    if (isMasterBall(ball)) return true
    val safeMax = maxHp.coerceAtLeast(1)
    val hp = currentHp.coerceIn(1, safeMax)
    val a = floor((3.0 * safeMax - 2.0 * hp) * catchRate * ballBonus(ball) / (3.0 * safeMax))
    if (a >= CERTAIN) return true
    if (a <= 0.0) return false
    val b = floor(SHAKE_NUMERATOR / sqrt(sqrt(SHAKE_DENOMINATOR / a)))
    repeat(4) { if (rng.pick(SHAKE_ROLL_MAX) >= b) return false }
    return true
  }

  /**
   * What a ball multiplies the rate by, read off the name rather than a table of ids: the item data
   * carries no ball class, and every ball says which one it is in the word before "Ball". An
   * unknown ball is an ordinary one, which is the safe way round.
   */
  private fun ballBonus(ball: ItemDef): Double =
      when {
        ball.name.startsWith("Ultra", ignoreCase = true) -> 2.0
        ball.name.startsWith("Great", ignoreCase = true) -> 1.5
        ball.name.startsWith("Safari", ignoreCase = true) -> 1.5
        ball.name.startsWith("Sport", ignoreCase = true) -> 1.5
        else -> 1.0
      }

  private fun isMasterBall(ball: ItemDef): Boolean = ball.name.startsWith("Master", true)
}
