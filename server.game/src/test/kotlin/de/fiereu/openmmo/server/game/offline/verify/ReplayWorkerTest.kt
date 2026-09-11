package de.fiereu.openmmo.server.game.offline.verify

import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.InMemoryChainRepository
import de.fiereu.openmmo.server.game.storage.InMemoryImportRepository
import de.fiereu.openmmo.server.game.storage.InMemoryRequestRepository
import de.fiereu.openmmo.server.game.storage.RequestRepository
import de.fiereu.openmmo.server.game.storage.StoredChain
import de.fiereu.openmmo.server.game.storage.StoredLink
import de.fiereu.openmmo.server.game.storage.StoredRequest
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldHaveSize
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain
import java.security.MessageDigest
import java.time.LocalDateTime
import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runTest

/** The lanes that drive the queue. */
@OptIn(ExperimentalCoroutinesApi::class)
class ReplayWorkerTest :
    FunSpec({
      val now = LocalDateTime.of(2026, 9, 4, 18, 0, 0)
      val idle = 5.seconds

      fun sha(bytes: ByteArray): String =
          MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

      /** Answers the image the recording names, so an honest chain verifies; or throws, once. */
      class FakeRunner : ReplayRunner {
        val recordingsRun = mutableListOf<String>()
        var throwsLeft = 0

        override fun revisions(): Set<Int> = setOf(12)

        override fun run(run: ReplayRun): ReplayOutcome {
          if (throwsLeft > 0) {
            throwsLeft--
            throw IllegalStateException("the sandbox is gone")
          }
          recordingsRun += run.recording.decodeToString()
          val image = run.recording
          return ReplayOutcome.Quit(image, sha(image))
        }
      }

      /** A store whose first look at the queue fails, the way a database that is down does. */
      class Flaky(private val inner: RequestRepository) : RequestRepository by inner {
        var failuresLeft = 1

        override suspend fun pending(limit: Int): List<StoredRequest> {
          if (failuresLeft > 0) {
            failuresLeft--
            throw IllegalStateException("connection refused")
          }
          return inner.pending(limit)
        }
      }

      class Rig(scope: CoroutineScope, cores: Int = 1, requests: RequestRepository? = null) {
        val chains = InMemoryChainRepository()
        val requests = requests ?: InMemoryRequestRepository()
        val imports = InMemoryImportRepository()
        val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
        val runner = FakeRunner()
        val limits = ReplayLimits(cores = cores)
        val ids = EntityIdService()
        val service =
            ReplayVerificationService(
                chains,
                imports,
                store,
                runner,
                limits,
                ids,
                blocking = Dispatchers.Unconfined,
                requests = this.requests,
                clock = { now })

        suspend fun character(name: String): Long =
            store.createCharacter(1, name, CharacterGender.MALE, Region.SINNOH).info.id

        /** One honest session whose quit image is its own recording, and an ask for all of it. */
        suspend fun queue(characterId: Long, script: String): Long {
          val recording = script.toByteArray()
          val link =
              SessionLink(
                  version = 1,
                  revision = 12,
                  rtc = now.minusHours(2),
                  bootSha256 = null,
                  recordingName = "session 1",
                  recordingSha256 = sha(recording),
                  quitSha256 = sha(recording),
                  endFrame = 1000,
              )
          val chainId = ids.newChainId()
          chains.record(
              StoredChain(
                  id = chainId,
                  characterId = characterId,
                  importId = null,
                  uploadedAt = now.minusHours(1),
                  anchorSha256 = null,
                  linkCount = 1,
                  frameTotal = 1000,
                  verifiedLink = -1,
                  verdict = ReplayVerdict.HELD,
              ),
              listOf(StoredLink(link, recording)))
          val id = ids.newRequestId()
          requests.record(
              StoredRequest(
                  id = id,
                  chainId = chainId,
                  characterId = characterId,
                  monsterPid = null,
                  requestedAt = now.minusMinutes(30),
                  frameBudget = 1000,
                  freeFramesLeft = limits.freeFrames,
                  feePaid = 0,
              ))
          return id
        }
      }

      fun TestScope.worker(rig: Rig) = ReplayWorker(rig.service, rig.limits, backgroundScope, idle)

      test("a queued request is run without anybody asking") {
        runTest {
          val rig = Rig(backgroundScope)
          val id = rig.queue(rig.character("Alone"), "0 keys none\n900 keys A\n906 keys none\n")

          worker(rig).start()
          testScheduler.runCurrent()

          checkNotNull(rig.requests.find(id)).verdict shouldBe ReplayVerdict.VERIFIED
          rig.runner.recordingsRun shouldHaveSize 1
        }
      }

      test("an empty queue is looked at again after the idle, and an ask arriving then is taken") {
        runTest {
          val rig = Rig(backgroundScope)
          worker(rig).start()
          testScheduler.runCurrent()
          rig.runner.recordingsRun shouldHaveSize 0

          val id = rig.queue(rig.character("Later"), "0 keys none\n900 keys A\n906 keys none\n")
          testScheduler.runCurrent()
          checkNotNull(rig.requests.find(id)).verdict shouldBe ReplayVerdict.PENDING

          advanceTimeBy(idle)
          testScheduler.runCurrent()
          checkNotNull(rig.requests.find(id)).verdict shouldBe ReplayVerdict.VERIFIED
        }
      }

      test("two lanes take different requests, and each is run once") {
        runTest {
          val rig = Rig(backgroundScope, cores = 2)
          val a = rig.queue(rig.character("Aleph"), "0 keys none\n900 keys A\n906 keys none\n")
          val b = rig.queue(rig.character("Beth"), "0 keys none\n900 keys B\n906 keys none\n")

          worker(rig).start()
          testScheduler.runCurrent()

          checkNotNull(rig.requests.find(a)).verdict shouldBe ReplayVerdict.VERIFIED
          checkNotNull(rig.requests.find(b)).verdict shouldBe ReplayVerdict.VERIFIED
          rig.runner.recordingsRun.toSet() shouldHaveSize 2
          rig.runner.recordingsRun shouldHaveSize 2
        }
      }

      test("a replay that throws settles its request about the server, and the lane goes on") {
        runTest {
          val rig = Rig(backgroundScope)
          rig.runner.throwsLeft = 1
          val a = rig.queue(rig.character("Cursed"), "0 keys none\n900 keys A\n906 keys none\n")
          val b = rig.queue(rig.character("Fine"), "0 keys none\n900 keys B\n906 keys none\n")

          worker(rig).start()
          testScheduler.runCurrent()

          val cursed = checkNotNull(rig.requests.find(a))
          cursed.verdict shouldBe ReplayVerdict.INCONCLUSIVE
          checkNotNull(cursed.verdictReason) shouldContain "the sandbox is gone"
          checkNotNull(rig.requests.find(b)).verdict shouldBe ReplayVerdict.VERIFIED
        }
      }

      test("a store that is down does not take the lane with it") {
        runTest {
          val flaky = Flaky(InMemoryRequestRepository())
          val rig = Rig(backgroundScope, requests = flaky)
          val id = rig.queue(rig.character("Patient"), "0 keys none\n900 keys A\n906 keys none\n")

          worker(rig).start()
          testScheduler.runCurrent()
          checkNotNull(rig.requests.find(id)).verdict shouldBe ReplayVerdict.PENDING

          advanceTimeBy(idle)
          testScheduler.runCurrent()
          checkNotNull(rig.requests.find(id)).verdict shouldBe ReplayVerdict.VERIFIED
        }
      }

      test("a server with no lanes to give starts none, and a queued request waits") {
        runTest {
          val rig = Rig(backgroundScope, cores = 0)
          val id = rig.queue(rig.character("Waiting"), "0 keys none\n900 keys A\n906 keys none\n")

          worker(rig).start()
          advanceTimeBy(idle * 3)
          testScheduler.runCurrent()

          checkNotNull(rig.requests.find(id)).verdict shouldBe ReplayVerdict.PENDING
          rig.runner.recordingsRun shouldHaveSize 0
        }
      }
    })
