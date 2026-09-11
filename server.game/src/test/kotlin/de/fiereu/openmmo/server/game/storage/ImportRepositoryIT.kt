package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.common.CharacterInfo
import de.fiereu.openmmo.common.test.DockerAvailable
import de.fiereu.openmmo.server.game.offline.ImportSnapshot
import io.kotest.core.annotation.EnabledIf
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldBeEmpty
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime
import java.time.temporal.ChronoUnit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import org.flywaydb.core.Flyway
import org.jooq.DSLContext
import org.jooq.impl.DSL
import org.testcontainers.containers.PostgreSQLContainer

/**
 * The two things the in-memory doubles cannot answer for: the week's money sum and the refusal
 * upsert are both SQL, and both are the sort of SQL that is wrong in a way nothing else notices.
 */
@EnabledIf(DockerAvailable::class)
class ImportRepositoryIT :
    FunSpec({
      val container = PostgreSQLContainer<Nothing>("postgres:18")
      val entityIds = EntityIdService()
      lateinit var imports: JooqImportRepository
      lateinit var violations: JooqViolationRepository
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
                        name = "Ash$id",
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

      fun row(
          characterId: Long,
          at: LocalDateTime,
          moneyBefore: Int,
          moneyAfter: Int,
      ): ImportRecord =
          ImportRecord(
              id = entityIds.newImportId(),
              characterId = characterId,
              importedAt = at,
              playTimeSeconds = 3600,
              saveSha256 = "c".repeat(64),
              clientRevision = 7,
              trainerId = 0x00010002,
              partyCount = 1,
              boxCount = 0,
              speciesCount = 1,
              levelTotal = 20,
              levelMax = 20,
              moneyBefore = moneyBefore,
              moneyAfter = moneyAfter,
              badgesBefore = 0,
              badgesAfter = 1,
              verdicts = listOf("money: 1 brought to 2", "Potion dropped: nothing crosses"),
              snapshotVersion = ImportSnapshot.VERSION,
          )

      beforeSpec {
        container.start()
        Flyway.configure()
            .dataSource(container.jdbcUrl, container.username, container.password)
            .locations("classpath:db/migration", "classpath:db/dev")
            .load()
            .migrate()
        dsl = DSL.using(container.jdbcUrl, container.username, container.password)
        imports = JooqImportRepository(dsl, Dispatchers.IO)
        violations = JooqViolationRepository(dsl, Dispatchers.IO)
        characters = JooqCharacterRepository(dsl, Dispatchers.IO)
      }

      afterSpec { container.stop() }

      test("an import round-trips, snapshot and all, and the blob is fetched on its own") {
        val id = character(userId = 90)
        val record = row(id, now(), 1000, 5000)
        val blob = byteArrayOf(9, 8, 7, -1, 0)

        imports.record(record, blob)

        val read = imports.listFor(id, 5).single()
        read shouldBe record
        val (bytes, version) = imports.loadSnapshot(record.id).shouldNotBeNull()
        bytes.toList() shouldBe blob.toList()
        version shouldBe ImportSnapshot.VERSION
      }

      // Kept when the money window went with the cap it fed: the rollback's own once-only rule was
      // asserted inside that test, and it is about two moderators reaching for the same import.
      test("an import is rolled back once, however many people reach for it") {
        val id = character(userId = 96)
        val row = row(id, now().minusHours(1), 0, 500)
        imports.record(row, ByteArray(0))

        imports.markRolledBack(row.id, now(), "a moderator") shouldBe true
        imports.markRolledBack(row.id, now(), "somebody else") shouldBe false
      }

      test("the seal marks every import that still stands and leaves a rolled back one alone") {
        val id = character(userId = 92)
        val standing = row(id, now().minusHours(2), 0, 10)
        val undone = row(id, now().minusHours(3), 0, 10)
        imports.record(standing, ByteArray(0))
        imports.record(undone, ByteArray(0))
        imports.markRolledBack(undone.id, now(), "a moderator")

        imports.seal(id, now(), "a trade with Ash")

        imports.find(standing.id).shouldNotBeNull().sealedReason shouldBe "a trade with Ash"
        imports.find(undone.id).shouldNotBeNull().sealedAt shouldBe null
      }

      test("a refusal recorded twice adds up rather than replacing or failing") {
        val id = character(userId = 93)
        val first = now().minusMinutes(5)

        // The write is fire and forget, so each one is waited for before the next is made: two
        // upserts of the same row racing each other would decide which detail is the last one at
        // random, and this is measuring that the second adds to the first.
        suspend fun settled(count: Long): List<DurableViolation> {
          val giveUpAt = System.nanoTime() + 10_000_000_000L
          var tally = violations.countsFor(id)
          while (tally.sumOf { it.total } < count && System.nanoTime() < giveUpAt) {
            delay(25)
            tally = violations.countsFor(id)
          }
          return tally
        }

        violations.bump(id, "IMPOSSIBLE_PACE", "eight steps in a frame", first)
        settled(1)
        violations.bump(id, "IMPOSSIBLE_PACE", "nine steps in a frame", now())
        settled(2)
        violations.bump(id, "NOT_YOURS", "somebody else's monster", now())
        val tally = settled(3)

        val pace = tally.single { it.kind == "IMPOSSIBLE_PACE" }
        pace.total shouldBe 2L
        pace.firstAt shouldBe first
        pace.lastDetail shouldBe "nine steps in a frame"
        violations.worst(1).single().kind shouldBe "IMPOSSIBLE_PACE"
      }

      test("a character with no imports has none, rather than a row of zeroes") {
        val id = character(userId = 94)

        imports.listFor(id, 5).shouldBeEmpty()
      }
    })
