package de.fiereu.openmmo.maps

import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

/**
 * What a Sinnoh map runs when a player arrives on it lives in a second archive the header names,
 * `res/field/scripts/scripts_init_*.s`, and the generator used to emit "" and no frame table for
 * all 593 of them.
 */
class SinnohMapEntryScriptTest :
    FunSpec({
      val sinnoh = 3
      val maps = MapManager()

      test("the Twinleaf bedroom runs the entry script and frame table its archive names") {
        val bedroom = maps.getMap(sinnoh, 1, 159)!!

        bedroom.onTransitionScript shouldBe "5"
        bedroom.onFrameScripts shouldBe
            listOf(
                MapFrameScript(
                    varKey = "sinnoh/VAR_PLAYER_HOUSE_SPECIAL_PROGRAM_STATE",
                    value = 0,
                    script = "3",
                ))
      }

      /**
       * The server runs the first row whose var matches, so file order is the answer and not a
       * detail. These four ids are written 0x2334 upward in the decomp and come out decimal, the
       * way every other script id on a map does.
       */
      test("a frame table keeps its rows in file order and its ids in one base") {
        val pokecenter = maps.getMap(sinnoh, 0, 135)!!

        pokecenter.onFrameScripts.map { it.value to it.script } shouldBe
            listOf(1 to "9012", 2 to "9013", 4 to "9014", 3 to "9015")
      }

      /** A row can compare against a named constant, which is a value and not a script id. */
      test("the Distortion World's progress constants resolve to their enum positions") {
        maps.getMap(sinnoh, 2, 62)!!.onFrameScripts.single().value shouldBe 2
        maps.getMap(sinnoh, 2, 69)!!.onFrameScripts.single().value shouldBe 7
        maps.getMap(sinnoh, 0, 88)!!.onFrameScripts.single().value shouldBe 0
      }

      /** 113 headers name the empty archive, and an empty archive is not a missing one. */
      test("a map whose archive holds no entries carries no entry scripts") {
        val unusedGymRoom = maps.getMap(sinnoh, 0, 92)!!

        unusedGymRoom.onTransitionScript shouldBe ""
        unusedGymRoom.onFrameScripts shouldBe emptyList()
      }
    })
