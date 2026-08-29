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
    /**
     * Inbound frames one session may have read at once, and the rate it is topped back up at. Past
     * these the channel stops reading rather than dropping anything, so they sit far above what
     * play reaches. Zero for either turns the limiter off.
     */
    val inboundBurst: Int = 512,
    val inboundPerSecond: Int = 256,
)
