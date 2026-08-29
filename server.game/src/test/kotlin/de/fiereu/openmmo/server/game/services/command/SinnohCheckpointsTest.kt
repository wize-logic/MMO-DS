package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.TileBehavior
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.server.game.script.Badge
import de.fiereu.openmmo.story.generated.sinnoh.SinnohFlags
import de.fiereu.openmmo.story.generated.sinnoh.SinnohVars
import io.kotest.assertions.withClue
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import io.kotest.matchers.shouldNotBe

private val TALK_GATES =
    setOf(
        "gym-roark",
        "gym-gardenia",
        "gym-maylene",
        "gym-wake",
        "gym-fantina",
        "gym-byron",
        "gym-candice",
        "gym-volkner",
        "league-aaron",
        "league-bertha",
        "league-flint",
        "league-lucian",
        "league-cynthia",
    )

private val WALL_GATES = setOf("hm-surf", "hm-strength", "hm-waterfall", "hm-rock-climb")

class SinnohCheckpointsTest :
    FunSpec({
      test("every checkpoint stands on a walkable tile of a map we have") {
        val maps = MapManager()

        SINNOH_CHECKPOINTS.forEach { checkpoint ->
          val map =
              maps.getMap(
                  checkpoint.region.wireValue,
                  checkpoint.bankId.toByte(),
                  checkpoint.mapId.toByte(),
              )
          withClue(checkpoint.name) {
            map shouldNotBe null
            val tile = map!!.tileAt(checkpoint.x, checkpoint.y)
            tile shouldNotBe null
            tile!!.blocksMovement() shouldBe false
          }
        }
      }

      test("names are unique and lower case, and do not collide with Kanto") {
        val all = ALL_STORY_CHECKPOINTS
        all.map { it.name } shouldBe all.map { it.name }.distinct()
        SINNOH_CHECKPOINTS.all { it.name == it.name.lowercase() } shouldBe true
      }

      test("the rival checkpoint is a fresh bedroom, so the four-tile trigger still matches") {
        val rival = SINNOH_CHECKPOINTS.single { it.name == "twinleaf-rival" }
        rival.storyFlags.contains(
            SinnohFlags.FLAG_HIDE_TWINLEAF_TOWN_PLAYER_HOUSE_2F_RIVAL) shouldBe true
        rival.storyVars.containsKey(SinnohVars.VAR_PLAYER_HOUSE_RIVAL_STATE) shouldBe false
      }

      test("the starter checkpoint keeps the trigger var at 0 and hides nobody extra") {
        val starter = SINNOH_CHECKPOINTS.single { it.name == "route-201-starter" }
        starter.storyVars.containsKey(SinnohVars.VAR_FOLLOWER_RIVAL_STATE) shouldBe false
        starter.party.isEmpty() shouldBe true
      }

      test("every gym and league talk gate carries a party, and no badge of its own") {
        TALK_GATES.forEach { name ->
          val gate = SINNOH_CHECKPOINTS.single { it.name == name }
          withClue(name) {
            gate.party.isEmpty() shouldBe false
            // Talking with the badge already held is the after-text, not the fight.
            if (name.startsWith("gym-")) {
              Badge.entries.none { gate.storyFlags.contains(it.keyIn("sinnoh")) } shouldBe true
            }
          }
        }
      }

      test("each HM wall checkpoint carries the badge and the move that wall asks for") {
        val surf = SINNOH_CHECKPOINTS.single { it.name == "hm-surf" }
        surf.storyFlags.contains(Badge.FEN.keyIn("sinnoh")) shouldBe true
        surf.party.single().moveIds.contains(57) shouldBe true

        val strength = SINNOH_CHECKPOINTS.single { it.name == "hm-strength" }
        strength.storyFlags.contains(Badge.MINE.keyIn("sinnoh")) shouldBe true
        strength.party.single().moveIds.contains(70) shouldBe true

        val waterfall = SINNOH_CHECKPOINTS.single { it.name == "hm-waterfall" }
        waterfall.storyFlags.contains(Badge.BEACON.keyIn("sinnoh")) shouldBe true
        waterfall.party.single().moveIds.contains(127) shouldBe true

        val climb = SINNOH_CHECKPOINTS.single { it.name == "hm-rock-climb" }
        climb.storyFlags.contains(Badge.ICICLE.keyIn("sinnoh")) shouldBe true
        climb.party.single().moveIds.contains(431) shouldBe true
      }

      test("hm wall checkpoints face the tile that is the wall") {
        val maps = MapManager()
        val surf = SINNOH_CHECKPOINTS.single { it.name == "hm-surf" }
        faceBehavior(maps, surf) shouldBe TileBehavior.SURFABLE_WATER

        val strength = SINNOH_CHECKPOINTS.single { it.name == "hm-strength" }
        val cave = maps.getMap(3, 1, 3)!!
        cave.npcs.any {
          it.script == "10002" &&
              it.x == strength.x + strength.facing.dx &&
              it.y == strength.y + strength.facing.dy
        } shouldBe true

        val waterfall = firstStand(maps, TileBehavior.WATERFALL)
        val wf = SINNOH_CHECKPOINTS.single { it.name == "hm-waterfall" }
        withClue("hm-waterfall should stand at $waterfall") {
          Triple(wf.bankId, wf.mapId, Triple(wf.x, wf.y, wf.facing)) shouldBe waterfall
        }

        val climb = firstStand(maps, TileBehavior.ROCK_CLIMB_NORTH_SOUTH)
        val rc = SINNOH_CHECKPOINTS.single { it.name == "hm-rock-climb" }
        withClue("hm-rock-climb should stand at $climb") {
          Triple(rc.bankId, rc.mapId, Triple(rc.x, rc.y, rc.facing)) shouldBe climb
        }
      }

      test("route-201-grass stands on tall grass of Route 201") {
        val maps = MapManager()
        val grass = firstGrass(maps)
        val g = SINNOH_CHECKPOINTS.single { it.name == "route-201-grass" }
        withClue("route-201-grass should stand at $grass") {
          Triple(g.bankId, g.mapId, g.x to g.y) shouldBe grass
        }
      }

      test("every named wall is one of the four this server can still see") {
        SINNOH_CHECKPOINTS.map { it.name }.filter { it.startsWith("hm-") }.toSet() shouldBe
            WALL_GATES
      }
    })

