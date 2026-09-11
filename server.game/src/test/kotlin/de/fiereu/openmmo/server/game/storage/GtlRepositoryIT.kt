package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.common.CharacterInfo
import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.test.DockerAvailable
import io.kotest.core.annotation.EnabledIf
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.nulls.shouldBeNull
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime
import java.time.temporal.ChronoUnit
import kotlinx.coroutines.Dispatchers
import org.flywaydb.core.Flyway
import org.jooq.DSLContext
import org.jooq.impl.DSL
import org.testcontainers.containers.PostgreSQLContainer

/**
 * What the in-memory double cannot answer for: the double keeps the listing object it was handed,
 * so every field of an escrowed monster survives it by construction.
 */
@EnabledIf(DockerAvailable::class)
class GtlRepositoryIT :
    FunSpec({
      val container = PostgreSQLContainer<Nothing>("postgres:18")
      val entityIds = EntityIdService()
      lateinit var shelf: JooqGtlRepository
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
                        name = "Shelf$id",
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

      /** Every field a monster can carry set to something other than its default. */
      fun decorated(ownerId: Long): Pokemon =
          Pokemon(
              id = entityIds.newMonsterId(),
              ownerId = ownerId,
              container = PokemonContainer.PARTY,
              containerSlot = 3,
              dexId = 487,
              seed = -559038737,
              ot = "Cynthia",
              nickname = "Renegade",
              level = 70,
              hp = 291,
              xp = 800000,
              eVs =
                  EVs().also {
                    it.hp = 4
                    it.atk = 252
                    it.spd = 252
                  },
              iVs =
                  IVs().also {
                    it.hp = 31
                    it.atk = 30
                    it.spDef = 29
                  },
              moves =
                  listOf(
                      PokemonMove(444, 5),
                      PokemonMove(247, 15),
                      PokemonMove(89, 10),
                      PokemonMove(63, 5)),
              isShiny = true,
              hasHiddenAbility = true,
              isAlpha = true,
              isSecret = true,
              isFatefulEncounter = true,
              isRaidEncounter = true,
              caughtAt = now(),
              isEgg = false,
              form = 1,
              conditions =
                  ContestConditions(cool = 60, beauty = 55, cute = 40, smart = 30, tough = 20),
              sheen = 200,
              superContestRibbons = 0x000F_00F0L,
              caughtRegionId = 3,
              caughtBankId = 2,
              caughtMapId = 111,
              caughtLocationLabel = 88,
              friendship = 254,
              heldItemId = 234,
              status = 0x48,
              offlineOrigin = false,
          )

      fun listing(sellerId: Long, mon: Pokemon?, price: Int, at: LocalDateTime) =
          GtlListing(
              id = 0,
              sellerId = sellerId,
              sellerName = "Shelf$sellerId",
              kind = if (mon == null) GTL_KIND_ITEM else GTL_KIND_POKEMON,
              itemId = if (mon == null) 17 else null,
              quantity = 1,
              price = price,
              state = GTL_STATE_ACTIVE,
              buyerId = null,
              buyerName = null,
              listedAt = at,
              expiresAt = at.plusDays(7),
              soldAt = null,
              pokemon = mon,
              remaining = 1,
              unclaimedUnits = 0,
              fee = 100,
          )

      beforeSpec {
        container.start()
        Flyway.configure()
            .dataSource(container.jdbcUrl, container.username, container.password)
            .locations("classpath:db/migration", "classpath:db/dev")
            .load()
            .migrate()
        dsl = DSL.using(container.jdbcUrl, container.username, container.password)
        shelf = JooqGtlRepository(dsl, Dispatchers.IO)
        characters = JooqCharacterRepository(dsl, Dispatchers.IO)
      }

      afterSpec { container.stop() }

      test("an escrowed monster comes back off the shelf whole") {
        val seller = character(1)
        val mon = decorated(seller)
        val row = shelf.insert(listing(seller, mon, 5000, now()))

        val back = shelf.ownPage(seller, 0, 10).listings.single { it.id == row.id }.pokemon
        // The escrow has no owner and no slot: whoever takes delivery seats it. Everything else
        // about the monster is the seller's and has to survive the shelf untouched.
        back shouldBe
            mon.copy(ownerId = seller, container = PokemonContainer.GTS, containerSlot = 0)
      }

      test("a listing is found by its own id, however many the seller has") {
        val seller = character(2)
        val other = character(3)
        val t = now()
        // More rows than any one page of the seller's shelf would hold.
        val ids =
            (0 until 40).map {
              shelf.insert(listing(seller, null, 100 + it, t.plusSeconds(it.toLong()))).id
            }
        val theirs = shelf.insert(listing(other, null, 900, t)).id

        checkNotNull(shelf.findOwn(seller, ids.first())).price shouldBe 100
        checkNotNull(shelf.findOwn(seller, ids.last())).price shouldBe 139
        // Somebody else's row is not the seller's to read.
        shelf.findOwn(seller, theirs).shouldBeNull()
      }

      test("a closed listing with nothing owed on it is off the seller's shelf") {
        val seller = character(4)
        val id = shelf.insert(listing(seller, null, 700, now())).id

        checkNotNull(shelf.close(id, seller, GTL_STATE_ACTIVE))
        shelf.findOwn(seller, id).shouldBeNull()
      }
    })
