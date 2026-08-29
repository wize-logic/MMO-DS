package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.net.game.packets.DuelInviteOutcomePacket
import de.fiereu.openmmo.net.game.packets.DuelInvitePacket
import de.fiereu.openmmo.net.game.packets.InGameChallengeResponsePacket
import de.fiereu.openmmo.net.game.packets.LinkBattleDataPacket
import de.fiereu.openmmo.net.game.packets.LinkBattleOpenPacket
import de.fiereu.openmmo.server.game.session.CLIENT_RUNS_SCRIPTS
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** How long a challenge stands before it lapses. */
private const val INVITE_TTL_MS = 60_000L

/** The three answers the challenger can hear. The official client's byte has no measured meaning. */
private const val OUTCOME_DECLINED: Byte = 0
private const val OUTCOME_ACCEPTED: Byte = 1
private const val OUTCOME_LAPSED: Byte = 2

private data class PendingInvite(
    val fromId: Long,
    val toId: Long,
    val fromName: String,
    val sentAt: Long,
)

/**
 * One running native link battle: two clients whose engines compute the fight between them,
 * and a server that only relays.
 */
private class LinkBattle(
    val id: Int,
    val hostId: Long,
    val guestId: Long,
) {
  val result = ConcurrentHashMap<Long, Int>()

  fun peerOf(charId: Long): Long? =
      when (charId) {
        hostId -> guestId
        guestId -> hostId
        else -> null
      }
}

