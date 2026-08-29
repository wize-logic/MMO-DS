package de.fiereu.network.handlers

import io.github.oshai.kotlinlogging.KotlinLogging
import io.netty.channel.ChannelHandlerContext
import io.netty.channel.ChannelInboundHandlerAdapter
import java.util.concurrent.TimeUnit

private val log = KotlinLogging.logger {}

private const val NANOS_PER_SECOND = 1_000_000_000.0

/**
 * Holds one session's inbound frames to a sustained rate, by not reading rather than by refusing.
 *
 * Everything behind this is work a peer can ask for, and most of the game protocol hops onto a
 * mailbox the socket fills faster than the handlers drain it. A token bucket decides when to stop
 * pulling from the socket. Frames already read are always passed on, so nothing is dropped and no
 * session is closed for being quick: when the bucket runs dry the channel stops reading until a
 * token accrues, and TCP carries that back to the sender. A join burst is spent from [burst] and at
 * worst delayed by a few milliseconds.
 */
class InboundRateLimiter(
    private val burst: Int,
    private val perSecond: Int,
    private val clock: () -> Long = System::nanoTime,
) : ChannelInboundHandlerAdapter() {

  private var tokens: Double = burst.toDouble()
  private var lastRefill: Long = clock()
  private var paused = false

  override fun channelRead(ctx: ChannelHandlerContext, msg: Any) {
    refill()
    tokens = (tokens - 1.0).coerceAtLeast(0.0)
    // Passed on either way. This decides what is read next, never what is answered.
    ctx.fireChannelRead(msg)
    if (tokens < 1.0) pause(ctx)
  }

  private fun refill() {
    val now = clock()
    val elapsed = now - lastRefill
    if (elapsed <= 0) return
    lastRefill = now
    tokens = (tokens + elapsed / NANOS_PER_SECOND * perSecond).coerceAtMost(burst.toDouble())
  }

  private fun pause(ctx: ChannelHandlerContext) {
    if (paused) return
    paused = true
    ctx.channel().config().isAutoRead = false
    val wait = ((1.0 - tokens) / perSecond * NANOS_PER_SECOND).toLong().coerceAtLeast(1L)
    log.debug { "Holding reads from ${ctx.channel().remoteAddress()} for ${wait / 1_000_000}ms" }
    ctx.channel().eventLoop().schedule({ resume(ctx) }, wait, TimeUnit.NANOSECONDS)
  }

  private fun resume(ctx: ChannelHandlerContext) {
    if (!paused) return
    refill()
    // The wait covered one token, so a clock that moved less would leave the channel readable with
    // an empty bucket.
    if (tokens < 1.0) {
      val wait = ((1.0 - tokens) / perSecond * NANOS_PER_SECOND).toLong().coerceAtLeast(1L)
      ctx.channel().eventLoop().schedule({ resume(ctx) }, wait, TimeUnit.NANOSECONDS)
      return
    }
    paused = false
    ctx.channel().config().isAutoRead = true
    ctx.read()
  }
}
