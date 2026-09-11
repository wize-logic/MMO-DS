package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.common.auth.AccountRoles
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.net.game.packets.ChatMessagePacket
import de.fiereu.openmmo.server.game.offline.verify.ReplayLimits
import de.fiereu.openmmo.server.game.offline.verify.ReplayOutcome
import de.fiereu.openmmo.server.game.offline.verify.ReplayRun
import de.fiereu.openmmo.server.game.offline.verify.ReplayRunner
import de.fiereu.openmmo.server.game.offline.verify.ReplayVerdict
import de.fiereu.openmmo.server.game.offline.verify.ReplayVerificationService
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.ImportRecord
import de.fiereu.openmmo.server.game.storage.InMemoryChainRepository
import de.fiereu.openmmo.server.game.storage.InMemoryImportRepository
import de.fiereu.openmmo.server.game.storage.InMemoryRequestRepository
import de.fiereu.openmmo.server.game.storage.StoredChain
import de.fiereu.openmmo.server.game.storage.StoredRequest
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

private class RequeueFixture(scope: CoroutineScope) {
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
  val service =
      ReplayVerificationService(
          chains,
          imports,
          store,
          runner,
          ReplayLimits(),
          ids,
          blocking = Dispatchers.Unconfined,
          requests = requests,
          clock = { now })
  val command = RequeueImportCommand(service, requests, chains, store)
  val chat = ChatCommandService(store, setOf(command))

  suspend fun moderator(): FakeSession {
    val id = store.createCharacter(1, "Mod", CharacterGender.MALE, Region.SINNOH).info.id
    return FakeSession(characterId = id, roles = AccountRoles.of(AccountRole.MODERATOR))
  }

  suspend fun player(name: String): Long =
      store.createCharacter(2, name, CharacterGender.FEMALE, Region.SINNOH).info.id

  /** An import, the chain behind it, and one settled check on that chain. */
  suspend fun settled(
      characterId: Long,
      verdict: ReplayVerdict,
      at: LocalDateTime = now,
      requeuedBy: String? = null,
  ): StoredRequest {
    val importId = ids.newImportId()
    imports.record(
        ImportRecord(
            id = importId,
            characterId = characterId,
            importedAt = at,
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
            replayVerdict = verdict.name),
        ByteArray(0))
    val chainId = ids.newChainId()
    chains.record(
        StoredChain(
            id = chainId,
            characterId = characterId,
            importId = importId,
            uploadedAt = at,
            anchorSha256 = null,
            linkCount = 2,
            frameTotal = 2000,
            verifiedLink = 0,
            verdict = ReplayVerdict.HELD),
        emptyList())
    val request =
        StoredRequest(
            id = ids.newRequestId(),
            chainId = chainId,
            characterId = characterId,
            monsterPid = null,
            requestedAt = at,
            frameBudget = 1000,
            freeFramesLeft = 0,
            feePaid = 5000,
            feeRefunded = if (verdict == ReplayVerdict.INCONCLUSIVE) 5000 else 0,
            framesRun = 1000,
            verdict = verdict,
            verdictReason = if (verdict == ReplayVerdict.DIVERGED) "session 2" else null,
            stoppedLink = 1,
            settledAt = at.plusMinutes(5),
            requeuedAt = if (requeuedBy != null) at.plusMinutes(6) else null,
            requeuedBy = requeuedBy,
        )
    requests.record(request)
    return request
  }
}

private fun FakeSession.replies() = sent.filterIsInstance<ChatMessagePacket>().map { it.message }

@OptIn(ExperimentalCoroutinesApi::class)
class RequeueImportCommandTest :
    FunSpec({
      test("the newest check that can go back is put back, and the import row says so") {
        runTest {
          val fx = RequeueFixture(backgroundScope)
          val mod = fx.moderator()
          val who = fx.player("Dawn")
          val older = fx.settled(who, ReplayVerdict.INCONCLUSIVE, fx.now.minusDays(2))
          val newer = fx.settled(who, ReplayVerdict.DIVERGED, fx.now.minusDays(1))

          fx.chat.tryHandle(mod, "/requeue-import dawn") shouldBe true

          mod.replies().single() shouldContain "Check ${newer.id} of Dawn's"
          mod.replies().single() shouldContain "was DIVERGED"
          mod.replies().single() shouldContain "from session 2"
          checkNotNull(fx.requests.find(newer.id)).verdict shouldBe ReplayVerdict.PENDING
          checkNotNull(fx.requests.find(newer.id)).requeuedBy shouldBe "Mod"
          checkNotNull(fx.requests.find(older.id)).verdict shouldBe ReplayVerdict.INCONCLUSIVE
          val chain = checkNotNull(fx.chains.find(newer.chainId))
          checkNotNull(fx.imports.find(checkNotNull(chain.importId))).replayVerdict shouldBe
              "PENDING"
        }
      }

      test("an id picks the check itself, or the newest one behind that import") {
        runTest {
          val fx = RequeueFixture(backgroundScope)
          val mod = fx.moderator()
          val who = fx.player("Lucas")
          val older = fx.settled(who, ReplayVerdict.INCONCLUSIVE, fx.now.minusDays(2))
          val newer = fx.settled(who, ReplayVerdict.DIVERGED, fx.now.minusDays(1))
          val olderImport = checkNotNull(checkNotNull(fx.chains.find(older.chainId)).importId)

          fx.chat.tryHandle(mod, "/requeue-import lucas $olderImport") shouldBe true
          mod.replies().last() shouldContain "Check ${older.id} of Lucas's"
          checkNotNull(fx.requests.find(older.id)).verdict shouldBe ReplayVerdict.PENDING

          // One at a time: the newer one has to wait for the older one to answer.
          fx.chat.tryHandle(mod, "/requeue-import lucas ${newer.id}") shouldBe true
          mod.replies().last() shouldContain "already waiting"
        }
      }

      test("a check already put back, or verified, is refused by name") {
        runTest {
          val fx = RequeueFixture(backgroundScope)
          val mod = fx.moderator()
          val who = fx.player("Barry")
          val twice = fx.settled(who, ReplayVerdict.INCONCLUSIVE, requeuedBy = "Someone")
          val done = fx.settled(who, ReplayVerdict.VERIFIED, fx.now.minusDays(1))

          fx.chat.tryHandle(mod, "/requeue-import barry") shouldBe true
          mod.replies().last() shouldContain "no check to run again"

          fx.chat.tryHandle(mod, "/requeue-import barry ${twice.id}") shouldBe true
          mod.replies().last() shouldContain "already put back once, by Someone"

          fx.chat.tryHandle(mod, "/requeue-import barry ${done.id}") shouldBe true
          mod.replies().last() shouldContain "verified"
        }
      }

      test("a name nobody has, an id that is nobody's, and no name at all") {
        runTest {
          val fx = RequeueFixture(backgroundScope)
          val mod = fx.moderator()
          val who = fx.player("Cynthia")
          fx.settled(who, ReplayVerdict.DIVERGED)

          fx.chat.tryHandle(mod, "/requeue-import") shouldBe true
          mod.replies().last() shouldContain "Usage:"
          fx.chat.tryHandle(mod, "/requeue-import nobody") shouldBe true
          mod.replies().last() shouldContain "Nobody here is called nobody"
          fx.chat.tryHandle(mod, "/requeue-import cynthia 12345") shouldBe true
          mod.replies().last() shouldContain "No check of Cynthia's is 12345"
        }
      }
    })
