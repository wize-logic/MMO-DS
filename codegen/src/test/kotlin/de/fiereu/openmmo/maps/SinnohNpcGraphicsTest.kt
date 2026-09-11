package de.fiereu.openmmo.maps

import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldNotContain
import io.kotest.matchers.shouldBe

/**
 * A Sinnoh object event names its sprite as an `OBJ_EVENT_GFX_*` constant, and the client indexes
 * its own table by the number behind it.
 */
class SinnohNpcGraphicsTest :
    FunSpec({
      val sinnoh = 3
      val maps = MapManager()
      // The generated set has no index, so a region-wide claim walks every bank and map number.
      // A ported map travels as Sinnoh too, but its people are drawn by the client out of another
      // cartridge's art, so they carry no sprite of this game's and are not part of this claim.
      val drawn =
          (0..255)
              .flatMap { bank -> (0..255).mapNotNull { map -> maps.getMap(sinnoh, bank, map) } }
              .filterNot { it.ported }
              .flatMap { map -> map.npcs.map { it.graphicsId } }
              .toSet()

      test("the rival in the bedroom is Barry and not the player sprite") {
        maps.getMap(sinnoh, 1, 159)!!.npcs.single().graphicsId shouldBe 148 // OBJ_EVENT_GFX_BARRY
      }

      /**
       * The generator resolves every name it reads or fails, so the sweep is the claim that nothing
       * fell back. A player graphic belongs to the avatar the client draws itself, so an object
       * event carrying one is a name that did not resolve.
       */
      test("Sinnoh draws its people with 172 sprites and none of them a player") {
        drawn.size shouldBe 172
        // PLAYER_M, PLAYER_F, DP_PLAYER_M, DP_PLAYER_F.
        listOf(0, 97, 252, 253).forEach { drawn shouldNotContain it }
      }
    })
