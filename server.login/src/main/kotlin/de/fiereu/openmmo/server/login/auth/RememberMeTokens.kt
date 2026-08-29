package de.fiereu.openmmo.server.login.auth

import de.fiereu.openmmo.common.utils.toHex
import java.security.MessageDigest
import java.security.SecureRandom

/**
 * The long lived credential a client keeps in place of a password.
 *
 * It used to be an HMAC signed with the same secret, algorithm and shape as the short lived ticket
 * the game server verifies, so a blob that verified as one verified as the other, and the game
 * server's copy of that secret could mint thirty day credentials. Nothing could withdraw one
 * either, because a signature is valid until it expires.
 *
 * So it is a row now. The token is [TOKEN_BYTES] of randomness, the server keeps only its hash, and
 * revoking one is deleting it. No key is involved, so no other server can make one. Using a token
 * spends it and the caller issues a fresh one, which keeps the sliding expiry and means a copy
 * somebody else took stops working once the owner signs in.
 */
interface RememberMeTokens {

  /** A new token for this user. The bytes go to the client; only their hash is kept. */
  suspend fun issue(userId: Int): ByteArray

  /** The user these bytes belong to, spending the token. Null when it is unknown or expired. */
  suspend fun consume(token: ByteArray): Int?

  /** Drop every token this user holds, which is what signing them out everywhere means. */
  suspend fun revokeAll(userId: Int): Int

  companion object {
    /** Enough that guessing one is not a strategy. */
    const val TOKEN_BYTES = 32

    private val random = SecureRandom()

    fun newToken(): ByteArray = ByteArray(TOKEN_BYTES).also(random::nextBytes)

    /**
     * What goes in the column. A plain digest rather than a password hash on purpose: the input is
     * 256 bits of randomness, so there is no guessing to slow down, and a login should not pay for
     * stretching something that was never a password.
     */
    fun fingerprint(token: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(token).toHex()
  }
}
