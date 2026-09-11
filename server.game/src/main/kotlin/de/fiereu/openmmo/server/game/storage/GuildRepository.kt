package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.common.enums.GuildRank
import de.fiereu.openmmo.db.game.tables.references.GUILDS
import de.fiereu.openmmo.db.game.tables.references.GUILD_ACTIVITY
import de.fiereu.openmmo.db.game.tables.references.GUILD_MEMBERS
import de.fiereu.openmmo.db.game.tables.references.GUILD_RANKS
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicLong
import javax.inject.Inject
import javax.inject.Named
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import org.jooq.DSLContext
import org.jooq.impl.DSL

/** The newest entries of a team's log, which is all its activity page can draw. */
const val GUILD_ACTIVITY_KEPT = 255

/** Every guild the server holds, and the roster, rank table and log of each. */
interface GuildRepository {
  suspend fun loadAll(): List<Guild>

  /**
   * Writes a new guild with its founder, its rank table and its first log entry, and answers the id
   * the database gave it.
   */
  suspend fun insert(guild: Guild): Long

  suspend fun delete(guildId: Long)

  suspend fun addMember(guildId: Long, member: GuildMember)

  suspend fun removeMember(guildId: Long, characterId: Long)

  /** The rank and the leader flag of one member, who is named by their character id. */
  suspend fun setMemberSeat(member: GuildMember)

  /** The fields of the profile a leader may edit. */
  suspend fun updateProfile(guild: Guild)

  suspend fun setRankLabel(guildId: Long, rankOrdinal: Int, label: String)

  suspend fun setRankMask(guildId: Long, rankOrdinal: Int, mask: Short)

  suspend fun appendActivity(guildId: Long, entry: GuildLogEntry)
}

