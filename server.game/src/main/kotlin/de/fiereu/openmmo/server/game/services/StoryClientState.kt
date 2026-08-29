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
  ): ScriptStatePacket =
      ScriptStatePacket(
          flags
              .mapNotNull { VmStoryKeys.flagId(regionId, it) }
              .sorted()
              .map { ScriptFlagEntry(it.toShort(), true) },
          vars.entries
              .mapNotNull { (key, value) ->
                VmStoryKeys.varId(regionId, key)?.let { id -> id to value }
              }
              .filter { it.second != 0 }
              .sortedBy { it.first }
              .map { (id, value) -> ScriptVarEntry(id.toShort(), value.toShort()) },
          // Whole save blocks, in id order so a seat is the same bytes twice running. A block the
          // store does not hold is left off, and the client keeps whatever its own fresh save has.
          saveBlocks.entries.sortedBy { it.key }.map { SaveBlockEntry(it.key, it.value) },
      )

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

  private const val KANTO_REGION = 0
  private const val HOENN_REGION = 1
  private const val GBA_VARS_START = 0x4000
  private const val GBA_VARS_END = 0x40ff
}
