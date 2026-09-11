package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.common.CharacterInfo
import de.fiereu.openmmo.common.test.DockerAvailable
import io.kotest.core.annotation.EnabledIf
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.nulls.shouldBeNull
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
 * What the in-memory double cannot answer for: the image is a blob and has to come back byte for
 * byte, a verdict lands once (`WHERE verdict = 'PENDING'`), the anchor query picks the newest
 * CHECKED copy and only a CHECKED one, and pruning keeps exactly the newest few.
 */
@EnabledIf(DockerAvailable::class)
class ExportRepositoryIT :
    FunSpec({
      val container = PostgreSQLContainer<Nothing>("postgres:18")
      val entityIds = EntityIdService()
      lateinit var exports: JooqExportRepository
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
                        name = "Export$id",
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

      fun export(characterId: Long, at: LocalDateTime, sha: String) =
          StoredExport(
              id = entityIds.newExportId(),
              characterId = characterId,
              exportedAt = at,
              sha256 = sha,
              verdict = ExportVerdict.PENDING,
          )

      beforeSpec {
        container.start()
        Flyway.configure()
            .dataSource(container.jdbcUrl, container.username, container.password)
            .locations("classpath:db/migration", "classpath:db/dev")
            .load()
            .migrate()
        dsl = DSL.using(container.jdbcUrl, container.username, container.password)
        exports = JooqExportRepository(dsl, Dispatchers.IO)
        characters = JooqCharacterRepository(dsl, Dispatchers.IO)
      }

      afterSpec { container.stop() }

      test("a copy round-trips whole, and its row reads back without it") {
        val who = character(1)
        val image = ByteArray(512 * 1024) { (it * 31).toByte() }
        val row = export(who, now(), "a".repeat(64))
        exports.record(row, image)

        checkNotNull(exports.image(row.id)).contentEquals(image) shouldBe true
        exports.find(row.id) shouldBe row
      }

      test("only a checked copy is the anchor, the newest of them, and a verdict lands once") {
        val who = character(2)
        val t = now()
        val older = export(who, t.minusHours(2), "b".repeat(64))
        val newer = export(who, t.minusHours(1), "c".repeat(64))
        val newest = export(who, t, "d".repeat(64))
        exports.record(older, ByteArray(16) { 1 })
        exports.record(newer, ByteArray(16) { 2 })
        exports.record(newest, ByteArray(16) { 3 })

        exports.newestChecked(who).shouldBeNull()
        exports.settle(older.id, ExportVerdict.CHECKED, null, t) shouldBe true
        exports.settle(newer.id, ExportVerdict.CHECKED, null, t) shouldBe true
        exports.settle(newest.id, ExportVerdict.MISMATCH, "money", t) shouldBe true
        // Already answered: the second word does not land.
        exports.settle(newer.id, ExportVerdict.UNCHECKED, "again", t) shouldBe false

        checkNotNull(exports.newestChecked(who)).id shouldBe newer.id
        checkNotNull(exports.checkedImage(who, "c".repeat(64)))
            .contentEquals(ByteArray(16) { 2 }) shouldBe true
        exports.checkedImage(who, "d".repeat(64)).shouldBeNull()
        checkNotNull(exports.find(newest.id)).verdictReason shouldBe "money"
      }

      test("pruning keeps the newest few and nobody else's") {
        val who = character(3)
        val other = character(4)
        val t = now()
        val mine = (0 until 5).map { export(who, t.minusMinutes(5L - it), "e".repeat(63) + it) }
        mine.forEach { exports.record(it, ByteArray(8)) }
        val theirs = export(other, t, "f".repeat(64))
        exports.record(theirs, ByteArray(8))

        exports.prune(who, 3) shouldBe 2
        val left = exports.listFor(who, 10)
        left.map { it.id } shouldBe mine.takeLast(3).reversed().map { it.id }
        exports.find(theirs.id).shouldNotBeNull()
        exports.prune(who, 3) shouldBe 0
      }
    })
