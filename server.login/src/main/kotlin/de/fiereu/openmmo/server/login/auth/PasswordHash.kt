package de.fiereu.openmmo.server.login.auth

import java.security.MessageDigest
import java.security.SecureRandom
import java.util.Base64
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.PBEKeySpec

/** How a credential is stored, and why it is not what the client sent. */
object PasswordHash {

  private const val ALGORITHM = "PBKDF2WithHmacSHA256"
  private const val PREFIX = "pbkdf2-sha256"
  private const val SALT_BYTES = 16
  private const val KEY_BITS = 256

  /**
   * The work factor a new credential is written with, which is the figure the OWASP guidance names
   * for this algorithm. About 150ms on the machine it was measured on: a real cost to a guess and
   * an unnoticeable one to a login.
   */
  const val ITERATIONS = 600_000

  private val random = SecureRandom()

  /** The row to store for a credential that arrived from the client, salted and stretched. */
  fun hash(clientHash: String): String {
    val salt = ByteArray(SALT_BYTES).also(random::nextBytes)
    val derived = derive(clientHash, salt, ITERATIONS)
    val encoder = Base64.getEncoder().withoutPadding()
    return "$PREFIX\$$ITERATIONS\$${encoder.encodeToString(salt)}\$${encoder.encodeToString(derived)}"
  }

  /** Whether [clientHash] is the credential [stored] was written from. */
  fun verify(stored: String, clientHash: String): Boolean {
    if (isLegacy(stored)) return samePassword(stored, clientHash)
    val parts = stored.split('$')
    if (parts.size != 4 || parts[0] != PREFIX) return false
    val iterations = parts[1].toIntOrNull() ?: return false
    if (iterations <= 0) return false
    val decoder = Base64.getDecoder()
    val salt = runCatching { decoder.decode(parts[2]) }.getOrNull() ?: return false
    val expected = runCatching { decoder.decode(parts[3]) }.getOrNull() ?: return false
    return MessageDigest.isEqual(expected, derive(clientHash, salt, iterations))
  }

  /**
   * Whether this row should be written again the next time its owner proves they own it: either it
   * is one of the old unsalted values, or it was written at a work factor lower than the one in use
   * now.
   */
  fun needsRehash(stored: String): Boolean {
    if (isLegacy(stored)) return true
    val parts = stored.split('$')
    if (parts.size != 4 || parts[0] != PREFIX) return true
    return (parts[1].toIntOrNull() ?: 0) < ITERATIONS
  }

  /**
   * Whether this row is one of the old unsalted SHA-1 hex values, which is the shape and nothing
   * else: forty characters of hex. A new row always carries its algorithm in front.
   */
  fun isLegacy(stored: String): Boolean =
      stored.length == 40 && stored.all { it.isDigit() || it in 'a'..'f' || it in 'A'..'F' }

  private fun derive(clientHash: String, salt: ByteArray, iterations: Int): ByteArray =
      SecretKeyFactory.getInstance(ALGORITHM)
          .generateSecret(PBEKeySpec(clientHash.toCharArray(), salt, iterations, KEY_BITS))
          .encoded
}
