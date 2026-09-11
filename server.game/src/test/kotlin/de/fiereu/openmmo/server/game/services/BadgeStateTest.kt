package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.net.game.packets.LocalPlayerStatePacket
import de.fiereu.openmmo.server.game.script.Badge
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

/** Where a badge lives, decided once. */
@OptIn(ExperimentalCoroutinesApi::class)
class BadgeStateTest :
    FunSpec({
      test("a badge's story key is its own decomp name, namespaced by region") {
        Badge.COAL.keyIn("sinnoh") shouldBe "sinnoh/BADGE_ID_COAL"
        Badge.BEACON.keyIn("sinnoh") shouldBe "sinnoh/BADGE_ID_BEACON"
        // The ordinal is the wire id: Sinnoh's eight are the decomp's BADGE_ID_* values, and
        // Johto's and Kanto's follow in HeartGold's own order, each region keyed by its home.
        Badge.entries.map { Badge.wireId(it).toInt() } shouldBe (0..23).toList()
        Badge.entries.take(8).all { it.home == "sinnoh" } shouldBe true
        Badge.ZEPHYR.key shouldBe "johto/BADGE_ID_ZEPHYR"
        Badge.BOULDER.key shouldBe "kanto/BADGE_ID_BOULDER"
      }

      test("the badges a character has earned reach the client in its player state") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val character = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH)
          val charId = character.info.id
          val session = FakeSession(characterId = charId, regionId = 3, bankId = 1, mapId = 159)
          val world = WorldStateService(WorldClock())

          world.send(session, checkNotNull(store.getCharacter(charId)))
          session.sent.filterIsInstance<LocalPlayerStatePacket>().single().badges shouldBe
              emptyList()

          val story = StoryService(store)
          story.setFlag(charId, Badge.COAL.keyIn("sinnoh"))
          story.setFlag(charId, Badge.FEN.keyIn("sinnoh"))

          val after = FakeSession(characterId = charId, regionId = 3, bankId = 1, mapId = 159)
          world.send(after, checkNotNull(store.getCharacter(charId)))
          after.sent.filterIsInstance<LocalPlayerStatePacket>().single().badges shouldBe
              listOf(0.toShort(), 3.toShort())
        }
      }

      /**
       * And a badge earned in one region is not a badge in another: the key is namespaced like
       * every other story key, so Hoenn's eight and Sinnoh's eight cannot be each other.
       */
      test("badge keys do not collide across regions") {
        Badge.COAL.keyIn("hoenn") shouldBe "hoenn/BADGE_ID_COAL"
        (Badge.COAL.keyIn("hoenn") == Badge.COAL.keyIn("sinnoh")) shouldBe false
      }
    })
