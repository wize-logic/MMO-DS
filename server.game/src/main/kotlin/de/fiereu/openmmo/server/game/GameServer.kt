package de.fiereu.openmmo.server.game

import de.fiereu.network.ConnectionLimits
import de.fiereu.network.PipelineNames
import de.fiereu.network.PipelineOptions
import de.fiereu.network.SessionIdentity
import de.fiereu.network.handlers.ConnectionGuard
import de.fiereu.network.installPipeline
import de.fiereu.network.session
import de.fiereu.openmmo.net.game.GameProtocol
import de.fiereu.openmmo.server.game.config.GameServerConfig
import de.fiereu.openmmo.server.game.handler.GameAppHandler
import de.fiereu.openmmo.server.game.matchmaking.MatchmakingRounds
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.session.SessionRegistryHandler
import io.github.oshai.kotlinlogging.KotlinLogging
import io.netty.bootstrap.ServerBootstrap
import io.netty.channel.Channel
import io.netty.channel.ChannelInitializer
import io.netty.channel.ChannelOption
import io.netty.channel.EventLoopGroup
import io.netty.channel.socket.nio.NioServerSocketChannel
import java.security.interfaces.ECPrivateKey
import javax.inject.Inject
import javax.inject.Named
import javax.inject.Provider
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

@Singleton
class GameServer
@Inject
constructor(
    private val config: GameServerConfig,
    @param:Named("boss") private val bossGroup: EventLoopGroup,
    @param:Named("worker") private val workerGroup: EventLoopGroup,
    private val rootKey: ECPrivateKey,
    private val handlerProvider: Provider<GameAppHandler>,
    private val sessionRegistry: SessionRegistry,
    private val matchmakingRounds: MatchmakingRounds,
) {
  /**
   * One guard for the whole server, because it counts. See
   * [de.fiereu.network.handlers.ConnectionGuard]: it bounds how many sockets and handshake keypairs
   * an unnamed peer can make this process carry, and closes one that never speaks.
   */
  private val connectionGuard =
      ConnectionLimits().let {
        ConnectionGuard(it.maxTotal, it.maxPerAddress, it.handshakeTimeoutSeconds)
      }

  fun start() {
    matchmakingRounds.start()
    val channel =
        ServerBootstrap()
            .group(bossGroup, workerGroup)
            .channel(NioServerSocketChannel::class.java)
            .option(ChannelOption.SO_BACKLOG, 128)
            .childOption(ChannelOption.TCP_NODELAY, true)
            .childHandler(
                object : ChannelInitializer<Channel>() {
                  override fun initChannel(ch: Channel) {
                    installPipeline(
                        pipeline = ch.pipeline(),
                        side = de.fiereu.network.Side.SERVER,
                        identity = SessionIdentity.ServerRoot(rootKey),
                        applicationProtocol = GameProtocol,
                        applicationHandlerFactory = { handlerProvider.get() },
                        options = PipelineOptions(checksumSize = config.checksumSize),
                        connectionGuard = connectionGuard,
                    )
                    val session = ch.session() ?: error("session missing after installPipeline")
                    ch.pipeline()
                        .addAfter(
                            PipelineNames.PROTOCOL_HANDLER,
                            "session-registry",
                            SessionRegistryHandler(sessionRegistry, session),
                        )
                  }
                },
            )
            .bind(config.host, config.port)
            .sync()
            .channel()
    log.info { "Game server listening on ${channel.localAddress()}" }
    channel.closeFuture().sync()
  }

  fun shutdown() {
    workerGroup.shutdownGracefully()
    bossGroup.shutdownGracefully()
  }
}
