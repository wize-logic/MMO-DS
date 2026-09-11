package de.fiereu.openmmo.net.game.codecs

import de.fiereu.bytecodec.*
import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.ContestType
import de.fiereu.openmmo.common.MAX_FRIENDSHIP
import de.fiereu.openmmo.common.MON_STATUS_MASK
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.*

/** The record's trailing list is the only place a field can be added to it. */
private const val TAIL_TAG_SUPER_CONTEST_RIBBONS = 1

/** Where the monster was caught: region, bank and map, three little-endian 16-bit values. */
private const val TAIL_TAG_CAUGHT_WHERE = 2

private const val CAUGHT_WHERE_BYTES = 6

/** The item the monster is carrying, one little-endian 16-bit wire item id. */
private const val TAIL_TAG_HELD_ITEM = 3

private const val HELD_ITEM_BYTES = 2

/**
 * The engine's own location label for where the monster was caught, one little-endian 16-bit value.
 */
private const val TAIL_TAG_CAUGHT_LABEL = 4

private const val CAUGHT_LABEL_BYTES = 2

/**
 * What the monster is suffering from, one little-endian 16-bit copy of the client engine's own
 * condition word.
 */
private const val TAIL_TAG_STATUS = 5

private const val STATUS_BYTES = 2

/**
 * A monster with no ribbons and no recorded catch place writes no tail at all, so every record this
 * server sent before either existed stays byte-for-byte what it was.
 */
private fun encodeTail(p: Pokemon): ByteArray {
  val out = ArrayList<Byte>(2 + Long.SIZE_BYTES + 2 + CAUGHT_WHERE_BYTES)
  if (p.superContestRibbons != 0L) {
    out.add(TAIL_TAG_SUPER_CONTEST_RIBBONS.toByte())
    out.add(Long.SIZE_BYTES.toByte())
    var bits = p.superContestRibbons
    for (i in 0 until Long.SIZE_BYTES) {
      out.add((bits and 0xFF).toByte())
      bits = bits ushr 8
    }
  }
  // Absent, not zeroed: region 0 bank 0 map 0 is a real map, so a monster whose place nobody
  // recorded must carry no entry at all rather than one naming that map.
  if (p.caughtMapId >= 0) {
    out.add(TAIL_TAG_CAUGHT_WHERE.toByte())
    out.add(CAUGHT_WHERE_BYTES.toByte())
    for (v in listOf(p.caughtRegionId, p.caughtBankId, p.caughtMapId)) {
      out.add((v and 0xFF).toByte())
      out.add(((v shr 8) and 0xFF).toByte())
    }
  }
  // Nothing held writes no entry, so a record for a monster carrying nothing is byte-for-byte
  // what it was before this tag existed.
  if (p.heldItemId != 0) {
    out.add(TAIL_TAG_HELD_ITEM.toByte())
    out.add(HELD_ITEM_BYTES.toByte())
    out.add((p.heldItemId and 0xFF).toByte())
    out.add(((p.heldItemId shr 8) and 0xFF).toByte())
  }
  // Label 0 is the one the game draws as the Mystery Zone, which is what a record with no entry
  // already gets, so writing it would say nothing and cost two bytes on every monster.
  if (p.caughtLocationLabel != 0) {
    out.add(TAIL_TAG_CAUGHT_LABEL.toByte())
    out.add(CAUGHT_LABEL_BYTES.toByte())
    out.add((p.caughtLocationLabel and 0xFF).toByte())
    out.add(((p.caughtLocationLabel shr 8) and 0xFF).toByte())
  }
  // A healthy monster writes no entry, so its record is byte-for-byte what it was before this tag
  // existed, which is what nearly every record is.
  val status = p.status and MON_STATUS_MASK
  if (status != 0) {
    out.add(TAIL_TAG_STATUS.toByte())
    out.add(STATUS_BYTES.toByte())
    out.add((status and 0xFF).toByte())
    out.add(((status shr 8) and 0xFF).toByte())
  }
  return out.toByteArray()
}

/** One tag/length/value walk, returning the value bytes of [tag] at [len], or null. */
private fun tailEntry(tail: ByteArray, tag: Int, len: Int): ByteArray? {
  var at = 0
  while (at + 2 <= tail.size) {
    val t = tail[at].toInt() and 0xFF
    val n = tail[at + 1].toInt() and 0xFF
    at += 2
    // A truncated entry means the tail is not what it claims; believe none of it rather than half.
    if (at + n > tail.size) return null
    if (t == tag && n == len) return tail.copyOfRange(at, at + n)
    at += n
  }
  return null
}

private fun decodeSuperContestRibbons(tail: ByteArray): Long {
  val v = tailEntry(tail, TAIL_TAG_SUPER_CONTEST_RIBBONS, Long.SIZE_BYTES) ?: return 0L
  var bits = 0L
  for (i in Long.SIZE_BYTES - 1 downTo 0) {
    bits = (bits shl 8) or (v[i].toLong() and 0xFF)
  }
  return bits
}

