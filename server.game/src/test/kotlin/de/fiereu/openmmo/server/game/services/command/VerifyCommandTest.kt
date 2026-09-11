package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.net.game.packets.ChatMessagePacket
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.offline.verify.ReplayLimits
import de.fiereu.openmmo.server.game.offline.verify.ReplayOutcome
import de.fiereu.openmmo.server.game.offline.verify.ReplayRun
import de.fiereu.openmmo.server.game.offline.verify.ReplayRunner
import de.fiereu.openmmo.server.game.offline.verify.ReplayVerdict
import de.fiereu.openmmo.server.game.offline.verify.ReplayVerificationService
import de.fiereu.openmmo.server.game.offline.verify.SessionLink
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.ImportRecord
import de.fiereu.openmmo.server.game.storage.InMemoryChainRepository
import de.fiereu.openmmo.server.game.storage.InMemoryImportRepository
import de.fiereu.openmmo.server.game.storage.InMemoryRequestRepository
import de.fiereu.openmmo.server.game.storage.StoredChain
import de.fiereu.openmmo.server.game.storage.StoredLink
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldHaveSize
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain
import java.security.MessageDigest
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

private class VerifyFixture(scope: CoroutineScope) {
  val now: LocalDateTime = LocalDateTime.of(2026, 9, 4, 18, 0, 0)
  val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
  val chains = InMemoryChainRepository()
  val imports = InMemoryImportRepository()
  val requests = InMemoryRequestRepository()
  val ids = EntityIdService()
  val runner =
      object : ReplayRunner {
        override fun revisions(): Set<Int> = setOf(12)

        override fun run(run: ReplayRun): ReplayOutcome = ReplayOutcome.Broke("never run here")
      }
  /** No free allowance, so a fee shows. */
  val limits = ReplayLimits(freeHours = 0)
  val service =
      ReplayVerificationService(
          chains,
          imports,
          store,
          runner,
          limits,
          ids,
          blocking = Dispatchers.Unconfined,
          requests = requests,
          clock = { now })
  val command = VerifyCommand(service, SpeciesRegistry())
  val chat = ChatCommandService(store, setOf(command))

  fun sha(bytes: ByteArray): String =
      MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

  suspend fun player(name: String): Pair<FakeSession, Long> {
    val id = store.createCharacter(2, name, CharacterGender.FEMALE, Region.SINNOH).info.id
    return FakeSession(characterId = id) to id
  }

  fun monster(owner: Long, seed: Int, marked: Boolean) =
      Pokemon(
          id = ids.newMonsterId(),
          ownerId = owner,
          container = PokemonContainer.PARTY,
          containerSlot = 0,
          dexId = 387,
          seed = seed,
          ot = "Dawn",
          nickname = "Turty",
          level = 5,
          hp = 20,
          xp = 135,
          eVs = EVs(),
          iVs = IVs(),
          moves = listOf(PokemonMove(33, 35)),
          isShiny = false,
          hasHiddenAbility = false,
          isAlpha = false,
          isSecret = false,
          isFatefulEncounter = false,
          isRaidEncounter = false,
          caughtAt = now,
          heldItemId = 0,
          offlineOrigin = marked,
      )

  /** An import with a chain of [hours] of play on file behind it. */
  suspend fun onFile(characterId: Long, hours: Long = 1) {
    val importId = ids.newImportId()
    imports.record(
        ImportRecord(
            id = importId,
            characterId = characterId,
            importedAt = now,
            playTimeSeconds = 100,
            saveSha256 = "a".repeat(64),
            clientRevision = 12,
            trainerId = 1,
            partyCount = 1,
            boxCount = 0,
            speciesCount = 1,
            levelTotal = 5,
            levelMax = 5,
            moneyBefore = 0,
            moneyAfter = 0,
            badgesBefore = 0,
            badgesAfter = 0,
            verdicts = emptyList(),
            snapshotVersion = 1,
            replayVerdict = "HELD"),
        ByteArray(0))
    val recording = "0 keys none\n900 keys A\n906 keys none\n".toByteArray()
    val link =
        SessionLink(
            version = 1,
            revision = 12,
            rtc = now.minusHours(3),
            bootSha256 = null,
            recordingName = "session 1",
            recordingSha256 = sha(recording),
            quitSha256 = "a".repeat(64),
            endFrame = ReplayLimits.hours(hours),
        )
    chains.record(
        StoredChain(
            id = ids.newChainId(),
            characterId = characterId,
            importId = importId,
            uploadedAt = now,
            anchorSha256 = null,
            linkCount = 1,
            frameTotal = ReplayLimits.hours(hours),
            verifiedLink = -1,
            verdict = ReplayVerdict.HELD),
        listOf(StoredLink(link, recording)))
  }
}

