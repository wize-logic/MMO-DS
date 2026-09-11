package de.fiereu.network.handshake

import de.fiereu.openmmo.common.test.assertValueRoundtrip
import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.encodeToBytes
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import java.security.interfaces.ECPublicKey

class HandshakeCodecsTest :
    FunSpec({
      test("ClientHello roundtrip recovers timestamp despite XOR obfuscation") {
        val ts = 1764115200L
        val bytes = ClientHelloCodec.encodeToBytes(ClientHelloPacket(ts))
        bytes.size shouldBe 16
        ClientHelloCodec.decodeBytes(bytes).timestamp shouldBe ts
      }

      test("ServerHello roundtrips pubkey, signature, checksum size") {
        val keyPair = EcKeys.generateEphemeralKeyPair()
        val pkt =
            ServerHelloPacket(
                ephemeralPublic = keyPair.public as ECPublicKey,
                signature = ByteArray(64) { it.toByte() },
                checksumSize = 16,
            )
        val bytes = ServerHelloCodec.encodeToBytes(pkt)
        val decoded = ServerHelloCodec.decodeBytes(bytes)
        decoded.ephemeralPublic shouldBe pkt.ephemeralPublic
        decoded.signature shouldBe pkt.signature
        decoded.checksumSize shouldBe 16
      }

      test("a ServerHello signature covers the point, the size and the hello timestamp") {
        val point = ByteArray(65) { it.toByte() }
        val signed = HandshakeSignature.payload(point, 16, 0x0102030405060708L)

        signed.size shouldBe point.size + 1 + 8
        signed.copyOfRange(0, point.size) shouldBe point
        signed[point.size] shouldBe 16.toByte()
        // Big-endian, like the frame checksum's round counter and unlike the ClientHello's
        // little-endian wire field: this is a signature input, not a wire field.
        signed.copyOfRange(point.size + 1, signed.size) shouldBe
            byteArrayOf(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08)
      }

      test("a signature does not carry over to another size or another hello") {
        val root = EcKeys.generateEphemeralKeyPair()
        val ephemeral = EcKeys.generateEphemeralKeyPair()
        val point = EcKeys.toUncompressedPoint(ephemeral.public as ECPublicKey)
        val timestamp = 1_700_000_000_000L
        val signature =
            EcKeys.sign(
                root.private as java.security.interfaces.ECPrivateKey,
                HandshakeSignature.payload(point, 16, timestamp),
            )
        val rootPublic = root.public as ECPublicKey

        fun verifies(size: Int, hello: Long) =
            EcKeys.verify(rootPublic, HandshakeSignature.payload(point, size, hello), signature)

        verifies(16, timestamp) shouldBe true
        // A size byte rewritten in flight, which the plaintext handshake frame invites.
        verifies(2, timestamp) shouldBe false
        // The same ServerHello replayed at another client's hello.
        verifies(16, timestamp + 1) shouldBe false
      }

      test("ClientReady roundtrips public key") {
        val keyPair = EcKeys.generateEphemeralKeyPair()
        val pkt = ClientReadyPacket(ephemeralPublic = keyPair.public as ECPublicKey)
        ClientReadyCodec.assertValueRoundtrip(pkt)
      }
    })
