package de.fiereu.openmmo.server.game.services

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.enums.ChatType
import de.fiereu.openmmo.common.enums.Language
import de.fiereu.openmmo.net.game.packets.ChatMessagePacket
import de.fiereu.openmmo.net.game.packets.ChatMessageSendPacket
import de.fiereu.openmmo.server.game.services.command.ChatCommandService
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** The channels a client may send on. */
private val SENDABLE_CHANNELS =
    setOf(
        ChatType.NORMAL,
        ChatType.SHOUT,
        ChatType.WHISPER,
        ChatType.TRADE,
        ChatType.GLOBAL,
        ChatType.CHANNEL,
        ChatType.TEAM,
        ChatType.LINK,
    )

@Singleton
class ChatService
@Inject
constructor(
    private val commands: ChatCommandService,
    private val sessions: SessionRegistry,
    private val characters: CharacterStore,
    private val limits: ChatLimits,
) {

  /** A line the player typed. A leading `/` is a server command and stays on this session. */
  suspend fun onSend(session: SessionContext, packet: ChatMessageSendPacket) {
    val charId = session.attributes[PLAYER_STATE]?.characterId
    // Ahead of the command dispatch, not behind it. A `/` line is answered and never broadcast, so
    // it used to reach the handler without passing either limit: it was the one thing a player
    // could send at socket speed, and the argument it carried was whatever a frame held.
    if (charId != null && !limits.allow(charId)) return
    val raw = limits.cut(charId, packet.message ?: packet.target)
    if (commands.tryHandle(session, raw)) return

    val text = raw.trim()
    if (text.isEmpty()) return

    val state = session.attributes[PLAYER_STATE] ?: return
    val me = charId?.let(characters::getCharacter) ?: return
    val type = typeForMode(packet.mode) ?: return
    val body =
        if (type == ChatType.WHISPER) limits.cut(charId, packet.message ?: "").trim() else text
    if (body.isEmpty()) return

    val outgoing =
        ChatMessagePacket(
            type = type,
            language = Language.EN,
            message = body,
            sender = me.info.name,
            senderId = me.info.id,
        )

    if (type == ChatType.WHISPER) {
      deliverWhisper(session, limits.cut(charId, packet.target).trim(), outgoing)
    } else {
      // The local channel is the sender's map; the named channels are the whole server.
      val local = type == ChatType.NORMAL
      for (id in sessions.onlineCharacterIds()) {
        val ctx = sessions.getByCharacterId(id) ?: continue
        if (!ctx.channel.isActive) continue
        if (local && !sameMap(state, ctx.attributes[PLAYER_STATE])) continue
        ctx.send(outgoing)
      }
    }
  }

  private fun sameMap(a: PlayerState, b: PlayerState?): Boolean =
      b != null && a.regionId == b.regionId && a.bankId == b.bankId && a.mapId == b.mapId

  private fun deliverWhisper(from: SessionContext, name: String, packet: ChatMessagePacket) {
    if (name.isEmpty()) {
      from.send(notice("Whisper who?"))
      return
    }
    val targetId =
        sessions.onlineCharacterIds().firstOrNull { id ->
          characters.getCharacter(id)?.info?.name.equals(name, ignoreCase = true)
        }
    if (targetId == null) {
      from.send(notice("No one by that name is online."))
      return
    }
    from.send(packet)
    if (targetId != packet.senderId) {
      sessions.getByCharacterId(targetId)?.takeIf { it.channel.isActive }?.send(packet)
    }
  }

  /** The channel this mode names, or null when it names one no client may send on. */
  private fun typeForMode(mode: Byte): ChatType? {
    val type = ChatType.entries.getOrNull(mode.toInt()) ?: return ChatType.NORMAL
    if (type in SENDABLE_CHANNELS) return type
    log.warn { "A client asked to speak on $type, which is not a channel it may send on" }
    return null
  }
}
