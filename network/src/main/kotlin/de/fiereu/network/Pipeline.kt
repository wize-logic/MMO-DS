package de.fiereu.network

import de.fiereu.network.checksum.NoOpChecksum
import de.fiereu.network.cipher.NoOpSessionCipher
import de.fiereu.network.handlers.ChecksumFrameDecoder
import de.fiereu.network.handlers.ChecksumFrameEncoder
import de.fiereu.network.handlers.CipherDecoder
import de.fiereu.network.handlers.CipherEncoder
import de.fiereu.network.handlers.ConnectionGuard
import de.fiereu.network.handlers.IdleSessionCloser
import de.fiereu.network.handlers.InboundRateLimiter
import de.fiereu.network.handlers.PacketFrameDecoder
import de.fiereu.network.handlers.PacketFrameEncoder
import de.fiereu.network.handshake.ClientSessionHandshakeHandler
import de.fiereu.network.handshake.ServerSessionHandshakeHandler
import de.fiereu.network.internal.MutableSessionContext
import de.fiereu.network.internal.SESSION_KEY
import io.netty.channel.ChannelHandler
import io.netty.channel.ChannelPipeline
import io.netty.handler.logging.LogLevel
import io.netty.handler.logging.LoggingHandler
import io.netty.handler.timeout.IdleStateHandler
import io.netty.handler.timeout.WriteTimeoutHandler
import java.util.concurrent.TimeUnit

fun installPipeline(
    pipeline: ChannelPipeline,
    side: Side,
    identity: SessionIdentity,
    applicationProtocol: Protocol,
    applicationHandlerFactory: () -> ProtocolHandler,
    options: PipelineOptions = PipelineOptions(),
    /**
     * The one guard a server shares across every channel it accepts, or null on a client and in a
     * test. It counts, so there has to be exactly one of it: see
     * [de.fiereu.network.handlers.ConnectionGuard].
     */
    connectionGuard: ConnectionGuard? = null,
) {
  val channel = pipeline.channel()
  val session = MutableSessionContext(side, channel, applicationProtocol)
  channel.attr(SESSION_KEY).set(session)

  val handshakeHandler: ProtocolHandler =
      when (side) {
        Side.SERVER -> {
          require(identity is SessionIdentity.ServerRoot) {
            "Server side requires SessionIdentity.ServerRoot"
          }
          ServerSessionHandshakeHandler(
              rootPrivate = identity.rootPrivate,
              applicationProtocol = applicationProtocol,
              applicationHandlerFactory = applicationHandlerFactory,
              options = options,
          )
        }
        Side.CLIENT -> {
          require(identity is SessionIdentity.ClientTrust) {
            "Client side requires SessionIdentity.ClientTrust"
          }
          ClientSessionHandshakeHandler(
              rootPublic = identity.rootPublic,
              applicationProtocol = applicationProtocol,
              applicationHandlerFactory = applicationHandlerFactory,
              options = options,
          )
        }
      }

  // First, so a connection that is past a cap is closed before anything is built for it.
  if (connectionGuard != null) {
    pipeline.addLast(PipelineNames.CONNECTION_GUARD, connectionGuard)
  }
  pipeline.addLast(
      PipelineNames.WRITE_TIMEOUT,
      WriteTimeoutHandler(options.writeTimeout.inWholeSeconds, TimeUnit.SECONDS),
  )
  // Beside the guard, and measuring raw bytes rather than packets, so it covers a session before
  // and after the handshake: the guard's deadline stops there, and nothing stopped anything after.
  if (options.idleTimeout.isPositive()) {
    pipeline.addLast(
        PipelineNames.IDLE_TIMEOUT,
        IdleStateHandler(0, 0, options.idleTimeout.inWholeSeconds, TimeUnit.SECONDS),
    )
    pipeline.addLast(PipelineNames.IDLE_CLOSER, IdleSessionCloser())
  }
  if (options.frameLogging) {
    pipeline.addLast(PipelineNames.FRAME_LOGGER, LoggingHandler(LogLevel.TRACE))
  }
  pipeline.addLast(PipelineNames.FRAME_DECODER, PacketFrameDecoder(options.maxFrameLength))
  // Behind the frame decoder, so it counts packets rather than whatever a TCP segment happens to
  // carry, and in front of everything that decodes or allocates on a peer's behalf.
  if (options.inboundBurst > 0 && options.inboundPerSecond > 0) {
    pipeline.addLast(
        PipelineNames.INBOUND_RATE_LIMITER,
        InboundRateLimiter(options.inboundBurst, options.inboundPerSecond),
    )
  }
  pipeline.addLast(PipelineNames.FRAME_ENCODER, PacketFrameEncoder())
  pipeline.addLast(PipelineNames.CHECKSUM_DECODER, ChecksumFrameDecoder(NoOpChecksum))
  pipeline.addLast(PipelineNames.CHECKSUM_ENCODER, ChecksumFrameEncoder(NoOpChecksum))
  pipeline.addLast(PipelineNames.CIPHER_DECODER, CipherDecoder(NoOpSessionCipher))
  pipeline.addLast(PipelineNames.CIPHER_ENCODER, CipherEncoder(NoOpSessionCipher))
  if (options.frameLogging) {
    pipeline.addLast(PipelineNames.PROTOCOL_LOGGER, LoggingHandler(LogLevel.TRACE))
  }
  pipeline.addLast(PipelineNames.PROTOCOL_HANDLER, handshakeHandler)
}

/**
 * Adds a handler in front of the protocol logger, so the logger only sees uncompressed packets:
 * decompressed on the way in, not yet compressed on the way out.
 */
internal fun ChannelPipeline.addBeforeProtocolLogger(name: String, handler: ChannelHandler) {
  val anchor =
      if (get(PipelineNames.PROTOCOL_LOGGER) != null) {
        PipelineNames.PROTOCOL_LOGGER
      } else {
        PipelineNames.PROTOCOL_HANDLER
      }
  addBefore(anchor, name, handler)
}
