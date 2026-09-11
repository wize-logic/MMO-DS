package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.TileBehavior
import de.fiereu.openmmo.maps.MapDef
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import java.util.Base64

/** How far one step goes. */
class StepLandingTest :
    FunSpec({
      test("each directional ledge lands two tiles away") {
        val cases =
            listOf(
                Direction.DOWN to TileBehavior.JUMP_SOUTH,
                Direction.UP to TileBehavior.JUMP_NORTH,
                Direction.LEFT to TileBehavior.JUMP_WEST,
                Direction.RIGHT to TileBehavior.JUMP_EAST,
            )

        cases.forEach { (direction, behavior) ->
          val map = testMap(behavior)
          val fromX = 2 - direction.dx
          val fromY = 2 - direction.dy

          MapEventLayout.stepLanding(map, fromX, fromY, direction) shouldBe
              (2 + direction.dx to 2 + direction.dy)
          // Walked into from the far side the ledge is an ordinary tile, so the step is one.
          val backX = 2 + direction.dx
          val backY = 2 + direction.dy
          MapEventLayout.stepLanding(map, backX, backY, direction.opposite()) shouldBe (2 to 2)
        }
      }

      test("each directional jump-twice lands three tiles away") {
        val cases =
            listOf(
                Direction.DOWN to TileBehavior.JUMP_SOUTH_TWICE,
                Direction.UP to TileBehavior.JUMP_NORTH_TWICE,
                Direction.LEFT to TileBehavior.JUMP_WEST_TWICE,
                Direction.RIGHT to TileBehavior.JUMP_EAST_TWICE,
            )

        cases.forEach { (direction, behavior) ->
          val map = testMap(behavior)
          val fromX = 2 - direction.dx
          val fromY = 2 - direction.dy

          MapEventLayout.stepLanding(map, fromX, fromY, direction) shouldBe
              (fromX + direction.dx * 3 to fromY + direction.dy * 3)
        }
      }

      test("the terrain settles the length, whatever the client claims") {
        val ledge = testMap(TileBehavior.JUMP_SOUTH)

        // A plain tile is one tile however long the step says it was.
        MapEventLayout.stepLanding(ledge, 0, 0, Direction.RIGHT, claimedTiles = 3) shouldBe (1 to 0)
        // A ledge is two even from a client that claims one.
        MapEventLayout.stepLanding(ledge, 2, 1, Direction.DOWN, claimedTiles = 1) shouldBe (2 to 3)
      }

      test("a bike ramp is the one tile the client's own answer is read on") {
        val map = testMap(TileBehavior.BIKE_RAMP_EAST)

        // The step onto the ramp is one tile, from the tile before it.
        MapEventLayout.stepLanding(map, 1, 2, Direction.RIGHT, claimedTiles = 3) shouldBe (2 to 2)
        // Off the ramp, the top cycling gear throws the rider three tiles and the lower one.
        MapEventLayout.stepLanding(map, 2, 2, Direction.RIGHT, claimedTiles = 3) shouldBe (5 to 2)
        MapEventLayout.stepLanding(map, 2, 2, Direction.RIGHT, claimedTiles = 1) shouldBe (3 to 2)
        // And only in the direction the ramp is ridden.
        MapEventLayout.stepLanding(map, 2, 2, Direction.UP, claimedTiles = 3) shouldBe (2 to 1)
      }

      test("the Distortion World answers with the client's own count") {
        val dw = distortionMap()

        MapEventLayout.surfaceIsClientOwned(dw) shouldBe true
        MapEventLayout.stepLanding(dw, 2, 2, Direction.RIGHT, claimedTiles = 1) shouldBe (3 to 2)
        MapEventLayout.stepLanding(dw, 2, 2, Direction.RIGHT, claimedTiles = 3) shouldBe (5 to 2)
        // Bank 2 map 66 is not one of the ten, and neither is anything outside Sinnoh.
        MapEventLayout.surfaceIsClientOwned(distortionMap(mapId = 66)) shouldBe false
        MapEventLayout.surfaceIsClientOwned(distortionMap(regionId = 1)) shouldBe false
      }

      test("a ramp tile is entered against its own collision, a plain wall is not") {
        TileBehavior.BIKE_RAMP_EAST.rampRidesToward shouldBe Direction.RIGHT
        TileBehavior.BIKE_RAMP_WEST.rampRidesToward shouldBe Direction.LEFT
        TileBehavior.NORMAL.rampRidesToward shouldBe null
        TileBehavior.JUMP_EAST_TWICE.rampRidesToward shouldBe null
      }
    })

private fun distortionMap(regionId: Byte = 3, mapId: Byte = 62): MapDef {
  val width = 8
  val height = 8
  return MapDef(
      regionId = regionId,
      bankId = 2,
      mapId = mapId,
      width = width,
      height = height,
      blockData = Base64.getEncoder().encodeToString(ByteArray(width * height * 2)),
      behaviorData = Base64.getEncoder().encodeToString(ByteArray(width * height)),
  )
}

private fun testMap(behavior: TileBehavior): MapDef {
  val width = 8
  val height = 8
  val blocks = ByteArray(width * height * 2)
  val behaviors = ByteArray(width * height)
  behaviors[2 * width + 2] = behavior.ordinal.toByte()
  return MapDef(
      regionId = 1,
      bankId = 50,
      mapId = 0,
      width = width,
      height = height,
      blockData = Base64.getEncoder().encodeToString(blocks),
      behaviorData = Base64.getEncoder().encodeToString(behaviors),
  )
}
