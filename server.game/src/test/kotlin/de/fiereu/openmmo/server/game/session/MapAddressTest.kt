package de.fiereu.openmmo.server.game.session

import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

/**
 * Sinnoh's Pokemon Centers are map 159 and the ported Johto maps run past 128, so a map id that
 * reached the server as a signed byte is negative for a large part of the world.
 */
class MapAddressTest :
    FunSpec({
      test("a map id that arrived as a signed byte is stored unsigned") {
        val state = PlayerState(userId = 1)

        state.setMapAddress(3, 1, (159.toByte()).toInt())

        state.regionId shouldBe 3
        state.bankId shouldBe 1
        state.mapId shouldBe 159
      }

      test("logging in and warping onto one map agree on its address") {
        val joined = PlayerState(userId = 1)
        val warped = PlayerState(userId = 2)

        joined.setMapAddress(3, 1, (159.toByte()).toInt())
        warped.setMapAddress(3, 1, 159)

        joined.mapId shouldBe warped.mapId
      }

      test("a high map id still keys its own bank and region") {
        // Unmasked, the sign bits swallow the bank and the region, so every map over 127
        // anywhere in the world shared one cache key.
        mapCacheKey(3, 1, (159.toByte()).toInt()) shouldBe mapCacheKey(3, 1, 159)
        mapCacheKey(3, 1, 159) shouldBe ((3 shl 16) or (1 shl 8) or 159)
        mapCacheKey(3, 2, 159) shouldBe ((3 shl 16) or (2 shl 8) or 159)
      }
    })
