package de.fiereu.openmmo.server.game.offline

import de.fiereu.openmmo.common.CharacterInfo
import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.DynamicWarp
import de.fiereu.openmmo.common.HealLocation
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.Skin
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.PokemonStat
import de.fiereu.openmmo.common.enums.PokemonStats
import de.fiereu.openmmo.common.enums.SkinSlot
import de.fiereu.openmmo.server.game.storage.StoredCharacter
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.time.LocalDateTime
import java.time.ZoneOffset

/**
 * A character exactly as it stood, plus the engine blocks it was carrying and how much of its bag
 * had come out of a save file.
 */
data class CharacterCopy(
    val character: StoredCharacter,
    val blocks: Map<Int, ByteArray>,
    val offlineItems: Map<Int, Int> = emptyMap(),
)

/** The character an import is about to replace, written down whole so it can be put back. */
object ImportSnapshot {

  const val VERSION = 4

  /** Written last and checked first, so a truncated or half-written blob is refused. */
  private const val TRAILER = 0x4F4D5342 // "OMSB"

  class UnreadableSnapshot(message: String) : IllegalStateException(message)

  fun encode(copy: CharacterCopy): ByteArray {
    val bytes = ByteArrayOutputStream()
    DataOutputStream(bytes).use { out ->
      out.writeInt(VERSION)
      writeInfo(out, copy.character.info)
      writeMonsters(out, copy.character.pokemon)
      writeMonsters(out, copy.character.pcStorage)
      writeMonsters(out, copy.character.daycare)
      out.writeInt(copy.character.items.size)
      for ((itemId, quantity) in copy.character.items) {
        out.writeInt(itemId)
        out.writeInt(quantity)
      }
      out.writeInt(copy.character.storyFlags.size)
      for (flag in copy.character.storyFlags) out.writeUTF(flag)
      out.writeInt(copy.character.storyVars.size)
      for ((key, value) in copy.character.storyVars) {
        out.writeUTF(key)
        out.writeInt(value)
      }
      out.writeInt(copy.character.skins.size)
      for ((slot, skin) in copy.character.skins) {
        out.writeUTF(slot.name)
        out.writeUTF(skin.slot.name)
        writeNullableInt(out, skin.type?.toInt())
        writeNullableInt(out, skin.color?.toInt())
      }
      out.writeInt(copy.blocks.size)
      for ((id, data) in copy.blocks) {
        out.writeInt(id)
        out.writeInt(data.size)
        out.write(data)
      }
      out.writeInt(copy.offlineItems.size)
      for ((itemId, quantity) in copy.offlineItems) {
        out.writeInt(itemId)
        out.writeInt(quantity)
      }
      out.writeInt(TRAILER)
    }
    return bytes.toByteArray()
  }

  fun decode(blob: ByteArray, version: Int): CharacterCopy {
    if (version != VERSION) {
      throw UnreadableSnapshot(
          "snapshot is version $version and this server writes and reads $VERSION")
    }
    val input = DataInputStream(ByteArrayInputStream(blob))
    val stated = input.readInt()
    if (stated != VERSION) {
      throw UnreadableSnapshot("snapshot says version $stated and its row says $version")
    }
    val info = readInfo(input)
    val party = readMonsters(input)
    val box = readMonsters(input)
    val daycare = readMonsters(input)
    val items = mutableMapOf<Int, Int>()
    repeat(input.readInt()) { items[input.readInt()] = input.readInt() }
    val flags = mutableSetOf<String>()
    repeat(input.readInt()) { flags += input.readUTF() }
    val vars = mutableMapOf<String, Int>()
    repeat(input.readInt()) { vars[input.readUTF()] = input.readInt() }
    val skins = mutableMapOf<SkinSlot, Skin>()
    repeat(input.readInt()) {
      val key = SkinSlot.valueOf(input.readUTF())
      skins[key] =
          Skin(
              SkinSlot.valueOf(input.readUTF()),
              readNullableInt(input)?.toUShort(),
              readNullableInt(input)?.toUByte())
    }
    val blocks = mutableMapOf<Int, ByteArray>()
    repeat(input.readInt()) {
      val id = input.readInt()
      blocks[id] = ByteArray(input.readInt()).also { input.readFully(it) }
    }
    val offlineItems = mutableMapOf<Int, Int>()
    repeat(input.readInt()) { offlineItems[input.readInt()] = input.readInt() }
    if (input.readInt() != TRAILER) throw UnreadableSnapshot("snapshot ends short of its trailer")
    return CharacterCopy(
        StoredCharacter(
            info = info,
            pokemon = party.toMutableList(),
            pcStorage = box.toMutableList(),
            daycare = daycare.toMutableList(),
            items = items,
            storyFlags = flags,
            storyVars = vars,
            skins = skins),
        blocks,
        offlineItems)
  }