/** Everything said since the last look, as one line: a long answer arrives in chunks. */
private fun FakeSession.said(): String {
  val text = sent.filterIsInstance<ChatMessagePacket>().joinToString(" ") { it.message }
  sent.clear()
  return text
}

@OptIn(ExperimentalCoroutinesApi::class)
class VerifyCommandTest :
    FunSpec({
      test("a party slot asks for that monster, names it, and says what it costs") {
        runTest {
          val fx = VerifyFixture(backgroundScope)
          val (session, who) = fx.player("Dawn")
          fx.onFile(who, hours = 2)
          fx.store.addPokemon(who, fx.monster(who, 0x1234, marked = true))
          fx.store.addMoney(who, 100_000)
          val before = checkNotNull(fx.store.getCharacter(who)).info.money

          fx.chat.tryHandle(session, "/verify 1") shouldBe true

          val reply = session.said()
          reply shouldContain "in slot 1 will be checked"
          reply shouldContain "2 hour(s)"
          reply shouldContain "for ¥${2 * fx.limits.feePerHour}"
          val asked = fx.requests.listFor(who, 5).single()
          asked.monsterPid shouldBe 0x1234
          asked.feePaid shouldBe 2 * fx.limits.feePerHour
          checkNotNull(fx.store.getCharacter(who)).info.money shouldBe
              before - 2 * fx.limits.feePerHour
        }
      }

      test("all asks for the whole chain, and a trusted monster has nothing to check") {
        runTest {
          val fx = VerifyFixture(backgroundScope)
          val (session, who) = fx.player("Lucas")
          fx.onFile(who)
          fx.store.addPokemon(who, fx.monster(who, 0x1234, marked = false))
          fx.store.addMoney(who, 100_000)

          fx.chat.tryHandle(session, "/verify 1") shouldBe true
          session.said() shouldContain "already trusted"
          fx.requests.listFor(who, 5) shouldHaveSize 0

          fx.chat.tryHandle(session, "/verify all") shouldBe true
          session.said() shouldContain "everything behind your save will be checked"
          fx.requests.listFor(who, 5).single().monsterPid shouldBe null
        }
      }

      test("nothing on file, a slot the party has not got, and no argument") {
        runTest {
          val fx = VerifyFixture(backgroundScope)
          val (session, who) = fx.player("Barry")
          fx.store.addPokemon(who, fx.monster(who, 0x1234, marked = true))

          fx.chat.tryHandle(session, "/verify") shouldBe true
          session.said() shouldContain "Usage:"
          fx.chat.tryHandle(session, "/verify 3") shouldBe true
          session.said() shouldContain "your party has 1 in it"
          fx.chat.tryHandle(session, "/verify 1") shouldBe true
          session.said() shouldContain "Not asked: no sessions of yours are on file"
        }
      }

      test("a check the player cannot pay for takes nothing and asks nothing") {
        runTest {
          val fx = VerifyFixture(backgroundScope)
          val (session, who) = fx.player("Broke")
          // Eight hours at five thousand an hour, against what a new character starts with.
          fx.onFile(who, hours = 8)
          fx.store.addPokemon(who, fx.monster(who, 0x1234, marked = true))

          fx.chat.tryHandle(session, "/verify 1") shouldBe true

          session.said() shouldContain "which you do not have"
          fx.requests.listFor(who, 5) shouldHaveSize 0
        }
      }
    })