private fun faceBehavior(maps: MapManager, checkpoint: StoryCheckpoint): TileBehavior? {
  val map =
      maps.getMap(
          checkpoint.region.wireValue,
          checkpoint.bankId.toByte(),
          checkpoint.mapId.toByte(),
      ) ?: return null
  return map.tileAt(checkpoint.x + checkpoint.facing.dx, checkpoint.y + checkpoint.facing.dy)
      ?.behavior
}

private data class Stand(
    val bank: Int,
    val map: Int,
    val x: Int,
    val y: Int,
    val facing: Direction
)

private fun firstStand(
    maps: MapManager,
    want: TileBehavior
): Triple<Int, Int, Triple<Int, Int, Direction>> {
  for (bank in 0 until 60) {
    for (id in 0 until 256) {
      val map = maps.getMap(3, bank.toByte(), id.toByte()) ?: continue
      if (map.warps.isEmpty() && map.npcs.isEmpty()) continue
      val header = (bank shl 8) or id
      val w = map.width
      val h = map.height
      for (y in 0 until h) {
        for (x in 0 until w) {
          if (map.terrain != null && map.terrain!!.headerAt(x, y) != header) continue
          if (map.tileAt(x, y)?.behavior != want) continue
          for (facing in listOf(Direction.UP, Direction.DOWN, Direction.LEFT, Direction.RIGHT)) {
            val sx = x - facing.dx
            val sy = y - facing.dy
            if (sx !in 0 until w || sy !in 0 until h) continue
            if (map.terrain != null && map.terrain!!.headerAt(sx, sy) != header) continue
            val stand = map.tileAt(sx, sy) ?: continue
            if (stand.blocksMovement()) continue
            if (stand.behavior == want) continue
            return Triple(bank, id, Triple(sx, sy, facing))
          }
        }
      }
    }
  }
  error("no standing tile facing $want")
}

private fun firstGrass(maps: MapManager): Triple<Int, Int, Pair<Int, Int>> {
  val map = checkNotNull(maps.getMap(3, 1, 86))
  val header = (1 shl 8) or 86
  for (y in 832 until 864) {
    for (x in 96 until 160) {
      if (map.terrain?.headerAt(x, y) != header) continue
      if (map.tileAt(x, y)?.behavior == TileBehavior.TALL_GRASS) return Triple(1, 86, x to y)
    }
  }
  error("no tall grass on Route 201")
}
