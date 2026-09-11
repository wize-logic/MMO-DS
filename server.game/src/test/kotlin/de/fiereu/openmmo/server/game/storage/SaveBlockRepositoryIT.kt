package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.common.CharacterInfo
import de.fiereu.openmmo.common.test.DockerAvailable
import io.kotest.core.annotation.EnabledIf
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime
import java.time.temporal.ChronoUnit
import kotlinx.coroutines.Dispatchers
import org.flywaydb.core.Flyway
import org.jooq.DSLContext
import org.jooq.impl.DSL
import org.testcontainers.containers.PostgreSQLContainer

/**
 * The one thing the in-memory double cannot answer for: that a replace is a delete of what it does
 * not name and an upsert of what it does, in one transaction, and that a save still touches nothing
 * it does not name.
 */
@EnabledIf(DockerAvailable::class)
class SaveBlockRepositoryIT :
    FunSpec({
      val container = PostgreSQLContainer<Nothing>("postgres:18")
      val entityIds = EntityIdService()
      lateinit var blocks: JooqSaveBlockRepository
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

      fun Map<Int, ByteArray>.bytes(): Map<Int, List<Byte>> = mapValues { it.value.toList() }

      beforeSpec {
        container.start()
        Flyway.configure()
            .dataSource(container.jdbcUrl, container.username, container.password)
            .locations("classpath:db/migration", "classpath:db/dev")
            .load()
            .migrate()
        dsl = DSL.using(container.jdbcUrl, container.username, container.password)
        blocks = JooqSaveBlockRepository(dsl, Dispatchers.IO)
        characters = JooqCharacterRepository(dsl, Dispatchers.IO)
      }

      afterSpec { container.stop() }

      test("a save adds and updates what it names and leaves the rest alone") {
        val id = character(userId = 80)
        blocks.save(id, mapOf(7 to byteArrayOf(1), 8 to byteArrayOf(2)))

        blocks.save(id, mapOf(8 to byteArrayOf(3), 0xC2 to byteArrayOf(4)))

        blocks.load(id).bytes() shouldBe
            mapOf(7 to listOf<Byte>(1), 8 to listOf<Byte>(3), 0xC2 to listOf<Byte>(4))
      }

      test("a replace makes what it names the whole of what is held") {
        val id = character(userId = 81)
        val other = character(userId = 82)
        blocks.save(id, mapOf(7 to byteArrayOf(1), 8 to byteArrayOf(2), 0xC2 to byteArrayOf(9)))
        blocks.save(other, mapOf(7 to byteArrayOf(5)))

        blocks.replace(id, mapOf(8 to byteArrayOf(2), 0xC2 to byteArrayOf(1, 2, 3)))

        blocks.load(id).bytes() shouldBe mapOf(8 to listOf<Byte>(2), 0xC2 to listOf<Byte>(1, 2, 3))
        // Another character's rows are not this character's.
        blocks.load(other).bytes() shouldBe mapOf(7 to listOf<Byte>(5))

        blocks.replace(id, emptyMap())

        blocks.load(id) shouldBe emptyMap()
      }
    })
