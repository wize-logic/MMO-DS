package de.fiereu.openmmo.server.login.auth

import java.time.Duration
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

/**
 * How many failed passwords one source may offer before it is told to wait.
 *
 * Nothing bounded this, so a peer could try every password in a list against every account in it,
 * as fast as the socket allowed. Checking a password also costs real work now ([PasswordHash]), so
 * an unbounded attempt is unbounded server CPU somebody else asked for. The check runs before the
 * hash, not after.
 *
 * Two counters, and neither is "this account, from anywhere": an account from one address, which is
 * somebody working through passwords for one player, and an address, which is somebody working
 * through a list of accounts. Locking an account outright is deliberately absent, because it would
 * let anybody lock any player out by failing a few logins on their behalf.
 *
 * Only failures count, and a success clears what that pair has spent.
 */
@Singleton
class LoginAttemptLimiter(
    private val perAccount: Int,
    private val perAddress: Int,
    private val window: Duration,
    private val maxKeys: Int,
    private val clock: () -> Long,
) {

  /** What the server builds. The other constructor is for a test that drives its own clock. */
  @Inject
  constructor() :
      this(
          perAccount = 10,
          perAddress = 30,
          window = Duration.ofMinutes(15),
          maxKeys = 50_000,
          System::nanoTime,
      )

  private data class Window(val startedAt: Long, val count: Int)

  private val failures = ConcurrentHashMap<String, Window>()

  /** Whether this pair may be asked to check a password at all. */
  fun allow(username: String, address: String): Boolean =
      count(accountKey(username, address)) < perAccount && count(address) < perAddress

  /** One password that was not the right one. */
  fun recordFailure(username: String, address: String) {
    bump(accountKey(username, address))
    bump(address)
  }

  /** The right one arrived, so this account and address stop owing anything against each other. */
  fun recordSuccess(username: String, address: String) {
    failures.remove(accountKey(username, address))
  }

  private fun accountKey(username: String, address: String) = "$address|${username.lowercase()}"

  private fun count(key: String): Int {
    val window = failures[key] ?: return 0
    return if (expired(window, clock())) 0 else window.count
  }

  private fun bump(key: String) {
    val now = clock()
    // A key is whatever a caller sent, so the table's size is a caller's choice too. Past the cap
    // the oldest windows go rather than the newest being refused: a full table must not become a
    // way to lock everybody else out, which is the failure the counters exist to prevent.
    if (failures.size >= maxKeys) {
      failures.entries.removeIf { expired(it.value, now) }
      if (failures.size >= maxKeys) {
        failures.entries
            .sortedBy { it.value.startedAt }
            .take(maxKeys / 4)
            .forEach { failures.remove(it.key, it.value) }
      }
    }
    failures.compute(key) { _, current ->
      if (current == null || expired(current, now)) Window(now, 1)
      else current.copy(count = current.count + 1)
    }
  }

  private fun expired(window: Window, now: Long) = now - window.startedAt >= this.window.toNanos()
}
