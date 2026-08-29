package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.net.game.packets.UndergroundTalkPacket
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.world.UndergroundMap
import io.github.oshai.kotlinlogging.KotlinLogging
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** The widest conversation body the receiving client will accept. */
private const val MAX_UG_TALK_BYTES = 512

/** One live conversation: who pressed A, and who was faced. */
private class Conversation(val initiatorId: Long, val responderId: Long) {
  fun peerOf(charId: Long): Long? =
      when (charId) {
        initiatorId -> responderId
        responderId -> initiatorId
        else -> null
      }
}

/** Pressing A on another player in the Underground, and the conversation it opens. */
@Singleton
class UndergroundTalkService
@Inject
constructor(
    private val sessions: SessionRegistry,
) {
  /** The last availability each underground character reported. Absent means free. */
  private val availability = ConcurrentHashMap<Long, Int>()

  /** Both ends of every live conversation, keyed by each party. */
  private val talking = ConcurrentHashMap<Long, Conversation>()

  fun onPacket(event: PacketEvent<UndergroundTalkPacket>) {
    val session = event.session
    val state = session.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val packet = event.packet
    when (packet.kind) {
      UndergroundTalkPacket.KIND_REQUEST -> onRequest(session, state, charId, packet.entityId)
      UndergroundTalkPacket.KIND_STATE -> onState(charId, packet)
      UndergroundTalkPacket.KIND_DATA -> relay(charId, packet)
      UndergroundTalkPacket.KIND_END ->
          end(
              charId,
              tellPeer = packet.payload.firstOrNull()?.toInt() == UndergroundTalkPacket.END_REFUSED)
      else -> Unit
    }
  }

  /** A session went away mid-conversation. Its peer's menu is told rather than left waiting. */
  fun onDisconnect(session: SessionContext) {
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    availability.remove(charId)
    end(charId, tellPeer = true)
  }

  /**
   * This character is no longer underground, it climbed out, or a warp took it. The conversation
   * cannot survive the map, and the peer's menu is waiting on a reply that is not coming.
   */
  fun onLeftUnderground(charId: Long) {
    availability.remove(charId)
    end(charId, tellPeer = true)
  }

  /** True while this character is inside a conversation. */
  fun inConversation(charId: Long): Boolean = talking.containsKey(charId)

  private fun onState(charId: Long, packet: UndergroundTalkPacket) {
    val reported = packet.payload.firstOrNull()?.toInt() ?: UndergroundTalkPacket.STATE_FREE
    availability[charId] = reported
  }

  private fun onRequest(
      session: SessionContext,
      state: PlayerState,
      charId: Long,
      target: Long,
  ) {
    if (!isUnderground(state)) return
    if (talking.containsKey(charId)) return

    // The client sends back the id it was given in a LoadEntity, and that reader keeps the low 32
    // bits of it (`mmo_game_read_load_entity`). Match on the same 32 bits so the truncation cannot
    // cost anyone a conversation.
    val targetSession =
        sessions
            .onlineCharacterIds()
            .firstOrNull { it != charId && (it and 0xFFFFFFFFL) == (target and 0xFFFFFFFFL) }
            ?.let { sessions.getByCharacterId(it) } ?: return
    val targetState = targetSession.attributes[PLAYER_STATE] ?: return
    val targetId = targetState.characterId ?: return
    if (!isUnderground(targetState)) return
    if (!adjacent(state, targetState)) {
      log.debug {
        "Character $charId pressed A on $targetId from (${state.x}, ${state.y}) to" +
            " (${targetState.x}, ${targetState.y}), which is not next to it"
      }
      return
    }

    val refusal =
        when {
          availability[targetId] == UndergroundTalkPacket.STATE_MINING ->
              UndergroundTalkPacket.TALK_MINING
          talking.containsKey(targetId) -> UndergroundTalkPacket.TALK_FAIL
          availability[targetId] == UndergroundTalkPacket.STATE_BUSY ->
              UndergroundTalkPacket.TALK_FAIL
          else -> null
        }
    if (refusal != null) {
      session.send(result(targetId, refusal, UndergroundTalkPacket.ROLE_INITIATOR))
      return
    }

    val conversation = Conversation(charId, targetId)
    // Seat both at once. A second request that arrives between these two writes would find its
    // target already in `talking` and be refused, which is the same answer the cartridge gives.
    if (talking.putIfAbsent(charId, conversation) != null) return
    if (talking.putIfAbsent(targetId, conversation) != null) {
      talking.remove(charId, conversation)
      session.send(
          result(targetId, UndergroundTalkPacket.TALK_FAIL, UndergroundTalkPacket.ROLE_INITIATOR))
      return
    }
    session.send(
        result(targetId, UndergroundTalkPacket.TALK_SUCCESS, UndergroundTalkPacket.ROLE_INITIATOR))
    targetSession.send(
        result(charId, UndergroundTalkPacket.TALK_SUCCESS, UndergroundTalkPacket.ROLE_RESPONDER))
    log.info { "Underground: character $charId is talking to $targetId" }
  }

  private fun relay(charId: Long, packet: UndergroundTalkPacket) {
    val peerId = talking[charId]?.peerOf(charId) ?: return
    if (packet.payload.isEmpty()) return
    // The client refuses a body past its own buffer whole, so a wider one is undeliverable before
    // it leaves and relaying it only spends the peer's parser.
    if (packet.payload.size > MAX_UG_TALK_BYTES) {
      log.warn {
        "char=$charId relayed ${packet.payload.size} Underground bytes, past the engine's own" +
            " $MAX_UG_TALK_BYTES, dropped"
      }
      return
    }
    sessions
        .getByCharacterId(peerId)
        ?.send(UndergroundTalkPacket(UndergroundTalkPacket.KIND_DATA, charId, packet.payload))
  }

  /** Close the pairing, and say so only when the other party cannot find out for themselves. */
  private fun end(charId: Long, tellPeer: Boolean) {
    val conversation = talking.remove(charId) ?: return
    val peerId = conversation.peerOf(charId) ?: return
    talking.remove(peerId, conversation)
    if (!tellPeer) return
    sessions
        .getByCharacterId(peerId)
        ?.send(UndergroundTalkPacket(UndergroundTalkPacket.KIND_END, charId, ByteArray(0)))
    log.debug { "Underground: the conversation between $charId and $peerId ended" }
  }

  private fun result(peerId: Long, talkResult: Int, role: Int) =
      UndergroundTalkPacket(
          UndergroundTalkPacket.KIND_RESULT,
          peerId,
          byteArrayOf(talkResult.toByte(), role.toByte()))

  private companion object {
    fun isUnderground(state: PlayerState): Boolean =
        UndergroundMap.isUnderground(
            state.regionId.toByte(), state.bankId.toByte(), state.mapId.toByte())

    /** The official client asks for the one tile in front of the player and nothing else. */
    fun adjacent(a: PlayerState, b: PlayerState): Boolean {
      val dx = Math.abs(a.x - b.x)
      val dy = Math.abs(a.y - b.y)
      return dx + dy in 1..2
    }
  }
}
