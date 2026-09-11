package de.fiereu.openmmo.trainer

import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.trainer.generated.GeneratedHoennTrainers
import de.fiereu.openmmo.trainer.generated.GeneratedJohtoTrainers
import de.fiereu.openmmo.trainer.generated.GeneratedKantoTrainers
import de.fiereu.openmmo.trainer.generated.GeneratedSinnohTrainers
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

/** A monster on a trainer's team. Empty [moveIds] means the level up moveset is used. */
data class TrainerMon(
    val dexId: Int,
    val level: Int,
    val iv: Int,
    val heldItem: Int,
    val moveIds: List<Int>,
)

data class TrainerDef(
    val id: Int,
    val name: String,
    val trainerClass: Int,
    val doubleBattle: Boolean,
    /** What the class pays per level of its last monster. */
    val prizeRate: Int,
    val party: List<TrainerMon>,
    /**
     * The story flag that says this trainer has been beaten, or "" where the region keeps that in a
     * bitfield the script layer owns instead.
     */
    val defeatedFlag: String = "",
    /**
     * The story key of the badge beating this trainer earns, or "" for the many who hand out
     * nothing: a gym leader's, read out of the gym script that says `GiveBadge` (HeartGold's
     * leaders, whose fights this server plays), the Sinnoh leaders' badges come from the story
     * scripts instead and leave this empty.
     */
    val badge: String = "",
)

private data class TrainerKey(val region: Region, val id: Int)

/**
 * The trainers from the decomp, keyed by region and trainer id. Every region numbers its trainers
 * from zero, so the region is part of the key.
 */
@Singleton
class TrainerRegistry @Inject constructor() {

  private val trainers = ConcurrentHashMap<TrainerKey, TrainerDef>()

  init {
    GeneratedHoennTrainers.loadInto(this)
    GeneratedJohtoTrainers.loadInto(this)
    GeneratedKantoTrainers.loadInto(this)
    GeneratedSinnohTrainers.loadInto(this)
  }

  fun register(region: Region, def: TrainerDef) {
    val previous = trainers.put(TrainerKey(region, def.id), def)
    // Generated data, so a clash means the parser produced two trainers for one id and the second
    // would quietly replace the first.
    check(previous == null) {
      "Two $region trainers share id ${def.id}: ${previous?.name}, ${def.name}"
    }
  }

  fun get(region: Region, id: Int): TrainerDef? = trainers[TrainerKey(region, id)]

  fun size(): Int = trainers.size
}
