package de.fiereu.network.handshake

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.U16LE
import de.fiereu.network.PipelineOptions
import de.fiereu.network.Protocol
import de.fiereu.network.ProtocolHandler
import de.fiereu.network.SessionIdentity
import de.fiereu.network.SessionPhase
import de.fiereu.network.Side
import de.fiereu.network.TypedProtocolHandler
import de.fiereu.network.bidi
import de.fiereu.network.installPipeline
import de.fiereu.network.internal.SESSION_KEY
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import io.netty.buffer.Unpooled
import io.netty.channel.Channel
import io.netty.channel.ChannelInitializer
import io.netty.channel.embedded.EmbeddedChannel
import java.security.interfaces.ECPrivateKey
import java.security.interfaces.ECPublicKey

private data class Ping(val value: Int)

private object PingCodec : PacketCodec<Ping>() {
  override fun CodecScope<Ping>.body() = Ping(field(U16LE, Ping::value))
}

private object PingProtocol : Protocol() {
  init {
    bidi<Ping>(0x55u, PingCodec)
  }
}

private class Sink : TypedProtocolHandler<PingProtocol>(PingProtocol, Side.SERVER) {
  init {
    on<Ping> {}
  }
}

/** The wire frame a peer would send: a little endian length over itself, then opcode and body. */
private fun frame(opcode: Int, body: ByteArray): ByteArray {
  val total = body.size + 3
  return byteArrayOf((total and 0xFF).toByte(), ((total shr 8) and 0xFF).toByte()) +
      byteArrayOf(opcode.toByte()) +
      body
}

private const val CLIENT_READY_OPCODE = 0x02

class HandshakeOrderTest :
    FunSpec({
      test("a ClientReady with no ClientHello before it does not establish a session") {
        val rootKeyPair = EcKeys.generateEphemeralKeyPair()
        val server =
            EmbeddedChannel(
                object : ChannelInitializer<Channel>() {
                  override fun initChannel(ch: Channel) {
                    installPipeline(
                        pipeline = ch.pipeline(),
                        side = Side.SERVER,
                        identity = SessionIdentity.ServerRoot(rootKeyPair.private as ECPrivateKey),
                        applicationProtocol = PingProtocol,
                        applicationHandlerFactory = { Sink() as ProtocolHandler },
                        options = PipelineOptions(checksumSize = 8),
                    )
                  }
                },
            )

        // A peer's own ephemeral point, which is all a ClientReady carries.
        val point =
            EcKeys.toUncompressedPoint(
                EcKeys.generateEphemeralKeyPair().public as ECPublicKey,
            )
        val body =
            byteArrayOf((point.size and 0xFF).toByte(), ((point.size shr 8) and 0xFF).toByte()) +
                point

        server.writeInbound(Unpooled.wrappedBuffer(frame(CLIENT_READY_OPCODE, body)))

        // Reaching established is what the connection guard's handshake deadline looks for, so a
        // session that got there on one unanswered frame would then be held for as long as its
        // peer liked.
        server.attr(SESSION_KEY).get().phase shouldBe SessionPhase.HANDSHAKE
        server.isOpen shouldBe false
      }
    })
