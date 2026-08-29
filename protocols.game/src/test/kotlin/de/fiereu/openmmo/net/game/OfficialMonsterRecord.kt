package de.fiereu.openmmo.net.game

/**
 * The monster record as the game client itself reads it, transcribed from the client's reader
 * so that tests have an oracle independent of this project's own encoder.
 */
object OfficialMonsterRecord {

  data class Fields(
      val end: Int,
      val id: Long,
      val ownerId: Long,
      val seed: Int,
      val dexId: Int,
      val ot: String,
      val nickname: String,
      val level: Int,
      val hp: Int,
      val xp: Int,
      val moveIds: List<Int>,
      val movePps: List<Int>,
      val evs: List<Int>,
      val ivBits: Int,
      val caughtAt: Int,
  )

  /** Returns the offset one past the record starting at [offset]. */
  fun read(bytes: ByteArray, offset: Int, flagged: Boolean = true): Int =
      fields(bytes, offset, flagged).end

  fun fields(bytes: ByteArray, offset: Int, flagged: Boolean = true): Fields {
    val c = Cursor(bytes, offset)
    val id = c.long()
    if (c.u8() == 1) c.long()
    c.u8()
    val ownerId = c.long()
    c.long()
    c.u8()
    c.short()
    val dexId = c.u16()
    val seed = c.int()
    c.long()
    val ot = c.string()
    val nickname = c.string()
    c.u8()
    c.u8()
    val level = c.u8()
    val hp = c.short()
    c.short()
    val xp = c.int()
    c.u8()
    c.short()
    val moveIds = List(4) { c.u16() }
    val movePps = List(4) { c.u8() }
    repeat(4) { c.short() }
    val evs = List(6) { c.u8() }
    repeat(5) { c.u8() }
    c.u8()
    repeat(3) { c.u8() }
    c.int()
    c.u8()
    c.u8()
    val ivBits = c.int()
    c.u8()
    c.long()
    c.short()
    val caughtAt = c.int()
    c.short()
    // The client reads a trailing int here only when its caller passes false, which the character
    // list never does.
    if (!flagged) c.int()
    c.u8()
    c.u8()
    repeat(c.u8()) { c.u8() }
    return Fields(
        end = c.at,
        id = id,
        ownerId = ownerId,
        seed = seed,
        dexId = dexId,
        ot = ot,
        nickname = nickname,
        level = level,
        hp = hp,
        xp = xp,
        moveIds = moveIds,
        movePps = movePps,
        evs = evs,
        ivBits = ivBits,
        caughtAt = caughtAt,
    )
  }

  private class Cursor(val bytes: ByteArray, var at: Int) {
    fun u8(): Int = bytes[at++].toInt() and 0xFF

    fun u16(): Int = u8() or (u8() shl 8)

    fun short(): Int = u16().toShort().toInt()

    fun int(): Int = u16() or (u16() shl 16)

    fun long(): Long = (int().toLong() and 0xFFFFFFFFL) or (int().toLong() shl 32)

    fun string(): String {
      val sb = StringBuilder()
      while (true) {
        val ch = u16()
        if (ch == 0) return sb.toString()
        sb.append(ch.toChar())
      }
    }
  }
}
