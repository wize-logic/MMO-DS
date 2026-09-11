package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.PokemonNature
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.common.enums.compress
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.items.generated.Items
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.net.game.packets.BagDeltaPacket
import de.fiereu.openmmo.net.game.packets.BattleOutcomeMon
import de.fiereu.openmmo.net.game.packets.BattleOutcomeMove
import de.fiereu.openmmo.net.game.packets.BattleOutcomePacket
import de.fiereu.openmmo.net.game.packets.ClientScriptOwnershipPacket
import de.fiereu.openmmo.net.game.packets.LocalCharacterDeltaPacket
import de.fiereu.openmmo.net.game.packets.MoneyDeltaPacket
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket
import de.fiereu.openmmo.net.game.packets.PokemonReleasePacket
import de.fiereu.openmmo.net.game.packets.RegisteredItemPacket
import de.fiereu.openmmo.net.game.packets.ScriptFlagEntry
import de.fiereu.openmmo.net.game.packets.ScriptGrantPacket
import de.fiereu.openmmo.net.game.packets.ScriptStatePacket
import de.fiereu.openmmo.net.game.packets.ScriptVarEntry
import de.fiereu.openmmo.net.game.packets.ScriptWarpArrivedPacket
import de.fiereu.openmmo.pokemon.EvolutionRegistry
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import de.fiereu.openmmo.pokemon.MoveSourceRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.BattleRegistry
import de.fiereu.openmmo.server.game.battle.BattleRng
import de.fiereu.openmmo.server.game.battle.ExpCurves
import de.fiereu.openmmo.server.game.battle.StatCalculator
import de.fiereu.openmmo.server.game.battle.WildMonFactory
import de.fiereu.openmmo.server.game.offline.verify.ReplayVerdict
import de.fiereu.openmmo.server.game.script.Badge
import de.fiereu.openmmo.server.game.session.CLIENT_RUNS_SCRIPTS
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.ImportRecord
import de.fiereu.openmmo.server.game.storage.ImportRepository
import de.fiereu.openmmo.server.game.storage.InMemoryImportRepository
import de.fiereu.openmmo.server.game.storage.InMemorySaveBlockRepository
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.fieldMoveService
import de.fiereu.openmmo.server.game.testsupport.staticEncounterService
import de.fiereu.openmmo.server.game.testsupport.testGameConfig
import de.fiereu.openmmo.server.game.world.WarpNeighbours
import de.fiereu.openmmo.server.game.world.interest.InterestManager
import de.fiereu.openmmo.server.game.world.interest.PassThroughInterestPolicy
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

