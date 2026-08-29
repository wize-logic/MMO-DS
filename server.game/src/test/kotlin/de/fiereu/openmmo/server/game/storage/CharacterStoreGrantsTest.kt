package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.common.MAX_PARTY_SIZE
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest

/** Where a grant lands, and what a balance does under two writers. */
@OptIn(ExperimentalCoroutinesApi::class)
class CharacterStoreGrantsTest :
    FunSpec({
      suspend fun CharacterStore.player(): Long =
          createCharacter(1, "Ash", CharacterGender.MALE, Region.HOENN).info.id

      test("a spend the balance cannot cover is refused whole") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.player()

          store.addMoney(id, -30_001) shouldBe false

          store.getCharacter(id)!!.info.money shouldBe 30_000
        }
      }

      test("an earning past the ceiling is clamped, and is still a payment that happened") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.player()

          store.addMoney(id, MONEY_MAX) shouldBe true

          store.getCharacter(id)!!.info.money shouldBe MONEY_MAX
        }
      }

      /**
       * The report a client sends when its own engine earned or spent is a *delta*, and this is
       * why.
       */
      test("a spend and an earning that overlap both land") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.player()

          listOf(async { store.addMoney(id, -20_000) }, async { store.addMoney(id, 1) })
              .awaitAll() shouldBe listOf(true, true)

          store.getCharacter(id)!!.info.money shouldBe 10_001
        }
      }

      test("a seventh party member is refused rather than seated where nothing can reach it") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.player()
          repeat(MAX_PARTY_SIZE) { store.addPokemon(id, monster(id)).shouldNotBeNull() }

          store.addPokemon(id, monster(id)) shouldBe null

          store.getCharacter(id)!!.pokemon.size shouldBe MAX_PARTY_SIZE
        }
      }

      test("the party seats in order, whatever slot the caller filled in") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.player()

          store.addPokemon(id, monster(id).copy(containerSlot = 5))!!.containerSlot shouldBe 0
          store.addPokemon(id, monster(id).copy(containerSlot = 5))!!.containerSlot shouldBe 1
        }
      }

      test("a box grant takes the slot it asks for, and the first free one when that is taken") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.player()
          val boxed = monster(id).copy(container = PokemonContainer.PC)

          store.addPokemon(id, boxed, preferredSlot = 12)!!.containerSlot shouldBe 12
          store.addPokemon(id, boxed.copy(id = 2), preferredSlot = 12)!!.containerSlot shouldBe 0
        }
      }

      /**
       * Two scenes granting at once used to be handed the same free slot, and the box screen drops
       * one of a pair that share one: it keys the container by slot before writing it back, so the
       * next thing the player drags deletes the monster that lost the collision.
       */
      test("two grants that arrive together are seated in different slots") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.player()
          val boxed = monster(id).copy(container = PokemonContainer.PC)

          val seated =
              listOf(
                      async { store.addPokemon(id, boxed, preferredSlot = 0) },
                      async { store.addPokemon(id, boxed.copy(id = 2), preferredSlot = 0) },
                  )
                  .awaitAll()

          seated.mapNotNull { it?.containerSlot?.toInt() }.toSet() shouldBe setOf(0, 1)
        }
      }

      test("a full box refuses a grant instead of seating it nowhere") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.player()
          val full =
              (0 until PC_STORAGE_SIZE).map {
                monster(id)
                    .copy(
                        id = it.toLong(),
                        container = PokemonContainer.PC,
                        containerSlot = it.toShort())
              }
          store.rearrangeMonsters(id) { party, _ -> party to full }

          store.addPokemon(id, monster(id).copy(container = PokemonContainer.PC)) shouldBe null
        }
      }

      /**
       * A rollback is there so that a scene stopped before its own story var was advanced cannot be
       * replayed on top of the rewards it already handed out.
       */
      test("an unfinished script gives back what it granted and keeps where it left the player") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val id = store.player()
          val before = store.getCharacter(id)!!

          store.addItem(id, itemId = 17, amount = 3)
          store.addMoney(id, 5_000)
          store.updatePosition(id, 42, 7, facing = Direction.LEFT)

          store.rollBackTo(id, before)

          val after = store.getCharacter(id)!!
          after.items[17] shouldBe null
          after.info.money shouldBe 30_000
          after.info.positionX shouldBe 42
          after.info.positionY shouldBe 7
          after.info.positionFacing shouldBe Direction.LEFT
        }
      }

      test("the unload writes the rollback, not the state it undid") {
        runTest {
          val repo = FakeCharacterRepository()
          val store = CharacterStore(repo, EntityIdService(), this)
          val id = store.player()
          val before = store.getCharacter(id)!!
          store.addItem(id, itemId = 17, amount = 3)

          store.unloadCharacterAsync(id, unfinishedScript = before)
          advanceUntilIdle()

          repo.saved[id]!!.items[17] shouldBe null
        }
      }
    })

private fun monster(ownerId: Long): Pokemon =
    Pokemon(
        id = EntityIdService().newMonsterId(),
        ownerId = ownerId,
        container = PokemonContainer.PARTY,
        containerSlot = 0,
        dexId = 19,
        seed = 0,
        ot = "Ash",
        nickname = "",
        level = 3,
        hp = 14,
        xp = 27,
        eVs = EVs(),
        iVs = IVs(),
        moves = listOf(),
        isShiny = false,
        hasHiddenAbility = false,
        isAlpha = false,
        isSecret = false,
        isFatefulEncounter = false,
        isRaidEncounter = false,
        caughtAt = LocalDateTime.now(),
    )
