package de.fiereu.network

import kotlin.time.Duration
import kotlin.time.Duration.Companion.minutes
import kotlin.time.Duration.Companion.seconds

data class PipelineOptions(
    val checksumSize: Int = 16,
    val writeTimeout: Duration = 25.minutes,
    val maxFrameLength: Int = 0xFFFF,
    val compressionThreshold: Int = 256,
    val maxHelloSkew: Duration = 10.seconds,
    val frameLogging: Boolean = false,
    /** Inbound frames one session may have read at once, and the rate it is topped back up at. */
    val inboundBurst: Int = 512,
    val inboundPerSecond: Int = 256,
    /** How long a session may go with no traffic in either direction before it is closed. */
    val idleTimeout: Duration = 10.minutes,
)

/**
 * Ceilings on connections themselves, for the one [de.fiereu.network.handlers.ConnectionGuard] a
 * server builds and shares across every channel it accepts.
 */
data class ConnectionLimits(
    val maxTotal: Int = 2_000,
    val maxPerAddress: Int = 24,
    val handshakeTimeoutSeconds: Long = 20,
)
