package de.fiereu.network.handlers

import io.github.oshai.kotlinlogging.KotlinLogging
import io.netty.channel.ChannelHandlerContext
import io.netty.channel.ChannelInboundHandlerAdapter
import io.netty.handler.timeout.IdleState
import io.netty.handler.timeout.IdleStateEvent

private val log = KotlinLogging.logger {}

/** Closes a session that has said nothing and been told nothing for the idle window. */
class IdleSessionCloser : ChannelInboundHandlerAdapter() {

  override fun userEventTriggered(ctx: ChannelHandlerContext, evt: Any) {
    if (evt is IdleStateEvent && evt.state() == IdleState.ALL_IDLE) {
      log.debug { "Closing ${ctx.channel().remoteAddress()}: nothing on the session" }
      ctx.close()
      return
    }
    ctx.fireUserEventTriggered(evt)
  }
}
