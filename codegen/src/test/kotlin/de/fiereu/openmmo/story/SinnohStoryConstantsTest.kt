package de.fiereu.openmmo.story

import de.fiereu.openmmo.story.generated.sinnoh.SinnohFlags
import de.fiereu.openmmo.story.generated.sinnoh.SinnohVars
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

/**
 * Sinnoh's flag and var ids come out of one enum dump whose values are a running count rather
 * than line numbers: an entry written `NAME = OTHER` takes that entry's value and the count
 * carries on from there.
 */
class SinnohStoryConstantsTest :
    FunSpec({
      // A name ending in its own hex id. The MAP_LOCAL blocks name their offset inside the block
      // instead, so they say nothing about the absolute value.
      val selfNaming = Regex("""^(?!.*_MAP_LOCAL_).*_0x([0-9A-Fa-f]{2,4})$""")

      /** Every `const val` on the generated object, by the key it holds. */
      fun keysOf(constants: Any): List<String> =
          constants::class
              .java
              .declaredFields
              .filter { it.type == String::class.java }
              .map {
                it.isAccessible = true
                it.get(constants) as String
              }

      fun selfNamed(keys: List<String>): List<Pair<String, Int>> =
          keys.mapNotNull { key ->
            selfNaming.find(key)?.let { key to it.groupValues[1].toInt(16) }
          }

      test("every entry that names its own value has that value") {
        val checked =
            selfNamed(keysOf(SinnohFlags)).map { (key, named) ->
              key to (SinnohFlags.numericId(key) == named)
            } +
                selfNamed(keysOf(SinnohVars)).map { (key, named) ->
                  key to (SinnohVars.numericId(key) == named)
                }

        // The decomp itself names two entries after 0x193, and the second one sits two slots
        // later. That is a naming duplicate in the source, not a drift in the count: the entry
        // above it, FLAG_UNUSED_0x0193, is 0x193 exactly.
        checked.filterNot { it.second }.map { it.first } shouldBe
            listOf(SinnohFlags.FLAG_DUMMY_0x0193)
        // A dump that stopped naming its entries this way would leave the check comparing
        // nothing, which is worth failing over rather than passing on silence.
        (checked.size > 1500) shouldBe true
      }

      /** The var space starts at 0x4000, which is why a flag id and a var id cannot collide. */
      test("vars are numbered from the DS var base and flags below it") {
        SinnohVars.numericId(SinnohVars.VAR_PLAYER_HOUSE_RIVAL_STATE) shouldBe 0x40A5
        SinnohFlags.numericId(SinnohFlags.FLAG_HIDE_TWINLEAF_TOWN_PLAYER_HOUSE_2F_RIVAL) shouldBe
            371
      }
    })