  // ---- the character's own row -----------------------------------------------------------------

  private fun writeInfo(out: DataOutputStream, info: CharacterInfo) {
    out.writeLong(info.id)
    out.writeUTF(info.name)
    out.writeUTF(info.namePrefix)
    out.writeInt(info.userId)
    out.writeByte(info.rivalSex.toInt())
    out.writeInt(info.skinRegionSelectionIndex)
    writeTime(out, info.lastLogin)
    writeTime(out, info.createdAt)
    out.writeInt(info.money)
    out.writeInt(info.permissions)
    out.writeShort(info.remainingSafariSteps.toInt())
    out.writeByte(info.remainingSafariBalls.toInt())
    out.writeByte(info.pcExtraSlots.toInt())
    out.writeByte(info.battleBoxExtraSlots.toInt())
    out.writeByte(info.templateAmount.toInt())
    out.writeByte(info.positionRegionId.toInt())
    out.writeByte(info.positionBankId.toInt())
    out.writeByte(info.positionMapId.toInt())
    out.writeShort(info.positionX.toInt())
    out.writeShort(info.positionY.toInt())
    out.writeUTF(info.positionFacing.name)
    out.writeShort(info.repelLeft.toInt())
    out.writeShort(info.repelItemId.toInt())
    out.writeShort(info.lureLeft.toInt())
    out.writeShort(info.lureItemId.toInt())
    out.writeShort(info.registeredItem.toInt())
    val warp = info.dynamicWarp
    out.writeBoolean(warp != null)
    if (warp != null) {
      out.writeByte(warp.regionId.toInt())
      out.writeByte(warp.bankId.toInt())
      out.writeByte(warp.mapId.toInt())
      out.writeShort(warp.x.toInt())
      out.writeShort(warp.y.toInt())
      out.writeUTF(warp.facing.name)
    }
    val heal = info.lastHealLocation
    out.writeBoolean(heal != null)
    if (heal != null) {
      out.writeByte(heal.regionId.toInt())
      out.writeByte(heal.bankId.toInt())
      out.writeByte(heal.mapId.toInt())
      out.writeShort(heal.x.toInt())
      out.writeShort(heal.y.toInt())
    }
  }

  private fun readInfo(input: DataInputStream): CharacterInfo =
      CharacterInfo(
          id = input.readLong(),
          name = input.readUTF(),
          namePrefix = input.readUTF(),
          userId = input.readInt(),
          rivalSex = input.readByte(),
          skinRegionSelectionIndex = input.readInt(),
          lastLogin = readTime(input),
          createdAt = readTime(input),
          money = input.readInt(),
          permissions = input.readInt(),
          remainingSafariSteps = input.readShort(),
          remainingSafariBalls = input.readByte(),
          pcExtraSlots = input.readByte(),
          battleBoxExtraSlots = input.readByte(),
          templateAmount = input.readByte(),
          positionRegionId = input.readByte(),
          positionBankId = input.readByte(),
          positionMapId = input.readByte(),
          positionX = input.readShort(),
          positionY = input.readShort(),
          positionFacing = Direction.valueOf(input.readUTF()),
          repelLeft = input.readShort(),
          repelItemId = input.readShort(),
          lureLeft = input.readShort(),
          lureItemId = input.readShort(),
          registeredItem = input.readShort(),
          dynamicWarp =
              if (!input.readBoolean()) null
              else
                  DynamicWarp(
                      regionId = input.readByte(),
                      bankId = input.readByte(),
                      mapId = input.readByte(),
                      x = input.readShort(),
                      y = input.readShort(),
                      facing = Direction.valueOf(input.readUTF())),
          lastHealLocation =
              if (!input.readBoolean()) null
              else
                  HealLocation(
                      regionId = input.readByte(),
                      bankId = input.readByte(),
                      mapId = input.readByte(),
                      x = input.readShort(),
                      y = input.readShort()),
      )

  // ---- the monsters ----------------------------------------------------------------------------

  private fun writeMonsters(out: DataOutputStream, monsters: List<Pokemon>) {
    out.writeInt(monsters.size)
    for (monster in monsters) writeMonster(out, monster)
  }

  private fun readMonsters(input: DataInputStream): List<Pokemon> =
      List(input.readInt()) { readMonster(input) }