/** The record of a scene the client ran. */
@OptIn(ExperimentalCoroutinesApi::class)
class LocalScriptServiceTest :
    FunSpec({
      /** The item the bag reports move around, by the id this build's registry gives it. */
      val potion = ItemRegistry().idOf(Items.POTION)

      fun budget(
          moneyGained: Int = 1_000_000,
          itemsGained: Int = 2_000,
          monstersGranted: Int = 20,
          levelsGained: Int = 30,
          contestPointsGained: Int = 1_000,
          ribbonsWon: Int = 4,
          friendshipGained: Int = 500,
      ) =
          GrantBudget(
              GrantBudget.Limits(
                  moneyGained = moneyGained,
                  itemsGained = itemsGained,
                  monstersGranted = monstersGranted,
                  levelsGained = levelsGained,
                  contestPointsGained = contestPointsGained,
                  ribbonsWon = ribbonsWon,
                  friendshipGained = friendshipGained,
              ),
          ) {
            0L
          }

      // Heatran, which is a static site in this game, and a Bidoof, which is every route.
      val HEATRAN = 485
      val BIDOOF = 399

      /** A capture report of one species, as the client's engine sends one. */
      fun grantOf(dexId: Int) =
          ScriptGrantPacket(
              dexId = dexId,
              level = 50,
              hp = -1,
              container = 1,
              slot = -1,
              seed = 0,
              ivBits = 0,
              isShiny = false,
              nickname = "",
          )

      /** An import standing on a character: the shape, not the contents, is what is read. */
      fun importRow(characterId: Long) =
          ImportRecord(
              id = 1,
              characterId = characterId,
              importedAt = LocalDateTime.of(2026, 9, 6, 12, 0),
              playTimeSeconds = 3600,
              saveSha256 = "a".repeat(64),
              clientRevision = 7,
              trainerId = 42,
              partyCount = 1,
              boxCount = 0,
              speciesCount = 1,
              levelTotal = 50,
              levelMax = 50,
              moneyBefore = 0,
              moneyAfter = 0,
              badgesBefore = 0,
              badgesAfter = 8,
              verdicts = emptyList(),
              snapshotVersion = 1,
          )

      fun service(
          store: CharacterStore,
          maps: MapManager,
          budget: GrantBudget = budget(),
          battles: BattleRegistry = BattleRegistry(),
          // The two the capture path asks about provenance: which static site's fight this catch
          // belongs to, and whether the character is standing on an import nobody has replayed.
          statics: StaticEncounterService? = null,
          imports: ImportRepository = InMemoryImportRepository(),
      ) =
          LocalScriptService(
              StoryService(store),
              store,
              maps,
              PresenceService(
                  InterestManager(),
                  PassThroughInterestPolicy(),
                  MapLoadService(maps, SpeciesRegistry()),
                  store),
              StoryPlayerService(
                  store,
                  WildMonFactory(
                      SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService()),
                  SpeciesRegistry(),
                  MoveRegistry(),
                  ItemRegistry(),
              ),
              LearnsetRegistry(),
              MoveSourceRegistry(),
              UndergroundTalkService(SessionRegistry()),
              ItemRegistry(),
              SpeciesRegistry(),
              EvolutionRegistry(),
              budget,
              MoveRegistry(),
              ReportedIndividual(testGameConfig()),
              ContestRibbonCredits(),
              ViolationLog(),
              WarpNeighbours(maps),
              InMemorySaveBlockRepository(),
              fieldMoveService(store, maps),
              battles,
              statics ?: staticEncounterService(store, maps),
              imports,
          )

      test("a reported flag and var are stored, and seat back as the same ids") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val session =
              FakeSession(characterId = id, regionId = 3, bankId = 1, mapId = 158).also {
                it.attributes[CLIENT_RUNS_SCRIPTS] = true
              }

          service(store, MapManager())
              .onScriptState(
                  PacketEvent(
                      ScriptStatePacket(
                          flags = listOf(ScriptFlagEntry(2408, true)),
                          vars = listOf(ScriptVarEntry(16400, 7)),
                      ),
                      session,
                  ))

          val stored = store.getCharacter(id)!!
          ("sinnoh/vm/flag/2408" in stored.storyFlags) shouldBe true
          stored.storyVars["sinnoh/vm/var/16400"] shouldBe 7

          val seat = StoryClientState.scriptState(3, stored.storyFlags, stored.storyVars)
          seat.flags shouldBe listOf(ScriptFlagEntry(2408, true))
          seat.vars shouldBe listOf(ScriptVarEntry(16400, 7))
        }
      }

      /**
       * The seat sends these rows under the character's own region, so the report has to write them
       * under the same one. A session carries a region of its own that is only filled in when a map
       * address is seated, and until then it reads as Hoenn.
       */
      test("a report is keyed to the character's region, not the session's") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val session =
              FakeSession(characterId = id, regionId = 1, bankId = 51, mapId = 3).also {
                it.attributes[CLIENT_RUNS_SCRIPTS] = true
              }

          service(store, MapManager())
              .onScriptState(
                  PacketEvent(
                      ScriptStatePacket(
                          flags = listOf(ScriptFlagEntry(2408, true)),
                          vars = listOf(ScriptVarEntry(16400, 7)),
                      ),
                      session,
                  ))

          val stored = store.getCharacter(id)!!
          ("sinnoh/vm/flag/2408" in stored.storyFlags) shouldBe true
          stored.storyVars["sinnoh/vm/var/16400"] shouldBe 7
          stored.storyFlags.none { it.startsWith("hoenn/") } shouldBe true
          stored.storyVars.keys.none { it.startsWith("hoenn/") } shouldBe true

          val seat =
              StoryClientState.scriptState(
                  stored.info.positionRegionId, stored.storyFlags, stored.storyVars)
          seat.flags shouldBe listOf(ScriptFlagEntry(2408, true))
          seat.vars shouldBe listOf(ScriptVarEntry(16400, 7))
        }
      }

      /**
       * A cleared flag has to leave the store, not merely arrive as `on = false`: the seat sends
       * set flags only, because the client clears the whole block before writing one.
       */
      test("a cleared flag is removed, so the seat stops carrying it") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val session =
              FakeSession(characterId = id, regionId = 3, bankId = 1, mapId = 158).also {
                it.attributes[CLIENT_RUNS_SCRIPTS] = true
              }
          val svc = service(store, MapManager())

          svc.onScriptState(
              PacketEvent(
                  ScriptStatePacket(listOf(ScriptFlagEntry(2408, true)), emptyList()), session))
          svc.onScriptState(
              PacketEvent(
                  ScriptStatePacket(listOf(ScriptFlagEntry(2408, false)), emptyList()), session))

          val stored = store.getCharacter(id)!!
          ("sinnoh/vm/flag/2408" in stored.storyFlags) shouldBe false
          StoryClientState.scriptState(3, stored.storyFlags, stored.storyVars).flags shouldBe
              emptyList()
        }
      }

      /**
       * The two id spaces share one store and must not read each other. The ported Kotlin corpus
       * writes `sinnoh/FLAG_NAME`; the engine's VM writes numbers.
       */
      test("the seat ignores the named GBA-derived keys beside it") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val story = StoryService(store)
          story.setFlag(id, "sinnoh/FLAG_ADVENTURE_STARTED")
          story.setVar(id, "sinnoh/VAR_SOMETHING", 4)

          val stored = store.getCharacter(id)!!
          val seat = StoryClientState.scriptState(3, stored.storyFlags, stored.storyVars)
          seat.flags shouldBe emptyList()
          seat.vars shouldBe emptyList()
        }
      }

      /** And the other way: a region that is not the character's owns none of these keys. */
      test("the seat does not leak one region's vm state into another") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val session =
              FakeSession(characterId = id, regionId = 3, bankId = 1, mapId = 158).also {
                it.attributes[CLIENT_RUNS_SCRIPTS] = true
              }
          service(store, MapManager())
              .onScriptState(
                  PacketEvent(
                      ScriptStatePacket(listOf(ScriptFlagEntry(2408, true)), emptyList()), session))

          val stored = store.getCharacter(id)!!
          StoryClientState.scriptState(1, stored.storyFlags, stored.storyVars).flags shouldBe
              emptyList()
        }
      }

      /** A zero var is absent from the store, and an absent var must not seat as a stale value. */
      test("a var back at zero leaves the seat rather than seating its old value") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val session =
              FakeSession(characterId = id, regionId = 3, bankId = 1, mapId = 158).also {
                it.attributes[CLIENT_RUNS_SCRIPTS] = true
              }
          val svc = service(store, MapManager())

          svc.onScriptState(
              PacketEvent(
                  ScriptStatePacket(emptyList(), listOf(ScriptVarEntry(16400, 7))), session))
          svc.onScriptState(
              PacketEvent(
                  ScriptStatePacket(emptyList(), listOf(ScriptVarEntry(16400, 0))), session))

          val stored = store.getCharacter(id)!!
          StoryClientState.scriptState(3, stored.storyVars.keys, stored.storyVars).vars shouldBe
              emptyList()
        }
      }

      /** The declaration that keeps one scene from being run twice. */
      test("a session's script ownership is recorded, and defaults to the server's") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val session = FakeSession(characterId = id, regionId = 3, bankId = 1, mapId = 158)
          val svc = service(store, MapManager())

          session.attributes[CLIENT_RUNS_SCRIPTS] shouldBe null

          svc.onScriptOwnership(PacketEvent(ClientScriptOwnershipPacket(true), session))
          session.attributes[CLIENT_RUNS_SCRIPTS] shouldBe true
        }
      }

      /**
       * The answer decides whether this session's value reports are listened to at all, so it is
       * asked once and cannot be taken back, otherwise it is a switch a client flips on for the
       * length of a claim and off again afterwards.
       */
      test("the first answer stands and a later one is refused") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val session = FakeSession(characterId = id, regionId = 3, bankId = 1, mapId = 158)
          val svc = service(store, MapManager())

          svc.onScriptOwnership(PacketEvent(ClientScriptOwnershipPacket(false), session))
          svc.onScriptOwnership(PacketEvent(ClientScriptOwnershipPacket(true), session))

          session.attributes[CLIENT_RUNS_SCRIPTS] shouldBe false
        }
      }

      /**
       * The reports that carry value cannot be verified, the scene that made them ran on the client
       * and this server was told afterwards. What can be done is to bound them, and to refuse the
       * ones that describe something no scene and no fight could have produced.
       */
      context("a report that carries value") {
        fun playing(store: CharacterStore, id: Long) =
            FakeSession(characterId = id, regionId = 3, bankId = 1, mapId = 158).also {
              it.attributes[CLIENT_RUNS_SCRIPTS] = true
            }

        test("an item id that is not an item is refused") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)

            service(store, MapManager())
                .onBagDelta(PacketEvent(BagDeltaPacket(itemId = 60_000, delta = 99), session))

            store.getCharacter(id)!!.items[60_000] shouldBe null
          }
        }

        test("registering a held key item is recorded, and clearing it takes it back off") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            // 5445 is the Old Rod: a key item, and the one issue #4 was filed with.
            store.addItem(id, 5445, 1)

            svc.onRegisteredItem(PacketEvent(RegisteredItemPacket(5445), session))
            store.getCharacter(id)!!.info.registeredItem shouldBe 5445.toShort()

            svc.onRegisteredItem(PacketEvent(RegisteredItemPacket(0), session))
            store.getCharacter(id)!!.info.registeredItem shouldBe 0.toShort()
          }
        }

        test("registering an item that is not a key item, or not held, is refused") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            store.addItem(id, potion, 1)

            // A potion is held but not a key item; a Bicycle (5450) is a key item not held.
            svc.onRegisteredItem(PacketEvent(RegisteredItemPacket(potion), session))
            svc.onRegisteredItem(PacketEvent(RegisteredItemPacket(5450), session))

            store.getCharacter(id)!!.info.registeredItem shouldBe 0.toShort()
          }
        }

        test("items stop arriving once the allowance for the window is gone") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager(), budget(itemsGained = 200))

            repeat(4) {
              svc.onBagDelta(PacketEvent(BagDeltaPacket(itemId = potion, delta = 99), session))
            }

            // Two landed; the third crossed the line and was refused whole, and so was the one
            // after it, asking again inside the window does not hand back the difference.
            store.getCharacter(id)!!.items[potion] shouldBe 198
          }
        }

        test("a consumed item costs no allowance, so a long session can keep using its bag") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager(), budget(itemsGained = 100))
            store.addItem(id, potion, 99)

            repeat(10) {
              svc.onBagDelta(PacketEvent(BagDeltaPacket(itemId = potion, delta = -9), session))
            }
            svc.onBagDelta(PacketEvent(BagDeltaPacket(itemId = potion, delta = 9), session))

            store.getCharacter(id)!!.items[potion] shouldBe 18
          }
        }

        /**
         * The report is a delta applied to the balance the store holds, never a total worked out
         * from one read a moment before. A stale total lands on top of whatever was spent in
         * between and pays it back.
         */
        test("a purchase that overlaps an earning is not paid back by it") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val before = store.getCharacter(id)!!.info.money

            // What founding a guild or buying through the server's own shop does, while the
            // client's own arithmetic reports a coin earned somewhere else.
            store.addMoney(id, -5_000)
            svc.onMoneyDelta(PacketEvent(MoneyDeltaPacket(delta = 1), session))

            store.getCharacter(id)!!.info.money shouldBe before - 5_000 + 1
          }
        }

        test("a spend the wallet cannot cover is refused and corrected on screen") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val held = store.getCharacter(id)!!.info.money

            service(store, MapManager())
                .onMoneyDelta(PacketEvent(MoneyDeltaPacket(delta = -(held + 1)), session))

            store.getCharacter(id)!!.info.money shouldBe held
            session.sent.filterIsInstance<LocalCharacterDeltaPacket>().last().money shouldBe held
          }
        }

        test("earnings stop at the allowance for the window") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager(), budget(moneyGained = 10_000))
            val before = store.getCharacter(id)!!.info.money

            svc.onMoneyDelta(PacketEvent(MoneyDeltaPacket(delta = 10_000), session))
            svc.onMoneyDelta(PacketEvent(MoneyDeltaPacket(delta = 10_000), session))

            store.getCharacter(id)!!.info.money shouldBe before + 10_000
          }
        }

        /**
         * A fight moves a monster's level, experience and hit points, and it moves them one way. A
         * report that takes any of them backwards, or forwards further than a fight can, is a
         * client writing a party rather than recording one.
         */
        test("a battle outcome may only move a monster the way a fight moves it") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            val seated = store.addPokemon(id, factory.create(387, 20, BattleRng(seed = 1))!!)!!

            fun report(level: Int, xp: Int, hp: Int) =
                svc.onBattleOutcome(
                    PacketEvent(
                        BattleOutcomePacket(
                            listOf(
                                BattleOutcomeMon(
                                    seated.id,
                                    level,
                                    xp,
                                    hp,
                                    List(4) { BattleOutcomeMove(0, 0) }))),
                        session,
                    ))

            // A level and an experience total below the record: refused.
            report(level = 19, xp = seated.xp, hp = 1)
            store.getCharacter(id)!!.pokemon.single().level shouldBe 20

            // A jump no single fight produces: refused.
            report(level = 100, xp = seated.xp + 1, hp = 1)
            store.getCharacter(id)!!.pokemon.single().level shouldBe 20

            // A fight's worth of growth: recorded.
            report(level = 22, xp = seated.xp + 500, hp = 3)
            store.getCharacter(id)!!.pokemon.single().level shouldBe 22
          }
        }

        /**
         * The engine runs the game's own evolution screen, so a monster that evolves has already
         * changed species on the client by the time the report arrives.
         */
        test("a battle outcome carries the species the engine evolved the monster into") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            // Charmander, one level short of the sixteen it becomes a Charmeleon at.
            val seated = store.addPokemon(id, factory.create(4, 15, BattleRng(seed = 1))!!)!!

            fun report(level: Int, species: Int) =
                svc.onBattleOutcome(
                    PacketEvent(
                        BattleOutcomePacket(
                            listOf(
                                BattleOutcomeMon(
                                    seated.id,
                                    level,
                                    seated.xp,
                                    hp = 1,
                                    moves = List(4) { BattleOutcomeMove(0, 0) },
                                    species = species))),
                        session,
                    ))

            // A species the level does not reach yet: refused, and the record stands.
            report(level = 15, species = 5)
            store.getCharacter(id)!!.pokemon.single().dexId shouldBe 4

            // The evolution the fight really produced.
            report(level = 16, species = 5)
            store.getCharacter(id)!!.pokemon.single().dexId shouldBe 5

            // A species a Charmeleon does not become at all.
            report(level = 16, species = 149)
            store.getCharacter(id)!!.pokemon.single().dexId shouldBe 5

            // A report making no claim leaves the evolved species alone.
            report(level = 16, species = 0)
            store.getCharacter(id)!!.pokemon.single().dexId shouldBe 5
          }
        }

        test("a battle outcome carries the friendship the engine walked into it") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            // A Golbat, which becomes a Crobat on friendship and on nothing else.
            val seated = store.addPokemon(id, factory.create(42, 30, BattleRng(seed = 1))!!)!!
            // A fresh monster starts at its own species' base, not at a flat number.
            seated.friendship shouldBe 70

            fun report(friendship: Int, species: Int = 0) =
                svc.onBattleOutcome(
                    PacketEvent(
                        BattleOutcomePacket(
                            listOf(
                                BattleOutcomeMon(
                                    seated.id,
                                    level = 30,
                                    xp = seated.xp,
                                    hp = 1,
                                    moves = List(4) { BattleOutcomeMove(0, 0) },
                                    species = species,
                                    friendship = friendship))),
                        session,
                    ))

            // One short of the threshold: the friendship is kept and the evolution is not.
            report(friendship = 219, species = 169)
            store.getCharacter(id)!!.pokemon.single().friendship shouldBe 219
            store.getCharacter(id)!!.pokemon.single().dexId shouldBe 42

            report(friendship = 220, species = 169)
            store.getCharacter(id)!!.pokemon.single().friendship shouldBe 220
            store.getCharacter(id)!!.pokemon.single().dexId shouldBe 169

            // Fainting takes it back down, which the record follows.
            report(friendship = 200)
            store.getCharacter(id)!!.pokemon.single().friendship shouldBe 200

            // A client making no claim leaves it where it is.
            report(friendship = -1)
            store.getCharacter(id)!!.pokemon.single().friendship shouldBe 200
          }
        }

        /**
         * The engine hatches an egg itself, end to end: an egg's remaining cycles ride the record's
         * friendship byte, exactly where the game's own `Egg_CreateEgg` keeps them, the field's
         * step chain spends one every 255 steps, and the game's hatch scene clears the bit.
         */
        test("a battle outcome hatches an egg and cannot turn a monster back into one") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            // An Eevee egg with its last cycle spent, which is what the client seats and walks.
            val egg =
                store.addPokemon(
                    id,
                    factory
                        .create(133, 1, BattleRng(seed = 1))!!
                        .copy(isEgg = true, friendship = 0))!!
            egg.isEgg shouldBe true

            fun report(isEgg: Boolean, friendship: Int) =
                svc.onBattleOutcome(
                    PacketEvent(
                        BattleOutcomePacket(
                            listOf(
                                BattleOutcomeMon(
                                    egg.id,
                                    level = 1,
                                    xp = egg.xp,
                                    hp = 1,
                                    moves = List(4) { BattleOutcomeMove(0, 0) },
                                    friendship = friendship,
                                    isEgg = isEgg))),
                        session,
                    ))

            // Still counting down: the bit stands and the cycles left stand with it.
            report(isEgg = true, friendship = 0)
            store.getCharacter(id)!!.pokemon.single().isEgg shouldBe true

            // The hatch the engine ran. It leaves a hatched monster liking its trainer at 120.
            report(isEgg = false, friendship = 120)
            store.getCharacter(id)!!.pokemon.single().isEgg shouldBe false
            store.getCharacter(id)!!.pokemon.single().friendship shouldBe 120

            // And nothing puts it back. An egg is the one shape whose level, moves and catch place
            // a client could otherwise reset by hand.
            report(isEgg = true, friendship = 120)
            store.getCharacter(id)!!.pokemon.single().isEgg shouldBe false
          }
        }

        test("a battle outcome carries the item the party menu gave the monster") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            val catalogue = ItemRegistry()
            val razorClaw = catalogue.idOf(Items.RAZOR_CLAW)
            val leftovers = catalogue.idOf(Items.LEFTOVERS)
            // A Sneasel, which becomes a Weavile on a Razor Claw and on nothing else.
            val seated = store.addPokemon(id, factory.create(215, 30, BattleRng(seed = 1))!!)!!
            seated.heldItemId shouldBe 0

            fun report(heldItemId: Int, species: Int = 0) =
                svc.onBattleOutcome(
                    PacketEvent(
                        BattleOutcomePacket(
                            listOf(
                                BattleOutcomeMon(
                                    seated.id,
                                    level = 30,
                                    xp = seated.xp,
                                    hp = 1,
                                    moves = List(4) { BattleOutcomeMove(0, 0) },
                                    species = species,
                                    heldItemId = heldItemId))),
                        session,
                    ))

            // A give. The bag half of it is a BagDelta of its own and nothing here moves the bag.
            report(heldItemId = razorClaw)
            store.getCharacter(id)!!.pokemon.single().heldItemId shouldBe razorClaw

            // The evolution the item is the whole trigger for. The report carries no item, because
            // the game clears a held item as it evolves on one. The check reads the stored one.
            report(heldItemId = 0, species = 461)
            store.getCharacter(id)!!.pokemon.single().dexId shouldBe 461
            store.getCharacter(id)!!.pokemon.single().heldItemId shouldBe 0

            // A client making no claim leaves it where it is; a take says 0 and is taken.
            report(heldItemId = leftovers)
            report(heldItemId = -1)
            store.getCharacter(id)!!.pokemon.single().heldItemId shouldBe leftovers
            report(heldItemId = 0)
            store.getCharacter(id)!!.pokemon.single().heldItemId shouldBe 0
          }
        }

        test("an Everstone on the record refuses the evolution the report claims") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            val everstone = ItemRegistry().idOf(Items.EVERSTONE)
            val seated = store.addPokemon(id, factory.create(4, 15, BattleRng(seed = 1))!!)!!

            fun report(heldItemId: Int, species: Int = 0) =
                svc.onBattleOutcome(
                    PacketEvent(
                        BattleOutcomePacket(
                            listOf(
                                BattleOutcomeMon(
                                    seated.id,
                                    level = 16,
                                    xp = seated.xp,
                                    hp = 1,
                                    moves = List(4) { BattleOutcomeMove(0, 0) },
                                    species = species,
                                    heldItemId = heldItemId))),
                        session,
                    ))

            report(heldItemId = everstone)
            store.getCharacter(id)!!.pokemon.single().heldItemId shouldBe everstone
            // The engine would not have run the screen at all, so the claim is a client's.
            report(heldItemId = everstone, species = 5)
            store.getCharacter(id)!!.pokemon.single().dexId shouldBe 4
            // Taken off, the same level-up stands.
            report(heldItemId = 0)
            report(heldItemId = 0, species = 5)
            store.getCharacter(id)!!.pokemon.single().dexId shouldBe 5
          }
        }

        test("a key item is not something a monster can be reported holding") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            val seated = store.addPokemon(id, factory.create(215, 30, BattleRng(seed = 1))!!)!!

            fun report(heldItemId: Int) =
                svc.onBattleOutcome(
                    PacketEvent(
                        BattleOutcomePacket(
                            listOf(
                                BattleOutcomeMon(
                                    seated.id,
                                    level = 30,
                                    xp = seated.xp,
                                    hp = 1,
                                    moves = List(4) { BattleOutcomeMove(0, 0) },
                                    heldItemId = heldItemId))),
                        session,
                    ))

            // The game's own give flow never offers one, so a report of one is a client sending
            // numbers rather than recording a scene.
            report(KEY_ITEM_IDS.first)
            store.getCharacter(id)!!.pokemon.single().heldItemId shouldBe 0
            // An id no registry knows is the same thing said differently.
            report(60_000)
            store.getCharacter(id)!!.pokemon.single().heldItemId shouldBe 0
          }
        }

        test("friendship stops being taken once the allowance for the window is gone") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager(), budget = budget(friendshipGained = 100))
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            val seated = store.addPokemon(id, factory.create(42, 30, BattleRng(seed = 1))!!)!!

            fun report(friendship: Int) =
                svc.onBattleOutcome(
                    PacketEvent(
                        BattleOutcomePacket(
                            listOf(
                                BattleOutcomeMon(
                                    seated.id,
                                    level = 30,
                                    xp = seated.xp,
                                    hp = 1,
                                    moves = List(4) { BattleOutcomeMove(0, 0) },
                                    friendship = friendship))),
                        session,
                    ))

            // Eighty of the hundred, taken.
            report(friendship = 150)
            store.getCharacter(id)!!.pokemon.single().friendship shouldBe 150
            // The jump to the evolution threshold is seventy more, past what is left.
            report(friendship = 220)
            store.getCharacter(id)!!.pokemon.single().friendship shouldBe 150
          }
        }

        /**
         * The species is settled against the level the record is going to hold, not the one the
         * report claims. A window that has run out of levels keeps the monster where it was, and an
         * evolution taken at the claimed level would have left a Charmeleon standing at 15.
         */
        test("a level the window refuses does not carry an evolution with it") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager(), budget(levelsGained = 0))
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            val seated = store.addPokemon(id, factory.create(4, 15, BattleRng(seed = 1))!!)!!

            svc.onBattleOutcome(
                PacketEvent(
                    BattleOutcomePacket(
                        listOf(
                            BattleOutcomeMon(
                                seated.id,
                                level = 16,
                                xp = seated.xp,
                                hp = 1,
                                moves = List(4) { BattleOutcomeMove(0, 0) },
                                species = 5))),
                    session,
                ))

            val after = store.getCharacter(id)!!.pokemon.single()
            after.level shouldBe 15
            after.dexId shouldBe 4
          }
        }

        /**
         * Level and experience are the same fact told twice, and only the level was bounded. A row
         * could stand still at the level it started on and put two billion experience behind it,
         * which the next server-run battle reads back out as level 100.
         */
        test("experience past what the reported level can hold is cut down to it") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            val seated = store.addPokemon(id, factory.create(387, 20, BattleRng(seed = 1))!!)!!

            svc.onBattleOutcome(
                PacketEvent(
                    BattleOutcomePacket(
                        listOf(
                            BattleOutcomeMon(
                                seated.id,
                                level = 20,
                                xp = Int.MAX_VALUE,
                                hp = 1,
                                moves = List(4) { BattleOutcomeMove(0, 0) }))),
                    session,
                ))

            val rate = SpeciesRegistry().get(387)!!.growthRate
            val after = store.getCharacter(id)!!.pokemon.single()
            after.xp shouldBe ExpCurves.totalXpFor(rate, 21) - 1
            after.level shouldBe 20
          }
        }

        /**
         * Ten levels a report was the whole bound, and reports were free, so a party reached level
         * 100 in about as long as it takes to send sixty packets.
         */
        test("levels stop being taken once the allowance for the window is gone") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager(), budget(levelsGained = 5))
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            val seated = store.addPokemon(id, factory.create(387, 20, BattleRng(seed = 1))!!)!!
            val rate = SpeciesRegistry().get(387)!!.growthRate

            fun report(level: Int) =
                svc.onBattleOutcome(
                    PacketEvent(
                        BattleOutcomePacket(
                            listOf(
                                BattleOutcomeMon(
                                    seated.id,
                                    level,
                                    ExpCurves.totalXpFor(rate, level),
                                    hp = 1,
                                    moves = List(4) { BattleOutcomeMove(0, 0) }))),
                        session,
                    ))

            report(25)
            store.getCharacter(id)!!.pokemon.single().level shouldBe 25

            // The window is spent, so the growth is left where the record has it.
            report(30)
            store.getCharacter(id)!!.pokemon.single().level shouldBe 25
          }
        }

        /**
         * These are what a link Super Contest scores on, so unlike the rest of this file an untrue
         * one is a player beating somebody else rather than beating the game.
         */
        test("one report raises a contest condition by a Poffin, not to the ceiling") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            val seated = store.addPokemon(id, factory.create(387, 20, BattleRng(seed = 1))!!)!!

            svc.onBattleOutcome(
                PacketEvent(
                    BattleOutcomePacket(
                        listOf(
                            BattleOutcomeMon(
                                seated.id,
                                seated.level.toInt(),
                                seated.xp,
                                hp = 1,
                                moves = List(4) { BattleOutcomeMove(0, 0) },
                                conditions = ContestConditions(255, 255, 255, 255, 255),
                                sheen = 255,
                                superContestRibbons = -1L))),
                    session,
                ))

            val after = store.getCharacter(id)!!.pokemon.single()
            after.conditions shouldBe ContestConditions(60, 60, 60, 60, 60)
            after.sheen shouldBe 60
            // Every bit set is twenty contests won at once, which is not a contest that happened.
            // The forty-four bits that name no contest are dropped before they are counted.
            after.superContestRibbons shouldBe 0L
          }
        }

        /**
         * The reporter caps itself at 192 flags and 64 vars and carries the rest to its next
         * report, so a wider one did not come from it. The codec would take about 21,000, each a
         * write to this character's story rows.
         */
        test("a report wider than a client sends is refused whole") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            // A new character starts with the flags its region's new game sets, so what this
            // asserts is that the report added none of its own.
            val before = store.getCharacter(id)!!.storyFlags.toSet()

            service(store, MapManager())
                .onScriptState(
                    PacketEvent(
                        ScriptStatePacket(
                            flags = (0 until 193).map { ScriptFlagEntry(it.toShort(), true) },
                            vars = emptyList(),
                        ),
                        session,
                    ))

            store.getCharacter(id)!!.storyFlags shouldBe before
          }
        }

        test("an outcome cannot leave a monster with more hit points than it has") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            val seated = store.addPokemon(id, factory.create(387, 20, BattleRng(seed = 1))!!)!!

            service(store, MapManager())
                .onBattleOutcome(
                    PacketEvent(
                        BattleOutcomePacket(
                            listOf(
                                BattleOutcomeMon(
                                    seated.id,
                                    seated.level.toInt(),
                                    seated.xp,
                                    hp = 30_000,
                                    moves = List(4) { BattleOutcomeMove(0, 0) }))),
                        session,
                    ))

            val after = store.getCharacter(id)!!.pokemon.single()
            val definition = SpeciesRegistry().get(after.dexId)!!
            after.hp shouldBe StatCalculator.computeAll(definition, after).hp.toShort()
          }
        }

        /* The last road imported progress had into the market. */
        test("a static site taken on an unverified import is marked") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val statics = staticEncounterService(store, MapManager())
            val imports = InMemoryImportRepository()
            imports.record(importRow(id), ByteArray(0))
            statics.claims[id] = StaticEncounterService.Claim(HEATRAN)
            val svc = service(store, MapManager(), statics = statics, imports = imports)

            svc.onScriptGrant(PacketEvent(grantOf(HEATRAN), session))

            store.getCharacter(id)!!.pokemon.single().offlineOrigin shouldBe true
          }
        }

        test("the same catch on a character that imported nothing is not marked") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val statics = staticEncounterService(store, MapManager())
            statics.claims[id] = StaticEncounterService.Claim(HEATRAN)
            val svc = service(store, MapManager(), statics = statics)

            svc.onScriptGrant(PacketEvent(grantOf(HEATRAN), session))

            store.getCharacter(id)!!.pokemon.single().offlineOrigin shouldBe false
          }
        }

        test("an ordinary catch on an unverified import is not marked, and spends no claim") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val statics = staticEncounterService(store, MapManager())
            val imports = InMemoryImportRepository()
            imports.record(importRow(id), ByteArray(0))
            statics.claims[id] = StaticEncounterService.Claim(HEATRAN)
            val svc = service(store, MapManager(), statics = statics, imports = imports)

            // A Bidoof caught on the way to the site. The claim is the legendary's and stays put.
            svc.onScriptGrant(PacketEvent(grantOf(BIDOOF), session))

            store.getCharacter(id)!!.pokemon.single().offlineOrigin shouldBe false
            statics.claims[id].shouldNotBeNull()
          }
        }

        test("a verified import leaves the site's catch alone") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val statics = staticEncounterService(store, MapManager())
            val imports = InMemoryImportRepository()
            val row = importRow(id)
            imports.record(row, ByteArray(0))
            // The play behind the import was replayed here and agreed with: there is nothing left
            // to hold against what its badges opened.
            imports.markReplay(row.id, ReplayVerdict.VERIFIED.name, null)
            statics.claims[id] = StaticEncounterService.Claim(HEATRAN)
            val svc = service(store, MapManager(), statics = statics, imports = imports)

            svc.onScriptGrant(PacketEvent(grantOf(HEATRAN), session))

            store.getCharacter(id)!!.pokemon.single().offlineOrigin shouldBe false
          }
        }

        test("a monster stops being granted once the allowance is gone") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager(), budget(monstersGranted = 2))

            repeat(4) {
              svc.onScriptGrant(
                  PacketEvent(
                      ScriptGrantPacket(
                          dexId = 387,
                          level = 5,
                          hp = -1,
                          container = 1,
                          slot = -1,
                          seed = 0,
                          ivBits = 0,
                          isShiny = false,
                          nickname = "",
                      ),
                      session,
                  ))
            }

            store.getCharacter(id)!!.pokemon.size shouldBe 2
          }
        }

        /**
         * A capture is one monster however many times it is reported, and it is the server that
         * says which one.
         */
        test("a reported capture is the server's monster, and stable across a retry") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val ivs =
                IVs().apply {
                  hp = 31
                  atk = 2
                  def = 3
                  spd = 4
                  spAtk = 5
                  spDef = 6
                }

            svc.onScriptGrant(
                PacketEvent(
                    ScriptGrantPacket(
                        dexId = 387,
                        level = 5,
                        hp = -1,
                        container = 1,
                        slot = -1,
                        seed = 0x1234_5678,
                        ivBits = ivs.compress(),
                        isShiny = true,
                        nickname = "",
                    ),
                    session,
                ))

            val granted = store.getCharacter(id)!!.pokemon.single()
            val rolled = ReportedIndividual(testGameConfig()).forToken(id, 387, 0x1234_5678)

            // Not the claim. The server's own draw for that claim.
            granted.seed shouldBe rolled.seed
            granted.iVs.compress() shouldBe rolled.ivBits
            granted.isShiny shouldBe rolled.isShiny
            (granted.iVs.compress() == ivs.compress()) shouldBe false
            // The nature is read out of the seed, which is why the seed had to stop being a claim.
            // Masked the way the record masks it: the seed is unsigned on the wire.
            granted.nature shouldBe
                PokemonNature.entries[
                        ((rolled.seed.toLong() and 0xFFFFFFFFL) % PokemonNature.entries.size)
                            .toInt()]
          }
        }

        /** The same capture told twice is one monster, not two. */
        test("a re-reported capture is refused and the container reseated") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val grant =
                ScriptGrantPacket(
                    dexId = 396,
                    level = 2,
                    hp = -1,
                    container = 0,
                    slot = 0,
                    seed = 0x0BAD_F00D,
                    ivBits = 0,
                    isShiny = false,
                    nickname = "STARLY",
                )

            svc.onScriptGrant(PacketEvent(grant, session))
            store.getCharacter(id)!!.pcStorage.size shouldBe 1
            session.sent.clear()

            svc.onScriptGrant(PacketEvent(grant, session))

            store.getCharacter(id)!!.pcStorage.size shouldBe 1
            session.sent.filterIsInstance<PokemonContainerPacket>().map { it.container } shouldBe
                listOf(PokemonContainer.PC)
          }
        }

        /**
         * The box screen let a monster go. The record loses it, and both containers come back so
         * the screen shows the record, the round trip the session's next seat depends on.
         */
        test("a release removes the monster from the record and reseats both containers") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            val kept = store.addPokemon(id, factory.create(387, 20, BattleRng(seed = 1))!!)!!
            val boxed =
                store.addPokemon(
                    id,
                    factory
                        .create(396, 5, BattleRng(seed = 2))!!
                        .copy(container = PokemonContainer.PC))!!

            session.sent.clear()
            svc.onPokemonRelease(PacketEvent(PokemonReleasePacket(boxed.id), session))

            store.getCharacter(id)!!.pcStorage.size shouldBe 0
            store.getCharacter(id)!!.pokemon.map { it.id } shouldBe listOf(kept.id)
            session.sent.filterIsInstance<PokemonContainerPacket>().map { it.container } shouldBe
                listOf(PokemonContainer.PARTY, PokemonContainer.PC)
          }
        }

        test("a release that would empty the party is refused, and the screen still corrected") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            val only = store.addPokemon(id, factory.create(387, 20, BattleRng(seed = 1))!!)!!

            session.sent.clear()
            svc.onPokemonRelease(PacketEvent(PokemonReleasePacket(only.id), session))

            store.getCharacter(id)!!.pokemon.map { it.id } shouldBe listOf(only.id)
            // The engine already played its goodbye, so the refusal has to reach the screen as
            // the containers it must snap back to, not only the log.
            session.sent.filterIsInstance<PokemonContainerPacket>().map { it.container } shouldBe
                listOf(PokemonContainer.PARTY, PokemonContainer.PC)
          }
        }

        /**
         * A release is the third gesture that takes a monster out of a container, and the two
         * places it may not land are the two a box move and a shelf listing are already refused
         * from: a battle writes its own copy of the party back when it ends, and a settlement at a
         * trade table is two writes with a window between them.
         */
        test("a release while the character is at a trade table is refused") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            store.addPokemon(id, factory.create(387, 20, BattleRng(seed = 1))!!)
            val boxed =
                store.addPokemon(
                    id,
                    factory
                        .create(396, 5, BattleRng(seed = 2))!!
                        .copy(container = PokemonContainer.PC))!!
            session.state().atTradeTable = true

            session.sent.clear()
            svc.onPokemonRelease(PacketEvent(PokemonReleasePacket(boxed.id), session))

            store.getCharacter(id)!!.pcStorage.map { it.id } shouldBe listOf(boxed.id)
            session.sent.filterIsInstance<PokemonContainerPacket>().map { it.container } shouldBe
                listOf(PokemonContainer.PARTY, PokemonContainer.PC)
          }
        }

        test("a release while the character is in a battle is refused") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val battles = BattleRegistry()
            val svc = service(store, MapManager(), battles = battles)
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            store.addPokemon(id, factory.create(387, 20, BattleRng(seed = 1))!!)
            val boxed =
                store.addPokemon(
                    id,
                    factory
                        .create(396, 5, BattleRng(seed = 2))!!
                        .copy(container = PokemonContainer.PC))!!
            battles.create(id, session, emptyList(), emptyList(), BattleRng(seed = 3))

            session.sent.clear()
            svc.onPokemonRelease(PacketEvent(PokemonReleasePacket(boxed.id), session))

            store.getCharacter(id)!!.pcStorage.map { it.id } shouldBe listOf(boxed.id)
            session.sent.filterIsInstance<PokemonContainerPacket>().map { it.container } shouldBe
                listOf(PokemonContainer.PARTY, PokemonContainer.PC)
          }
        }

        test("a release from the party leaves the remaining slots contiguous") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            val svc = service(store, MapManager())
            val factory =
                WildMonFactory(
                    SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService())
            val first = store.addPokemon(id, factory.create(387, 20, BattleRng(seed = 1))!!)!!
            val middle = store.addPokemon(id, factory.create(396, 5, BattleRng(seed = 2))!!)!!
            val last = store.addPokemon(id, factory.create(399, 4, BattleRng(seed = 3))!!)!!

            svc.onPokemonRelease(PacketEvent(PokemonReleasePacket(middle.id), session))

            val party = store.getCharacter(id)!!.pokemon
            party.map { it.id } shouldBe listOf(first.id, last.id)
            party.map { it.containerSlot } shouldBe listOf<Short>(0, 1)
          }
        }
      }

      /**
       * A destination this server cannot draw is refused rather than written: a position on a map
       * that does not exist is one nobody can be seen on, and it would strand the character there
       * across a relog.
       */
      test("a warp arrival on a map the server does not have is refused") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val before = store.getCharacter(id)!!.info
          val session =
              FakeSession(characterId = id, regionId = 3, bankId = 1, mapId = 158).also {
                it.attributes[CLIENT_RUNS_SCRIPTS] = true
              }

          service(store, MapManager())
              .onScriptWarpArrived(
                  PacketEvent(
                      ScriptWarpArrivedPacket(0x7FFF, 4, 6, Direction.DOWN.ordinal), session))

          val after = store.getCharacter(id)!!.info
          after.positionBankId shouldBe before.positionBankId
          after.positionMapId shouldBe before.positionMapId
          after.positionX shouldBe before.positionX
          after.positionY shouldBe before.positionY
        }
      }

      /**
       * Fly crosses the region in one step with no warp tile behind it, so it arrives looking
       * exactly like the teleport the reachability check exists to refuse.
       */
      test("a Fly landing is taken for the badge, the move and a town already visited") {
        runTest {
          val maps = MapManager()
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          // Standing in Jubilife City (header 3), whose header allows Fly.
          store.updatePosition(id, 180, 777, 0, 3)
          val session =
              FakeSession(characterId = id, regionId = 3, bankId = 0, mapId = 3).also {
                it.attributes[CLIENT_RUNS_SCRIPTS] = true
              }
          val svc = service(store, maps)
          // Twinleaf Town's fly tile: spawn row 1, header 411 (bank 1 map 155) at (116, 886).
          val landing = ScriptWarpArrivedPacket(411, 116, 886, Direction.DOWN.ordinal)

          // Nothing in place: this is the teleport the audit refuses.
          svc.onScriptWarpArrived(PacketEvent(landing, session))
          store.getCharacter(id)!!.info.positionMapId shouldBe 3.toByte()

          store.addPokemon(id, flier(id))
          store.setStoryFlag(id, Badge.COBBLE.keyIn("sinnoh"))
          // Still refused: the player has never stood in Twinleaf, so the town map would not have
          // offered it. The id is FLAG_FIRST_ARRIVAL_TWINLEAF_TOWN.
          svc.onScriptWarpArrived(PacketEvent(landing, session))
          store.getCharacter(id)!!.info.positionMapId shouldBe 3.toByte()

          store.setStoryFlag(id, "sinnoh/vm/flag/2480")
          svc.onScriptWarpArrived(PacketEvent(landing, session))
          val after = store.getCharacter(id)!!.info
          after.positionBankId shouldBe 1.toByte()
          after.positionMapId shouldBe 155.toByte()
          after.positionX shouldBe 116.toShort()
          after.positionY shouldBe 886.toShort()
        }
      }

      test("a Fly out of a cave is refused, because the cartridge would not have offered one") {
        runTest {
          val maps = MapManager()
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          // Oreburgh Gate 1F, header 258, whose header says Fly does not work here.
          checkNotNull(maps.getMap(3, 1, 2)).flyAllowed shouldBe false
          store.updatePosition(id, 10, 10, 1, 2)
          store.addPokemon(id, flier(id))
          store.setStoryFlag(id, Badge.COBBLE.keyIn("sinnoh"))
          store.setStoryFlag(id, "sinnoh/vm/flag/2480")
          val session =
              FakeSession(characterId = id, regionId = 3, bankId = 1, mapId = 2).also {
                it.attributes[CLIENT_RUNS_SCRIPTS] = true
              }

          service(store, maps)
              .onScriptWarpArrived(
                  PacketEvent(
                      ScriptWarpArrivedPacket(411, 116, 886, Direction.DOWN.ordinal), session))

          store.getCharacter(id)!!.info.positionMapId shouldBe 2.toByte()
        }
      }
    })

/** A party member that knows Fly, which is one of the four things a Fly landing is checked on. */
private fun flier(ownerId: Long): Pokemon =
    Pokemon(
        id = EntityIdService().newMonsterId(),
        ownerId = ownerId,
        container = PokemonContainer.PARTY,
        containerSlot = 0,
        dexId = 398,
        seed = 0,
        ot = "Lucas",
        nickname = "",
        level = 30,
        hp = 60,
        xp = 0,
        eVs = EVs(),
        iVs = IVs(),
        moves = listOf(PokemonMove(19, 15)),
        isShiny = false,
        hasHiddenAbility = false,
        isAlpha = false,
        isSecret = false,
        isFatefulEncounter = false,
        isRaidEncounter = false,
        caughtAt = LocalDateTime.now(),
    )
