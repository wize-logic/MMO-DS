package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.common.DynamicWarp
import de.fiereu.openmmo.common.HealLocation
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.story.generated.hoenn.HoennFlags
import de.fiereu.openmmo.story.generated.hoenn.HoennVars
import de.fiereu.openmmo.story.generated.kanto.KantoFlags
import de.fiereu.openmmo.story.generated.sinnoh.SinnohFlags
import de.fiereu.openmmo.story.generated.sinnoh.SinnohVars

/**
 * Where a fresh character starts and the story state its source game would already have set. Every
 * region needs its own entry, so a new region is one function here rather than another branch in
 * [CharacterStore].
 */
internal data class NewGameStart(
    val bankId: Byte,
    val mapId: Byte,
    val x: Short,
    val y: Short,
    val dynamicWarp: DynamicWarp? = null,
    /**
     * The respawn the opening sets before the player can reach a Pokémon Center. Both decomps set
     * it from a script that branches (on gender in Emerald, on a scene var in FireRed), so it is
     * spelled out here rather than carried by the map the way a Center's is.
     */
    val healLocation: HealLocation,
    val storyFlags: Set<String> = emptySet(),
    val storyVars: Map<String, Int> = emptyMap(),
    /**
     * the official client's starting purse. The the official client CharacterInfo captures carry
     * this exact value.
     */
    val money: Int = 30000,
    /**
     * Low eight bits of CharacterInfo.permissions as the list capture writes them. This is what the
     * client is told about the character and nothing more; what an account may do lives on the
     * account, in AccountRole.
     */
    val permissions: Int = 8,
)

internal object NewGameStarts {

  fun forRegion(region: Region, female: Boolean): NewGameStart =
      when (region) {
        Region.HOENN -> hoenn(female)
        Region.KANTO -> kanto()
        Region.SINNOH -> sinnoh()
        // Nobody is made in Johto. It exists as a region so the trainers of the ported cartridge
        // have one key to live under; a ported map itself travels as Sinnoh, because it is a
        // Platinum header. `Region.JOHTO.creatable` says the same thing to the character screen.
        Region.JOHTO -> error("no character starts in ${region.displayName}")
      }

  /**
   * Platinum keeps both of these in a table of its own rather than in an opening script:
   * `location.c`'s sPlayerStartLocation is the player's bedroom in Twinleaf Town, and
   * sPlayerFirstRespawnLocation is the tile outside the house it sends them back to.
   */
  private fun sinnoh(): NewGameStart =
      NewGameStart(
          bankId = 1,
          mapId = (415 and 0xFF).toByte(),
          x = 4,
          y = 6,
          healLocation =
              HealLocation(
                  regionId = Region.SINNOH.wireValue,
                  bankId = 1,
                  mapId = (411 and 0xFF).toByte(),
                  x = 116,
                  y = 886,
              ),
          storyFlags = SinnohFlags.initiallySet,
          storyVars =
              mapOf(
                  SinnohVars.VAR_TWINLEAF_TOWN_GUITARIST_TRIGGER_STATE to 1,
                  SinnohVars.VAR_OREBURGH_GATE_1F_HIKER_STATE to 1,
                  SinnohVars.VAR_CONSECUTIVE_BONUS_ROUND_WINS to 9,
              ),
      )

  /** Emerald opens in the moving truck, whose exit goes through the player's dynamic warp. */
  private fun hoenn(female: Boolean): NewGameStart =
      NewGameStart(
          bankId = 75,
          mapId = 40,
          x = 2,
          y = 2,
          dynamicWarp =
              DynamicWarp(
                  Region.HOENN.wireValue,
                  50,
                  9,
                  if (female) 12 else 3,
                  10,
                  Direction.RIGHT,
              ),
          // InsideOfTruck: setrespawn HEAL_LOCATION_LITTLEROOT_TOWN_{BRENDANS,mays}_HOUSE_2F.
          healLocation =
              HealLocation(
                  regionId = Region.HOENN.wireValue,
                  bankId = 51,
                  mapId = if (female) 1 else 3,
                  x = 4,
                  y = 2,
              ),
          storyFlags =
              HoennFlags.initiallySet +
                  (if (female) HoennFlags.femaleIntro else HoennFlags.maleIntro) +
                  HoennFlags.FLAG_HIDE_MAP_NAME_POPUP,
          storyVars =
              mapOf(
                  HoennVars.VAR_LITTLEROOT_INTRO_STATE to if (female) 2 else 1,
                  (if (female) HoennVars.VAR_LITTLEROOT_HOUSES_STATE_MAY
                  else HoennVars.VAR_LITTLEROOT_HOUSES_STATE_BRENDAN) to 1,
              ),
      )

  /**
   * FireRed opens in the player's bedroom above their house in Pallet Town, at the coordinates its
   * new game code warps to. Nothing about the opening depends on the player's gender.
   */
  private fun kanto(): NewGameStart =
      NewGameStart(
          bankId = 4,
          mapId = 1,
          x = 6,
          y = 6,
          // PalletTown_PlayersHouse_2F_EventScript_SetRespawn: setrespawn
          // HEAL_LOCATION_PALLET_TOWN.
          healLocation =
              HealLocation(
                  regionId = Region.KANTO.wireValue,
                  bankId = 3,
                  mapId = 0,
                  x = 6,
                  y = 8,
              ),
          storyFlags = KantoFlags.initiallySet,
      )
}
