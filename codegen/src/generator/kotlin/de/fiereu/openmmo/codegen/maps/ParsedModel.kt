package de.fiereu.openmmo.codegen.maps

data class ParsedMap(
    val regionName: String,
    val sourceName: String,
    val groupName: String,
    val mapId: String,
    val region: Int,
    val bank: Int,
    val index: Int,
    val width: Int,
    val height: Int,
    val paletteIdx1: Int,
    val paletteIdx2: Int,
    val musicId: Int,
    val mapsecId: Int,
    val borderTiles: List<Int>,
    val blockData: String,
    val behaviorData: String,
    val encounters: List<ParsedEncounterTable>,
    // Form selectors from the map's encounter archive, or null where the region has none.
    val encounterForms: ParsedEncounterForms?,
    val lighting: String,
    val weather: String,
    val mapType: String,
    val encounterType: String,
    val connections: List<ParsedConnection>,
    val warps: List<ParsedWarp>,
    val visibleNpcs: List<ParsedNpc>,
    val bgEvents: List<ParsedBgEvent>,
    // Decomp label of the map's ON_TRANSITION script, or "" when the map has none.
    val onTransitionScript: String,
    // Where entering this map sends the player back to after a white out (the decomp setrespawn),
    // or null when the map sets no respawn.
    val healLocation: ParsedHealLocation?,
    // The map's ON_FRAME table: run the script once its var equals the value, on map entry.
    val onFrameScripts: List<ParsedFrameScript>,
    // Tile triggers from map.json coord_events.
    val coordScripts: List<ParsedCoordScript>,
    /**
     * Fully qualified name of the generated [de.fiereu.openmmo.maps.TerrainPlane] this map is
     * placed on, for the DS regions whose geometry belongs to a shared matrix. Empty for the
     * GBA regions, which carry their own block data.
     */
    val terrainRef: String = "",
)

/** One land data file's terrain attribute plane, as it is stored: little endian u16 a tile. */
data class ParsedTerrainChunk(
    val name: String,
    val encoded: String,
)

/** A grid of [ParsedTerrainChunk] names, and the altitudes the matrix stores beside them. */
data class ParsedTerrainMatrix(
    val name: String,
    val cols: Int,
    val rows: Int,
    val chunkSide: Int,
    /** Row major, null where the matrix leaves a cell empty. */
    val chunks: List<String?>,
    val altitudes: List<Int>,
    /** The map header id that owns each cell, row major, the matrix's own `headers` grid. */
    val headers: List<Int>,
)

/** Everything one region's parse produced: its maps, and the terrain they are placed on. */
data class ParsedRegion(
    val maps: List<ParsedMap>,
    val terrainChunks: List<ParsedTerrainChunk> = emptyList(),
    val terrainMatrices: List<ParsedTerrainMatrix> = emptyList(),
    /** Behaviour per source attribute byte, indexed by the byte. Empty for the GBA regions. */
    val tileBehaviors: List<String> = emptyList(),
    val terrainPackage: String = "",
)

data class ParsedHealLocation(
    val healLocationId: String,
    val region: Int,
    val bank: Int,
    val map: Int,
    val x: Int,
    val y: Int,
)

data class ParsedFrameScript(
    val varKey: String,
    val value: Int,
    val script: String,
)

data class ParsedCoordScript(
    val x: Int,
    val y: Int,
    val elevation: Int,
    val varKey: String,
    val value: Int,
    val script: String,
    // The trigger's rectangle. The GBA maps have no such field and stay 1x1.
    val width: Int = 1,
    val height: Int = 1,
)

data class ParsedConnection(
    val direction: String,
    val offset: Int,
    val targetBank: Int,
    val targetMap: Int,
)

data class ParsedWarp(
    val x: Int,
    val y: Int,
    val elevation: Int,
    val targetRegion: Int,
    val targetBank: Int,
    val targetMap: Int,
    val targetX: Int,
    val targetY: Int,
    val targetElevation: Int,
    val dynamic: Boolean = false,
)

data class ParsedNpc(
    val entityIdx: Int,
    val graphicsId: Int,
    val x: Int,
    val y: Int,
    val elevation: Int,
    val movementType: String,
    val movementRangeX: Int,
    val movementRangeY: Int,
    val trainerType: Int,
    val facing: String,
    val script: String,
    // Namespaced story flag that hides this npc by default, or "" when it is always shown.
    val hideFlag: String,
    // The source game's own movement id, for a region whose movement table is not the GBA one.
    val rawMovementId: Int = -1,
    // How far a trainer sees along its facing, and which trainer it is where the object says so.
    val sightRange: Int = 0,
    val trainerId: Int = 0,
)

data class ParsedBgEvent(
    val x: Int,
    val y: Int,
    val facingDir: String,
    val script: String,
)

data class ParsedEncounterTable(
    val method: String,
    val encounterRate: Int,
    val slots: List<ParsedEncounterSlot>,
    val overrides: List<ParsedEncounterOverride> = emptyList(),
)

// A conditional swap into a table's slots: the species replace the ones at those slot indices,
// position for position, while the variant holds.
data class ParsedEncounterOverride(
    val variant: String,
    val slots: List<Int>,
    val speciesIds: List<Int>,
)

// The form selectors a map's encounter archive carries, rendered as a WildEncounterForms.
data class ParsedEncounterForms(
    val shellosForm: Int,
    val gastrodonForm: Int,
    val unownTableId: Int,
    val unusedFormWords: List<Int>,
)

data class ParsedEncounterSlot(
    val speciesId: Int,
    val minLevel: Int,
    val maxLevel: Int,
    val weight: Int,
)
