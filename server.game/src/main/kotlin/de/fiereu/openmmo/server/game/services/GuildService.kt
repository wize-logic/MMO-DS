package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.enums.GuildPermission
import de.fiereu.openmmo.common.enums.GuildRank
import de.fiereu.openmmo.net.game.packets.LocalCharacterDeltaPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildActivityLogEntry
import de.fiereu.openmmo.net.game.packets.guild.GuildActivityLogPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildActivityLogPageRequestPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildAppearance
import de.fiereu.openmmo.net.game.packets.guild.GuildCreatePacket
import de.fiereu.openmmo.net.game.packets.guild.GuildDisbandPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildInvitePacket
import de.fiereu.openmmo.net.game.packets.guild.GuildLeavePacket
import de.fiereu.openmmo.net.game.packets.guild.GuildMemberEntry
import de.fiereu.openmmo.net.game.packets.guild.GuildMemberKickPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildMemberPresencePacket
import de.fiereu.openmmo.net.game.packets.guild.GuildMemberRankAssignPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildMembershipPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildMotdUpdatePacket
import de.fiereu.openmmo.net.game.packets.guild.GuildProfileData
import de.fiereu.openmmo.net.game.packets.guild.GuildProfileSyncPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildRankLabelUpdatePacket
import de.fiereu.openmmo.net.game.packets.guild.GuildRankPermissionUpdatePacket
import de.fiereu.openmmo.net.game.packets.guild.SyncGuildMembersPacket
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.Guild
import de.fiereu.openmmo.server.game.storage.GuildMember
import de.fiereu.openmmo.server.game.storage.GuildStore
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

private const val GUILD_FOUND_COST = 15000

// The entries list is length-prefixed with a single byte, so a page holds at most 255 entries.
private const val MAX_ACTIVITY_LOG_ENTRIES = 255

/** What the text fields hold. Each is decoded with no ceiling and copied to every member. */
private const val MAX_GUILD_NAME_CHARS = 24
private const val MAX_GUILD_TAG_CHARS = 8
private const val MAX_GUILD_MOTD_CHARS = 200
private const val MAX_RANK_LABEL_CHARS = 16

/** The ranks [Guild.permMasks] carries, in its order. The Boss is absent: they hold the guild. */
private val PERM_MASK_ORDER =
    listOf(
        GuildRank.EXECUTIVE,
        GuildRank.COMMANDER,
        GuildRank.OFFICER,
        GuildRank.MEMBER,
        GuildRank.GRUNT,
    )

