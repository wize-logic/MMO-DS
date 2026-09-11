package de.fiereu.network.handshake

import de.fiereu.network.InvalidServerSignatureException
import de.fiereu.network.PipelineNames
import de.fiereu.network.PipelineOptions
import de.fiereu.network.Protocol
import de.fiereu.network.ProtocolHandler
import de.fiereu.network.SessionPhase
import de.fiereu.network.Side
import de.fiereu.network.TypedProtocolHandler
import de.fiereu.network.WeakChecksumSizeException
import de.fiereu.network.addBeforeProtocolLogger
import de.fiereu.network.cipher.AesCtrSessionCipher
import de.fiereu.network.handlers.ChecksumFrameDecoder
import de.fiereu.network.handlers.ChecksumFrameEncoder
import de.fiereu.network.handlers.CipherDecoder
import de.fiereu.network.handlers.CipherEncoder
import de.fiereu.network.handlers.CompressionDecoder
import de.fiereu.network.internal.MutableSessionContext
import io.github.oshai.kotlinlogging.KotlinLogging
import java.security.interfaces.ECPublicKey

private val log = KotlinLogging.logger {}

internal class ClientSessionHandshakeHandler(
    private val rootPublic: ECPublicKey,
    private val applicationProtocol: Protocol,
    private val applicationHandlerFactory: () -> ProtocolHandler,
    private val options: PipelineOptions,
) : TypedProtocolHandler<SessionHandshakeProtocol>(SessionHandshakeProtocol, Side.CLIENT) {

  private val ephemeralKeyPair = EcKeys.generateEphemeralKeyPair()

  /** The timestamp this session put in its ClientHello; the server signs its answer over it. */
  private var helloTimestamp = 0L

  override fun onActive() {
    helloTimestamp = System.currentTimeMillis()
    session.send(ClientHelloPacket(timestamp = helloTimestamp))
  }

  init {
    on<ServerHelloPacket> { event ->
      val publicBytes = EcKeys.toUncompressedPoint(event.packet.ephemeralPublic)
      val signed =
          HandshakeSignature.payload(publicBytes, event.packet.checksumSize, helloTimestamp)
      if (!EcKeys.verify(rootPublic, signed, event.packet.signature)) {
        throw InvalidServerSignatureException()
      }
      // Only now is the size a signed fact rather than a byte off the wire, so this is where a
      // profile too weak to authenticate a frame gets refused rather than installed.
      if (event.packet.checksumSize < HandshakeSignature.MIN_CHECKSUM_SIZE) {
        throw WeakChecksumSizeException(
            event.packet.checksumSize,
            HandshakeSignature.MIN_CHECKSUM_SIZE,
        )
      }
      val crypto =
          SessionCryptoState.derive(
              localPrivate = ephemeralKeyPair.private,
              remotePublic = event.packet.ephemeralPublic,
              role = AesCtrSessionCipher.Role.CLIENT,
              checksumSize = event.packet.checksumSize,
          )
      event.session.send(
          ClientReadyPacket(ephemeralPublic = ephemeralKeyPair.public as ECPublicKey),
      )
      val pipeline = event.session.channel.pipeline()
      (pipeline.get(PipelineNames.CHECKSUM_DECODER) as ChecksumFrameDecoder).checksum =
          crypto.incomingChecksum
      (pipeline.get(PipelineNames.CHECKSUM_ENCODER) as ChecksumFrameEncoder).checksum =
          crypto.outgoingChecksum
      (pipeline.get(PipelineNames.CIPHER_DECODER) as CipherDecoder).cipher = crypto.cipher
      (pipeline.get(PipelineNames.CIPHER_ENCODER) as CipherEncoder).cipher = crypto.cipher
      if (applicationProtocol.compressed) {
        pipeline.addBeforeProtocolLogger(
            PipelineNames.COMPRESSION_DECODER,
            CompressionDecoder(),
        )
      }
      val appHandler = applicationHandlerFactory()
      pipeline.replace(
          PipelineNames.PROTOCOL_HANDLER,
          PipelineNames.PROTOCOL_HANDLER,
          appHandler,
      )
      (event.session as MutableSessionContext).transitionTo(SessionPhase.ESTABLISHED)
      log.debug { "Client handshake complete with ${event.session.remoteAddress}" }
    }
  }
}
