package de.fiereu.openmmo.server.game.battle

import de.fiereu.openmmo.items.ItemDef
import kotlin.math.floor
import kotlin.math.sqrt

/** Whether a thrown ball holds, by the games' own arithmetic. */
object CatchRoll {

  /** Above this the ball holds with no shakes at all. */
  private const val CERTAIN = 255.0

  /** The two constants the shake check is built from. */
  private const val SHAKE_NUMERATOR = 1_048_576.0
  private const val SHAKE_DENOMINATOR = 16_711_680.0
  private const val SHAKE_ROLL_MAX = 65536

  /**
   * Answers whether [ball] holds a wild monster of [catchRate] sitting on [currentHp] of [maxHp].
   */
  fun holds(ball: ItemDef, catchRate: Int, currentHp: Int, maxHp: Int, rng: BattleRng): Boolean {
    if (isMasterBall(ball)) return true
    val safeMax = maxHp.coerceAtLeast(1)
    val hp = currentHp.coerceIn(1, safeMax)
    val bonus = ballBonus(ball)
    val a = floor((3.0 * safeMax - 2.0 * hp) * catchRate * bonus / (3.0 * safeMax))
    if (a >= CERTAIN) return true
    if (a <= 0.0) return false
    val b = floor(SHAKE_NUMERATOR / sqrt(sqrt(SHAKE_DENOMINATOR / a)))
    repeat(4) { if (rng.pick(SHAKE_ROLL_MAX) >= b) return false }
    return true
  }

  /** What a ball multiplies the rate by. */
  private fun ballBonus(ball: ItemDef): Double =
      when {
        ball.name.startsWith("Ultra", ignoreCase = true) -> 2.0
        ball.name.startsWith("Great", ignoreCase = true) -> 1.5
        ball.name.startsWith("Safari", ignoreCase = true) -> 1.5
        ball.name.startsWith("Sport", ignoreCase = true) -> 1.5
        ball.name.startsWith("Net", ignoreCase = true) -> 1.0
        else -> 1.0
      }

  private fun isMasterBall(ball: ItemDef): Boolean = ball.name.startsWith("Master", true)
}
