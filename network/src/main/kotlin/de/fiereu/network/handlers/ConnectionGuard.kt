package de.fiereu.network.handlers

import de.fiereu.network.SessionPhase
import de.fiereu.network.session
import io.github.oshai.kotlinlogging.KotlinLogging
import io.netty.channel.ChannelHandler
import io.netty.channel.ChannelHandlerContext
import io.netty.channel.ChannelInboundHandlerAdapter
import java.net.InetSocketAddress
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

private val log = KotlinLogging.logger {}

/** What a peer may hold open, and for how long before it has said who it is. */
@ChannelHandler.Sharable
class ConnectionGuard(
    private val maxTotal: Int,
    private val maxPerAddress: Int,
    private val handshakeTimeoutSeconds: Long,
) : ChannelInboundHandlerAdapter() {

  private val total = AtomicInteger()
  private val perAddress = ConcurrentHashMap<String, AtomicInteger>()

  override fun channelActive(ctx: ChannelHandlerContext) {
    val address = addressOf(ctx)
    val now = total.incrementAndGet()
    val here = perAddress.computeIfAbsent(address) { AtomicInteger() }.incrementAndGet()
    if (now > maxTotal) {
      log.warn {
        "Refusing ${ctx.channel().remoteAddress()}: $now connections, the cap is $maxTotal"
      }
      ctx.close()
      return
    }
    if (here > maxPerAddress) {
      log.warn { "Refusing $address: $here connections from it, the cap is $maxPerAddress" }
      ctx.close()
      return
    }
    // Armed on the way in and never cancelled, because the check it runs is cheap and a channel
    // that closed on its own is already gone by the time it fires.
    ctx.channel()
        .eventLoop()
        .schedule(
            {
              val channel = ctx.channel()
              if (channel.isActive && channel.session()?.phase != SessionPhase.ESTABLISHED) {
                log.warn {
                  "Closing ${channel.remoteAddress()}: no handshake inside" +
                      " ${handshakeTimeoutSeconds}s"
                }
                channel.close()
              }
            },
            handshakeTimeoutSeconds,
            TimeUnit.SECONDS,
        )
    ctx.fireChannelActive()
  }

  override fun channelInactive(ctx: ChannelHandlerContext) {
    total.decrementAndGet()
    val address = addressOf(ctx)
    // Removed rather than left at zero, so the table follows the peers instead of growing with
    // every address that has ever connected.
    perAddress.computeIfPresent(address) { _, count ->
      if (count.decrementAndGet() <= 0) null else count
    }
    ctx.fireChannelInactive()
  }

  /** How many connections are open, for a status line. */
  fun openConnections(): Int = total.get()

  private fun addressOf(ctx: ChannelHandlerContext): String =
      (ctx.channel().remoteAddress() as? InetSocketAddress)?.address?.hostAddress ?: "unknown"
}
