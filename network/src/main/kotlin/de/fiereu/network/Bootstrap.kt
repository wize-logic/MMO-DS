package de.fiereu.network

import de.fiereu.network.handlers.ConnectionGuard
import io.netty.bootstrap.Bootstrap
import io.netty.bootstrap.ServerBootstrap
import io.netty.channel.Channel
import io.netty.channel.ChannelInitializer

fun ServerBootstrap.installNetwork(
    identity: SessionIdentity.ServerRoot,
    applicationProtocol: Protocol,
    applicationHandlerFactory: () -> ProtocolHandler,
    options: PipelineOptions = PipelineOptions(),
    /** Null leaves connections uncapped, which is only ever right in a test. */
    connectionLimits: ConnectionLimits? = ConnectionLimits(),
): ServerBootstrap {
  // Built once, out here, because it counts across every channel this server accepts.
  val guard =
      connectionLimits?.let {
        ConnectionGuard(it.maxTotal, it.maxPerAddress, it.handshakeTimeoutSeconds)
      }
  return childHandler(
      object : ChannelInitializer<Channel>() {
        override fun initChannel(ch: Channel) {
          installPipeline(
              pipeline = ch.pipeline(),
              side = Side.SERVER,
              identity = identity,
              applicationProtocol = applicationProtocol,
              applicationHandlerFactory = applicationHandlerFactory,
              options = options,
              connectionGuard = guard,
          )
        }
      },
  )
}

fun Bootstrap.installNetwork(
    identity: SessionIdentity.ClientTrust,
    applicationProtocol: Protocol,
    applicationHandlerFactory: () -> ProtocolHandler,
    options: PipelineOptions = PipelineOptions(),
): Bootstrap =
    handler(
        object : ChannelInitializer<Channel>() {
          override fun initChannel(ch: Channel) {
            installPipeline(
                pipeline = ch.pipeline(),
                side = Side.CLIENT,
                identity = identity,
                applicationProtocol = applicationProtocol,
                applicationHandlerFactory = applicationHandlerFactory,
                options = options,
            )
          }
        },
    )
