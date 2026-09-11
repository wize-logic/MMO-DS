package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.common.CharacterInfo
import de.fiereu.openmmo.common.test.DockerAvailable
import de.fiereu.openmmo.server.game.offline.verify.ReplayVerdict
import io.kotest.core.annotation.EnabledIf
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime
import java.time.temporal.ChronoUnit
import kotlinx.coroutines.Dispatchers
import org.flywaydb.core.Flyway
import org.jooq.DSLContext
import org.jooq.impl.DSL
import org.testcontainers.containers.PostgreSQLContainer

/**
 * The asks, over SQL: settled once, put back once, and the week's count reading a waiting ask's
 * whole budget and a settled one's actual run, the two things a sum in the wrong column would get
 * silently wrong.
 */
@EnabledIf(DockerAvailable::class)
class RequestRepositoryIT :
    FunSpec({
      val container = PostgreSQLContainer<Nothing>("postgres:18")
      val entityIds = EntityIdService()
      lateinit var requests: JooqRequestRepository
      lateinit var chains: JooqChainRepository
      lateinit var characters: JooqCharacterRepository
      lateinit var dsl: DSLContext

      fun now(): LocalDateTime = LocalDateTime.now().truncatedTo(ChronoUnit.MICROS)

      suspend fun character(userId: Int): Long {
        val id = entityIds.newCharacterId()
        characters.insertAggregate(
            StoredCharacter(
                info =
                    CharacterInfo(
                        id = id,
                        name = "Ask$id",
                        userId = userId,
                        rivalSex = 1,
                        lastLogin = now(),
                        createdAt = now(),
                        money = 30000,
                        permissions = 0,
                        remainingSafariSteps = 0,
                        remainingSafariBalls = 0,
                        pcExtraSlots = 0,
                        battleBoxExtraSlots = 0,
                        templateAmount = 0,
                        positionRegionId = 3,
                        positionBankId = 1,
                        positionMapId = 86,
                        positionX = 4,
                        positionY = 4,
                        repelLeft = 0,
                        repelItemId = 0,
                        lureLeft = 0,
                        lureItemId = 0,
                    ),
                pokemon = mutableListOf(),
                pcStorage = mutableListOf(),
                items = mutableMapOf(),
            ))
        return id
      }

      suspend fun chain(characterId: Long): Long {
        val id = entityIds.newChainId()
        chains.record(
            StoredChain(
                id = id,
                characterId = characterId,
                importId = null,
                uploadedAt = now(),
                anchorSha256 = null,
                linkCount = 1,
                frameTotal = 1000,
                verifiedLink = -1,
                verdict = ReplayVerdict.HELD),
            emptyList())
        return id
      }

      fun request(characterId: Long, chainId: Long, at: LocalDateTime = now(), pid: Int? = null) =
          StoredRequest(
              id = entityIds.newRequestId(),
              chainId = chainId,
              characterId = characterId,
              monsterPid = pid,
              requestedAt = at,
              frameBudget = 3000,
              freeFramesLeft = 1000,
              feePaid = 5000,
          )

      beforeSpec {
        container.start()
        Flyway.configure()
            .dataSource(container.jdbcUrl, container.username, container.password)
            .locations("classpath:db/migration", "classpath:db/dev")
            .load()
            .migrate()
        dsl = DSL.using(container.jdbcUrl, container.username, container.password)
        requests = JooqRequestRepository(dsl, Dispatchers.IO)
        chains = JooqChainRepository(dsl, Dispatchers.IO)
        characters = JooqCharacterRepository(dsl, Dispatchers.IO)
      }

      afterSpec { container.stop() }

      test("an ask round-trips, the monster's personality and the nulls included") {
        val who = character(userId = 90)
        val row = request(who, chain(who), pid = -0x12345678)
        requests.record(row)
        requests.find(row.id) shouldBe row
        requests.find(row.id).shouldNotBeNull().waitingSince shouldBe row.requestedAt
      }

      test("the queue is oldest ask first, and an ask is settled once") {
        val who = character(userId = 91)
        val chainId = chain(who)
        val newer = request(who, chainId, now())
        val older = request(character(userId = 92), chain(who), now().minusHours(2))
        requests.record(newer)
        requests.record(older)

        requests.pending(100).map { it.id }.filter { it == older.id || it == newer.id } shouldBe
            listOf(older.id, newer.id)

        requests.settle(older.id, ReplayVerdict.DIVERGED, "session 1", 0, 900, 0, now()) shouldBe
            true
        requests.settle(older.id, ReplayVerdict.VERIFIED, null, 0, 900, 5000, now()) shouldBe false
        val read = requests.find(older.id).shouldNotBeNull()
        read.verdict shouldBe ReplayVerdict.DIVERGED
        read.stoppedLink shouldBe 0
        read.framesRun shouldBe 900L
        read.feeRefunded shouldBe 0
        requests.inFlightFor(who) shouldBe 1
      }

      test("a settled ask goes back once, never while it waits, never once verified") {
        val who = character(userId = 93)
        val row = request(who, chain(who))
        requests.record(row)

        requests.requeue(row.id, "Mod", now()) shouldBe false
        requests.settle(row.id, ReplayVerdict.INCONCLUSIVE, "exit 139", null, 0, 5000, now())
        val at = now()
        requests.requeue(row.id, "Mod", at) shouldBe true

        val back = requests.find(row.id).shouldNotBeNull()
        back.verdict shouldBe ReplayVerdict.PENDING
        back.verdictReason shouldBe null
        back.settledAt shouldBe null
        back.requeuedAt shouldBe at
        back.requeuedBy shouldBe "Mod"
        back.waitingSince shouldBe at
        back.feeRefunded shouldBe 5000

        requests.settle(row.id, ReplayVerdict.VERIFIED, null, 0, 1000, 5000, now()) shouldBe true
        requests.requeue(row.id, "Mod", now()) shouldBe false
      }

      test("the week counts a waiting ask's whole budget and a settled one's actual run") {
        val who = character(userId = 94)
        val chainId = chain(who)
        val waiting = request(who, chainId, now().minusDays(1))
        val ran = request(who, chainId, now().minusDays(2))
        val old = request(who, chainId, now().minusDays(9))
        for (row in listOf(waiting, ran, old)) requests.record(row)
        requests.settle(ran.id, ReplayVerdict.VERIFIED, null, 0, 1200, 0, now())
        requests.settle(old.id, ReplayVerdict.VERIFIED, null, 0, 3000, 0, now())

        requests.framesSince(who, now().minusDays(7)) shouldBe 3000L + 1200L
        requests.depth() shouldBe requests.pending(1000).size
        requests.listFor(who, 10).map { it.id } shouldBe listOf(waiting.id, ran.id, old.id)
      }
    })
