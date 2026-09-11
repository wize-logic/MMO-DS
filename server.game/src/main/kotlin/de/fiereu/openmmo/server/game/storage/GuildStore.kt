package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.common.enums.GuildPermission
import de.fiereu.openmmo.common.enums.GuildRank
import java.time.Instant
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

data class GuildMember(
    val id: Long,
    val name: String,
    val rank: GuildRank,
    val leader: Boolean,
)

// Wire codes are provisional and unverified against the real client.
enum class GuildActivityType(val code: Int) {
  FOUNDED(0),
  JOINED(1);

  companion object {
    /** A code the table holds that no entry claims is dropped rather than guessed at. */
    fun ofCode(code: Int): GuildActivityType? = entries.firstOrNull { it.code == code }
  }
}

data class GuildLogEntry(
    val type: GuildActivityType,
    val actor: String,
    val target: String,
    val timestamp: Int,
)

/**
 * The ranks [Guild.permMasks] carries, in the order it carries them. [GuildRank.BOSS] is absent
 * because the leader is not permissioned: they hold the guild.
 */
val PERM_MASK_RANKS: List<GuildRank> =
    listOf(
        GuildRank.EXECUTIVE,
        GuildRank.COMMANDER,
        GuildRank.OFFICER,
        GuildRank.MEMBER,
        GuildRank.GRUNT,
    )

/** What a guild's ranks may do before its leader has said otherwise. */
val DEFAULT_PERM_MASKS: List<Short> = listOf(5, 5, 5, 0, 0)

/** A team. */
data class Guild(
    var id: Long,
    val name: String,
    val tag: String,
    val members: MutableList<GuildMember>,
    val activityLog: MutableList<GuildLogEntry> = mutableListOf(),
    var message: String = "Your Team has been successfully created!",
    var unknown: Int = 0,
    var foundedAt: Int = Instant.now().epochSecond.toInt(),
    var expiry: Int = 0,
    val permMasks: MutableList<Short> = DEFAULT_PERM_MASKS.toMutableList(),
    val rankNames: MutableList<String> = MutableList(GuildRank.entries.size) { "" },
) {
  /** The mask this guild gives [rank], which is the one the member's own window drew. */
  fun maskOf(rank: GuildRank): Short {
    val at = PERM_MASK_RANKS.indexOf(rank)
    return if (at in permMasks.indices) permMasks[at] else 0
  }
}

/** Every guild on the server, held in memory over the tables that outlive the process. */
@Singleton
class GuildStore @Inject constructor(private val repository: GuildRepository) {
  private val guilds = ConcurrentHashMap<Long, Guild>()
  private val guildByChar = ConcurrentHashMap<Long, Long>()

  suspend fun load() {
    for (guild in repository.loadAll()) {
      guilds[guild.id] = guild
      for (member in guild.members) guildByChar[member.id] = guild.id
    }
  }

  /** How many teams this server holds, which is what [load] has to report having read. */
  fun count(): Int = guilds.size

  suspend fun createGuild(name: String, tag: String, leaderId: Long, leaderName: String): Guild {
    val leader = GuildMember(leaderId, leaderName, GuildRank.BOSS, leader = true)
    val guild = Guild(0L, name, tag, mutableListOf(leader))
    guild.activityLog.add(GuildLogEntry(GuildActivityType.FOUNDED, leaderName, "", now()))
    guild.id = repository.insert(guild)
    guilds[guild.id] = guild
    guildByChar[leaderId] = guild.id
    return guild
  }

  fun getGuildForChar(charId: Long): Guild? = guildByChar[charId]?.let { guilds[it] }

  suspend fun addMember(guild: Guild, member: GuildMember) {
    val entry = GuildLogEntry(GuildActivityType.JOINED, member.name, "", now())
    repository.addMember(guild.id, member)
    repository.appendActivity(guild.id, entry)
    guild.members.add(member)
    guildByChar[member.id] = guild.id
    guild.activityLog.add(entry)
  }

  suspend fun setMemberRank(guild: Guild, entityId: Long, rank: GuildRank) {
    val index = guild.members.indexOfFirst { it.id == entityId }
    if (index < 0) return
    val seated = guild.members[index].copy(rank = rank)
    repository.setMemberSeat(seated)
    guild.members[index] = seated
  }

  /** Drop a member, if they are one of this guild's. */
  suspend fun removeMember(guild: Guild, entityId: Long) {
    if (guild.members.none { it.id == entityId }) return
    repository.removeMember(guild.id, entityId)
    guild.members.removeAll { it.id == entityId }
    guildByChar.remove(entityId, guild.id)
  }

  /**
   * Hand the guild to one of its own members. A name that is not a member changes nothing: the old
   * loop demoted whoever held the guild whether or not it found a successor, which left a guild
   * with no Boss and no way to appoint one.
   */
  suspend fun transferLeadership(guild: Guild, newLeaderId: Long) {
    if (guild.members.none { it.id == newLeaderId }) return
    for (i in guild.members.indices) {
      val member = guild.members[i]
      val seated =
          when {
            member.id == newLeaderId -> member.copy(rank = GuildRank.BOSS, leader = true)
            member.leader || member.rank == GuildRank.BOSS ->
                member.copy(rank = GuildRank.EXECUTIVE, leader = false)
            else -> member
          }
      if (seated == member) continue
      repository.setMemberSeat(seated)
      guild.members[i] = seated
    }
  }

  suspend fun leaveGuild(charId: Long) {
    val guildId = guildByChar.remove(charId) ?: return
    repository.removeMember(guildId, charId)
    guilds[guildId]?.members?.removeAll { it.id == charId }
  }

  suspend fun disbandGuild(charId: Long) {
    val guildId = guildByChar[charId] ?: return
    val guild = guilds.remove(guildId) ?: return
    repository.delete(guildId)
    for (member in guild.members) {
      guildByChar.remove(member.id)
    }
  }

  suspend fun setMotd(guild: Guild, message: String) {
    guild.message = message
    repository.updateProfile(guild)
  }

  suspend fun setRankLabel(guild: Guild, ordinal: Int, label: String) {
    if (ordinal !in guild.rankNames.indices) return
    guild.rankNames[ordinal] = label
    repository.setRankLabel(guild.id, ordinal, label)
  }

  /** Rewrite what every rank may do. */
  suspend fun setPermissions(guild: Guild, permissions: Map<GuildRank, Set<GuildPermission>>) {
    for (i in PERM_MASK_RANKS.indices) {
      val mask =
          permissions[PERM_MASK_RANKS[i]].orEmpty().fold(0) { acc, p -> acc or (1 shl p.ordinal) }
      if (i >= guild.permMasks.size) continue
      guild.permMasks[i] = mask.toShort()
      repository.setRankMask(guild.id, PERM_MASK_RANKS[i].ordinal, mask.toShort())
    }
  }

  private fun now(): Int = Instant.now().epochSecond.toInt()
}
