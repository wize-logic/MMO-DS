package de.fiereu.openmmo.server.game.offline

import de.fiereu.openmmo.common.enums.Ability
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.PokemonStat
import de.fiereu.openmmo.common.enums.TileBehavior
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.offline.CartridgeLimits
import de.fiereu.openmmo.pokemon.EvolutionRegistry
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import de.fiereu.openmmo.pokemon.MoveSourceRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.ExpCurves
import de.fiereu.openmmo.server.game.services.ITEM_ID_BASE
import de.fiereu.openmmo.server.game.services.MAX_SAVE_BLOCK_IDS
import de.fiereu.openmmo.server.game.storage.PC_STORAGE_SIZE
import io.kotest.assertions.withClue
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldBeEmpty
import io.kotest.matchers.collections.shouldContain
import io.kotest.matchers.collections.shouldNotBeEmpty
import io.kotest.matchers.collections.shouldNotContain
import io.kotest.matchers.ints.shouldBeLessThanOrEqual
import io.kotest.matchers.nulls.shouldBeNull
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain as textShouldContain

/** The legality table, one test per row. */
class OfflineImportLegalityTest :
    FunSpec({
      val species = SpeciesRegistry()
      val learnsets = LearnsetRegistry()
      val moves = MoveRegistry()
      val maps = MapManager()
      val cartridge = CartridgeLimits()
      val door =
          OfflineImportLegality(
              species = species,
              learnsets = learnsets,
              moveSources = MoveSourceRegistry(),
              evolutions = EvolutionRegistry(),
              moves = moves,
              items = ItemRegistry(),
              maps = maps,
              cartridge = cartridge,
          )

      // Route 201, and the first tile of it anybody can stand on: a real place on a real map, so
      // the position row is measured against the world the server actually draws.
      val route201 = checkNotNull(maps.getMap(3, 1, 86))
      val standable =
          (0 until route201.height)
              .flatMap { y -> (0 until route201.width).map { x -> x to y } }
              .first { (x, y) ->
                val tile = route201.tileAt(x, y)
                tile != null &&
                    !tile.blocksMovement() &&
                    tile.behavior != TileBehavior.SURFABLE_WATER &&
                    tile.behavior != TileBehavior.WATERFALL
              }
      val here = OfflinePosition(3, 1, 86, standable.first, standable.second)
      val home = OfflinePosition(3, 1, 158, 5, 5)

      fun monster(
          dexId: Int = TURTWIG,
          pid: Int = 0x12345678,
          level: Int = 20,
          moveIds: List<Int> = listOf(TACKLE),
          slot: Int = 0,
          container: PokemonContainer = PokemonContainer.PARTY,
          ivs: Int = 10,
          evs: Int = 4,
          otId: Int = 0x00010002,
          nickname: String = "Turty",
      ) =
          OfflineMonster(
              pid = pid,
              dexId = dexId,
              form = 0,
              level = level,
              xp =
                  species.get(dexId)?.let {
                    ExpCurves.totalXpFor(it.growthRate, level.coerceIn(1, ExpCurves.MAX_LEVEL))
                  } ?: 0,
              ivs = PokemonStat.entries.associateWith { ivs },
              evs = PokemonStat.entries.associateWith { evs },
              moves = moveIds.map { OfflineMove(it, checkNotNull(moves.get(it)).pp, 0) },
              nickname = nickname,
              otName = "Barry",
              otId = otId,
              abilityId = species.get(dexId)?.ability1?.ordinal ?: 0,
              hasHiddenAbility = false,
              natureByte = pid.mod(NATURE_COUNT),
              isShiny = false,
              heldItemId = 0,
              friendship = 70,
              isEgg = false,
              eggCyclesLeft = 0,
              container = container,
              containerSlot = slot,
          )

      fun save(
          monsters: List<OfflineMonster> = listOf(monster()),
          money: Int = 1000,
          bag: List<OfflineItem> = emptyList(),
          badges: Int = 4,
          position: OfflinePosition = here,
          dexSeen: Set<Int> = setOf(TURTWIG),
          dexCaught: Set<Int> = setOf(TURTWIG),
          blocks: Map<Int, ByteArray> = emptyMap(),
          trainerId: Int = 0x00010002,
      ) =
          OfflineSave(
              trainerId = trainerId,
              monsters = monsters,
              money = money,
              bag = bag,
              badges = badges,
              dexSeen = dexSeen,
              dexCaught = dexCaught,
              position = position,
              playTimeSeconds = 3600,
              blocks = blocks,
          )

      val context = ImportContext(lastHealLocation = home)

      /** Runs the door and proves it read the save rather than rewriting it. */
      fun read(save: OfflineSave, ctx: ImportContext = context): LegalityResult {
        val before = save.copy()
        val result = door.check(save, ctx)
        save shouldBe before
        return result
      }

      fun only(result: LegalityResult) = checkNotNull(result.save).monsters.single()

      context("what the cartridge can produce") {
        test("a species this server has never heard of takes its monster with it") {
          val result = read(save(listOf(monster(dexId = 9999))))
          checkNotNull(result.save).monsters.shouldBeEmpty()
          result.verdicts
              .filterIsInstance<Dropped>()
              .single { it.what != "money" }
              .why textShouldContain "9999"
        }

        test("a species the cartridge cannot produce is dropped and named") {
          val result = read(save(listOf(monster(dexId = MEWTWO))))
          checkNotNull(result.save).monsters.shouldBeEmpty()
          result.verdicts
              .filterIsInstance<Dropped>()
              .single { it.what != "money" }
              .why textShouldContain "MEWTWO"
        }

        test("the same monster comes home when it left with the character's export") {
          val mewtwo = monster(dexId = MEWTWO, pid = 0xABCD)
          val result = read(save(listOf(mewtwo)), context.copy(exportedPids = setOf(mewtwo.pid)))
          only(result).dexId shouldBe MEWTWO
        }

        test("a form has no table to measure it against, so it is carried and written down") {
          val result = read(save(listOf(monster().copy(form = 1))))
          only(result).form shouldBe 1
          result.verdicts.filterIsInstance<Allowed>().map { it.what } shouldContain
              "#$TURTWIG Turty form"
        }
      }

      context("the numbers on one monster") {
        test("a level past a hundred is brought back to it") {
          only(read(save(listOf(monster(level = 255))))).level shouldBe ExpCurves.MAX_LEVEL
        }

        test("experience is recomputed from the level, whatever the file said") {
          val lying = monster(level = 20).copy(xp = 999_999)
          only(read(save(listOf(lying)))).xp shouldBe
              ExpCurves.totalXpFor(checkNotNull(species.get(TURTWIG)).growthRate, 20)
        }

        test("an IV past thirty-one is brought back to it") {
          only(read(save(listOf(monster(ivs = 99))))).ivs.values.forEach { it shouldBe 31 }
        }

        test("EVs come back inside the cartridge's own per stat and total ceilings") {
          val evs = only(read(save(listOf(monster(evs = 255))))).evs
          evs.values.forEach { it shouldBeLessThanOrEqual 252 }
          evs.values.sum() shouldBe 510
        }

        test("the nature is the personality value's, not the file's byte") {
          val result = read(save(listOf(monster().copy(natureByte = 3))))
          only(result).natureByte shouldBe 0x12345678.mod(NATURE_COUNT)
        }

        test("shininess is recomputed the cartridge's way from the pid and the trainer id") {
          // The two halves of the trainer id and the two halves of the personality value, exclusive
          // ored together, under eight. This pair folds to zero, and a file claiming otherwise does
          // not make it so either way.
          val shiny = monster(pid = 0x00010002, otId = 0x00010002).copy(isShiny = false)
          only(read(save(listOf(shiny)))).isShiny shouldBe true
          val plain = monster(pid = 0x12345678, otId = 0x00010002).copy(isShiny = true)
          only(read(save(listOf(plain)))).isShiny shouldBe false
        }

        test("a hidden ability is taken away and the pid's own ability put back") {
          val turtwig = checkNotNull(species.get(TURTWIG))
          turtwig.ability2 shouldBe Ability.NONE
          val result = read(save(listOf(monster().copy(hasHiddenAbility = true))))
          only(result).hasHiddenAbility shouldBe false
          only(result).abilityId shouldBe turtwig.ability1.ordinal
        }

        test("friendship comes back inside the range the record keeps it in") {
          only(read(save(listOf(monster().copy(friendship = 400))))).friendship shouldBe 255
        }

        test("an egg cannot have more cycles left than its species hatches in") {
          val egg = monster().copy(isEgg = true, eggCyclesLeft = 200)
          only(read(save(listOf(egg)))).eggCyclesLeft shouldBe
              checkNotNull(species.get(TURTWIG)).eggCycles
        }
      }

      context("moves") {
        test("a move the species cannot learn is dropped by name") {
          val result = read(save(listOf(monster(moveIds = listOf(TACKLE, HYDRO_PUMP)))))
          only(result).moves.map { it.moveId } shouldNotContain HYDRO_PUMP
          result.verdicts
              .filterIsInstance<Dropped>()
              .single { it.what != "money" }
              .what textShouldContain "Hydro Pump"
        }

        test("what it learned before it evolved is still its own") {
          // Raichu has no way at all to Agility: no level teaches it one, no machine, no tutor and
          // no egg. Pikachu learns it at 45, and a Raichu is a Pikachu that touched a stone.
          learnsets.get(RAICHU).map { it.moveId } shouldNotContain AGILITY
          val grown = monster(dexId = RAICHU, level = 50, moveIds = listOf(AGILITY))
          only(read(save(listOf(grown)))).moves.map { it.moveId } shouldContain AGILITY
        }

        test("a machine move is legal even though no level teaches it") {
          val result = read(save(listOf(monster(level = 50, moveIds = listOf(CUT)))))
          only(result).moves.map { it.moveId } shouldContain CUT
        }

        test("PP and PP Ups come back inside what the move can hold") {
          val greedy = monster().copy(moves = listOf(OfflineMove(TACKLE, 99, 9)))
          val move = only(read(save(listOf(greedy)))).moves.single()
          move.ppUps shouldBe 3
          move.pp shouldBe checkNotNull(moves.get(TACKLE)).pp * 160 / 100
        }

        test("a monster left with nothing gets its first move back rather than landing empty") {
          val result = read(save(listOf(monster(moveIds = listOf(HYDRO_PUMP)))))
          only(result).moves.single().moveId shouldBe learnsets.get(TURTWIG).first().moveId
        }
      }

      context("names and the trainer id") {
        test("a nickname longer than the cartridge's field is cut to it") {
          only(read(save(listOf(monster(nickname = "ABCDEFGHIJKLMNOP"))))).nickname shouldBe
              "ABCDEFGHIJ"
        }

        test("an OT name is cut to the cartridge's shorter field") {
          val long = monster().copy(otName = "Barrymore")
          only(read(save(listOf(long)))).otName shouldBe "Barrymo"
        }

        test("a name with nothing drawable left in it falls back rather than landing empty") {
          only(read(save(listOf(monster(nickname = ""))))).nickname shouldBe "TURTWIG"
        }

        test("a first import takes the save's trainer id; a later one keeps the one it adopted") {
          checkNotNull(read(save(trainerId = 4242)).save).trainerId shouldBe 4242
          // The only later import there can be is one that matches: a save carrying any other
          // trainer id is a different game and the identity row turns it away before this one is
          // read at all.
          val later = context.copy(firstImport = false, serverTrainerId = 4242)
          checkNotNull(read(save(trainerId = 4242), later).save).trainerId shouldBe 4242
        }
      }

      context("one of a kind") {
        test("the same monster twice keeps the first copy and drops the second") {
          val twice = listOf(monster(pid = 5, slot = 0), monster(pid = 5, slot = 1))
          val result = read(save(twice))
          checkNotNull(result.save).monsters.size shouldBe 1
          result.verdicts
              .filterIsInstance<Dropped>()
              .single { it.what != "money" }
              .why textShouldContain "second copy"
        }

        test("a monster that already lives on another character here is dropped") {
          val result = read(save(listOf(monster(pid = 5))), context.copy(pidsElsewhere = setOf(5)))
          checkNotNull(result.save).monsters.shouldBeEmpty()
        }

        test(
            "a monster boarding at this server's day care stays there; the file's copy is dropped") {
              val result =
                  read(save(listOf(monster(pid = 5))), context.copy(daycarePids = setOf(5)))
              checkNotNull(result.save).monsters.shouldBeEmpty()
              result.verdicts
                  .filterIsInstance<Dropped>()
                  .single { it.what != "money" }
                  .why textShouldContain "day care"
            }
      }

      context("the bag, the wallet and the badges") {
        test("an item this server does not have is dropped by name") {
          val result = read(save(bag = listOf(OfflineItem(60000, 1))))
          checkNotNull(result.save).bag.shouldBeEmpty()
          result.verdicts
              .filterIsInstance<Dropped>()
              .single { it.what != "money" }
              .what textShouldContain "60000"
        }

        test("what an edited file could be spent on does not cross") {
          // Each of these is cheap to earn again online and each is a way to turn an edited file
          // into something the mark cannot follow: a Rare Candy makes a level 100 out of a monster
          // this server rolled, and so the level 100 carries no mark at all.
          val fenced =
              listOf(
                  MASTER_BALL to "ball",
                  POTION to "medicine",
                  RARE_CANDY to "candy",
                  X_ATTACK to "battle item",
                  REPEL to "repel",
                  GRASS_MAIL to "mail",
                  CHERI_BERRY to "berry",
                  TM01 to "TM",
              )
          for ((itemId, what) in fenced) {
            val result = read(save(bag = listOf(OfflineItem(itemId, 5))))
            withClue(what) {
              checkNotNull(result.save).bag.shouldBeEmpty()
              result.verdicts.filterIsInstance<Dropped>().shouldNotBeEmpty()
            }
          }
        }

        test("equipment crosses, and an HM is equipment") {
          // HM01 sits one id past the last TM, so a band that overran by one would take the HMs
          // with it and strand a player who cannot Surf.
          val kept = listOf(HM01, EXPLORER_KIT, SUN_STONE)
          for (itemId in kept) {
            val result = read(save(bag = listOf(OfflineItem(itemId, 1))))
            withClue("$itemId") { checkNotNull(result.save).bag.single().itemId shouldBe itemId }
          }
        }

        test("each fenced band ends where the item table says the pocket does") {
          // The bands are cut out of the engine's own item list by index, and nothing at runtime
          // re-derives them. This is the loud failure if that list ever shifts under them: the
          // item at each end of each band, by the name the catalogue gives it.
          val catalogue = ItemRegistry()
          fun nameOf(id: Int) = checkNotNull(catalogue.get(id)) { "no item at $id" }.name
          nameOf(5001) shouldBe "Master Ball"
          nameOf(5016) shouldBe "Cherish Ball"
          nameOf(5017) shouldBe "Potion"
          nameOf(5054) shouldBe "Old Gateau"
          nameOf(5055) shouldBe "Guard Spec."
          nameOf(5069) shouldBe "White Flute"
          nameOf(5076) shouldBe "Super Repel"
          nameOf(5079) shouldBe "Repel"
          nameOf(5149) shouldBe "Cheri Berry"
          nameOf(5212) shouldBe "Rowap Berry"
          nameOf(5328) shouldBe "TM01"
          nameOf(5419) shouldBe "TM92"
          nameOf(5420) shouldBe "HM01"
        }

        test("a stack of a key item is one key item") {
          val result = read(save(bag = listOf(OfflineItem(EXPLORER_KIT, 99))))
          checkNotNull(result.save).bag.single().quantity shouldBe 1
        }

        test("an item the game has no way of handing out does not cross") {
          val result = read(save(bag = listOf(OfflineItem(AZURE_FLUTE, 1))))
          checkNotNull(result.save).bag.shouldBeEmpty()
          result.verdicts
              .filterIsInstance<Dropped>()
              .single { it.what != "money" }
              .why textShouldContain "hands one out"
        }

        test("an ordinary item crosses with the quantity it had") {
          val stones = OfflineItem(SUN_STONE, 12)
          checkNotNull(read(save(bag = listOf(stones))).save).bag.single().quantity shouldBe 12
        }

        /* Money does not cross at all (owner's call, 2026-09-06). */
        test("money stays in the offline game, whatever the file says") {
          checkNotNull(read(save(money = 99_999_999)).save).money shouldBe 0
          checkNotNull(read(save(money = 1)).save).money shouldBe 0
          val verdict =
              read(save(money = 4_242)).verdicts.filterIsInstance<Dropped>().single {
                it.what == "money"
              }
          verdict.why textShouldContain "4242"
          verdict.why textShouldContain "offline game"
        }

        test("a save with nothing in its wallet says nothing about money") {
          read(save(money = 0)).verdicts.none { it.what == "money" } shouldBe true
        }

        // A bitfield, not a count: the import writes one story flag per badge and a count
        // cannot say which. Bits above the eighth belong to no badge at all.
        test("bits above the eight badges there are do not cross") {
          checkNotNull(read(save(badges = 0xFF)).save).badges shouldBe 0xFF
          checkNotNull(read(save(badges = 0xFFFF)).save).badges shouldBe 0xFF
          checkNotNull(read(save(badges = 0b1010)).save).badges shouldBe 0b1010
        }

        test("a held item this server does not have is dropped, the monster is not") {
          only(read(save(listOf(monster().copy(heldItemId = 60000))))).heldItemId shouldBe 0
        }
      }

      context("the Pokedex, the place and the blocks") {
        test("caught implies seen, and a bit for nothing is not a species") {
          val result =
              read(save(dexSeen = setOf(TURTWIG), dexCaught = setOf(TURTWIG, 9999, MEWTWO)))
          val landed = checkNotNull(result.save)
          landed.dexCaught shouldBe setOf(TURTWIG, MEWTWO)
          landed.dexSeen shouldContain MEWTWO
        }

        test("a place the server cannot draw becomes the last place the character healed") {
          checkNotNull(read(save(position = OfflinePosition(3, 99, 99, 1, 1))).save)
              .position shouldBe home
        }

        test("a map out of another cartridge becomes the last place the character healed") {
          // Azalea Town, a real drawable map on a real standable tile, and one no offline save
          // was ever written on, because it is a ported header (594 and up) and the offline game
          // is this cartridge's own region.
          val azalea = checkNotNull(maps.getMap(3, 2, 156))
          azalea.ported shouldBe true
          val tile =
              (0 until azalea.height)
                  .flatMap { y -> (0 until azalea.width).map { x -> x to y } }
                  .first { (x, y) ->
                    val t = azalea.tileAt(x, y)
                    t != null &&
                        !t.blocksMovement() &&
                        t.behavior != TileBehavior.SURFABLE_WATER &&
                        t.behavior != TileBehavior.WATERFALL
                  }
          val there = OfflinePosition(3, 2, 156, tile.first, tile.second)

          checkNotNull(read(save(position = there)).save).position shouldBe home
        }

        test("a tile nobody can stand on becomes the last place the character healed") {
          val offMap = here.copy(x = route201.width + 10)
          checkNotNull(read(save(position = offMap)).save).position shouldBe home
        }

        test("a place they really were is left where it was") {
          checkNotNull(read(save(position = here)).save).position shouldBe here
        }

        test("play time is written down and gated on nothing") {
          read(save()).verdicts.filterIsInstance<Allowed>().map { it.what } shouldContain
              "play time"
        }

        test("the trainer id block crosses on the first import and stays behind on any other") {
          val blocks = mapOf(TRAINER_ID_BLOCK to ByteArray(4))
          checkNotNull(read(save(blocks = blocks)).save).blocks.keys shouldBe
              setOf(TRAINER_ID_BLOCK)

          val later =
              read(
                  save(blocks = blocks),
                  context.copy(firstImport = false, serverTrainerId = 0x00010002))

          checkNotNull(later.save).blocks.keys.shouldBeEmpty()
          later.verdicts
              .filterIsInstance<Dropped>()
              .single { it.what == "save block $TRAINER_ID_BLOCK" }
              .why textShouldContain "keeps"
        }

        test("the blocks the download page promises stay behind do not cross; the Pokedex does") {
          val blocks =
              mapOf(
                  POKEDEX_BLOCK to ByteArray(1),
                  DAYCARE_BLOCK to ByteArray(1),
                  UNDERGROUND_BLOCK to ByteArray(1),
                  POFFINS_BLOCK to ByteArray(1),
                  FRONTIER_BLOCK to ByteArray(1),
                  OPTIONS_BLOCK to ByteArray(1),
              )
          val result = read(save(blocks = blocks))
          checkNotNull(result.save).blocks.keys shouldBe setOf(POKEDEX_BLOCK)
          // The block holds whole monsters, and a player who left one there is told where it
          // went.
          result.verdicts
              .filterIsInstance<Dropped>()
              .single { it.what == "save block $DAYCARE_BLOCK" }
              .why textShouldContain "withdraw"
        }

        test("a block wider than any this game writes is dropped rather than carried") {
          // The file's own format says a block's length in sixteen bits, so an edited save can
          // hand over sixty-four kilobytes of one.
          val blocks = mapOf(POKEDEX_BLOCK to ByteArray(0xFFFF), JOURNAL_BLOCK to ByteArray(8))
          val result = read(save(blocks = blocks))

          checkNotNull(result.save).blocks.keys shouldBe setOf(JOURNAL_BLOCK)
          result.verdicts
              .filterIsInstance<Dropped>()
              .single { it.what == "save block $POKEDEX_BLOCK" }
              .why textShouldContain "65535 bytes"
        }

        test("more block ids than a character may hold are clamped to what fits") {
          val blocks = (100..149).associateWith { ByteArray(8) }
          val result = read(save(blocks = blocks))
          val kept = checkNotNull(result.save).blocks

          // The five that never cross are still the character's afterwards, so the room the file
          // is given is the ceiling less those.
          kept.keys shouldBe (100 until 100 + MAX_SAVE_BLOCK_IDS - 5).toSet()
          val clamp =
              result.verdicts.filterIsInstance<Clamped>().single { it.what == "save blocks" }
          clamp.to shouldBe "${MAX_SAVE_BLOCK_IDS - 5}"
        }
      }

      context("the structural rows, the only ones that turn a save away") {
        test("a party of seven is a save this game did not write") {
          val result = read(save((0..6).map { monster(pid = it, slot = it) }))
          result.save.shouldBeNull()
          checkNotNull(result.refusal).what shouldBe "party"
        }

        test("a party slot the party does not have is refused") {
          val stray = monster(slot = 9)
          checkNotNull(read(save(listOf(stray))).refusal).what shouldBe "party"
        }

        test("a monster past the end of the storage is refused") {
          val past = monster(pid = 9, container = PokemonContainer.PC, slot = PC_STORAGE_SIZE)
          checkNotNull(read(save(listOf(monster(), past))).refusal).what shouldBe "boxes"
        }

        test("two monsters in one slot is refused") {
          val two = listOf(monster(pid = 1, slot = 0), monster(pid = 2, slot = 0))
          checkNotNull(read(save(two)).refusal).what shouldBe "storage"
        }

        /* Whose game the file is, which is the one thing the rest of the table does not ask. */
        test("a save played as another trainer is not this character's game") {
          val ctx = context.copy(serverTrainerId = 0x00010002)
          val result = read(save(trainerId = 0x0BAD0BAD), ctx)
          result.save.shouldBeNull()
          val refusal = checkNotNull(result.refusal)
          refusal.what shouldBe OfflineImportLegality.WHOSE_GAME
          refusal.why textShouldContain "a new character"
        }

        test("the same trainer id is this character coming home") {
          val ctx = context.copy(serverTrainerId = 0x00010002)
          read(save(trainerId = 0x00010002), ctx).refusal.shouldBeNull()
        }

        test("a save holding none of the character's own Pokemon is not its game") {
          // No trainer id here yet, a character that has played online and never imported has
          // none, so what says the save is its own is the Pokemon the record already holds.
          val ctx = context.copy(exportedPids = setOf(0x1111, 0x2222))
          val result = read(save(listOf(monster(pid = 0x9999))), ctx)
          result.save.shouldBeNull()
          checkNotNull(result.refusal).what shouldBe OfflineImportLegality.WHOSE_GAME
        }

        test("one Pokemon in common is enough: it is the same game, played on") {
          val ctx = context.copy(exportedPids = setOf(0x1111, 0x2222))
          val kept = listOf(monster(pid = 0x1111), monster(pid = 0x3333, slot = 1))
          read(save(kept), ctx).refusal.shouldBeNull()
        }

        test("a character with no game of its own takes it: the offline-first arrival") {
          // Nothing to test identity against, and nothing that could: this is a player who played
          // Sinnoh with no server and is coming online for the first time. Allowed, and what it
          // brings stays marked, a chain anchored on somebody else's export never verifies.
          read(save(), context).refusal.shouldBeNull()
        }

        test("a refusal lands nothing and leaves the save it was handed exactly as it was") {
          val original = save((0..6).map { monster(pid = it, slot = it) })
          val result = read(original)
          result.accepted shouldBe false
          original.monsters.size shouldBe 7
        }

        test("an ordinary save with several things wrong in it is not refused") {
          val result = read(save(listOf(monster(level = 900, ivs = 99, evs = 255))))
          result.refusal.shouldBeNull()
          result.save.shouldNotBeNull()
        }
      }
    }) {
  private companion object {
    const val TURTWIG = 387
    const val RAICHU = 26
    const val MEWTWO = 150

    const val NATURE_COUNT = 25

    const val TACKLE = 33
    const val AGILITY = 97
    const val CUT = 15
    const val HYDRO_PUMP = 56

    /** Item ids on this wire: the engine's own index plus the block base. */
    const val POTION = ITEM_ID_BASE + 17
    const val EXPLORER_KIT = ITEM_ID_BASE + 428
    const val AZURE_FLUTE = ITEM_ID_BASE + 455

    /** One from each band that does not cross, and one evolution stone that does. */
    const val MASTER_BALL = ITEM_ID_BASE + 1
    const val RARE_CANDY = ITEM_ID_BASE + 50
    const val X_ATTACK = ITEM_ID_BASE + 57
    const val REPEL = ITEM_ID_BASE + 79
    const val GRASS_MAIL = ITEM_ID_BASE + 137
    const val CHERI_BERRY = ITEM_ID_BASE + 149
    const val TM01 = ITEM_ID_BASE + 328
    const val HM01 = ITEM_ID_BASE + 420
    const val SUN_STONE = ITEM_ID_BASE + 80

    /** `save_table.h` ids, and the id the mod had to invent for the settings screen. */
    const val POKEDEX_BLOCK = 7
    const val JOURNAL_BLOCK = 18
    const val DAYCARE_BLOCK = 8
    const val UNDERGROUND_BLOCK = 12
    const val POFFINS_BLOCK = 16
    const val FRONTIER_BLOCK = 23
    const val OPTIONS_BLOCK = 0xC0
    const val TRAINER_ID_BLOCK = OfflineImportLegality.TRAINER_ID_BLOCK
  }
}
