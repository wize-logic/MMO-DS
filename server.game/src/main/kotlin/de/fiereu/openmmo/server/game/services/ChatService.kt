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
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class ChatService
@Inject
constructor(
    private val commands: ChatCommandService,
    private val sessions: SessionRegistry,
    private val characters: CharacterStore,
) {

  /** A line the player typed. A leading `/` is a server command and stays on this session. */
  suspend fun onSend(session: SessionContext, packet: ChatMessageSendPacket) {
    val raw = packet.message ?: packet.target
    if (commands.tryHandle(session, raw)) return

    val text = raw.trim()
    if (text.isEmpty()) return

    val state = session.attributes[PLAYER_STATE] ?: return
    val me = state.characterId?.let(characters::getCharacter) ?: return
    val type = typeForMode(packet.mode)
    val body = if (type == ChatType.WHISPER) (packet.message ?: "").trim() else text
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
      deliverWhisper(session, packet.target.trim(), outgoing)
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

  private fun typeForMode(mode: Byte): ChatType =
      ChatType.entries.getOrNull(mode.toInt()) ?: ChatType.NORMAL
}
