package de.fiereu.openmmo.server.game.offline

import de.fiereu.bytecodec.Bool
import de.fiereu.bytecodec.ByteArrayReadBuffer
import de.fiereu.bytecodec.Codec
import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.GrowableWriteBuffer
import de.fiereu.bytecodec.MalformedPacketException
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.ReadBuffer
import de.fiereu.bytecodec.S16LE
import de.fiereu.bytecodec.S32LE
import de.fiereu.bytecodec.S64LE
import de.fiereu.bytecodec.U16LE
import de.fiereu.bytecodec.U8
import de.fiereu.bytecodec.WriteBuffer
import de.fiereu.bytecodec.bytesPrefixed
import de.fiereu.bytecodec.listPrefixed
import de.fiereu.bytecodec.stringPrefixed
import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.ContestType
import de.fiereu.openmmo.common.MON_STATUS_MASK
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.PokemonStat
import java.nio.charset.StandardCharsets

/** A save file as the client that read it puts it on the wire. */
data class OfflineSaveWire(
    val trainerId: Int,
    val money: Int,
    /** The engine's own badge bitfield, one bit a badge, in `BADGE_ID_*` order. */
    val badges: Int,
    val playTimeSeconds: Int,
    val position: WirePosition,
    /**
     * The engine's black-out warp id: a 1-based row of the cartridge's own spawn table and not a
     * tile, so it is a place only that table can name. 0 for a save that has never been in a
     * Pokemon Center.
     */
    val blackOutWarpId: Int,
    val monsters: List<OfflineMonster>,
    val bag: List<OfflineItem>,
    val dexSeen: Set<Int>,
    val dexCaught: Set<Int>,
    /** Engine flag numbers that are set. */
    val flagIds: List<Int>,
    /** Engine variable numbers and their values; a variable at 0 is left off. */
    val varIds: List<Pair<Int, Int>>,
    val blocks: Map<Int, ByteArray>,
    /**
     * The client's own hash of the file it read. Recognises the same save twice; decides nothing.
     */
    val saveSha256: String,
    val clientRevision: Int,
) {
  companion object {
    const val MAGIC = "OMIR"
    const val VERSION = 4

    /**
     * What a client can send at all, which is not what an honest save holds, the door below is the
     * one that knows that.
     */
    const val MAX_MONSTERS = 1024
    const val MAX_BAG_LINES = 2048
    const val MAX_DEX = 2048
    const val MAX_FLAGS = 8192
    const val MAX_VARS = 4096
    const val MAX_BLOCKS = 64

    /** The joined report, refused with a sentence rather than an exception if it is not one. */
    fun decode(bytes: ByteArray): OfflineSaveWire {
      val wire = OfflineSaveWireCodec.read(ByteArrayReadBuffer(bytes))
      wire.check()
      return wire
    }

    fun encode(wire: OfflineSaveWire): ByteArray {
      val buf = GrowableWriteBuffer(4096)
      OfflineSaveWireCodec.write(buf, wire)
      return buf.toByteArray()
    }
  }

  private fun check() {
    if (monsters.size > MAX_MONSTERS) tooMany("monsters", monsters.size, MAX_MONSTERS)
    if (bag.size > MAX_BAG_LINES) tooMany("bag lines", bag.size, MAX_BAG_LINES)
    if (dexSeen.size > MAX_DEX) tooMany("Pokedex seen", dexSeen.size, MAX_DEX)
    if (dexCaught.size > MAX_DEX) tooMany("Pokedex caught", dexCaught.size, MAX_DEX)
    if (flagIds.size > MAX_FLAGS) tooMany("story flags", flagIds.size, MAX_FLAGS)
    if (varIds.size > MAX_VARS) tooMany("story variables", varIds.size, MAX_VARS)
    if (blocks.size > MAX_BLOCKS) tooMany("save blocks", blocks.size, MAX_BLOCKS)
  }

  private fun tooMany(what: String, got: Int, cap: Int): Nothing =
      throw MalformedPacketException("$got $what in one report, past the $cap a client sends")
}

private val Text: Codec<String> = stringPrefixed(U16LE, StandardCharsets.UTF_8)

