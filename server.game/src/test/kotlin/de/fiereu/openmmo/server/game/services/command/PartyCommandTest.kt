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

private class PartyFixture(scope: CoroutineScope) {
  val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
  val species = SpeciesRegistry()
  val command =
      PartyCommand(
          species,
          WildMonFactory(species, MoveRegistry(), LearnsetRegistry(), EntityIdService()),
          store,
          PokemonStorageService(store, BattleRegistry()),
      )
  val service = ChatCommandService(store, setOf(command))

  suspend fun developer(): Pair<FakeSession, Long> {
    val id = store.createCharacter(1, "Steven", CharacterGender.MALE, Region.SINNOH).info.id
    return FakeSession(characterId = id, roles = AccountRoles.of(AccountRole.DEVELOPER)) to id
  }

  fun party(id: Long) = store.getCharacter(id)!!.pokemon
}

private fun FakeSession.replies() = sent.filterIsInstance<ChatMessagePacket>().map { it.message }

@OptIn(ExperimentalCoroutinesApi::class)
class PartyCommandTest :
    FunSpec({
      test("a named species goes in the first free party slot at the level asked") {
        runTest {
          val fx = PartyFixture(backgroundScope)
          val (session, id) = fx.developer()

          fx.service.tryHandle(session, "/party snivy 25") shouldBe true

          val party = fx.party(id)
          party shouldHaveSize 1
          party.single().dexId shouldBe 495
          party.single().level shouldBe 25
          party.single().container shouldBe PokemonContainer.PARTY
          party.single().containerSlot shouldBe 0.toShort()
          session.replies().single() shouldContain "SNIVY (slot 1)"
        }
      }

      test("random with a count fills the party with Gen 5 species and resends it") {
        runTest {
          val fx = PartyFixture(backgroundScope)
          val (session, id) = fx.developer()

          fx.service.tryHandle(session, "/party random 6 50") shouldBe true

          val party = fx.party(id)
          party shouldHaveSize 6
          party.forEach { it.dexId shouldBe (it.dexId.coerceIn(494, 649)) }
          party.map { it.containerSlot.toInt() } shouldBe (0..5).toList()
          party.forEach { it.level shouldBe 50 }
          session.sent
              .filterIsInstance<PokemonContainerPacket>()
              .filter { it.container == PokemonContainer.PARTY }
              .single()
              .pokemon shouldHaveSize 6
        }
      }

      test("a seventh is refused and the reply says so") {
        runTest {
          val fx = PartyFixture(backgroundScope)
          val (session, id) = fx.developer()

          fx.service.tryHandle(session, "/party random 6") shouldBe true
          fx.service.tryHandle(session, "/party pikachu") shouldBe true

          fx.party(id) shouldHaveSize 6
          session.replies().last() shouldContain "full"
        }
      }

      test("an unknown name is refused without touching the party") {
        runTest {
          val fx = PartyFixture(backgroundScope)
          val (session, id) = fx.developer()

          fx.service.tryHandle(session, "/party missingno") shouldBe true

          fx.party(id) shouldHaveSize 0
          session.replies().single() shouldContain "No species called"
        }
      }
    })
