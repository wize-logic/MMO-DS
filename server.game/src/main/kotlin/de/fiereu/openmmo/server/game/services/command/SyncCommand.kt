package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.net.game.packets.ChunkedImagePacket
import de.fiereu.openmmo.net.game.packets.ChunkedTransferAppendPacket
import de.fiereu.openmmo.net.game.packets.ChunkedTransferBeginPacket
import de.fiereu.openmmo.net.game.packets.DataDigestSyncBatchedPacket
import de.fiereu.openmmo.net.game.packets.DataDigestSyncPacket
import de.fiereu.openmmo.net.game.packets.StreamChunkPacket
import java.io.ByteArrayOutputStream
import java.util.zip.GZIPOutputStream
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class SyncCommand @Inject constructor() : ChatCommand {
  override val name = "sync"
  override val usage = "/sync"
  override val description = "sends a digest, a chunked transfer, a stream and an image"
  override val role = AccountRole.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    val wired = xorTn1(gzip("sync-ok".toByteArray(Charsets.US_ASCII)))
    val mid = wired.size / 2

    ctx.session.send(DataDigestSyncPacket(bitCount = 8, digest = byteArrayOf(0)))
    ctx.session.send(DataDigestSyncBatchedPacket(bitCount = 8, digest = byteArrayOf(0)))
    ctx.session.send(
        ChunkedTransferBeginPacket(transferId = 1L, totalSize = wired.size, data = ByteArray(0)))
    ctx.session.send(ChunkedTransferAppendPacket(data = wired.copyOfRange(0, mid)))
    ctx.session.send(ChunkedTransferAppendPacket(data = wired.copyOfRange(mid, wired.size)))
    ctx.session.send(
        StreamChunkPacket(
            streamId = 2L, finalChunk = false, chunk = "he".toByteArray(Charsets.US_ASCII)))
    ctx.session.send(
        StreamChunkPacket(
            streamId = 2L, finalChunk = true, chunk = "llo".toByteArray(Charsets.US_ASCII)))
    ctx.session.send(
        ChunkedImagePacket(
            controlType = 0,
            imageType = 1,
            chunkIndex = 0,
            last = true,
            data = "img".toByteArray(Charsets.US_ASCII),
        ))
  }

  private fun gzip(plain: ByteArray): ByteArray {
    val out = ByteArrayOutputStream()
    GZIPOutputStream(out).use { it.write(plain) }
    return out.toByteArray()
  }

  private fun xorTn1(src: ByteArray): ByteArray {
    val key = byteArrayOf(81, -109, 63, -32, 82, 99, 116, -50)
    return ByteArray(src.size) { i -> (src[i].toInt() xor key[i % 8].toInt()).toByte() }
  }
}
