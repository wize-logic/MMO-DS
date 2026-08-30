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

/**
 * The channels a client may send on. The mode byte used to index straight into [ChatType], and
 * three of those are not channels a player picks. Two are the ones this server announces with, and
 * they carry no sender, so a line sent on one looked like a real announcement. The third is the
 * battle channel, which has its own packet and its own scoping.
 */
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

/**
 * The longest line this server passes on, which is the client's own ceiling. A line is copied to
 * every session on its channel, and the codec reads a string with no ceiling of its own.
 */
private const val MAX_CHAT_CHARS = 128

@Singleton
class ChatService
@Inject
constructor(
    private val commands: ChatCommandService,
    private val sessions: SessionRegistry,
    private val characters: CharacterStore,
    private val violations: ViolationLog,
) {

  /**
   * How fast a player may say things. The length of a line was capped and the rate was not, and
   * length is the smaller half: a line is copied to every session on its channel, so a global one
   * costs the server the number of players online for every packet the sender pays for.
   */
  private val pace = PaceLimit(burst = 6.0, perSecond = 1.0)

  /** A line the player typed. A leading `/` is a server command and stays on this session. */
  suspend fun onSend(session: SessionContext, packet: ChatMessageSendPacket) {
    val raw = packet.message ?: packet.target
    if (commands.tryHandle(session, raw)) return

    val text = raw.trim()
    if (text.isEmpty()) return

    val state = session.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId
    if (charId != null && !pace.allow(charId)) {
      violations.record(
          charId, ViolationLog.Kind.IMPOSSIBLE_PACE, "is talking faster than anybody types")
      return
    }
    val me = state.characterId?.let(characters::getCharacter) ?: return
    val type = typeForMode(packet.mode) ?: return
    val body =
        (if (type == ChatType.WHISPER) (packet.message ?: "").trim() else text).cut(me.info.id)
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

  /** The channel this mode names, or null when it names one no client may send on. */
  private fun typeForMode(mode: Byte): ChatType? {
    val type = ChatType.entries.getOrNull(mode.toInt()) ?: return ChatType.NORMAL
    if (type in SENDABLE_CHANNELS) return type
    log.warn { "A client asked to speak on $type, which is not a channel it may send on" }
    return null
  }

  private fun String.cut(charId: Long): String {
    if (length <= MAX_CHAT_CHARS) return this
    log.warn { "char=$charId sent a $length character line, cut to $MAX_CHAT_CHARS" }
    return take(MAX_CHAT_CHARS)
  }
}