private val MoveCodec: Codec<OfflineMove> =
    object : PacketCodec<OfflineMove>() {
      override fun CodecScope<OfflineMove>.body(): OfflineMove {
        val moveId = field(U16LE) { it.moveId }
        val pp = field(U8) { it.pp }
        val ppUps = field(U8) { it.ppUps }
        return OfflineMove(moveId, pp, ppUps)
      }
    }

/** The six stats in [PokemonStat]'s own order, one byte each. */
private val StatsCodec: Codec<Map<PokemonStat, Int>> =
    object : Codec<Map<PokemonStat, Int>> {
      override fun read(buf: ReadBuffer): Map<PokemonStat, Int> =
          PokemonStat.entries.associateWith { U8.read(buf) }

      override fun write(buf: WriteBuffer, value: Map<PokemonStat, Int>) {
        PokemonStat.entries.forEach { U8.write(buf, (value[it] ?: 0) and 0xFF) }
      }
    }

private val MonsterCodec: Codec<OfflineMonster> =
    object : PacketCodec<OfflineMonster>() {
      override fun CodecScope<OfflineMonster>.body(): OfflineMonster {
        val pid = field(S32LE) { it.pid }
        val dexId = field(U16LE) { it.dexId }
        val form = field(U8) { it.form }
        val level = field(U8) { it.level }
        val xp = field(S32LE) { it.xp }
        val ivs = field(StatsCodec) { it.ivs }
        val evs = field(StatsCodec) { it.evs }
        val moves = field(MoveCodec.listPrefixed(U8)) { it.moves }
        val nickname = field(Text) { it.nickname }
        val otName = field(Text) { it.otName }
        val otId = field(S32LE) { it.otId }
        val abilityId = field(U16LE) { it.abilityId }
        val hasHiddenAbility = field(Bool) { it.hasHiddenAbility }
        val natureByte = field(U8) { it.natureByte }
        val isShiny = field(Bool) { it.isShiny }
        val heldItemId = field(U16LE) { it.heldItemId }
        val friendship = field(U8) { it.friendship }
        val isEgg = field(Bool) { it.isEgg }
        val eggCyclesLeft = field(U8) { it.eggCyclesLeft }
        // The container is the engine's own two homes, and the wire names them by the byte the
        // client writes rather than by an ordinal, so adding a third home to the enum later cannot
        // silently move a boxed monster into the party.
        val container = field(U8) { if (it.container == PokemonContainer.PARTY) 0 else 1 }
        val containerSlot = field(U16LE) { it.containerSlot }
        // The contest half, in the same shape the live monster record carries it: five
        // condition bytes in ContestType order, sheen, and the ribbon mask whole.
        val conditions = ContestType.entries.map { t -> field(U8) { it.conditions[t] and 0xFF } }
        val sheen = field(U8) { it.sheen and 0xFF }
        val superContestRibbons = field(S64LE) { it.superContestRibbons }
        val metLocationLabel = field(U16LE) { it.metLocationLabel }
        val ballItemId = field(U16LE) { it.ballItemId }
        val pokerus = field(U8) { it.pokerus and 0xFF }
        val markings = field(U8) { it.markings and 0xFF }
        // The condition word last, where the live monster record carries it too.
        val status = field(U16LE) { it.status and MON_STATUS_MASK }
        return OfflineMonster(
            pid = pid,
            dexId = dexId,
            form = form,
            level = level,
            xp = xp,
            ivs = ivs,
            evs = evs,
            moves = moves,
            nickname = nickname,
            otName = otName,
            otId = otId,
            abilityId = abilityId,
            hasHiddenAbility = hasHiddenAbility,
            natureByte = natureByte,
            isShiny = isShiny,
            heldItemId = heldItemId,
            friendship = friendship,
            isEgg = isEgg,
            eggCyclesLeft = eggCyclesLeft,
            container = if (container == 0) PokemonContainer.PARTY else PokemonContainer.PC,
            containerSlot = containerSlot,
            conditions = ContestConditions.ofList(conditions),
            sheen = sheen,
            superContestRibbons = superContestRibbons,
            metLocationLabel = metLocationLabel,
            ballItemId = ballItemId,
            pokerus = pokerus,
            markings = markings,
            status = status and MON_STATUS_MASK,
        )
      }
    }

