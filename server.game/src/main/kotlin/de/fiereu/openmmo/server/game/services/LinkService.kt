package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.net.game.packets.CancelSocialInteractionPacket
import de.fiereu.openmmo.net.game.packets.EntityAppearanceInfo
import de.fiereu.openmmo.net.game.packets.EntityGroupMember
import de.fiereu.openmmo.net.game.packets.EntityGroupSnapshotPacket
import de.fiereu.openmmo.net.game.packets.GroupListFrameSet
import de.fiereu.openmmo.net.game.packets.LinkKickMemberPacket
import de.fiereu.openmmo.net.game.packets.RequestSocialProfilePacket
import de.fiereu.openmmo.net.game.packets.SendChatCommandPacket
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.LINK_CAP
import de.fiereu.openmmo.server.game.storage.Link
import de.fiereu.openmmo.server.game.storage.LinkMember
import de.fiereu.openmmo.server.game.storage.LinkStore
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

@Singleton
class LinkService
@Inject
constructor(
    private val linkStore: LinkStore,
    private val sessionRegistry: SessionRegistry,
    private val characterStore: CharacterStore,
) {

  fun onInvite(event: PacketEvent<SendChatCommandPacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val self = characterStore.getCharacter(charId) ?: return
    val name = event.packet.message
    log.info { "LinkInvite char=$charId name='$name'" }
    if (name.isEmpty()) return
    if (name.equals(self.info.name, ignoreCase = true)) {
      log.info { "LinkInvite refused: self" }
      return
    }
    val resolved = resolveOnline(name)
    if (resolved == null) {
      log.info { "LinkInvite target='$name' is not in the world" }
      return
    }
    if (linkStore.get(resolved.first) != null) {
      log.info { "LinkInvite target='$name' already in a link" }
      return
    }
    val existing = linkStore.get(charId)
    val link =
        if (existing == null) {
          linkStore.create(charId, self.info.name, resolved.first, resolved.second)
        } else {
          // Adding to a group that already exists is the captain's, the same way kicking and
          // handing over the captaincy are. Without this any member could fill somebody else's
          // group to the cap with strangers, and the captain could not undo it faster than it
          // refilled.
          if (existing.leaderId != charId) {
            log.info { "LinkInvite refused: char=$charId is not the captain" }
            return
          }
          if (existing.members.size >= LINK_CAP) {
            log.info { "LinkInvite refused: full" }
            return
          }
          linkStore.add(existing, LinkMember(resolved.first, resolved.second))
          existing
        }
    broadcast(link)
  }

  fun onKick(event: PacketEvent<LinkKickMemberPacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val link = linkStore.get(charId) ?: return
    val target = event.packet.targetEntityId
    log.info { "LinkKick char=$charId target=$target" }
    if (link.leaderId != charId) return
    if (target == charId) return
    if (link.members.none { it.id == target }) return
    dropMember(link, target)
  }

  fun onLeave(event: PacketEvent<CancelSocialInteractionPacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    log.info { "LinkLeave char=$charId" }
    if (linkStore.get(charId) == null) {
      ctx.send(emptySnapshot())
      return
    }
    leave(charId)
  }

  fun onAssignCaptain(event: PacketEvent<RequestSocialProfilePacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val link = linkStore.get(charId) ?: return
    val target = event.packet.targetId
    log.info { "LinkCaptain char=$charId target=$target" }
    if (link.leaderId != charId) return
    if (link.members.none { it.id == target }) return
    linkStore.setLeader(link, target)
    broadcast(link)
  }

  fun onDisconnect(charId: Long) {
    leave(charId)
  }

  private fun leave(charId: Long) {
    val link = linkStore.get(charId) ?: return
    if (link.leaderId == charId || link.members.size <= 2) {
      disband(link)
      return
    }
    dropMember(link, charId)
  }

  private fun dropMember(link: Link, charId: Long) {
    val leftover = link.members.map { it.id }.filter { it != charId }
    linkStore.remove(link, charId)
    sessionRegistry.getByCharacterId(charId)?.send(emptySnapshot())
    if (leftover.size < 2) {
      disband(link)
      return
    }
    broadcast(link)
  }

  private fun disband(link: Link) {
    val ids = link.members.map { it.id }
    linkStore.disband(link)
    val pkt = emptySnapshot()
    for (id in ids) {
      sessionRegistry.getByCharacterId(id)?.send(pkt)
    }
  }

  private fun broadcast(link: Link) {
    val pkt = buildSnapshot(link)
    for (member in link.members) {
      sessionRegistry.getByCharacterId(member.id)?.send(pkt)
    }
  }

  private fun buildSnapshot(link: Link): EntityGroupSnapshotPacket =
      EntityGroupSnapshotPacket(
          present = true,
          leaderId = link.leaderId,
          members = link.members.map { memberOf(it) },
      )

  private fun emptySnapshot(): EntityGroupSnapshotPacket =
      EntityGroupSnapshotPacket(present = false, leaderId = null, members = null)

  private fun memberOf(member: LinkMember): EntityGroupMember =
      EntityGroupMember(
          entityId = member.id,
          appearance =
              EntityAppearanceInfo(
                  name = member.name,
                  gender = 0,
                  formId = 0,
                  kind = 0,
                  palettePack = 0,
                  slots = List(4) { 0 },
              ),
          frames = GroupListFrameSet(listType = null, frames = emptyList()),
      )

  private fun resolveOnline(name: String): Pair<Long, String>? {
    for (id in sessionRegistry.onlineCharacterIds()) {
      val stored = characterStore.getCharacter(id) ?: continue
      if (stored.info.name.equals(name, ignoreCase = true)) return id to stored.info.name
    }
    return null
  }
}
