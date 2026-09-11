package de.fiereu.openmmo.codegen.maps

object RenderUtil {

  private const val EMPTY_LIST = "emptyList()"
  private const val INLINE_OPEN = "listOf("
  private const val LIST_OPEN = "listOf(\n            "
  private const val LIST_SEP = ",\n            "
  private const val LIST_CLOSE = ",\n        )"

  fun borderTiles(tiles: List<Int>): String {
    val take = tiles.take(4)
    val filled =
        if (take.size < 4) take + List(4 - take.size) { if (take.isEmpty()) 8 else take.last() }
        else take
    return filled.joinToString(", ", INLINE_OPEN, ")") {
      "Tile2D(0x${it.toString(16).uppercase().padStart(4, '0')}, 0)"
    }
  }

  fun connections(conns: List<ParsedConnection>): String {
    if (conns.isEmpty()) return EMPTY_LIST
    return conns.joinToString(LIST_SEP, LIST_OPEN, LIST_CLOSE) {
      "MapData.GbaConnection(direction = ${it.direction}, unknown = ${it.offset}," +
          " targetBank = ${it.targetBank}, targetMap = ${it.targetMap})"
    }
  }

  fun warps(warps: List<ParsedWarp>): String {
    if (warps.isEmpty()) return EMPTY_LIST
    return warps.joinToString(LIST_SEP, LIST_OPEN, LIST_CLOSE) {
      "WarpTile(x = ${it.x}, y = ${it.y}, elevation = ${it.elevation}," +
          " targetRegionId = ${it.targetRegion}.toByte(), targetBankId = ${it.targetBank}.toByte()," +
          " targetMapId = ${it.targetMap}.toByte()," +
          " targetX = ${it.targetX}, targetY = ${it.targetY}, targetElevation = ${it.targetElevation}," +
          " dynamic = ${it.dynamic})"
    }
  }

  fun npcs(npcs: List<ParsedNpc>): String {
    if (npcs.isEmpty()) return EMPTY_LIST
    return npcs.joinToString(LIST_SEP, LIST_OPEN, LIST_CLOSE) {
      "NpcDef(entityIdx = ${it.entityIdx}, graphicsId = ${it.graphicsId}," +
          " x = ${it.x}, y = ${it.y}, elevation = ${it.elevation}," +
          " movementType = ${it.movementType}, movementRangeX = ${it.movementRangeX}," +
          " movementRangeY = ${it.movementRangeY}, trainerType = ${it.trainerType}," +
          " facing = ${it.facing}, script = ${escapeString(it.script)}," +
          " hideFlag = ${escapeString(it.hideFlag)}, rawMovementId = ${it.rawMovementId}," +
          " sightRange = ${it.sightRange}, trainerId = ${it.trainerId}," +
          " martShelf = ${it.martShelf})"
    }
  }

  fun frameScripts(scripts: List<ParsedFrameScript>): String {
    if (scripts.isEmpty()) return EMPTY_LIST
    return scripts.joinToString(LIST_SEP, LIST_OPEN, LIST_CLOSE) {
      "MapFrameScript(varKey = ${escapeString(it.varKey)}, value = ${it.value}," +
          " script = ${escapeString(it.script)})"
    }
  }

  fun coordScripts(scripts: List<ParsedCoordScript>): String {
    if (scripts.isEmpty()) return EMPTY_LIST
    return scripts.joinToString(LIST_SEP, LIST_OPEN, LIST_CLOSE) {
      "MapCoordScript(x = ${it.x}, y = ${it.y}, elevation = ${it.elevation}," +
          " varKey = ${escapeString(it.varKey)}, value = ${it.value}," +
          " script = ${escapeString(it.script)}," +
          " width = ${it.width}, height = ${it.height})"
    }
  }

  fun healLocation(location: ParsedHealLocation?): String {
    if (location == null) return "null"
    return "HealLocation(regionId = ${location.region}.toByte()," +
        " bankId = ${location.bank}.toByte(), mapId = ${location.map}.toByte()," +
        " x = ${location.x}.toShort(), y = ${location.y}.toShort())"
  }

  fun bgEvents(events: List<ParsedBgEvent>): String {
    if (events.isEmpty()) return EMPTY_LIST
    return events.joinToString(LIST_SEP, LIST_OPEN, LIST_CLOSE) {
      "BgEventDef(x = ${it.x}, y = ${it.y}," +
          " facingDir = ${escapeString(it.facingDir)}, script = ${escapeString(it.script)})"
    }
  }

  fun encounters(tables: List<ParsedEncounterTable>): String {
    if (tables.isEmpty()) return EMPTY_LIST
    return tables.joinToString(LIST_SEP, LIST_OPEN, LIST_CLOSE) { table ->
      val slots =
          table.slots.joinToString(", ", INLINE_OPEN, ")") {
            "WildEncounterSlot(speciesId = ${it.speciesId}, minLevel = ${it.minLevel}," +
                " maxLevel = ${it.maxLevel}, weight = ${it.weight})"
          }
      val overrides =
          if (table.overrides.isEmpty()) ""
          else
              table.overrides.joinToString(", ", ", overrides = listOf(", ")") {
                "WildEncounterOverride(variant = ${it.variant}," +
                    " slots = ${intList(it.slots)}, speciesIds = ${intList(it.speciesIds)})"
              }
      "WildEncounterTable(method = ${table.method}," +
          " encounterRate = ${table.encounterRate}, slots = $slots$overrides)"
    }
  }

  fun encounterForms(forms: ParsedEncounterForms?): String {
    if (forms == null) return "null"
    return "WildEncounterForms(shellosForm = ${forms.shellosForm}," +
        " gastrodonForm = ${forms.gastrodonForm}, unownTableId = ${forms.unownTableId}," +
        " unusedFormWords = ${intList(forms.unusedFormWords)})"
  }

  fun headbuttTrees(trees: List<ParsedHeadbuttTree>): String {
    if (trees.isEmpty()) return EMPTY_LIST
    return trees.joinToString(LIST_SEP, LIST_OPEN, LIST_CLOSE) {
      "HeadbuttTree(x = ${it.x}, y = ${it.y}, tree = ${it.tree}, secret = ${it.secret})"
    }
  }

  fun rubble(rubble: ParsedRubble?): String {
    if (rubble == null) return "null"
    val items = rubble.items.joinToString(", ", INLINE_OPEN, ")")
    return "RockSmashRubble(odds = ${rubble.odds}, items = $items)"
  }

  private fun intList(values: List<Int>): String =
      if (values.isEmpty()) EMPTY_LIST else values.joinToString(", ", INLINE_OPEN, ")")

  private fun escapeString(s: String): String =
      "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\""
}
