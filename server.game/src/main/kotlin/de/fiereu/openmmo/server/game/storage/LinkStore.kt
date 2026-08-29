package de.fiereu.openmmo.server.game.storage

import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CopyOnWriteArrayList
import javax.inject.Inject
import javax.inject.Singleton

const val LINK_CAP = 4

data class LinkMember(
    val id: Long,
    val name: String,
)

class Link(
    var leaderId: Long,
    val members: MutableList<LinkMember> = CopyOnWriteArrayList(),
)

@Singleton
class LinkStore @Inject constructor() {
  private val byChar = ConcurrentHashMap<Long, Link>()

  fun get(charId: Long): Link? = byChar[charId]

  fun create(leaderId: Long, leaderName: String, otherId: Long, otherName: String): Link {
    val link = Link(leaderId = leaderId)
    link.members.add(LinkMember(leaderId, leaderName))
    link.members.add(LinkMember(otherId, otherName))
    byChar[leaderId] = link
    byChar[otherId] = link
    return link
  }

  fun add(link: Link, member: LinkMember) {
    if (link.members.any { it.id == member.id }) return
    link.members.add(member)
    byChar[member.id] = link
  }

  fun remove(link: Link, charId: Long) {
    link.members.removeAll { it.id == charId }
    byChar.remove(charId)
  }

  fun setLeader(link: Link, charId: Long) {
    if (link.members.none { it.id == charId }) return
    link.leaderId = charId
  }

  fun disband(link: Link) {
    for (member in link.members.toList()) {
      byChar.remove(member.id)
    }
    link.members.clear()
  }
}
