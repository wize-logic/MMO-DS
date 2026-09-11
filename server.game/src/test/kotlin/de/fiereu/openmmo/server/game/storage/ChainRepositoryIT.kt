package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.common.CharacterInfo
import de.fiereu.openmmo.common.test.DockerAvailable
import de.fiereu.openmmo.server.game.offline.verify.Frontier
import de.fiereu.openmmo.server.game.offline.verify.ReplayVerdict
import de.fiereu.openmmo.server.game.offline.verify.SessionLink
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

/** What the in-memory double cannot answer for. */
@EnabledIf(DockerAvailable::class)
class ChainRepositoryIT :
    FunSpec({
      val container = PostgreSQLContainer<Nothing>("postgres:18")
      val entityIds = EntityIdService()
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
                        name = "Chain$id",
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

      fun link(ordinal: Int) =
          SessionLink(
              version = 1,
              revision = 12,
              rtc = now().minusHours((3 - ordinal).toLong()),
              bootSha256 = if (ordinal == 0) null else "b".repeat(64),
              recordingName = "session ${ordinal + 1}",
              recordingSha256 = "c".repeat(64),
              quitSha256 = "d".repeat(64),
              endFrame = 27168L + ordinal,
          )

      fun chain(characterId: Long, at: LocalDateTime = now()) =
          StoredChain(
              id = entityIds.newChainId(),
              characterId = characterId,
              importId = null,
              uploadedAt = at,
              anchorSha256 = null,
              linkCount = 2,
              frameTotal = 54_336,
              verifiedLink = -1,
              verdict = ReplayVerdict.HELD,
          )

      beforeSpec {
        container.start()
        Flyway.configure()
            .dataSource(container.jdbcUrl, container.username, container.password)
            .locations("classpath:db/migration", "classpath:db/dev")
            .load()
            .migrate()
        dsl = DSL.using(container.jdbcUrl, container.username, container.password)
        chains = JooqChainRepository(dsl, Dispatchers.IO)
        characters = JooqCharacterRepository(dsl, Dispatchers.IO)
      }

      afterSpec { container.stop() }

      test("a chain and its recordings round-trip, blobs and all") {
        val who = character(userId = 80)
        val row = chain(who)
        val recordings =
            listOf("0 keys none\n900 keys A\n".toByteArray(), "0 keys none\n".toByteArray())
        // Built once: the helper reads the clock, so a second call would make a different session.
        val written = listOf(link(0), link(1))

        chains.record(row, written.mapIndexed { i, l -> StoredLink(l, recordings[i]) })

        chains.find(row.id) shouldBe row
        val read = chains.sessions(row.id).shouldNotBeNull()
        read.links shouldBe written
        read.recordings[0].toList() shouldBe recordings[0].toList()
        read.recordings[1].toList() shouldBe recordings[1].toList()
        chains.frontier(row.id) shouldBe Frontier(-1, null)
      }

      test("the frontier moves forward and never back, image included") {
        val who = character(userId = 81)
        val row = chain(who)
        chains.record(
            row, listOf(StoredLink(link(0), ByteArray(1)), StoredLink(link(1), ByteArray(1))))

        chains.advance(row.id, Frontier(1, byteArrayOf(1, 2, 3, -1)))
        // A verdict arriving out of order, or a second worker on the same chain: the state after a
        // session already replayed is not something an earlier answer may undo.
        chains.advance(row.id, Frontier(0, byteArrayOf(9)))

        val held = chains.frontier(row.id).shouldNotBeNull()
        held.link shouldBe 1
        held.image!!.toList() shouldBe listOf<Byte>(1, 2, 3, -1)
      }

      test("a divergence ends a chain, and nothing said afterwards changes it") {
        val who = character(userId = 82)
        val row = chain(who)
        chains.record(row, listOf(StoredLink(link(0), ByteArray(1))))

        chains.mark(row.id, ReplayVerdict.DIVERGED, "session 1", 0, 900) shouldBe true
        chains.mark(row.id, ReplayVerdict.VERIFIED, null, null, null) shouldBe false

        val read = chains.find(row.id).shouldNotBeNull()
        read.verdict shouldBe ReplayVerdict.DIVERGED
        read.verdictReason shouldBe "session 1"
        read.divergedLink shouldBe 0
        read.divergedFrame shouldBe 900L
      }

      test("a character's chains come newest first") {
        val who = character(userId = 84)
        val older = chain(who, now().minusHours(2))
        val newer = chain(who, now())
        chains.record(older, listOf(StoredLink(link(0), ByteArray(1))))
        chains.record(newer, listOf(StoredLink(link(0), ByteArray(1))))

        chains.listFor(who, 10).map { it.id } shouldBe listOf(newer.id, older.id)
        chains.listFor(who, 1).map { it.id } shouldBe listOf(newer.id)
      }
    })
