package de.fiereu.openmmo.server.game.storage

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.GuildPermission
import de.fiereu.openmmo.common.enums.GuildRank
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.net.game.packets.guild.GuildDisbandPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildMemberRankAssignPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildRankPermissionUpdatePacket
import de.fiereu.openmmo.server.game.services.GuildService
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import io.kotest.matchers.shouldNotBe
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

@OptIn(ExperimentalCoroutinesApi::class)
class GuildStoreTest :
    FunSpec({
      test("creating a guild registers the leader as a member and binds the lookup") {
        val store = GuildStore(InMemoryGuildRepository())
        store.getGuildForChar(100L) shouldBe null

        val guild = store.createGuild("Knights", "KNT", leaderId = 100L, leaderName = "Leader")
        guild.name shouldBe "Knights"
        guild.tag shouldBe "KNT"
        guild.members.map { it.id } shouldBe listOf(100L)
        guild.members.single().rank shouldBe GuildRank.BOSS
        store.getGuildForChar(100L) shouldBe guild
      }

      test("invited members append to the roster") {
        val store = GuildStore(InMemoryGuildRepository())
        val guild = store.createGuild("Knights", "KNT", leaderId = 100L, leaderName = "Leader")
        store.addMember(guild, GuildMember(200L, "Grunt", GuildRank.GRUNT, leader = false))
        guild.members.map { it.name } shouldBe listOf("Leader", "Grunt")
        store.getGuildForChar(200L) shouldBe guild
      }

      test("rank assign updates a member and kick removes them") {
        val store = GuildStore(InMemoryGuildRepository())
        val guild = store.createGuild("Knights", "KNT", leaderId = 100L, leaderName = "Leader")
        store.addMember(guild, GuildMember(200L, "Recruit", GuildRank.GRUNT, leader = false))

        store.setMemberRank(guild, 200L, GuildRank.OFFICER)
        guild.members.single { it.id == 200L }.rank shouldBe GuildRank.OFFICER

        store.removeMember(guild, 200L)
        guild.members.map { it.id } shouldBe listOf(100L)
      }

      test("transferring leadership promotes the target and demotes the old Boss to Executive") {
        val store = GuildStore(InMemoryGuildRepository())
        val guild = store.createGuild("Knights", "KNT", leaderId = 100L, leaderName = "Leader")
        store.addMember(guild, GuildMember(200L, "Heir", GuildRank.OFFICER, leader = false))

        store.transferLeadership(guild, 200L)

        val byId = guild.members.associateBy { it.id }
        byId.getValue(200L).rank shouldBe GuildRank.BOSS
        byId.getValue(200L).leader shouldBe true
        byId.getValue(100L).rank shouldBe GuildRank.EXECUTIVE
        byId.getValue(100L).leader shouldBe false
      }

      /**
       * A kick used to clear the membership lookup for whatever id it was handed, whether or not
       * that character was in this guild.
       */
      test("a kick aimed outside the guild leaves the other guild alone") {
        val store = GuildStore(InMemoryGuildRepository())
        val mine = store.createGuild("Knights", "KNT", leaderId = 100L, leaderName = "Leader")
        val theirs = store.createGuild("Rockets", "RKT", leaderId = 200L, leaderName = "Boss")

        store.removeMember(mine, 200L)

        store.getGuildForChar(200L) shouldBe theirs
        theirs.members.map { it.id } shouldBe listOf(200L)
      }

      /**
       * The old loop demoted whoever held the guild whether or not it found a successor, so naming
       * anyone who was not a member left the guild with no Boss and no way to appoint one.
       */
      test("handing the guild to somebody who is not in it changes nothing") {
        val store = GuildStore(InMemoryGuildRepository())
        val guild = store.createGuild("Knights", "KNT", leaderId = 100L, leaderName = "Leader")

        store.transferLeadership(guild, 999L)

        guild.members.single().let {
          it.id shouldBe 100L
          it.rank shouldBe GuildRank.BOSS
          it.leader shouldBe true
        }
      }

      test("leave unbinds the leaver and disband removes the guild") {
        val store = GuildStore(InMemoryGuildRepository())
        store.createGuild("Knights", "KNT", leaderId = 100L, leaderName = "Leader")
        store.leaveGuild(100L)
        store.getGuildForChar(100L) shouldBe null

        store.createGuild("Knights", "KNT", leaderId = 100L, leaderName = "Leader")
        store.disbandGuild(100L)
        store.getGuildForChar(100L) shouldBe null
      }

      /** What a member is allowed to do, which nothing asked before. */
      context("what a rank may do") {
        /** A guild with its leader and one Grunt, both online, and the service in front of it. */
        class Fixture(scope: CoroutineScope) {
          val guilds = GuildStore(InMemoryGuildRepository())
          val characters = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
          val sessions = SessionRegistry()
          val service = GuildService(guilds, characters, sessions)
          lateinit var bossSession: FakeSession
          lateinit var gruntSession: FakeSession
          var bossId = 0L
          var gruntId = 0L

          suspend fun open(): Fixture {
            bossId =
                characters.createCharacter(1, "Boss", CharacterGender.MALE, Region.SINNOH).info.id
            gruntId =
                characters.createCharacter(2, "Grunt", CharacterGender.MALE, Region.SINNOH).info.id
            bossSession = FakeSession(characterId = bossId)
            gruntSession = FakeSession(characterId = gruntId)
            sessions.bindCharacter(bossSession, bossId)
            sessions.bindCharacter(gruntSession, gruntId)
            val made = guilds.createGuild("Knights", "KNT", bossId, "Boss")
            guilds.addMember(made, GuildMember(gruntId, "Grunt", GuildRank.GRUNT, leader = false))
            return this
          }
        }

        test("a Grunt cannot make themselves the Boss") {
          runTest {
            val f = Fixture(backgroundScope).open()

            f.service.onRankAssign(
                PacketEvent(
                    GuildMemberRankAssignPacket(f.gruntId, GuildRank.BOSS.ordinal),
                    f.gruntSession,
                ))

            val after = f.guilds.getGuildForChar(f.gruntId)!!
            after.members.single { it.id == f.gruntId }.rank shouldBe GuildRank.GRUNT
            after.members.single { it.id == f.bossId }.leader shouldBe true
          }
        }

        test("a Grunt cannot disband the guild out from under everyone") {
          runTest {
            val f = Fixture(backgroundScope).open()

            f.service.onDisband(
                PacketEvent(
                    GuildDisbandPacket(initiate = true, ownerEntityId = f.bossId), f.gruntSession))

            f.guilds.getGuildForChar(f.bossId) shouldNotBe null
          }
        }

        test("the Boss can disband it") {
          runTest {
            val f = Fixture(backgroundScope).open()

            f.service.onDisband(
                PacketEvent(
                    GuildDisbandPacket(initiate = true, ownerEntityId = f.bossId), f.bossSession))

            f.guilds.getGuildForChar(f.bossId) shouldBe null
          }
        }

        /** The table any member could rewrite was what made every check above decorative. */
        test("a Grunt cannot grant themselves permissions") {
          runTest {
            val f = Fixture(backgroundScope).open()

            f.service.onRankPermissionUpdate(
                PacketEvent(
                    GuildRankPermissionUpdatePacket(
                        mapOf(GuildRank.GRUNT to setOf(GuildPermission.INVITE))),
                    f.gruntSession,
                ))

            f.guilds.getGuildForChar(f.gruntId)!!.permMasks shouldBe listOf<Short>(5, 5, 5, 0, 0)
          }
        }
      }
    })
