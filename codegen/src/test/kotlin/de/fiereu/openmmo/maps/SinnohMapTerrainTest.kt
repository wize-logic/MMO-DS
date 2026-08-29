package de.fiereu.openmmo.maps

import de.fiereu.openmmo.common.enums.TileBehavior
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import io.kotest.matchers.shouldNotBe

/**
 * Sinnoh comes out of a DS decomp, so nothing the Hoenn and Kanto generators assume holds: the
 * geometry belongs to a *matrix* several map headers share rather than to a map, and the events on
 * it are placed in that matrix's own coordinates.
 */
class SinnohMapTerrainTest :
    FunSpec({
      val sinnoh = 3
      val twinleafTown = Triple(sinnoh, 1, 155)
      val route201 = Triple(sinnoh, 1, 86)

      test("Sinnoh's overworld headers share one matrix rather than owning a plane each") {
        val maps = MapManager()
        val town = maps.getMap(twinleafTown.first, twinleafTown.second, twinleafTown.third)
        val route = maps.getMap(route201.first, route201.second, route201.third)

        town.shouldNotBeNull()
        route.shouldNotBeNull()
        // Same instance, not merely equal: 84 headers point at map_matrix_000 and decoding it once
        // is the whole reason the plane hangs off the matrix.
        town.terrain shouldNotBe null
        (town.terrain === route.terrain) shouldBe true
        town.terrain!!.width shouldBe 960
        town.terrain!!.height shouldBe 960
      }

      test("a DS map's tiles are read at the matrix's own coordinates") {
        val maps = MapManager()
        val town = maps.getMap(twinleafTown.first, twinleafTown.second, twinleafTown.third)!!

        // location.c's sPlayerFirstRespawnLocation puts the player on 116,886 outside their house
        // and the game walks them off it, so that tile has to exist and be passable. It is also far
        // outside anything a per-map grid could address, which is the point of the coordinates.
        val spawn = town.tileAt(116, 886)
        spawn.shouldNotBeNull()
        spawn.collision.toInt() shouldBe 0

        town.tileAt(-1, 0) shouldBe null
        town.tileAt(960, 886) shouldBe null
        town.tileAt(116, 960) shouldBe null
      }

      test("the terrain plane decodes both halves of an attribute word") {
        val maps = MapManager()
        val plane = maps.getMap(route201.first, route201.second, route201.third)!!.terrain!!

        // Bit 15 is collision and the low byte is the behaviour. A plane that decoded only one of
        // them, or read the word big-endian, would come back all-zero on one of these.
        var blocked = 0
        var walkable = 0
        var grass = 0
        for (y in 0 until plane.height step 4) {
          for (x in 0 until plane.width step 4) {
            val tile = plane.tileAt(x, y) ?: continue
            if (tile.collision.toInt() != 0) blocked++ else walkable++
            if (tile.behavior == TileBehavior.TALL_GRASS) grass++
          }
        }
        (blocked > 0) shouldBe true
        (walkable > 0) shouldBe true
        // Route 201 is the first route and its land encounters need grass to stand on.
        (grass > 0) shouldBe true
      }

      test("the Twinleaf bedroom spawn can step in all four directions") {
        val maps = MapManager()
        // Header 415 is bank 1 map 159. location.c seats the player at (4, 6);
        // the four neighbouring tiles are coord-event squares, so they exist
        // and are passable or a new character cannot leave the bed.
        val bedroom = maps.getMap(3, 1, 159)
        bedroom.shouldNotBeNull()
        bedroom.width shouldBe 32
        bedroom.height shouldBe 32

        val spawn = bedroom.tileAt(4, 6)
        spawn.shouldNotBeNull()
        spawn.blocksMovement() shouldBe false

        val neighbors = listOf(4 to 5, 4 to 7, 3 to 6, 5 to 6)
        for ((x, y) in neighbors) {
          val tile = bedroom.tileAt(x, y)
          tile.shouldNotBeNull()
          tile.blocksMovement() shouldBe false
        }
      }
    })
