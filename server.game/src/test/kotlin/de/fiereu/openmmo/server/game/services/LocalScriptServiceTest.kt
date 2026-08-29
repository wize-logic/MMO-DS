package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Direction
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
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.BattleRng
import de.fiereu.openmmo.server.game.battle.ExpCurves
import de.fiereu.openmmo.server.game.battle.StatCalculator
import de.fiereu.openmmo.server.game.battle.WildMonFactory
import de.fiereu.openmmo.server.game.session.CLIENT_RUNS_SCRIPTS
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.InMemorySaveBlockRepository
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.world.interest.InterestManager
import de.fiereu.openmmo.server.game.world.interest.PassThroughInterestPolicy
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.booleans.shouldBeTrue
import io.kotest.matchers.shouldBe
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
      ) =
          GrantBudget(
              GrantBudget.Limits(
                  moneyGained = moneyGained,
                  itemsGained = itemsGained,
                  monstersGranted = monstersGranted,
                  levelsGained = levelsGained,
                  contestPointsGained = contestPointsGained,
                  ribbonsWon = ribbonsWon,
              ),
          ) {
            0L
          }

      fun service(
          store: CharacterStore,
          maps: MapManager,
          budget: GrantBudget = budget(),
      ) =
          LocalScriptService(
              StoryService(store),
              store,
              maps,
              PresenceService(
                  InterestManager(), PassThroughInterestPolicy(), MapLoadService(maps), store),
              StoryPlayerService(
                  store,
                  WildMonFactory(
                      SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService()),
                  SpeciesRegistry(),
                  MoveRegistry(),
                  ItemRegistry(),
              ),
              LearnsetRegistry(),
              UndergroundTalkService(SessionRegistry()),
              ItemRegistry(),
              SpeciesRegistry(),
              budget,
              InMemorySaveBlockRepository(),
          )

      test("a reported flag and var are stored, and seat back as the same ids") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val session = FakeSession(characterId = id, regionId = 3, bankId = 1, mapId = 158)

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
       * A cleared flag has to leave the store, not merely arrive as `on = false`: the seat sends
       * set flags only, because the client clears the whole block before writing one.
       */
      test("a cleared flag is removed, so the seat stops carrying it") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val session = FakeSession(characterId = id, regionId = 3, bankId = 1, mapId = 158)
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
          val session = FakeSession(characterId = id, regionId = 3, bankId = 1, mapId = 158)
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
          val session = FakeSession(characterId = id, regionId = 3, bankId = 1, mapId = 158)
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
         * Level and experience are the same fact told twice, and only the level was bounded. A row
         * could stand still and put two billion behind it, which the next server-run battle reads
         * back out as level 100.
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

        /** Ten levels a report was the whole bound, and reports were free. */
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

        /** These are what a link contest scores on, so an untrue one beats another player. */
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
            // Every bit set is twenty contests won at once, and the bits that name no contest are
            // dropped before they are counted.
            after.superContestRibbons shouldBe 0L
          }
        }

        /** The reporter caps itself and carries the rest to its next report. */
        test("a report wider than a client sends is refused whole") {
          runTest {
            val store =
                CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
            val id = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
            val session = playing(store, id)
            // A new character starts with the flags its region's new game sets.
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
         * A capture is a monster the engine already rolled. Rolling a second one over it changed
         * the nature the player saw the ball land on, its IVs, and, once in several thousand, the
         * fact that it was shiny at all.
         */
        test("a reported capture keeps the individual the client rolled") {
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
            granted.seed shouldBe 0x1234_5678
            granted.iVs.compress() shouldBe ivs.compress()
            granted.isShiny.shouldBeTrue()
            // The nature is read out of the seed, which is what makes carrying it worth the bytes.
            granted.nature shouldBe PokemonNature.entries[0x1234_5678 % PokemonNature.entries.size]
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
          val session = FakeSession(characterId = id, regionId = 3, bankId = 1, mapId = 158)

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
    })
