package de.fiereu.openmmo.server.game.storage

import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CopyOnWriteArraySet
import javax.inject.Inject
import javax.inject.Singleton

/**
 * An account's friends and blocked players, held in memory over a table that outlives the process.
 */
@Singleton
class SocialStore @Inject constructor(private val repository: SocialRepository) {
  private val friendsByUser = ConcurrentHashMap<Int, CopyOnWriteArraySet<String>>()
  private val blockedByUser = ConcurrentHashMap<Int, CopyOnWriteArraySet<String>>()

  /** Reads one account's lists in. Does nothing once they are in. */
  suspend fun load(userId: Int) {
    if (friendsByUser.containsKey(userId)) return
    val friends = repository.contacts(userId, SOCIAL_KIND_FRIEND)
    val blocked = repository.contacts(userId, SOCIAL_KIND_BLOCKED)
    blockedByUser.putIfAbsent(userId, CopyOnWriteArraySet(blocked))
    // Last, because it is the marker: an account is loaded when its friends are.
    friendsByUser.putIfAbsent(userId, CopyOnWriteArraySet(friends))
  }

  fun getFriends(userId: Int): Set<String> = friendsByUser[userId] ?: emptySet()

  suspend fun addFriend(userId: Int, name: String) {
    load(userId)
    repository.add(userId, SOCIAL_KIND_FRIEND, name)
    friendsByUser.getValue(userId).add(name)
  }

  suspend fun removeFriend(userId: Int, name: String): Boolean {
    load(userId)
    val set = friendsByUser.getValue(userId)
    if (!set.contains(name)) return false
    repository.remove(userId, SOCIAL_KIND_FRIEND, name)
    return set.remove(name)
  }

  fun getBlocked(userId: Int): Set<String> = blockedByUser[userId] ?: emptySet()

  suspend fun block(userId: Int, name: String) {
    load(userId)
    repository.add(userId, SOCIAL_KIND_BLOCKED, name)
    blockedByUser.getValue(userId).add(name)
  }

  suspend fun unblock(userId: Int, name: String): Boolean {
    load(userId)
    val set = blockedByUser.getValue(userId)
    if (!set.contains(name)) return false
    repository.remove(userId, SOCIAL_KIND_BLOCKED, name)
    return set.remove(name)
  }
}
