package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.net.game.packets.DialogOptionPacket
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleSideAddPokemonPacket
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

private const val POTION = 5017
private const val POKE_BALL = 5004

private class UseFixture(scope: CoroutineScope) {
  val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
  val service = ItemUseService(store, ItemRegistry(), SpeciesRegistry())

  suspend fun trainer(hp: Short = 1): Triple<FakeSession, Long, Long> {
    val created = store.createCharacter(1, "Ash", CharacterGender.MALE, Region.HOENN)
    val mon =
        Pokemon(
            id = EntityIdService().newMonsterId(),
            ownerId = created.info.id,
            container = PokemonContainer.PARTY,
            containerSlot = 0,
            dexId = 1,
            seed = 0,
            ot = "Ash",
            nickname = "",
            level = 50,
            hp = hp,
            xp = 0,
            eVs = EVs(),
            iVs = IVs(),
            moves = listOf(PokemonMove(33, 35)),
            isShiny = false,
            hasHiddenAbility = false,
            isAlpha = false,
            isSecret = false,
            isFatefulEncounter = false,
            isRaidEncounter = false,
            caughtAt = LocalDateTime.now(),
        )
    store.addPokemon(created.info.id, mon)
    store.addItem(created.info.id, POTION, 1)
    return Triple(FakeSession(created.info.id), created.info.id, mon.id)
  }

  suspend fun use(session: FakeSession, itemId: Int, target: Long) {
    service.onUse(PacketEvent(DialogOptionPacket(itemId, target), session))
  }

  suspend fun hp(charId: Long): Int = store.getCharacter(charId)!!.pokemon.single().hp.toInt()

  suspend fun held(charId: Long, id: Int): Int = store.getCharacter(charId)!!.items[id] ?: 0
}

@OptIn(ExperimentalCoroutinesApi::class)
class ItemUseServiceTest :
    FunSpec({
      test("a Potion heals twenty and spends the stack") {
        runTest {
          val fx = UseFixture(backgroundScope)
          val (session, charId, monId) = fx.trainer(hp = 1)

          fx.use(session, POTION, monId)

          fx.hp(charId) shouldBe 21
          fx.held(charId, POTION) shouldBe 0
          session.sent
              .filterIsInstance<PokemonContainerPacket>()
              .single()
              .pokemon
              .single()
              .hp shouldBe 21
          val bag = session.sent.filterIsInstance<BattleSideAddPokemonPacket>().single()
          bag.pokemon.frontSpriteId shouldBe POTION.toShort()
          bag.pokemon.backSpriteId shouldBe 0
        }
      }

      test("a full bar is left alone and the Potion stays in the bag") {
        runTest {
          val fx = UseFixture(backgroundScope)
          val (session, charId, monId) = fx.trainer(hp = 105)

          fx.use(session, POTION, monId)

          fx.hp(charId) shouldBe 105
          fx.held(charId, POTION) shouldBe 1
          session.sent.filterIsInstance<PokemonContainerPacket>() shouldBe emptyList()
        }
      }

      test("a ball is not applied outside battle") {
        runTest {
          val fx = UseFixture(backgroundScope)
          val (session, charId, monId) = fx.trainer()
          fx.store.addItem(charId, POKE_BALL, 1)

          fx.use(session, POKE_BALL, monId)

          fx.hp(charId) shouldBe 1
          fx.held(charId, POKE_BALL) shouldBe 1
          session.sent.shouldNotBeNull()
          session.sent.filterIsInstance<PokemonContainerPacket>() shouldBe emptyList()
        }
      }
    })