/** Challenging another player, and the native link battle an accepted challenge opens. */
@Singleton
class DuelService
@Inject
constructor(
    private val sessions: SessionRegistry,
    private val store: CharacterStore,
    private val battles: BattleService,
) {
  private val invites = ConcurrentHashMap<Long, PendingInvite>()
  private val running = ConcurrentHashMap<Long, LinkBattle>()
  private val nextId = AtomicInteger(1)

  /** Offer [foeSession]'s player a duel with [session]'s. */
  fun challenge(session: SessionContext, foeSession: SessionContext) {
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    val foeId = foeSession.attributes[PLAYER_STATE]?.characterId ?: return
    if (charId == foeId) return
    if (battles.inBattle(charId) || running.containsKey(charId)) {
      session.send(notice("You are already in a battle."))
      return
    }
    if (battles.inBattle(foeId) || running.containsKey(foeId)) {
      session.send(notice("They are already in a battle."))
      return
    }
    val self = store.getCharacter(charId) ?: return
    val foe = store.getCharacter(foeId) ?: return
    if (self.pokemon.none { it.container == PokemonContainer.PARTY && !it.isEgg && it.hp > 0 }) {
      session.send(notice("All of your monsters have fainted."))
      return
    }
    if (foe.pokemon.none { it.container == PokemonContainer.PARTY && !it.isEgg && it.hp > 0 }) {
      session.send(notice("They have no monster that can fight."))
      return
    }
    sweepLapsed()
    val standing = invites[foeId]
    if (standing != null && standing.fromId != charId) {
      session.send(notice("${foe.info.name} is already being challenged."))
      return
    }
    invites[foeId] = PendingInvite(charId, foeId, self.info.name, System.currentTimeMillis())
    foeSession.send(DuelInvitePacket(flags = 0, requestType = 0, name = self.info.name))
    foeSession.send(notice("${self.info.name} wants to battle! Answer the challenge."))
    session.send(notice("Waiting for ${foe.info.name} to answer..."))
    log.info { "Duel offered char=$charId (${self.info.name}) -> char=$foeId (${foe.info.name})" }
  }

  /** The challenged player answered. An accept is the only thing that opens a battle. */
  fun onResponse(event: PacketEvent<InGameChallengeResponsePacket>) {
    val session = event.session
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    sweepLapsed()
    val invite = invites.remove(charId)
    if (invite == null) {
      session.send(notice("Nobody is challenging you."))
      return
    }
    val challenger = sessions.getByCharacterId(invite.fromId)
    if (challenger == null) {
      session.send(notice("${invite.fromName} is no longer here."))
      return
    }
    if (!event.packet.accepted) {
      challenger.send(DuelInviteOutcomePacket(OUTCOME_DECLINED))
      challenger.send(notice("Your challenge was declined."))
      session.send(notice("Challenge declined."))
      log.info { "Duel declined char=$charId <- char=${invite.fromId}" }
      return
    }
    challenger.send(DuelInviteOutcomePacket(OUTCOME_ACCEPTED))
    seat(challenger, invite.fromId, session, charId)
  }

  /** Withdraw whatever this character has open when the session goes. */
  fun onDisconnect(session: SessionContext) {
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    invites.remove(charId)?.let { invite ->
      sessions.getByCharacterId(invite.fromId)?.let {
        it.send(DuelInviteOutcomePacket(OUTCOME_LAPSED))
        it.send(notice("Your challenge lapsed."))
      }
    }
    invites.entries.removeIf { it.value.fromId == charId }
    val battle = running[charId] ?: return
    val peerId = battle.peerOf(charId)
    close(battle)
    if (peerId != null) {
      sessions.getByCharacterId(peerId)?.let {
        it.send(LinkBattleDataPacket(battle.id, LinkBattleDataPacket.KIND_LEAVE, ByteArray(0)))
        it.send(notice("Your opponent left the battle."))
      }
    }
  }

  /**
   * One blob of a running link battle. The payload is the engine's own, a BattleMessageInfo
   * and its body, or a sync tag, and is relayed byte for byte to the peer without being
   * read.
   */
  fun onLinkData(event: PacketEvent<LinkBattleDataPacket>) {
    val session = event.session
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    val battle = running[charId] ?: return
    val packet = event.packet
    if (packet.battleId != battle.id) return
    val peerId = battle.peerOf(charId) ?: return
    when (packet.kind) {
      LinkBattleDataPacket.KIND_RESULT -> {
        battle.result[charId] = if (packet.payload.isEmpty()) 0 else packet.payload[0].toInt()
        if (battle.result.size == 2) finish(battle)
      }
      LinkBattleDataPacket.KIND_LEAVE -> {
        sessions.getByCharacterId(peerId)?.let {
          it.send(LinkBattleDataPacket(battle.id, LinkBattleDataPacket.KIND_LEAVE, ByteArray(0)))
          it.send(notice("Your opponent left the battle."))
        }
        close(battle)
      }
      else -> sessions.getByCharacterId(peerId)?.send(packet)
    }
  }

  /** True while this character is inside a native link battle. */
  fun inLinkBattle(charId: Long): Boolean = running.containsKey(charId)

  /** Open a link battle between two players who have already agreed to one. */
  fun seat(
      hostSession: SessionContext,
      hostId: Long,
      guestSession: SessionContext,
      guestId: Long,
  ): Boolean {
    val host = store.getCharacter(hostId) ?: return false
    val guest = store.getCharacter(guestId) ?: return false
    val hostParty = host.pokemon.filter { it.container == PokemonContainer.PARTY }
    val guestParty = guest.pokemon.filter { it.container == PokemonContainer.PARTY }
    if (hostParty.isEmpty() || guestParty.isEmpty()) {
      hostSession.send(notice("The battle could not start."))
      guestSession.send(notice("The battle could not start."))
      return false
    }
    // A link battle needs two engines. Anything else keeps the server-side turn engine.
    if (hostSession.attributes[CLIENT_RUNS_SCRIPTS] != true ||
        guestSession.attributes[CLIENT_RUNS_SCRIPTS] != true) {
      log.info { "Duel char=$hostId vs char=$guestId falls back to the server turn engine" }
      battles.startPlayerBattle(hostSession, guestSession)
      return true
    }
    val battle = LinkBattle(nextId.getAndIncrement(), hostId, guestId)
    running[hostId] = battle
    running[guestId] = battle
    // Each side is told the *other* party: its own it already holds, and both build the same two
    // from the same records.
    hostSession.send(
        LinkBattleOpenPacket(
            battle.id, 0, guest.info.name, guest.info.rivalSex.toInt() and 0xFF, guestParty))
    guestSession.send(
        LinkBattleOpenPacket(
            battle.id, 1, host.info.name, host.info.rivalSex.toInt() and 0xFF, hostParty))
    hostSession.send(notice("Battle with ${guest.info.name}!"))
    guestSession.send(notice("Battle with ${host.info.name}!"))
    log.info {
      "Link battle ${battle.id}: char=$hostId (${host.info.name}, net 0)" +
          " vs char=$guestId (${guest.info.name}, net 1)"
    }
    return true
  }

  /** Both scenes ended. */
  private fun finish(battle: LinkBattle) {
    val hostResult = battle.result[battle.hostId] ?: 0
    val guestResult = battle.result[battle.guestId] ?: 0
    close(battle)
    val hostName = store.getCharacter(battle.hostId)?.info?.name ?: "Someone"
    val guestName = store.getCharacter(battle.guestId)?.info?.name ?: "Someone"
    if (!mirrored(hostResult, guestResult)) {
      log.warn {
        "Link battle ${battle.id} ended with results that do not mirror:" +
            " $hostName=$hostResult $guestName=$guestResult"
      }
    }
    sessions.getByCharacterId(battle.hostId)?.send(notice(resultLine(hostResult, guestName)))
    sessions.getByCharacterId(battle.guestId)?.send(notice(resultLine(guestResult, hostName)))
    log.info { "Link battle ${battle.id} ended: $hostName=$hostResult $guestName=$guestResult" }
  }

  private fun close(battle: LinkBattle) {
    running.remove(battle.hostId, battle)
    running.remove(battle.guestId, battle)
  }

  private fun sweepLapsed() {
    val now = System.currentTimeMillis()
    invites.entries.removeIf { (_, invite) ->
      val lapsed = now - invite.sentAt > INVITE_TTL_MS
      if (lapsed) {
        sessions.getByCharacterId(invite.fromId)?.let {
          it.send(DuelInviteOutcomePacket(OUTCOME_LAPSED))
          it.send(notice("Your challenge lapsed."))
        }
      }
      lapsed
    }
  }

  private companion object {
    /** The engine's own BATTLE_RESULT values, which are what a client reports. */
    const val RESULT_WIN = 1
    const val RESULT_LOSE = 2
    const val RESULT_DRAW = 3
    const val RESULT_FLED = 5

    fun mirrored(a: Int, b: Int): Boolean =
        (a == RESULT_WIN && b == RESULT_LOSE) ||
            (a == RESULT_LOSE && b == RESULT_WIN) ||
            (a == RESULT_DRAW && b == RESULT_DRAW) ||
            (a == RESULT_FLED && b == RESULT_WIN) ||
            (a == RESULT_WIN && b == RESULT_FLED)

    fun resultLine(result: Int, foeName: String): String =
        when (result) {
          RESULT_WIN -> "You won against $foeName!"
          RESULT_LOSE -> "You lost to $foeName."
          RESULT_DRAW -> "The battle with $foeName was a draw."
          RESULT_FLED -> "You forfeited the battle with $foeName."
          else -> "The battle with $foeName ended."
        }
  }
}