/** The wire item id the monster is carrying, or 0 where the record carries none. */
private fun decodeHeldItem(tail: ByteArray): Int {
  val v = tailEntry(tail, TAIL_TAG_HELD_ITEM, HELD_ITEM_BYTES) ?: return 0
  return (v[0].toInt() and 0xFF) or ((v[1].toInt() and 0xFF) shl 8)
}

/** The location label the record carries, or 0 where it carries none. */
private fun decodeCaughtLabel(tail: ByteArray): Int {
  val v = tailEntry(tail, TAIL_TAG_CAUGHT_LABEL, CAUGHT_LABEL_BYTES) ?: return 0
  return (v[0].toInt() and 0xFF) or ((v[1].toInt() and 0xFF) shl 8)
}

/** The engine condition word the record carries, or 0 for a healthy monster. */
private fun decodeStatus(tail: ByteArray): Int {
  val v = tailEntry(tail, TAIL_TAG_STATUS, STATUS_BYTES) ?: return 0
  return ((v[0].toInt() and 0xFF) or ((v[1].toInt() and 0xFF) shl 8)) and MON_STATUS_MASK
}

/** region, bank, map, or three -1s where the record carries no place. */
private fun decodeCaughtWhere(tail: ByteArray): Triple<Int, Int, Int> {
  val v = tailEntry(tail, TAIL_TAG_CAUGHT_WHERE, CAUGHT_WHERE_BYTES) ?: return Triple(-1, -1, -1)
  fun at(i: Int): Int =
      (((v[i].toInt() and 0xFF) or ((v[i + 1].toInt() and 0xFF) shl 8)).toShort()).toInt()
  return Triple(at(0), at(2), at(4))
}

private fun packRarity(p: Pokemon): Int =
    (if (p.isShiny) PokemonRarityFlag.SHINY.mask else 0) or
        (if (p.hasHiddenAbility) PokemonRarityFlag.HIDDEN_ABILITY.mask else 0) or
        (if (p.isAlpha) PokemonRarityFlag.ALPHA.mask else 0) or
        (if (p.isSecret) PokemonRarityFlag.SECRET_SHINY.mask else 0) or
        (if (p.isFatefulEncounter) PokemonRarityFlag.FATEFUL_ENCOUNTER.mask else 0) or
        (if (p.isRaidEncounter) PokemonRarityFlag.RAID_ENCOUNTER.mask else 0)

// EVs are six raw bytes in wire order hp, atk, def, spd, spAtk, spDef. The offset is fixed by the
// client's reader; the order within the six is this project's, and no capture distinguishes it.
private fun evsFromWire(hp: Int, atk: Int, def: Int, spd: Int, spAtk: Int, spDef: Int): EVs =
    EVs().apply {
      this.hp = hp
      this.atk = atk
      this.def = def
      this.spd = spd
      this.spAtk = spAtk
      this.spDef = spDef
    }

// IVs are a packed 30-bit word (six 5-bit stats), each at the bit position of the client's own stat
// index, see IVs.compress. The top two bits carry unidentified flags and are dropped on write, so
// they stay zero for constructed monsters.
private fun ivsFromBits(bits: Int): IVs = decompressIVs(bits)

