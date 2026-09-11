package de.fiereu.network

object PipelineNames {
  const val CONNECTION_GUARD = "connection-guard"
  const val WRITE_TIMEOUT = "write-timeout"
  const val IDLE_TIMEOUT = "idle-timeout"
  const val IDLE_CLOSER = "idle-closer"
  const val FRAME_LOGGER = "frame-logger"
  const val FRAME_DECODER = "frame-decoder"
  const val INBOUND_RATE_LIMITER = "inbound-rate-limiter"
  const val FRAME_ENCODER = "frame-encoder"
  const val CHECKSUM_DECODER = "checksum-decoder"
  const val CHECKSUM_ENCODER = "checksum-encoder"
  const val CIPHER_DECODER = "cipher-decoder"
  const val CIPHER_ENCODER = "cipher-encoder"
  const val COMPRESSION_DECODER = "compression-decoder"
  const val COMPRESSION_ENCODER = "compression-encoder"
  const val PROTOCOL_LOGGER = "protocol-logger"
  const val PROTOCOL_HANDLER = "protocol-handler"
}
