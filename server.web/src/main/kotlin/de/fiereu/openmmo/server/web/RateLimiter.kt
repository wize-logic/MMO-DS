package de.fiereu.openmmo.server.web

import java.time.Duration
import java.util.concurrent.ConcurrentHashMap

/**
 * Fixed window counter, one window per key. Registration is the only thing anonymous traffic can
 * make this process write, so it is the only thing that is limited.
 */
class RateLimiter(
    private val limit: Int,
    private val window: Duration = Duration.ofHours(1),
    private val maxKeys: Int = DEFAULT_MAX_KEYS,
    private val clock: () -> Long = System::nanoTime,
) {
  private data class Window(val startedAt: Long, val count: Int)

  private val windows = ConcurrentHashMap<String, Window>()

  /** Counts one attempt against [key] and answers whether it is still under the limit. */
  fun allow(key: String): Boolean {
    val now = clock()
    if (windows.size >= maxKeys && !windows.containsKey(key)) {
      prune(now, force = true)
      if (windows.size >= maxKeys && !windows.containsKey(key)) return false
    }
    val updated =
        windows.compute(key) { _, current ->
          if (current == null || now - current.startedAt >= window.toNanos()) {
            Window(now, 1)
          } else {
            current.copy(count = current.count + 1)
          }
        }
    prune(now)
    return updated!!.count <= limit
  }

  /** Keeps the map from growing without bound when a lot of addresses each ask once. */
  private fun prune(now: Long, force: Boolean = false) {
    if (!force && windows.size < PRUNE_THRESHOLD) return
    windows.entries.removeIf { now - it.value.startedAt >= window.toNanos() }
  }

  private companion object {
    const val PRUNE_THRESHOLD = 4096
    const val DEFAULT_MAX_KEYS = 100_000
  }
}
