package de.fiereu.openmmo.maps

import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.MovementType

data class NpcDef(
    val entityIdx: Int,
    val graphicsId: Int,
    val x: Int,
    val y: Int,
    val elevation: Int,
    val movementType: MovementType,
    val movementRangeX: Int,
    val movementRangeY: Int,
    val trainerType: Int,
    val facing: Direction,
    val script: String = "0x0",
    /** Story flag that hides this npc by default, or "" when it is always shown. */
    val hideFlag: String = "",
    /**
     * The source game's own movement id, for regions whose movement table is not the GBA one that
     * [MovementType.forRegion] maps. -1 leaves the region mapping in charge, which is what the two
     * GBA regions want; anything else is sent as it was read.
     */
    val rawMovementId: Int = -1,
    /**
     * How far this npc can see along the direction it faces, the decomp's own sight range: a DS
     * object's first data word, a GBA object's `trainer_sight_or_berry_tree_id`. 0 for anyone who
     * is not a trainer.
     */
    val sightRange: Int = 0,
    /**
     * The trainer this npc *is*, or 0. A DS object carries the trainer id in the place a script id
     * would go, the build turns `TRAINER_X` into `SCRIPT_ID_OFFSET_SINGLE_BATTLES + id - 1`, so for
     * those there is nothing to port and the fight is the table's.
     */
    val trainerId: Int = 0,
    /** Which shelf this person sells, or -1 for the overwhelming majority who sell nothing. */
    val martShelf: Int = -1,
)