/** The monster record. */
object PokemonCodec : PacketCodec<Pokemon>() {
  override fun CodecScope<Pokemon>.body(): Pokemon {
    val id = field(S64LE, Pokemon::id)
    // A flagged second id. No capture has ever set the flag, so nothing is written after it.
    val hasSecondId = field(U8) { 0 } == 1
    optionalField(hasSecondId, S64LE) { null }
    field(U8) { 0 } // a four-valued enum, zero everywhere
    val ownerId = field(S64LE, Pokemon::ownerId)
    field(S64LE, Pokemon::ownerId) // read and discarded by the client
    // The record names its own container, through the same byte→container table the container
    // packet's own first byte goes through. A monster in the PC said "party" here until this read
    // what it was given.
    val containerByte = field(U8) { it.container.ordinal }
    val container =
        PokemonContainer.entries.getOrNull(containerByte)
            ?: error("unknown monster container $containerByte")
    val containerSlot = field(S16LE, Pokemon::containerSlot)
    val dexId = field(U16LE, Pokemon::dexId)
    val seed = field(S32LE, Pokemon::seed)
    field(S64LE, Pokemon::ownerId)
    val ot = field(Utf16LeNullTerminated, Pokemon::ot)
    val nickname = field(Utf16LeNullTerminated, Pokemon::nickname)
    field(U8) { 0 }
    field(U8) { 0 }
    val level = field(S8, Pokemon::level)
    val hp = field(S16LE, Pokemon::hp)
    field(S16LE) { 0 }
    val xp = field(S32LE, Pokemon::xp)
    // Two bits per move slot: the PP Ups applied to it. The client's max PP for a move is
    // base + floor(base * 0.2 * ups), so a monster with PP Ups shows short PP without this.
    field(U8) { 0 }
    // Friendship. The client's summary draws it as a percentage of 255, and the engine reads
    // it for an evolution at 220 and for the power of Return and Frustration.
    val friendship = field(S16LE) { it.friendship.toShort() }
    // Four slots always go on the wire, so a monster holding fewer writes empty ones. Only the
    // starter is padded when it is built; one caught from the wild keeps the one or two moves
    // it had, and indexing blind here threw mid-encode and shipped a malformed character list.
    val moveIds = List(4) { i -> field(S16LE) { it.moves.getOrNull(i)?.id ?: 0 } }
    val movePps = List(4) { i -> field(S8) { it.moves.getOrNull(i)?.pp ?: 0 } }
    repeat(4) { field(S16LE) { 0 } } // a second per-move word, zero in every capture
    val evHp = field(U8) { it.eVs.hp and 0xFF }
    val evAtk = field(U8) { it.eVs.atk and 0xFF }
    val evDef = field(U8) { it.eVs.def and 0xFF }
    val evSpd = field(U8) { it.eVs.spd and 0xFF }
    val evSpAtk = field(U8) { it.eVs.spAtk and 0xFF }
    val evSpDef = field(U8) { it.eVs.spDef and 0xFF }
    // The five contest conditions, one per ContestType in ordinal order.
    val conditions = ContestType.entries.map { t -> field(U8) { it.conditions[t] and 0xFF } }
    // Sheen: the one byte in this run the client reads and throws away. Written anyway, because
    // nothing else claims the position and this project's own client wants it.
    val sheen = field(U8) { it.sheen and 0xFF }
    field(U8) { 4 }
    field(U8) { 5 }
    field(U8) { 2 }
    field(S32LE) { -1 }
    field(U8) { 3 }
    // The alternate forme; the client pairs it with the dex id to pick a sprite, and reads it on
    // its
    // own for Shaymin. Giratina Origin is 1; 0 is the ordinary forme.
    val form = field(U8) { it.form and 0xFF }
    val ivBits = field(S32LE) { it.iVs.compress() }
    // The ability slot: 0 and 1 are the species' two abilities, 2 the hidden one. The client
    // only keeps a 2 when the hidden-ability rarity bit is set, so writing 0 here and the bit
    // separately would show the ordinary ability.
    field(U8) { 0 }
    field(S64LE) { 0x200000L } // species-dependent in captures; the observed value is written back
    val rarityBits = field(U16LE, ::packRarity)
    val caughtAt = field(TimestampLE, Pokemon::caughtAt)
    // A 16-bit word whose low byte is the egg flag; nothing else in it has ever been non-zero.
    val eggBits = field(U16LE) { if (it.isEgg) 1 else 0 }
    field(U8) { 0xFF } // an optional id, absent when negative
    field(U8) { 0xFF }
    val tail = field(bytesPrefixed(U8), ::encodeTail)
    val (caughtRegionId, caughtBankId, caughtMapId) = decodeCaughtWhere(tail)
    return Pokemon(
        id = id,
        ownerId = ownerId,
        container = container,
        containerSlot = containerSlot,
        dexId = dexId,
        seed = seed,
        ot = ot,
        nickname = nickname,
        level = level,
        hp = hp,
        xp = xp,
        eVs = evsFromWire(evHp, evAtk, evDef, evSpd, evSpAtk, evSpDef),
        iVs = ivsFromBits(ivBits),
        moves = List(4) { PokemonMove(moveIds[it], movePps[it]) },
        isShiny = PokemonRarityFlag.SHINY.isSet(rarityBits),
        hasHiddenAbility = PokemonRarityFlag.HIDDEN_ABILITY.isSet(rarityBits),
        isAlpha = PokemonRarityFlag.ALPHA.isSet(rarityBits),
        isSecret = PokemonRarityFlag.SECRET_SHINY.isSet(rarityBits),
        isFatefulEncounter = PokemonRarityFlag.FATEFUL_ENCOUNTER.isSet(rarityBits),
        isRaidEncounter = PokemonRarityFlag.RAID_ENCOUNTER.isSet(rarityBits),
        caughtAt = caughtAt,
        isEgg = (eggBits and 1) != 0,
        form = form,
        conditions = ContestConditions.ofList(conditions),
        sheen = sheen,
        superContestRibbons = decodeSuperContestRibbons(tail),
        caughtRegionId = caughtRegionId,
        caughtBankId = caughtBankId,
        caughtMapId = caughtMapId,
        caughtLocationLabel = decodeCaughtLabel(tail),
        friendship = friendship.toInt().coerceIn(0, MAX_FRIENDSHIP),
        heldItemId = decodeHeldItem(tail),
        status = decodeStatus(tail),
    )
  }
}
