package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.maps.EncounterConditions
import de.fiereu.openmmo.maps.EncounterVariant
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.generated.sinnoh.DailyEncounters
import de.fiereu.openmmo.server.game.script.Pokedex
import de.fiereu.openmmo.server.game.storage.StoredCharacter
import io.github.oshai.kotlinlogging.KotlinLogging
import java.time.LocalDate
import javax.inject.Inject
import javax.inject.Singleton
import kotlin.random.Random

private val log = KotlinLogging.logger {}

/** The state a wild table's conditional slots need, none of which is the table's own. */
@Singleton
class EncounterVariantService @Inject constructor(private val worldClock: WorldClock) {

  /** What holds for [stored] on [map] right now. */
  fun conditionsFor(
      stored: StoredCharacter?,
      map: MapDef,
      radarPatch: Boolean = false,
  ): EncounterConditions {
    val today = worldClock.now().toLocalDate()
    val nationalDex = stored != null && Pokedex.isNationalDexEnabled(stored)
    return EncounterConditions(
        timeOfDay = worldClock.timeOfDay(),
        swarming = map.name.isNotEmpty() && map.name == swarmMapOn(today),
        radarPatch = radarPatch,
        dualSlot = if (nationalDex) gbaSlot else null,
        dailySpecies = dailySpeciesOn(map, today, nationalDex),
    )
  }

  /**
   * What the day puts over slots 6 and 7 of [map]'s grass table on [date], and empty for a map that
   * has no such pair.
   */
  internal fun dailySpeciesOn(map: MapDef, date: LocalDate, nationalDex: Boolean): List<Int> {
    if (map.regionId.toInt() != DailyEncounters.REGION) return emptyList()
    val header = ((map.bankId.toInt() and 0xFF) shl 8) or (map.mapId.toInt() and 0xFF)
    val area = DailyEncounters.MARSH_AREAS.indexOf(header)
    return when {
      area >= 0 -> marshPairOn(date, area, nationalDex)
      header == DailyEncounters.TROPHY_GARDEN && nationalDex -> trophyGardenPairOn(date)
      else -> emptyList()
    }
  }

  /**
   * What the Great Marsh's [area] (0 through 5, the cartridge's own numbering) holds on [date]: One
   * species, written into both slots.
   */
  fun marshPairOn(date: LocalDate, area: Int, nationalDex: Boolean): List<Int> {
    val table = if (nationalDex) DailyEncounters.MARSH_NATIONAL_DEX else DailyEncounters.MARSH_LOCAL
    val index =
        (marshRollOn(date) ushr (DailyEncounters.AREA_BITS * area)) and DailyEncounters.AREA_MASK
    val species = table.getOrNull(index) ?: return emptyList()
    return listOf(species, species)
  }

  /** The day's `marshDaily`: one roll, out of which each of the six areas cuts its own index. */
  fun marshRollOn(date: LocalDate): Int = Random(MARSH_STREAM + date.toEpochDay()).nextInt()

  /** The two Mr. Backlot's garden holds on [date], never the same one twice. */
  fun trophyGardenPairOn(date: LocalDate): List<Int> {
    val mons = DailyEncounters.TROPHY_GARDEN_MONS
    if (mons.size < 2) return emptyList()
    val roll = Random(TROPHY_STREAM + date.toEpochDay())
    val first = roll.nextInt(mons.size)
    var second = roll.nextInt(mons.size)
    while (second == first) second = roll.nextInt(mons.size)
    return listOf(mons[first], mons[second])
  }

  /** The one map the swarm is on for [date]. */
  fun swarmMapOn(date: LocalDate): String =
      SWARM_MAPS[Random(date.toEpochDay()).nextInt(SWARM_MAPS.size)]

  /** Which cartridge this world's second slot holds, or null for an empty one. */
  private val gbaSlot: EncounterVariant? by lazy { readSlot(System.getenv(SLOT_ENV)) }

  internal fun readSlot(name: String?): EncounterVariant? {
    if (name.isNullOrBlank()) return null
    val wanted = "DUAL_SLOT_${name.trim().uppercase()}"
    val variant = EncounterVariant.entries.firstOrNull { it.isDualSlot && it.name == wanted }
    if (variant == null) {
      log.error {
        "$SLOT_ENV is \"$name\", which is not one of " +
            EncounterVariant.entries
                .filter { it.isDualSlot }
                .joinToString(", ") { it.name.removePrefix("DUAL_SLOT_").lowercase() } +
            "; the second slot is empty"
      }
      return null
    }
    log.info { "The second cartridge slot holds ${variant.name.removePrefix("DUAL_SLOT_")}" }
    return variant
  }

  companion object {
    /** Names a GBA cartridge: ruby, sapphire, emerald, firered or leafgreen. */
    const val SLOT_ENV = "OPENMMO_GBA_SLOT"

    /** Streams of their own for the two dailies that are not the swarm's. */
    private const val MARSH_STREAM = 0x9E3779B9L
    private const val TROPHY_STREAM = 0x7F4A7C15L

    /** `sSwarmMapIdTable`, in its own order, by the names this server's maps carry. */
    val SWARM_MAPS =
        listOf(
            "route_201",
            "route_202",
            "route_203",
            "route_206",
            "route_207",
            "route_208",
            "route_209",
            "route_214",
            "route_215",
            "route_217",
            "route_218",
            "route_221",
            "route_222",
            "route_224",
            "route_225",
            "route_226",
            "route_227",
            "route_228",
            "route_229",
            "route_230",
            "valley_windworks_outside",
            "eterna_forest",
        )
  }
}