private val ItemCodec: Codec<OfflineItem> =
    object : PacketCodec<OfflineItem>() {
      override fun CodecScope<OfflineItem>.body(): OfflineItem {
        val itemId = field(U16LE) { it.itemId }
        val quantity = field(U16LE) { it.quantity }
        return OfflineItem(itemId, quantity)
      }
    }

/** Where the save left the player, without a region. */
data class WirePosition(val bankId: Int, val mapId: Int, val x: Int, val y: Int)

private val PositionCodec: Codec<WirePosition> =
    object : PacketCodec<WirePosition>() {
      override fun CodecScope<WirePosition>.body(): WirePosition {
        val bankId = field(U8) { it.bankId }
        val mapId = field(U16LE) { it.mapId }
        val x = field(S16LE) { it.x.toShort() }
        val y = field(S16LE) { it.y.toShort() }
        return WirePosition(bankId, mapId, x.toInt(), y.toInt())
      }
    }

private val VarCodec: Codec<Pair<Int, Int>> =
    object : PacketCodec<Pair<Int, Int>>() {
      override fun CodecScope<Pair<Int, Int>>.body(): Pair<Int, Int> {
        val id = field(U16LE) { it.first }
        val value = field(S16LE) { it.second.toShort() }
        return id to value.toInt()
      }
    }

private val BlockCodec: Codec<Pair<Int, ByteArray>> =
    object : PacketCodec<Pair<Int, ByteArray>>() {
      override fun CodecScope<Pair<Int, ByteArray>>.body(): Pair<Int, ByteArray> {
        val id = field(U8) { it.first }
        val data = field(bytesPrefixed(U16LE)) { it.second }
        return id to data
      }
    }

object OfflineSaveWireCodec : PacketCodec<OfflineSaveWire>() {
  override fun CodecScope<OfflineSaveWire>.body(): OfflineSaveWire {
    val magic = field(stringPrefixed(U8, StandardCharsets.US_ASCII)) { OfflineSaveWire.MAGIC }
    if (magic != OfflineSaveWire.MAGIC) {
      throw MalformedPacketException("that is not a save report (magic '$magic')")
    }
    val version = field(U16LE) { OfflineSaveWire.VERSION }
    if (version != OfflineSaveWire.VERSION) {
      throw MalformedPacketException(
          "a save report of version $version, and this server reads ${OfflineSaveWire.VERSION}")
    }
    val saveSha256 = field(Text) { it.saveSha256 }
    val clientRevision = field(S32LE) { it.clientRevision }
    val trainerId = field(S32LE) { it.trainerId }
    val money = field(S32LE) { it.money }
    val badges = field(S32LE) { it.badges }
    val playTimeSeconds = field(S32LE) { it.playTimeSeconds }
    val position = field(PositionCodec) { it.position }
    val blackOutWarpId = field(U16LE) { it.blackOutWarpId }
    val monsters = field(MonsterCodec.listPrefixed(U16LE)) { it.monsters }
    val bag = field(ItemCodec.listPrefixed(U16LE)) { it.bag }
    val dexSeen = field(U16LE.listPrefixed(U16LE)) { it.dexSeen.sorted() }
    val dexCaught = field(U16LE.listPrefixed(U16LE)) { it.dexCaught.sorted() }
    val flagIds = field(U16LE.listPrefixed(U16LE)) { it.flagIds }
    val varIds = field(VarCodec.listPrefixed(U16LE)) { it.varIds }
    val blocks =
        field(BlockCodec.listPrefixed(U8)) { it.blocks.entries.map { e -> e.key to e.value } }
    return OfflineSaveWire(
        trainerId = trainerId,
        money = money,
        badges = badges,
        playTimeSeconds = playTimeSeconds,
        position = position,
        blackOutWarpId = blackOutWarpId,
        monsters = monsters,
        bag = bag,
        dexSeen = dexSeen.toSet(),
        dexCaught = dexCaught.toSet(),
        flagIds = flagIds,
        varIds = varIds,
        blocks = blocks.toMap(),
        saveSha256 = saveSha256,
        clientRevision = clientRevision,
    )
  }
}