@Singleton
class GuildService
@Inject
constructor(
    private val guildStore: GuildStore,
    private val characterStore: CharacterStore,
    private val sessionRegistry: SessionRegistry,
) {

  fun sendGuild(ctx: SessionContext) {
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val guild = guildStore.getGuildForChar(charId)
    if (guild == null) {
      ctx.send(GuildMembershipPacket(inGuild = false, profile = null))
      return
    }
    ctx.send(buildMembership(guild))
    ctx.send(buildMemberSync(guild))
  }

  fun announcePresence(charId: Long, online: Boolean) {
    val guild = guildStore.getGuildForChar(charId) ?: return
    val packet = GuildMemberPresencePacket(memberId = charId, online = online)
    for (member in guild.members) {
      if (member.id == charId) continue
      sessionRegistry.getByCharacterId(member.id)?.send(packet)
    }
  }

  suspend fun onCreateGuild(event: PacketEvent<GuildCreatePacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val stored = characterStore.getCharacter(charId) ?: return
    val packet = event.packet
    log.info {
      "CreateGuild name='${packet.guildName}' tag='${packet.guildTag}' char=$charId money=${stored.info.money}"
    }
    val name = packet.guildName.trim().take(MAX_GUILD_NAME_CHARS)
    val tag = packet.guildTag.trim().take(MAX_GUILD_TAG_CHARS)
    if (name.isEmpty() || tag.isEmpty()) return
    if (guildStore.getGuildForChar(charId) != null) {
      log.info { "CreateGuild refused: char=$charId already in a guild" }
      return
    }
    if (stored.info.money < GUILD_FOUND_COST) {
      log.info { "Insufficient funds to found a guild (need $GUILD_FOUND_COST)" }
      return
    }
    if (!characterStore.addMoney(charId, -GUILD_FOUND_COST)) return
    val after = characterStore.getCharacter(charId)
    if (after != null) ctx.send(LocalCharacterDeltaPacket(money = after.info.money))
    val guild = guildStore.createGuild(name, tag, charId, stored.info.name)
    ctx.send(buildMembership(guild))
    ctx.send(buildMemberSync(guild))
  }

  fun onActivityLogPageRequest(event: PacketEvent<GuildActivityLogPageRequestPacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val (guild, self) = seatOf(charId) ?: return
    if (!may(guild, self, GuildPermission.TEAM_LOG)) return
    val page = event.packet.pageIndex.toInt()
    log.info { "Guild activity log page=$page requested." }
    val packet = buildActivityLog(guild)
    log.info {
      "Sending guild activity log guild=${guild.id} sent=${packet.entries.size} total=${packet.totalCount}"
    }
    ctx.send(packet)
  }

  fun onGuildInvite(event: PacketEvent<GuildInvitePacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val (guild, self) = seatOf(charId) ?: return
    val target = event.packet.targetName
    log.info { "GuildInvite char=$charId target='$target'" }
    if (!may(guild, self, GuildPermission.INVITE)) return
    if (target.isEmpty()) return
    if (guild.members.any { it.name.equals(target, ignoreCase = true) }) return
    val resolved = resolveOnline(target)
    if (resolved == null) {
      log.info { "GuildInvite target='$target' is not in the world" }
      return
    }
    if (guildStore.getGuildForChar(resolved.first) != null) {
      log.info { "GuildInvite target='$target' already in a guild" }
      return
    }
    guildStore.addMember(
        guild,
        GuildMember(resolved.first, resolved.second, GuildRank.GRUNT, leader = false),
    )
    sessionRegistry.getByCharacterId(resolved.first)?.let { invited ->
      invited.send(buildMembership(guild))
    }
    broadcastMembers(guild)
  }

  fun onRankAssign(event: PacketEvent<GuildMemberRankAssignPacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val (guild, self) = seatOf(charId) ?: return
    val rank = GuildRank.entries.getOrNull(event.packet.rankOrdinal) ?: return
    // Who holds which seat is the leader's to decide. Every other member sending this packet was
    // promoting themselves.
    if (!isLeader(guild, self)) return
    val targetId = event.packet.memberEntityId
    if (targetId == charId) {
      log.info { "char=$charId asked to change their own rank in guild ${guild.id}" }
      return
    }
    if (guild.members.none { it.id == targetId }) {
      log.info { "char=$charId named $targetId, who is not in guild ${guild.id}" }
      return
    }
    if (rank == GuildRank.BOSS) {
      guildStore.transferLeadership(guild, targetId)
      log.info { "Leadership transferred char=$charId newLeader=$targetId" }
    } else {
      guildStore.setMemberRank(guild, targetId, rank)
      log.info { "RankAssign char=$charId member=$targetId rank=$rank" }
    }
    broadcastMembers(guild)
  }

  fun onKick(event: PacketEvent<GuildMemberKickPacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val (guild, self) = seatOf(charId) ?: return
    val targetId = event.packet.targetEntityId
    log.info { "Kick char=$charId member=$targetId" }
    if (!may(guild, self, GuildPermission.KICK)) return
    val target = guild.members.firstOrNull { it.id == targetId }
    if (target == null) {
      log.info { "char=$charId asked to kick $targetId, who is not in guild ${guild.id}" }
      return
    }
    // Walking out is Leave, the leader cannot be removed, and a kick only reaches downwards.
    if (targetId == charId) return
    if (target.leader || target.rank.ordinal >= self.rank.ordinal) {
      log.info {
        "char=$charId (${self.rank}) may not kick $targetId (${target.rank}) in guild ${guild.id}"
      }
      return
    }
    guildStore.removeMember(guild, targetId)
    sessionRegistry.getByCharacterId(targetId)?.send(GuildMembershipPacket(false, null))
    broadcastMembers(guild)
  }

  fun onLeave(event: PacketEvent<GuildLeavePacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val guild = guildStore.getGuildForChar(charId)
    log.info { "GuildLeave char=$charId" }
    guildStore.leaveGuild(charId)
    ctx.send(GuildMembershipPacket(inGuild = false, profile = null))
    if (guild != null) broadcastMembers(guild)
  }

  fun onDisband(event: PacketEvent<GuildDisbandPacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val initiate = event.packet.initiate
    val seat = seatOf(charId)
    log.info { "GuildDisband char=$charId guild=${seat?.first?.id} initiate=$initiate" }
    if (seat == null) return
    val (guild, self) = seat
    if (!initiate) return
    // The flag in the packet is the client asking itself whether the player meant it. It was the
    // only check, so any member could destroy the guild for everyone in it.
    if (!isLeader(guild, self)) return
    val members = guild.members.toList()
    guildStore.disbandGuild(charId)
    log.info { "Guild ${guild.id} disbanded by char=$charId" }
    val pkt = GuildMembershipPacket(inGuild = false, profile = null)
    for (member in members) {
      sessionRegistry.getByCharacterId(member.id)?.send(pkt)
    }
  }

  fun onMotdUpdate(event: PacketEvent<GuildMotdUpdatePacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val (guild, self) = seatOf(charId) ?: return
    if (!may(guild, self, GuildPermission.TEAM_MESSAGES)) return
    guild.message = event.packet.motdText.take(MAX_GUILD_MOTD_CHARS)
    log.info { "GuildMotdUpdate char=$charId guild=${guild.id} motd='${guild.message}'" }
    broadcastProfile(guild)
  }

  fun onRankLabelUpdate(event: PacketEvent<GuildRankLabelUpdatePacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val (guild, self) = seatOf(charId) ?: return
    if (!isLeader(guild, self)) return
    val ordinal = event.packet.rankOrdinal
    if (ordinal !in guild.rankNames.indices) return
    guild.rankNames[ordinal] = event.packet.rankLabel.take(MAX_RANK_LABEL_CHARS)
    log.info {
      "GuildRankLabelUpdate char=$charId guild=${guild.id} rank=$ordinal label='${event.packet.rankLabel}'"
    }
    broadcastProfile(guild)
  }

  fun onRankPermissionUpdate(event: PacketEvent<GuildRankPermissionUpdatePacket>) {
    val state = event.session.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val (guild, self) = seatOf(charId) ?: return
    // This table is what every check above reads. Any member could rewrite it, which made them all
    // decorative.
    if (!isLeader(guild, self)) return
    val sanitized =
        event.packet.permissions.mapValues { (rank, perms) ->
          if (rank == GuildRank.GRUNT && GuildPermission.KICK in perms) {
            log.warn { "Rejecting KICK permission for GRUNT" }
            perms - GuildPermission.KICK
          } else {
            perms
          }
        }
    guild.permissions.clear()
    guild.permissions.putAll(sanitized)
    val order =
        listOf(
            GuildRank.EXECUTIVE,
            GuildRank.COMMANDER,
            GuildRank.OFFICER,
            GuildRank.MEMBER,
            GuildRank.GRUNT,
        )
    for (i in order.indices) {
      val mask =
          sanitized[order[i]].orEmpty().fold(0) { acc, p -> acc or (1 shl p.ordinal) }.toShort()
      if (i < guild.permMasks.size) guild.permMasks[i] = mask
    }
    log.info { "RankPermUpdate char=$charId perms=$sanitized" }
    broadcastProfile(guild)
  }

  /**
   * The guild [charId] belongs to and the seat they hold in it. Every verb starts here. They used
   * to start at "which guild is this character in" and then act on whatever id the packet named.
   */
  private fun seatOf(charId: Long): Pair<Guild, GuildMember>? {
    val guild = guildStore.getGuildForChar(charId) ?: return null
    val member = guild.members.firstOrNull { it.id == charId } ?: return null
    return guild to member
  }

  /**
   * What a rank may do, read from the guild's own table. It is the copy the client is sent, so a
   * refusal is what that player's window drew as unavailable. Only the leader may edit it.
   */
  private fun permissionsOf(guild: Guild, rank: GuildRank): Set<GuildPermission> {
    if (rank == GuildRank.BOSS) return GuildPermission.entries.toSet()
    val index = PERM_MASK_ORDER.indexOf(rank)
    if (index < 0 || index >= guild.permMasks.size) return emptySet()
    val mask = guild.permMasks[index].toInt()
    return GuildPermission.entries.filterTo(mutableSetOf()) { (mask shr it.ordinal) and 1 == 1 }
  }

  private fun may(guild: Guild, member: GuildMember, permission: GuildPermission): Boolean {
    if (permission in permissionsOf(guild, member.rank)) return true
    log.info { "char=${member.id} (${member.rank}) may not $permission in guild ${guild.id}" }
    return false
  }

  /** The leader's own verbs: the ones that change who holds the guild, or whether it exists. */
  private fun isLeader(guild: Guild, member: GuildMember): Boolean {
    if (member.leader || member.rank == GuildRank.BOSS) return true
    log.info { "char=${member.id} (${member.rank}) is not the leader of guild ${guild.id}" }
    return false
  }

  private fun resolveOnline(name: String): Pair<Long, String>? {
    for (id in sessionRegistry.onlineCharacterIds()) {
      val stored = characterStore.getCharacter(id) ?: continue
      if (stored.info.name.equals(name, ignoreCase = true)) return id to stored.info.name
    }
    return null
  }

  private fun broadcastMembers(guild: Guild) {
    val pkt = buildMemberSync(guild)
    for (member in guild.members) {
      sessionRegistry.getByCharacterId(member.id)?.send(pkt)
    }
  }

  private fun broadcastProfile(guild: Guild) {
    val pkt = GuildProfileSyncPacket(buildProfile(guild))
    for (member in guild.members) {
      sessionRegistry.getByCharacterId(member.id)?.send(pkt)
    }
  }

  private fun buildProfile(guild: Guild): GuildProfileData =
      GuildProfileData(
          guildId = guild.id,
          name = guild.name,
          tag = guild.tag,
          foundedAt = guild.foundedAt,
          message = guild.message,
          unknown = guild.unknown,
          perms = guild.permMasks.toList(),
          expiry = guild.expiry,
          rankNames = guild.rankNames.toList(),
      )

  private fun buildMembership(guild: Guild): GuildMembershipPacket =
      GuildMembershipPacket(inGuild = true, profile = buildProfile(guild))

  private fun buildMemberSync(guild: Guild): SyncGuildMembersPacket =
      SyncGuildMembersPacket(
          replace = true,
          members =
              guild.members.map { member ->
                GuildMemberEntry(
                    rank = member.rank.ordinal.toByte(),
                    entityId = member.id,
                    joinedAt = 0,
                    appearance =
                        GuildAppearance(
                            name = member.name,
                            unk0 = 0,
                            lastSeen = 0,
                            kind = 0,
                            packedSlots = 0,
                            sprite = List(4) { 0 },
                        ),
                    online = sessionRegistry.getByCharacterId(member.id) != null,
                )
              },
      )

  private fun buildActivityLog(guild: Guild): GuildActivityLogPacket =
      GuildActivityLogPacket(
          totalCount = guild.activityLog.size.toShort(),
          entries =
              guild.activityLog.takeLast(MAX_ACTIVITY_LOG_ENTRIES).map { entry ->
                GuildActivityLogEntry(
                    type = entry.type.code,
                    actor = entry.actor,
                    target = entry.target,
                    timestamp = entry.timestamp,
                )
              },
      )
}
