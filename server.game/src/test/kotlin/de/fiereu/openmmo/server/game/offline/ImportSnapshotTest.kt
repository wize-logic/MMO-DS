package de.fiereu.openmmo.server.game.offline

import de.fiereu.openmmo.common.CharacterInfo
import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.DynamicWarp
import de.fiereu.openmmo.common.HealLocation
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.Skin
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.SkinSlot
import de.fiereu.openmmo.server.game.storage.StoredCharacter
import io.kotest.assertions.throwables.shouldThrow
import io.kotest.assertions.withClue
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime

/** The copy an import keeps of the character it replaced. */
class ImportSnapshotTest :
    FunSpec({
      val stamp = LocalDateTime.of(2026, 9, 4, 12, 34, 56, 123_000_000)

      fun monster(id: Long, container: PokemonContainer, slot: Int) =
          Pokemon(
              id = id,
              ownerId = 7L,
              container = container,
              containerSlot = slot.toShort(),
              dexId = 387,
              seed = -0x12345678,
              ot = "Barry",
              nickname = "Turty",
              level = 47,
              hp = 133,
              xp = 98_765,
              eVs = EVs().also { it.hp = 252 },
              iVs = IVs().also { it.spd = 31 },
              moves = listOf(PokemonMove(33, 35), PokemonMove(75, 25)),
              isShiny = true,
              hasHiddenAbility = true,
              isAlpha = true,
              isSecret = true,
              isFatefulEncounter = true,
              isRaidEncounter = true,
              caughtAt = stamp,
              isEgg = true,
              form = 1,
              conditions =
                  ContestConditions(cool = 11, beauty = 22, cute = 33, smart = 44, tough = 55),
              sheen = 66,
              superContestRibbons = 0x0123_4567_89AB_CDEFL,
              caughtRegionId = 3,
              caughtBankId = 1,
              caughtMapId = 86,
              caughtLocationLabel = 91,
              friendship = 199,
              heldItemId = 5217,
              status = 0x88,
              offlineOrigin = true,
          )

      val character =
          StoredCharacter(
              info =
                  CharacterInfo(
                      id = 7L,
                      name = "Probe",
                      namePrefix = "Sir",
                      userId = 42,
                      rivalSex = 1.toByte(),
                      skinRegionSelectionIndex = 2,
                      lastLogin = stamp,
                      createdAt = stamp.minusDays(30),
                      money = 123_456,
                      permissions = 3,
                      remainingSafariSteps = 321,
                      remainingSafariBalls = 12.toByte(),
                      pcExtraSlots = 4.toByte(),
                      battleBoxExtraSlots = 5.toByte(),
                      templateAmount = 6.toByte(),
                      positionRegionId = 3.toByte(),
                      positionBankId = 1.toByte(),
                      positionMapId = 86.toByte(),
                      positionX = 1234,
                      positionY = 4321,
                      positionFacing = Direction.LEFT,
                      repelLeft = 99,
                      repelItemId = 5076,
                      lureLeft = 88,
                      lureItemId = 5077,
                      registeredItem = 5222,
                      dynamicWarp = DynamicWarp(3, 2, 4, 10, 11, Direction.RIGHT),
                      lastHealLocation = HealLocation(3, 1, 158.toByte(), 5, 5),
                  ),
              pokemon = mutableListOf(monster(101L, PokemonContainer.PARTY, 0)),
              pcStorage = mutableListOf(monster(102L, PokemonContainer.PC, 3)),
              daycare = mutableListOf(monster(103L, PokemonContainer.DAYCARE, 0)),
              items = mutableMapOf(5217 to 3, 5420 to 1),
              storyFlags = mutableSetOf("sinnoh/BADGE_ID_COAL", "sinnoh/DEX_SEEN_387"),
              storyVars = mutableMapOf("sinnoh/LEAGUE_VICTORIES" to 2),
              skins = mapOf(SkinSlot.HAIR to Skin(SkinSlot.HAIR, 7u, 3u)),
          )

      val blocks =
          mapOf(0xC2 to byteArrayOf(1, 2, 3, -1, 0), 0xCB to ByteArray(300) { it.toByte() })

      test("a fully populated character comes back exactly as it went in") {
        val copy =
            ImportSnapshot.decode(
                ImportSnapshot.encode(CharacterCopy(character, blocks)), ImportSnapshot.VERSION)

        copy.character.info shouldBe character.info
        copy.character.pokemon shouldBe character.pokemon
        copy.character.pcStorage shouldBe character.pcStorage
        copy.character.daycare shouldBe character.daycare
        copy.character.items shouldBe character.items
        copy.character.storyFlags shouldBe character.storyFlags
        copy.character.storyVars shouldBe character.storyVars
        copy.character.skins shouldBe character.skins
        copy.blocks.keys shouldBe blocks.keys
        for ((id, data) in blocks) copy.blocks.getValue(id).toList() shouldBe data.toList()
      }

      test("the stats come back stat for stat, not as a total") {
        val spread = EVs().also { it.hp = 252 }
        val monsterWith =
            monster(104L, PokemonContainer.PARTY, 1)
                .copy(eVs = spread, iVs = IVs().also { it.atk = 7 })
        val one = character.copy(pokemon = mutableListOf(monsterWith))

        val back =
            ImportSnapshot.decode(
                ImportSnapshot.encode(CharacterCopy(one, emptyMap())), ImportSnapshot.VERSION)

        back.character.pokemon.single().eVs.hp shouldBe 252
        back.character.pokemon.single().eVs.atk shouldBe 0
        back.character.pokemon.single().iVs.atk shouldBe 7
      }

      test("a blob written by a version this server does not have is refused, not read") {
        val blob = ImportSnapshot.encode(CharacterCopy(character, emptyMap()))

        shouldThrow<ImportSnapshot.UnreadableSnapshot> {
          ImportSnapshot.decode(blob, ImportSnapshot.VERSION + 1)
        }
      }

      test("a blob that stops short of its trailer is refused") {
        val blob = ImportSnapshot.encode(CharacterCopy(character, emptyMap()))

        shouldThrow<Exception> {
          ImportSnapshot.decode(blob.copyOf(blob.size - 8), ImportSnapshot.VERSION)
        }
      }

      // The gate that outlives everything above. A new field on one of these classes is a field the
      // codec has to be taught, and a count that no longer matches is the only thing that says so
      // before a rollback quietly drops it.
      test("every class the codec writes by hand still has the fields it was written for") {
        withClue("CharacterInfo") { fieldsOf(CharacterInfo::class.java) shouldBe 28 }
        withClue("Pokemon") { fieldsOf(Pokemon::class.java) shouldBe 35 }
        withClue("StoredCharacter") { fieldsOf(StoredCharacter::class.java) shouldBe 8 }
        withClue("PokemonMove") { fieldsOf(PokemonMove::class.java) shouldBe 2 }
        withClue("ContestConditions") { fieldsOf(ContestConditions::class.java) shouldBe 5 }
        withClue("DynamicWarp") { fieldsOf(DynamicWarp::class.java) shouldBe 6 }
        withClue("HealLocation") { fieldsOf(HealLocation::class.java) shouldBe 5 }
        withClue("Skin") { fieldsOf(Skin::class.java) shouldBe 3 }
      }
    })

private fun fieldsOf(type: Class<*>): Int =
    type.declaredFields.count {
      !it.isSynthetic && !java.lang.reflect.Modifier.isStatic(it.modifiers)
    }
