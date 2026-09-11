package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.items.generated.Items
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.maps.NpcDef
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.net.game.packets.UndergroundTalkPacket
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.WildMonFactory
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import io.github.oshai.kotlinlogging.KotlinLogging
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

private val log = KotlinLogging.logger {}

/** An Apricorn tree grows one fruit a day, for a player with the box to put it in. */
@OptIn(ExperimentalCoroutinesApi::class)
class ApricornTest :
    FunSpec({
      /** A ported map with a tree on it, and the tree: the generator marks one `apricorn:i:k`. */
      fun orchard(maps: MapManager): Pair<MapDef, NpcDef>? {
        for (map in maps.all()) {
          val tree = map.npcs.firstOrNull { it.script.startsWith("apricorn:") } ?: continue
          return map to tree
        }
        return null
      }

      class Fixture(scope: CoroutineScope) {
        val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
        val maps = MapManager()
        val items = ItemRegistry()
        val species = SpeciesRegistry()
        val moves = MoveRegistry()
        val service =
            ApricornService(
                NpcService(maps, store),
                store,
                StoryService(store),
                StoryPlayerService(
                    store,
                    WildMonFactory(species, moves, LearnsetRegistry(), EntityIdService()),
                    species,
                    moves,
                    items),
                WorldClock(),
                items)

        /** A player standing south of the tree, facing it. */
        suspend fun picker(
            withBox: Boolean,
            map: MapDef,
            tree: NpcDef,
            name: String,
        ): Pair<FakeSession, Long> {
          val created = store.createCharacter(1, name, CharacterGender.MALE, Region.SINNOH)
          val id = created.info.id
          if (withBox) store.addItem(id, items.idOf(Items.APRICORN_BOX), 1)
          val session = FakeSession(id)
          val state = session.attributes[PLAYER_STATE].shouldNotBeNull()
          state.regionId = map.regionId.toInt()
          state.bankId = map.bankId.toInt()
          state.mapId = map.mapId.toInt()
          state.x = tree.x.toShort()
          state.y = (tree.y + 1).toShort()
          state.facingDirection = Direction.UP
          return session to id
        }

        suspend fun pick(session: FakeSession, index: Int) =
            service.onPick(
                PacketEvent(
                    UndergroundTalkPacket(
                        UndergroundTalkPacket.KIND_APRICORN_PICK, 0, byteArrayOf(index.toByte())),
                    session))

        fun verdicts(session: FakeSession): List<Int> =
            session.sent
                .filterIsInstance<UndergroundTalkPacket>()
                .filter { it.kind == UndergroundTalkPacket.KIND_APRICORN_VERDICT }
                .map { it.payload[0].toInt() }

        fun fruit(id: Long, kind: Int): Int {
          val item =
              listOf(
                  Items.RED_APRICORN,
                  Items.YLW_APRICORN,
                  Items.BLU_APRICORN,
                  Items.GRN_APRICORN,
                  Items.PNK_APRICORN,
                  Items.WHT_APRICORN,
                  Items.BLK_APRICORN)[kind - 1]
          return store.getCharacter(id).shouldNotBeNull().items[items.idOf(item)] ?: 0
        }
      }

      test("the first pick of the day is the tree's own fruit, the second finds nothing") {
        runTest {
          val fx = Fixture(this)
          val (map, tree) = orchard(fx.maps).shouldNotBeNull()
          val (index, kind) = tree.script.removePrefix("apricorn:").split(":").map { it.toInt() }
          log.info { "picking tree $index (kind $kind) on ${map.name} at (${tree.x}, ${tree.y})" }
          val (session, id) = fx.picker(withBox = true, map, tree, name = "Kurt")
          fx.pick(session, index)
          fx.verdicts(session) shouldBe listOf(ApricornService.PICKED + kind - 1)
          fx.fruit(id, kind) shouldBe 1
          fx.pick(session, index)
          fx.verdicts(session).last() shouldBe ApricornService.NOTHING_TODAY
          fx.fruit(id, kind) shouldBe 1
        }
      }

      test("no box, no fruit; a tree the player does not face is no tree") {
        runTest {
          val fx = Fixture(this)
          val (map, tree) = orchard(fx.maps).shouldNotBeNull()
          val (index, kind) = tree.script.removePrefix("apricorn:").split(":").map { it.toInt() }
          val (bare, bareId) = fx.picker(withBox = false, map, tree, name = "Maizie")
          fx.pick(bare, index)
          fx.verdicts(bare) shouldBe listOf(ApricornService.NO_BOX)
          fx.fruit(bareId, kind) shouldBe 0

          val (turned, turnedId) = fx.picker(withBox = true, map, tree, name = "Lyra")
          turned.attributes[PLAYER_STATE].shouldNotBeNull().facingDirection = Direction.DOWN
          fx.pick(turned, index)
          fx.verdicts(turned) shouldBe listOf(ApricornService.NO_BOX)
          fx.fruit(turnedId, kind) shouldBe 0
          // Facing the tree but naming another one is refused the same way.
          turned.attributes[PLAYER_STATE].shouldNotBeNull().facingDirection = Direction.UP
          fx.pick(turned, (index + 1) % 32)
          fx.verdicts(turned).last() shouldBe ApricornService.NO_BOX
          fx.fruit(turnedId, kind) shouldBe 0
        }
      }
    })
