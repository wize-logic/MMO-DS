package de.fiereu.network.handlers

import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import io.netty.channel.embedded.EmbeddedChannel
import java.net.InetSocketAddress
import java.net.SocketAddress
import java.util.concurrent.TimeUnit

/** A channel that answers a real address, which the guard keys its per-address count on. */
private val pendingAddress = ThreadLocal<SocketAddress>()

private class AddressedChannel(guard: ConnectionGuard) : EmbeddedChannel(guard) {
  override fun remoteAddress0(): SocketAddress = pendingAddress.get()
}

private fun from(host: String, guard: ConnectionGuard): AddressedChannel {
  pendingAddress.set(InetSocketAddress(host, 40000))
  return AddressedChannel(guard)
}

class ConnectionGuardTest :
    FunSpec({
      test("connections past the total are closed and the rest are let through") {
        val guard = ConnectionGuard(maxTotal = 2, maxPerAddress = 99, handshakeTimeoutSeconds = 5)

        val first = from("10.0.0.1", guard)
        val second = from("10.0.0.2", guard)
        val third = from("10.0.0.3", guard)

        first.isOpen shouldBe true
        second.isOpen shouldBe true
        third.isOpen shouldBe false
        guard.openConnections() shouldBe 2
      }

      test("a closed connection gives its place back") {
        val guard = ConnectionGuard(maxTotal = 1, maxPerAddress = 99, handshakeTimeoutSeconds = 5)

        val first = from("10.0.0.1", guard)
        first.isOpen shouldBe true
        first.close().sync()
        guard.openConnections() shouldBe 0

        // The slot the first one held is free, so this is not refused.
        from("10.0.0.2", guard).isOpen shouldBe true
      }

      test("one address cannot take every slot") {
        val guard = ConnectionGuard(maxTotal = 99, maxPerAddress = 2, handshakeTimeoutSeconds = 5)

        from("10.0.0.9", guard).isOpen shouldBe true
        from("10.0.0.9", guard).isOpen shouldBe true
        from("10.0.0.9", guard).isOpen shouldBe false
        // A different host is unaffected by what that one spent.
        from("10.0.0.8", guard).isOpen shouldBe true
      }

      test("a connection that never handshakes is closed at the deadline") {
        val guard = ConnectionGuard(maxTotal = 99, maxPerAddress = 99, handshakeTimeoutSeconds = 5)

        val channel = from("10.0.0.1", guard)
        channel.isOpen shouldBe true

        channel.advanceTimeBy(4, TimeUnit.SECONDS)
        channel.runScheduledPendingTasks()
        channel.isOpen shouldBe true

        channel.advanceTimeBy(2, TimeUnit.SECONDS)
        channel.runScheduledPendingTasks()
        channel.isOpen shouldBe false
      }
    })