class JooqGuildRepository
@Inject
constructor(
    private val dsl: DSLContext,
    @param:Named("db") private val dispatcher: CoroutineDispatcher,
) : GuildRepository {

  override suspend fun loadAll(): List<Guild> =
      withContext(dispatcher) {
        val members = dsl.selectFrom(GUILD_MEMBERS).orderBy(GUILD_MEMBERS.ID).fetch()
        val ranks = dsl.selectFrom(GUILD_RANKS).orderBy(GUILD_RANKS.RANK_ORDINAL).fetch()
        val activity = dsl.selectFrom(GUILD_ACTIVITY).orderBy(GUILD_ACTIVITY.ID).fetch()

        val rostersOf = members.groupBy { it.guildId }
        val ranksOf = ranks.groupBy { it.guildId }
        val logOf = activity.groupBy { it.guildId }

        dsl.selectFrom(GUILDS).orderBy(GUILDS.ID).fetch().map { row ->
          val guild =
              Guild(
                  id = row.id!!,
                  name = row.name,
                  tag = row.tag,
                  members =
                      rostersOf[row.id].orEmpty().mapTo(mutableListOf()) {
                        GuildMember(
                            id = it.characterId,
                            name = it.name,
                            // A rank the enum no longer has seats the member at the bottom
                            // rather than dropping them out of a roster everyone else can see.
                            rank =
                                GuildRank.entries.getOrNull(it.rankOrdinal.toInt())
                                    ?: GuildRank.GRUNT,
                            leader = it.leader == true,
                        )
                      },
                  message = row.message,
                  unknown = row.unknown!!,
                  foundedAt = row.foundedAt,
                  expiry = row.expiry!!,
              )
          for (rank in ranksOf[row.id].orEmpty()) {
            val ordinal = rank.rankOrdinal.toInt()
            if (ordinal in guild.rankNames.indices) guild.rankNames[ordinal] = rank.label!!
            val at = PERM_MASK_RANKS.indexOfFirst { it.ordinal == ordinal }
            if (at in guild.permMasks.indices) guild.permMasks[at] = rank.permMask!!
          }
          for (entry in logOf[row.id].orEmpty().takeLast(GUILD_ACTIVITY_KEPT)) {
            val type = GuildActivityType.ofCode(entry.entryType.toInt()) ?: continue
            guild.activityLog.add(
                GuildLogEntry(type, entry.actor, entry.target, entry.happenedAt),
            )
          }
          guild
        }
      }

  override suspend fun insert(guild: Guild): Long =
      withContext(dispatcher) {
        dsl.transactionResult { tx ->
          val db = DSL.using(tx)
          val id =
              db.insertInto(GUILDS)
                  .set(GUILDS.NAME, guild.name)
                  .set(GUILDS.TAG, guild.tag)
                  .set(GUILDS.MESSAGE, guild.message)
                  .set(GUILDS.UNKNOWN, guild.unknown)
                  .set(GUILDS.FOUNDED_AT, guild.foundedAt)
                  .set(GUILDS.EXPIRY, guild.expiry)
                  .returningResult(GUILDS.ID)
                  .fetchOne()
                  ?.value1() ?: error("the database gave the guild no id")
          for (ordinal in guild.rankNames.indices) {
            val rank = GuildRank.entries[ordinal]
            db.insertInto(GUILD_RANKS)
                .set(GUILD_RANKS.GUILD_ID, id)
                .set(GUILD_RANKS.RANK_ORDINAL, ordinal.toShort())
                .set(GUILD_RANKS.LABEL, guild.rankNames[ordinal])
                .set(GUILD_RANKS.PERM_MASK, guild.maskOf(rank))
                .execute()
          }
          for (member in guild.members) insertMember(db, id, member)
          for (entry in guild.activityLog) insertActivity(db, id, entry)
          id
        }
      }

  override suspend fun delete(guildId: Long) {
    withContext(dispatcher) {
      // The roster, the rank table and the log all cascade off this one row.
      dsl.deleteFrom(GUILDS).where(GUILDS.ID.eq(guildId)).execute()
    }
  }

  override suspend fun addMember(guildId: Long, member: GuildMember) {
    withContext(dispatcher) { insertMember(dsl, guildId, member) }
  }

  override suspend fun removeMember(guildId: Long, characterId: Long) {
    withContext(dispatcher) {
      dsl.deleteFrom(GUILD_MEMBERS)
          .where(GUILD_MEMBERS.GUILD_ID.eq(guildId).and(GUILD_MEMBERS.CHARACTER_ID.eq(characterId)))
          .execute()
    }
  }

  override suspend fun setMemberSeat(member: GuildMember) {
    withContext(dispatcher) {
      dsl.update(GUILD_MEMBERS)
          .set(GUILD_MEMBERS.RANK_ORDINAL, member.rank.ordinal.toShort())
          .set(GUILD_MEMBERS.LEADER, member.leader)
          .where(GUILD_MEMBERS.CHARACTER_ID.eq(member.id))
          .execute()
    }
  }

  override suspend fun updateProfile(guild: Guild) {
    withContext(dispatcher) {
      dsl.update(GUILDS)
          .set(GUILDS.MESSAGE, guild.message)
          .set(GUILDS.UNKNOWN, guild.unknown)
          .set(GUILDS.EXPIRY, guild.expiry)
          .where(GUILDS.ID.eq(guild.id))
          .execute()
    }
  }

  override suspend fun setRankLabel(guildId: Long, rankOrdinal: Int, label: String) {
    withContext(dispatcher) {
      dsl.insertInto(GUILD_RANKS)
          .set(GUILD_RANKS.GUILD_ID, guildId)
          .set(GUILD_RANKS.RANK_ORDINAL, rankOrdinal.toShort())
          .set(GUILD_RANKS.LABEL, label)
          .onDuplicateKeyUpdate()
          .set(GUILD_RANKS.LABEL, label)
          .execute()
    }
  }

  override suspend fun setRankMask(guildId: Long, rankOrdinal: Int, mask: Short) {
    withContext(dispatcher) {
      dsl.insertInto(GUILD_RANKS)
          .set(GUILD_RANKS.GUILD_ID, guildId)
          .set(GUILD_RANKS.RANK_ORDINAL, rankOrdinal.toShort())
          .set(GUILD_RANKS.PERM_MASK, mask)
          .onDuplicateKeyUpdate()
          .set(GUILD_RANKS.PERM_MASK, mask)
          .execute()
    }
  }

  override suspend fun appendActivity(guildId: Long, entry: GuildLogEntry) {
    withContext(dispatcher) { insertActivity(dsl, guildId, entry) }
  }

  private fun insertMember(db: DSLContext, guildId: Long, member: GuildMember) {
    db.insertInto(GUILD_MEMBERS)
        .set(GUILD_MEMBERS.GUILD_ID, guildId)
        .set(GUILD_MEMBERS.CHARACTER_ID, member.id)
        .set(GUILD_MEMBERS.NAME, member.name)
        .set(GUILD_MEMBERS.RANK_ORDINAL, member.rank.ordinal.toShort())
        .set(GUILD_MEMBERS.LEADER, member.leader)
        .execute()
  }

  private fun insertActivity(db: DSLContext, guildId: Long, entry: GuildLogEntry) {
    db.insertInto(GUILD_ACTIVITY)
        .set(GUILD_ACTIVITY.GUILD_ID, guildId)
        .set(GUILD_ACTIVITY.ENTRY_TYPE, entry.type.code.toShort())
        .set(GUILD_ACTIVITY.ACTOR, entry.actor)
        .set(GUILD_ACTIVITY.TARGET, entry.target)
        .set(GUILD_ACTIVITY.HAPPENED_AT, entry.timestamp)
        .execute()
  }
}

