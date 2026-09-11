package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.common.auth.AccountRoles
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.net.game.packets.ChatMessagePacket
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.BattleRegistry
import de.fiereu.openmmo.server.game.battle.WildMonFactory
import de.fiereu.openmmo.server.game.services.PokemonStorageService
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldHaveSize
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

private class MovesFixture(scope: CoroutineScope) {
  val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
  val species = SpeciesRegistry()
  val moves = MoveRegistry()
  val learnsets = LearnsetRegistry()
  val storage = PokemonStorageService(store, BattleRegistry())
  val party =
      PartyCommand(
          species, WildMonFactory(species, moves, learnsets, EntityIdService()), store, storage)
  val command = MovesCommand(species, moves, learnsets, store, storage)
  val service = ChatCommandService(store, setOf(party, command))

  suspend fun developer(): Pair<FakeSession, Long> {
    val id = store.createCharacter(1, "Steven", CharacterGender.MALE, Region.SINNOH).info.id
    return FakeSession(characterId = id, roles = AccountRoles.of(AccountRole.DEVELOPER)) to id
  }

  fun party(id: Long) = store.getCharacter(id)!!.pokemon
}

private fun FakeSession.replies() = sent.filterIsInstance<ChatMessagePacket>().map { it.message }

@OptIn(ExperimentalCoroutinesApi::class)
class MovesCommandTest :
    FunSpec({
      test("gen5 gives every party monster four moves Platinum never had, with their PP") {
        runTest {
          val fx = MovesFixture(backgroundScope)
          val (session, id) = fx.developer()
          fx.service.tryHandle(session, "/party snivy 50") shouldBe true
          fx.service.tryHandle(session, "/party pikachu 50") shouldBe true

          fx.service.tryHandle(session, "/moves all gen5") shouldBe true

          val party = fx.party(id)
          party shouldHaveSize 2
          party.forEach { mon ->
            mon.moves shouldHaveSize 4
            mon.moves.forEach { m ->
              (m.id > 467) shouldBe true
              m.pp shouldBe fx.moves.get(m.id.toInt())!!.pp.toByte()
            }
            mon.moves.map { it.id }.distinct() shouldHaveSize 4
          }
          session.sent
              .filterIsInstance<PokemonContainerPacket>()
              .filter { it.container == PokemonContainer.PARTY }
              .last()
              .pokemon
              .first()
              .moves
              .all { it.id > 467 } shouldBe true
        }
      }

      test("named moves land in the slot asked, and an unknown name teaches nothing") {
        runTest {
          val fx = MovesFixture(backgroundScope)
          val (session, id) = fx.developer()
          fx.service.tryHandle(session, "/party snivy 50") shouldBe true
          val before = fx.party(id).single().moves

          fx.service.tryHandle(session, "/moves 1 scald hone-claws") shouldBe true
          val after = fx.party(id).single().moves
          after[0].id shouldBe 503.toShort()
          after[1].id shouldBe 468.toShort()
          after[2].id shouldBe 0.toShort()

          fx.service.tryHandle(session, "/moves 1 missingno") shouldBe true
          fx.party(id).single().moves shouldBe after
          session.replies().last() shouldContain "No move called"
          before shouldHaveSize 4
        }
      }
    })
