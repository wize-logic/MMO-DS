package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.common.CharacterInfo
import de.fiereu.openmmo.common.enums.GuildPermission
import de.fiereu.openmmo.common.enums.GuildRank
import de.fiereu.openmmo.common.test.DockerAvailable
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

/** The restart, which the in-memory doubles cannot stand in for. */
@EnabledIf(DockerAvailable::class)
class SocialGuildStoreIT :
    FunSpec({
      val container = PostgreSQLContainer<Nothing>("postgres:18")
      val entityIds = EntityIdService()
      lateinit var social: JooqSocialRepository
      lateinit var guilds: JooqGuildRepository
      lateinit var characters: JooqCharacterRepository
      lateinit var dsl: DSLContext

      fun now(): LocalDateTime = LocalDateTime.now().truncatedTo(ChronoUnit.MICROS)

      suspend fun character(name: String): Long {
        val id = entityIds.newCharacterId()
        characters.insertAggregate(
            StoredCharacter(
                info =
                    CharacterInfo(
                        id = id,
                        name = name,
                        userId = 1,
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

      beforeSpec {
        container.start()
        Flyway.configure()
            .dataSource(container.jdbcUrl, container.username, container.password)
            .locations("classpath:db/migration", "classpath:db/dev")
            .load()
            .migrate()
        dsl = DSL.using(container.jdbcUrl, container.username, container.password)
        social = JooqSocialRepository(dsl, Dispatchers.IO)
        guilds = JooqGuildRepository(dsl, Dispatchers.IO)
        characters = JooqCharacterRepository(dsl, Dispatchers.IO)
      }

      afterSpec { container.stop() }

      test("both of an account's lists come back after a restart") {
        val before = SocialStore(social)
        before.addFriend(41, "Cynthia")
        before.addFriend(41, "Barry")
        before.block(41, "Troll")
        before.removeFriend(41, "Barry") shouldBe true

        val after = SocialStore(social)
        after.load(41)
        after.getFriends(41) shouldBe setOf("Cynthia")
        after.getBlocked(41) shouldBe setOf("Troll")
      }

      test("adding a name twice leaves one of it") {
        val store = SocialStore(social)
        store.addFriend(42, "Rowan")
        store.addFriend(42, "Rowan")

        social.contacts(42, SOCIAL_KIND_FRIEND) shouldBe listOf("Rowan")
      }

      test("a guild comes back with its roster, its rank table and its log") {
        val leader = character("Boss")
        val grunt = character("Grunt")

        val before = GuildStore(guilds)
        val founded = before.createGuild("Galactic", "GAL", leader, "Boss")
        before.addMember(founded, GuildMember(grunt, "Grunt", GuildRank.GRUNT, leader = false))
        before.setMotd(founded, "Meet at the lake")
        before.setRankLabel(founded, GuildRank.GRUNT.ordinal, "Recruit")
        before.setPermissions(
            founded,
            mapOf(GuildRank.EXECUTIVE to setOf(GuildPermission.INVITE, GuildPermission.KICK)),
        )

        val after = GuildStore(guilds)
        after.load()
        val guild = after.getGuildForChar(leader)
        guild.shouldNotBeNull()
        guild.id shouldBe founded.id
        guild.name shouldBe "Galactic"
        guild.tag shouldBe "GAL"
        guild.message shouldBe "Meet at the lake"
        guild.foundedAt shouldBe founded.foundedAt
        guild.members.map { it.id } shouldBe listOf(leader, grunt)
        guild.members.map { it.rank } shouldBe listOf(GuildRank.BOSS, GuildRank.GRUNT)
        guild.members.map { it.leader } shouldBe listOf(true, false)
        guild.rankNames[GuildRank.GRUNT.ordinal] shouldBe "Recruit"
        guild.maskOf(GuildRank.EXECUTIVE) shouldBe
            ((1 shl GuildPermission.INVITE.ordinal) or (1 shl GuildPermission.KICK.ordinal))
                .toShort()
        guild.maskOf(GuildRank.COMMANDER) shouldBe 0
        guild.activityLog.map { it.type } shouldBe
            listOf(GuildActivityType.FOUNDED, GuildActivityType.JOINED)
        guild.activityLog.map { it.actor } shouldBe listOf("Boss", "Grunt")
        // The second store found the member through the roster it read, not through a lookup it
        // was handed: that lookup is what a restart used to lose.
        after.getGuildForChar(grunt) shouldBe guild
      }

      test("a handed-over guild comes back with the new leader holding it") {
        val old = character("Old")
        val heir = character("Heir")

        val before = GuildStore(guilds)
        val guild = before.createGuild("Rockets", "RKT", old, "Old")
        before.addMember(guild, GuildMember(heir, "Heir", GuildRank.OFFICER, leader = false))
        before.transferLeadership(guild, heir)

        val after = GuildStore(guilds)
        after.load()
        val seats = after.getGuildForChar(heir)!!.members.associateBy { it.id }
        seats.getValue(heir).rank shouldBe GuildRank.BOSS
        seats.getValue(heir).leader shouldBe true
        seats.getValue(old).rank shouldBe GuildRank.EXECUTIVE
        seats.getValue(old).leader shouldBe false
      }

      test("a disbanded guild is gone, and its members are in none") {
        val leader = character("Doomed")

        val before = GuildStore(guilds)
        val guild = before.createGuild("Doomed", "DMD", leader, "Doomed")
        before.disbandGuild(leader)

        val after = GuildStore(guilds)
        after.load()
        after.getGuildForChar(leader) shouldBe null
        guilds.loadAll().none { it.id == guild.id } shouldBe true
      }

      /** Leaving frees the character to found or join another one, which the lookup enforces. */
      test("a member who left can join a second guild") {
        val leaver = character("Leaver")
        val host = character("Host")

        val store = GuildStore(guilds)
        val first = store.createGuild("First", "ONE", leaver, "Leaver")
        store.leaveGuild(leaver)
        val second = store.createGuild("Second", "TWO", host, "Host")
        store.addMember(second, GuildMember(leaver, "Leaver", GuildRank.GRUNT, leader = false))

        val after = GuildStore(guilds)
        after.load()
        after.getGuildForChar(leaver)?.id shouldBe second.id
        after.getGuildForChar(host)?.id shouldBe second.id
        first.members.none { it.id == leaver } shouldBe true
      }
    })
