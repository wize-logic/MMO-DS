package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.net.game.packets.PlayerVariableEntry
import de.fiereu.openmmo.net.game.packets.SaveBlockEntry
import de.fiereu.openmmo.net.game.packets.ScriptFlagEntry
import de.fiereu.openmmo.net.game.packets.ScriptStatePacket
import de.fiereu.openmmo.net.game.packets.ScriptVarEntry
import de.fiereu.openmmo.net.game.packets.StoryFlagUpdatePacket
import de.fiereu.openmmo.story.generated.hoenn.HoennFlags
import de.fiereu.openmmo.story.generated.hoenn.HoennVars
import de.fiereu.openmmo.story.generated.kanto.KantoFlags
import de.fiereu.openmmo.story.generated.kanto.KantoVars
import io.github.oshai.kotlinlogging.KotlinLogging

private val log = KotlinLogging.logger {}

/** Converts persisted story keys to client ids. */
internal object StoryClientState {
  fun flags(regionId: Byte, flags: Collection<String>): List<StoryFlagUpdatePacket> =
      flags.mapNotNull { flagUpdate(regionId, it, enabled = true) }.sortedBy { it.flagId }

  fun flagUpdate(regionId: Byte, key: String, enabled: Boolean): StoryFlagUpdatePacket? {
    val id = flagId(regionId, key) ?: return null
    return StoryFlagUpdatePacket(regionId, id, enabled)
  }

  fun variables(regionId: Byte, vars: Map<String, Int>): List<PlayerVariableEntry> =
      vars
          .mapNotNull { (key, value) ->
            val id = varId(regionId, key) ?: return@mapNotNull null
            if (id !in GBA_VARS_START..GBA_VARS_END) return@mapNotNull null
            PlayerVariableEntry((id - GBA_VARS_START).toShort(), clampToWire(key, value))
          }
          .sortedBy { it.key.toInt() }

  /**
   * The wire holds a variable's value in one signed byte, so a story value outside that range
   * cannot be sent at all. Saying so out loud beats handing the client a truncated value it will
   * treat as the real one.
   */
  private fun clampToWire(key: String, value: Int): Byte {
    if (value !in Byte.MIN_VALUE..Byte.MAX_VALUE) {
      log.warn { "story variable $key = $value does not fit the wire's byte; sending it truncated" }
    }
    return value.toByte()
  }

  /**
   * Every var in the GBA range, zeros included. A var back at 0 is stored as absent, so sending
   * only what is stored would leave the client holding the old value.
   */
  fun allVariables(regionId: Byte, vars: Map<String, Int>): List<PlayerVariableEntry> {
    val byId = variables(regionId, vars).associate { it.key.toInt() to it.value }
    return (0..(GBA_VARS_END - GBA_VARS_START)).map {
      PlayerVariableEntry(it.toShort(), byId[it] ?: 0)
    }
  }

  /**
   * The seat for the client's own script VM: every `region/vm/...` key this character holds, turned
   * back into the engine's flag and var ids.
   */
  fun scriptState(
      regionId: Byte,
      flags: Collection<String>,
      vars: Map<String, Int>,
      saveBlocks: Map<Int, ByteArray> = emptyMap(),
      // Rows the server derives from what it holds elsewhere (SyntheticRows): the badges and the
      // respawn. They are never stored as vm rows, so they are added here rather than read back.
      syntheticFlags: List<ScriptFlagEntry> = emptyList(),
      syntheticVars: List<ScriptVarEntry> = emptyList(),
  ): ScriptStatePacket =
      fitOnePacket(
          (flags
                  .mapNotNull { VmStoryKeys.flagId(regionId, it) }
                  .map { ScriptFlagEntry(it.toShort(), true) } + syntheticFlags)
              .sortedBy { it.id },
          (vars.entries
                  .mapNotNull { (key, value) ->
                    VmStoryKeys.varId(regionId, key)?.let { id -> id to value }
                  }
                  .filter { it.second != 0 }
                  .map { (id, value) -> ScriptVarEntry(id.toShort(), value.toShort()) } +
                  syntheticVars)
              .sortedBy { it.id },
          // Whole save blocks, in id order so a seat is the same bytes twice running. A block the
          // store does not hold is left off, and the client keeps whatever its own fresh save has.
          saveBlocks.entries.sortedBy { it.key }.map { SaveBlockEntry(it.key, it.value) },
      )

  /** The seat, cut down to what one packet can carry if it does not already fit. */
  private fun fitOnePacket(
      flags: List<ScriptFlagEntry>,
      vars: List<ScriptVarEntry>,
      blocks: List<SaveBlockEntry>,
  ): ScriptStatePacket {
    val total =
        SEAT_HEADER_BYTES +
            blocks.sumOf { SEAT_BLOCK_ROW_BYTES + it.data.size } +
            flags.size * SEAT_FLAG_ROW_BYTES +
            vars.size * SEAT_VAR_ROW_BYTES
    if (total <= SEAT_BYTES_MAX) return ScriptStatePacket(flags, vars, blocks)

    var left = SEAT_BYTES_MAX - SEAT_HEADER_BYTES
    val keptBlocks = mutableListOf<SaveBlockEntry>()
    for (block in blocks) {
      val cost = SEAT_BLOCK_ROW_BYTES + block.data.size
      if (cost > left) continue
      left -= cost
      keptBlocks += block
    }
    val keptFlags = flags.take(left / SEAT_FLAG_ROW_BYTES)
    left -= keptFlags.size * SEAT_FLAG_ROW_BYTES
    val keptVars = vars.take(left / SEAT_VAR_ROW_BYTES)
    log.error {
      "a script seat came to $total bytes, past the $SEAT_BYTES_MAX one packet carries:" +
          " sending ${keptBlocks.size} of ${blocks.size} save block(s)," +
          " ${keptFlags.size} of ${flags.size} flag(s) and ${keptVars.size} of ${vars.size}" +
          " variable(s). A block left off is seated from the game's own defaults and reported back" +
          " over the stored one, so this is a character to look at rather than a line to watch"
    }
    return ScriptStatePacket(keptFlags, keptVars, keptBlocks)
  }

  private fun flagId(regionId: Byte, key: String): Int? =
      when (regionId.toInt()) {
        KANTO_REGION -> KantoFlags.numericId(key)
        HOENN_REGION -> HoennFlags.numericId(key)
        else -> null
      }

  private fun varId(regionId: Byte, key: String): Int? =
      when (regionId.toInt()) {
        KANTO_REGION -> KantoVars.numericId(key)
        HOENN_REGION -> HoennVars.numericId(key)
        else -> null
      }

  private const val SEAT_BYTES_MAX = 60_000
  /** The two list counts and the block count, which every seat pays whatever it carries. */
  private const val SEAT_HEADER_BYTES = 5
  /** An id and a bool. */
  private const val SEAT_FLAG_ROW_BYTES = 3
  /** An id and a value. */
  private const val SEAT_VAR_ROW_BYTES = 4
  /** An id and the length in front of the bytes. */
  private const val SEAT_BLOCK_ROW_BYTES = 3

  private const val KANTO_REGION = 0
  private const val HOENN_REGION = 1
  private const val GBA_VARS_START = 0x4000
  private const val GBA_VARS_END = 0x40ff
}
