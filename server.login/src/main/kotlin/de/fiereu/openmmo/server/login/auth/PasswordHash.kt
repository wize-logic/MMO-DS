package de.fiereu.openmmo.server.login.auth

import java.security.MessageDigest
import java.security.SecureRandom
import java.util.Base64
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.PBEKeySpec

/**
 * How a credential is stored, and why it is not what the client sent.
 *
 * The client hashes the password before sending it, so a SHA-1 arrives, and that is what this
 * server used to keep: one column, unsalted, no work factor. It made the column the credential,
 * because the value in the row is the value the wire carries, so anyone who read the table could
 * log in as every account without breaking anything.
 *
 * The client cannot be changed from here, so a SHA-1 still arrives. What changed is that it is no
 * longer stored: the row holds PBKDF2 over it with a salt of its own, so a read of the table yields
 * something that cannot be replayed as a login.
 *
 * The stored string carries its own parameters, so the cost can be raised later and rows written
 * before that still verify:
 * ```
 * pbkdf2-sha256$<iterations>$<salt base64>$<derived key base64>
 * ```
 */
object PasswordHash {

  private const val ALGORITHM = "PBKDF2WithHmacSHA256"
  private const val PREFIX = "pbkdf2-sha256"
  private const val SALT_BYTES = 16
  private const val KEY_BITS = 256

  /**
   * The work factor a new credential is written with, about 50ms. Deliberately not the 600,000 the
   * OWASP guidance names for this algorithm: nothing limits how many login attempts a peer may make
   * yet, so every attempt is server CPU somebody else asked for. Raise it once a login is rate
   * limited; the format carries the number, so old rows keep working.
   */
  const val ITERATIONS = 210_000

  private val random = SecureRandom()

  /** The row to store for a credential that arrived from the client, salted and stretched. */
  fun hash(clientHash: String): String {
    val salt = ByteArray(SALT_BYTES).also(random::nextBytes)
    val derived = derive(clientHash, salt, ITERATIONS)
    val encoder = Base64.getEncoder().withoutPadding()
    return "$PREFIX\$$ITERATIONS\$${encoder.encodeToString(salt)}\$${encoder.encodeToString(derived)}"
  }

  /**
   * Whether [clientHash] is the credential [stored] was written from. A row written before this
   * class existed is a bare SHA-1 and is still accepted, so an account nobody has touched since is
   * not locked out; the caller rewrites it once [isLegacy] says so.
   */
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
   * Whether this row is one of the old unsalted values: forty characters of hex and nothing else.
   */
  fun isLegacy(stored: String): Boolean =
      stored.length == 40 && stored.all { it.isDigit() || it in 'a'..'f' || it in 'A'..'F' }

  private fun derive(clientHash: String, salt: ByteArray, iterations: Int): ByteArray =
      SecretKeyFactory.getInstance(ALGORITHM)
          .generateSecret(PBEKeySpec(clientHash.toCharArray(), salt, iterations, KEY_BITS))
          .encoded
}