/** The guilds a test builds without a database. */
class InMemoryGuildRepository : GuildRepository {
  private val nextId = AtomicLong(1)
  private val rows = ConcurrentHashMap<Long, Guild>()

  override suspend fun loadAll(): List<Guild> = rows.values.sortedBy { it.id }.map { it.deepCopy() }

  override suspend fun insert(guild: Guild): Long {
    val id = nextId.getAndIncrement()
    rows[id] = guild.deepCopy().also { it.id = id }
    return id
  }

  override suspend fun delete(guildId: Long) {
    rows.remove(guildId)
  }

  override suspend fun addMember(guildId: Long, member: GuildMember) {
    rows[guildId]?.members?.add(member)
  }

  override suspend fun removeMember(guildId: Long, characterId: Long) {
    rows[guildId]?.members?.removeAll { it.id == characterId }
  }

  override suspend fun setMemberSeat(member: GuildMember) {
    for (guild in rows.values) {
      val at = guild.members.indexOfFirst { it.id == member.id }
      if (at >= 0) guild.members[at] = member
    }
  }

  override suspend fun updateProfile(guild: Guild) {
    rows[guild.id]?.let {
      it.message = guild.message
      it.unknown = guild.unknown
      it.expiry = guild.expiry
    }
  }

  override suspend fun setRankLabel(guildId: Long, rankOrdinal: Int, label: String) {
    rows[guildId]?.rankNames?.let { if (rankOrdinal in it.indices) it[rankOrdinal] = label }
  }

  override suspend fun setRankMask(guildId: Long, rankOrdinal: Int, mask: Short) {
    val at = PERM_MASK_RANKS.indexOfFirst { it.ordinal == rankOrdinal }
    rows[guildId]?.permMasks?.let { if (at in it.indices) it[at] = mask }
  }

  override suspend fun appendActivity(guildId: Long, entry: GuildLogEntry) {
    rows[guildId]?.activityLog?.add(entry)
  }

  /** A guild whose lists are its own, so a store holding one cannot edit the stored one. */
  private fun Guild.deepCopy(): Guild =
      copy(
          members = members.toMutableList(),
          activityLog = activityLog.toMutableList(),
          permMasks = permMasks.toMutableList(),
          rankNames = rankNames.toMutableList(),
      )
}
