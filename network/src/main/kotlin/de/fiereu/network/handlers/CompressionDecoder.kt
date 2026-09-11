package de.fiereu.network.handlers

import io.netty.buffer.ByteBuf
import io.netty.channel.ChannelHandlerContext
import io.netty.handler.codec.ByteToMessageDecoder
import io.netty.handler.codec.DecoderException
import java.util.zip.Inflater

// Mirror of the encoder: one persistent raw inflater per connection, never reset, so the deflate
// window carries across packets. Each compressed segment is inflated after re-appending the
// 00 00 FF FF sync marker that the sender stripped.
class CompressionDecoder(private val maxInflated: Int = DEFAULT_MAX_INFLATED) :
    ByteToMessageDecoder() {

  private val inflater = Inflater(true)
  private val chunk = ByteArray(0x4000)

  override fun decode(ctx: ChannelHandlerContext, buffer: ByteBuf, out: MutableList<Any>) {
    if (buffer.readableBytes() < 2) return
    val opcode = buffer.readByte()
    val flag = buffer.readByte().toInt()
    val payloadLen = buffer.readableBytes()
    val output = ctx.alloc().buffer(payloadLen * 2 + 1)
    try {
      output.writeByte(opcode.toInt())
      if (flag == 0) {
        output.writeBytes(buffer, buffer.readerIndex(), payloadLen)
        buffer.skipBytes(payloadLen)
      } else {
        val input = ByteArray(payloadLen + 4)
        buffer.readBytes(input, 0, payloadLen)
        input[payloadLen] = 0
        input[payloadLen + 1] = 0
        input[payloadLen + 2] = 0xFF.toByte()
        input[payloadLen + 3] = 0xFF.toByte()
        inflater.setInput(input)
        // A deflate stream says how much it unpacks to only by unpacking, so a small frame can
        // name a very large one. Nothing stopped the loop below, so a peer could hand over a
        // few kilobytes and have the server allocate for as long as it kept producing.
        while (!inflater.needsInput()) {
          val written = inflater.inflate(chunk)
          if (written == 0) break
          if (output.readableBytes() + written > maxInflated) {
            throw DecoderException("a compressed frame inflated past $maxInflated bytes")
          }
          output.writeBytes(chunk, 0, written)
        }
      }
      out += output.retain()
    } finally {
      output.release()
    }
  }

  override fun handlerRemoved0(ctx: ChannelHandlerContext) {
    inflater.end()
  }

  companion object {
    /** Sixteen times the frame ceiling, which no honest frame comes near. */
    const val DEFAULT_MAX_INFLATED = 0xFFFF * 16
  }
}
