package de.fiereu.network.handshake

/* What a ServerHello's ECDSA signature actually covers. */
internal object HandshakeSignature {

  /** The shortest keyed checksum a session may negotiate. */
  const val MIN_CHECKSUM_SIZE = 4

  /** The bytes the root key signs, and the bytes a client verifies against. */
  fun payload(ephemeralPoint: ByteArray, checksumSize: Int, helloTimestamp: Long): ByteArray {
    val out = ByteArray(ephemeralPoint.size + 1 + Long.SIZE_BYTES)
    ephemeralPoint.copyInto(out)
    out[ephemeralPoint.size] = checksumSize.toByte()
    var offset = ephemeralPoint.size + 1
    for (shift in (Long.SIZE_BITS - Byte.SIZE_BITS) downTo 0 step Byte.SIZE_BITS) {
      out[offset++] = ((helloTimestamp ushr shift) and 0xFF).toByte()
    }
    return out
  }
}