  private fun writeMonster(out: DataOutputStream, monster: Pokemon) {
    out.writeLong(monster.id)
    out.writeLong(monster.ownerId)
    out.writeUTF(monster.container.name)
    out.writeShort(monster.containerSlot.toInt())
    out.writeInt(monster.dexId)
    out.writeInt(monster.seed)
    out.writeUTF(monster.ot)
    out.writeUTF(monster.nickname)
    out.writeByte(monster.level.toInt())
    out.writeShort(monster.hp.toInt())
    out.writeInt(monster.xp)
    writeStats(out, monster.eVs)
    writeStats(out, monster.iVs)
    out.writeInt(monster.moves.size)
    for (move in monster.moves) {
      out.writeShort(move.id.toInt())
      out.writeByte(move.pp.toInt())
    }
    out.writeBoolean(monster.isShiny)
    out.writeBoolean(monster.hasHiddenAbility)
    out.writeBoolean(monster.isAlpha)
    out.writeBoolean(monster.isSecret)
    out.writeBoolean(monster.isFatefulEncounter)
    out.writeBoolean(monster.isRaidEncounter)
    writeTime(out, monster.caughtAt)
    out.writeBoolean(monster.isEgg)
    out.writeInt(monster.form)
    out.writeInt(monster.conditions.cool)
    out.writeInt(monster.conditions.beauty)
    out.writeInt(monster.conditions.cute)
    out.writeInt(monster.conditions.smart)
    out.writeInt(monster.conditions.tough)
    out.writeInt(monster.sheen)
    out.writeLong(monster.superContestRibbons)
    out.writeInt(monster.caughtRegionId)
    out.writeInt(monster.caughtBankId)
    out.writeInt(monster.caughtMapId)
    out.writeInt(monster.caughtLocationLabel)
    out.writeInt(monster.friendship)
    out.writeInt(monster.heldItemId)
    out.writeInt(monster.status)
    out.writeBoolean(monster.offlineOrigin)
  }

  private fun readMonster(input: DataInputStream): Pokemon =
      Pokemon(
          id = input.readLong(),
          ownerId = input.readLong(),
          container = PokemonContainer.valueOf(input.readUTF()),
          containerSlot = input.readShort(),
          dexId = input.readInt(),
          seed = input.readInt(),
          ot = input.readUTF(),
          nickname = input.readUTF(),
          level = input.readByte(),
          hp = input.readShort(),
          xp = input.readInt(),
          eVs = readStats(input, EVs()),
          iVs = readStats(input, IVs()),
          moves = List(input.readInt()) { PokemonMove(input.readShort(), input.readByte()) },
          isShiny = input.readBoolean(),
          hasHiddenAbility = input.readBoolean(),
          isAlpha = input.readBoolean(),
          isSecret = input.readBoolean(),
          isFatefulEncounter = input.readBoolean(),
          isRaidEncounter = input.readBoolean(),
          caughtAt = readTime(input),
          isEgg = input.readBoolean(),
          form = input.readInt(),
          conditions =
              ContestConditions(
                  cool = input.readInt(),
                  beauty = input.readInt(),
                  cute = input.readInt(),
                  smart = input.readInt(),
                  tough = input.readInt()),
          sheen = input.readInt(),
          superContestRibbons = input.readLong(),
          caughtRegionId = input.readInt(),
          caughtBankId = input.readInt(),
          caughtMapId = input.readInt(),
          caughtLocationLabel = input.readInt(),
          friendship = input.readInt(),
          heldItemId = input.readInt(),
          status = input.readInt(),
          offlineOrigin = input.readBoolean(),
      )

  /**
   * The six stats in the client's own order, which is the order the word they compress into is read
   * in.
   */
  private fun writeStats(out: DataOutputStream, stats: PokemonStats) {
    for (stat in PokemonStat.entries) out.writeByte(stats.getValue(stat).toInt())
  }

  private fun <T : PokemonStats> readStats(input: DataInputStream, into: T): T {
    for (stat in PokemonStat.entries) into[stat] = input.readByte()
    return into
  }

  // ---- the small pieces ------------------------------------------------------------------------

  private fun writeTime(out: DataOutputStream, at: LocalDateTime) {
    out.writeLong(at.toEpochSecond(ZoneOffset.UTC))
    out.writeInt(at.nano)
  }

  private fun readTime(input: DataInputStream): LocalDateTime =
      LocalDateTime.ofEpochSecond(input.readLong(), input.readInt(), ZoneOffset.UTC)

  private fun writeNullableInt(out: DataOutputStream, value: Int?) {
    out.writeBoolean(value != null)
    if (value != null) out.writeInt(value)
  }

  private fun readNullableInt(input: DataInputStream): Int? =
      if (input.readBoolean()) input.readInt() else null
}
