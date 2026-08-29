package de.fiereu.openmmo.server.game.script

/** Finds the [Script] for a decomp script label, or null when nothing is wired for it. */
class ScriptRegistry(private val byLabel: Map<String, Script>) {
  fun forLabel(scriptLabel: String): Script? = byLabel[scriptLabel]

  /**
   * Sinnoh events carry a numeric script id, not a unique decomp label. Those are stored as
   * `region:bank:map:id` so "1" on two maps cannot collide.
   */
  fun forMap(regionId: Int, bankId: Int, mapId: Int, scriptLabel: String): Script? {
    if (scriptLabel.isEmpty() || scriptLabel == "0" || scriptLabel == "0x0") return null
    val r = regionId and 0xFF
    val b = bankId and 0xFF
    val m = mapId and 0xFF
    return byLabel["$r:$b:$m:$scriptLabel"] ?: byLabel[scriptLabel]
  }

  companion object {
    /** Empty since 2026-08-20. */
    fun generated(): ScriptRegistry = ScriptRegistry(emptyMap())
  }
}
