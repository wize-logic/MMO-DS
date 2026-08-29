package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.MAX_PARTY_SIZE
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.net.game.packets.DuelInvitePacket
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket
import de.fiereu.openmmo.net.game.packets.StringCommandPacket
import de.fiereu.openmmo.net.game.packets.TradeActionPacket
import de.fiereu.openmmo.net.game.packets.TradeCommPacket
import de.fiereu.openmmo.net.game.packets.TradeListEntryPacket
import de.fiereu.openmmo.net.game.packets.TradeSelectMonPacket
import de.fiereu.openmmo.net.game.packets.TradeStatePacket
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** How long a trade offer stands before it lapses. */
private const val INVITE_TTL_MS = 60_000L

/**
 * How long a table survives with nothing said at it. A pair whose scenes both died would otherwise
 * hold each other "busy" forever.
 */
private const val TABLE_IDLE_MS = 180_000L

/** The official invite packet carries a request type; 0 is a duel, so a trade offer is 1. */
private const val REQUEST_TYPE_TRADE: Byte = 1

/** The widest trade scene message the receiving client will accept. */
private const val MAX_TRADE_COMM_BYTES = 4096

/**
 * Both refusals at the open say the same thing on purpose: which of the two, a chair already taken,
 * or a party that emptied between the offer and the answer, is the other player's business and not
 * the wire's.
 */
private const val TRADE_OPEN_FAILED = "The trade could not open."

/** The c2s action byte on 0x50. The official client's values are unmeasured; these are ours. */
private const val ACTION_CANCEL: Byte = 0
private const val ACTION_ACCEPT: Byte = 1
private const val ACTION_CONFIRM: Byte = 2

private data class PendingOffer(
    val fromId: Long,
    val fromName: String,
    val sentAt: Long,
)

/**
 * One open trade table. [selected] holds each side's chosen party monster by its id, not its slot,
 * so a party reorder between selection and settlement is caught instead of trading the wrong
 * monster.
 */
private class TradeTable(val aId: Long, val bId: Long) {
  val selected = ConcurrentHashMap<Long, Long>()
  val confirmed = ConcurrentHashMap.newKeySet<Long>()

  /**
   * Held while a settlement is in flight, so exactly one runs. Both sides confirming at once is a
   * read and then a call, and the two arrive on different threads: both settled, the store refused
   * the second, and its refusal was what the players saw.
   */
  val settling = java.util.concurrent.atomic.AtomicBoolean(false)

  @Volatile var lastTouch: Long = System.currentTimeMillis()

  fun touch() {
    lastTouch = System.currentTimeMillis()
  }

  fun peerOf(charId: Long): Long? =
      when (charId) {
        aId -> bId
        bId -> aId
        else -> null
      }
}

/** Direct trading between two online players. */
@Singleton
class TradeService
@Inject
constructor(
    private val sessions: SessionRegistry,
    private val store: CharacterStore,
    private val battles: BattleService,
    private val duels: DuelService,
) {
  /** Standing offers, keyed by the character they were made to. */
  private val offers = ConcurrentHashMap<Long, PendingOffer>()

  /** Open tables, keyed by both participants. */
  private val tables = ConcurrentHashMap<Long, TradeTable>()

  /**
   * c2s 0x51: offer the named player a trade, by name exactly as every player verb on this wire.
   */
  fun onRequest(event: PacketEvent<StringCommandPacket>) {
    val session = event.session
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    val self = store.getCharacter(charId) ?: return
    val name = event.packet.command
    if (name.isEmpty()) return
    if (name.equals(self.info.name, ignoreCase = true)) {
      session.send(notice("You cannot trade with yourself."))
      return
    }
    if (busy(charId)) {
      session.send(notice("You are busy."))
      return
    }
    sweepLapsed()
    sweepIdleTables()
    // A trade is an exchange, not a gift: each side offers a party monster, so a
    // player who has none yet, the beginning of the game, cannot sit at the
    // table from either chair. Refused here, before anyone is asked anything.
    if (self.pokemon.none { it.container == PokemonContainer.PARTY }) {
      session.send(notice("You have no monster to trade."))
      return
    }
    val targetId =
        sessions.onlineCharacterIds().firstOrNull {
          store.getCharacter(it)?.info?.name.equals(name, ignoreCase = true)
        }
    val targetSession = targetId?.let { sessions.getByCharacterId(it) }
    if (targetId == null || targetSession == null) {
      session.send(notice("$name is not here."))
      return
    }
    val target = store.getCharacter(targetId) ?: return
    if (busy(targetId)) {
      session.send(notice("${target.info.name} is busy."))
      return
    }
    if (target.pokemon.none { it.container == PokemonContainer.PARTY }) {
      session.send(notice("${target.info.name} has no monster to trade yet."))
      return
    }
    // A standing offer the other way around is this player saying yes: two people asking each
    // other to trade are agreeing, not queueing.
    val mutual = offers[charId]
    if (mutual != null && mutual.fromId == targetId) {
      offers.remove(charId)
      open(targetSession, targetId, session, charId)
      return
    }
    val standing = offers[targetId]
    if (standing != null && standing.fromId != charId) {
      session.send(notice("${target.info.name} is already being asked."))
      return
    }
    offers[targetId] = PendingOffer(charId, self.info.name, System.currentTimeMillis())
    targetSession.send(
        DuelInvitePacket(flags = 0, requestType = REQUEST_TYPE_TRADE, name = self.info.name))
    targetSession.send(notice("${self.info.name} wants to trade!"))
    session.send(notice("Waiting for ${target.info.name}..."))
    log.info {
      "Trade offered char=$charId (${self.info.name}) -> char=$targetId (${target.info.name})"
    }
  }

  /** c2s 0x50: the one action byte the trade screen sends, answer, lock in, or walk away. */
  suspend fun onAction(event: PacketEvent<TradeActionPacket>) {
    val session = event.session
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    when (event.packet.action) {
      ACTION_ACCEPT -> accept(session, charId)
      ACTION_CONFIRM -> confirm(session, charId)
      ACTION_CANCEL -> cancel(session, charId)
      else -> log.warn { "char=$charId sent trade action ${event.packet.action}" }
    }
  }

  /**
   * Bidi 0xBF: one message of the engine's own trade scene, relayed to the other chair without
   * being read, its meaning belongs to the two engines, exactly as a link battle's blobs do.
   */
  fun onComm(event: PacketEvent<TradeCommPacket>) {
    val charId = event.session.attributes[PLAYER_STATE]?.characterId ?: return
    val table = tables[charId] ?: return
    val peerId = table.peerOf(charId) ?: return
    // The client refuses a message past its own buffer, so a wider one is undeliverable anyway.
    if (event.packet.payload.size > MAX_TRADE_COMM_BYTES) {
      log.warn {
        "char=$charId relayed ${event.packet.payload.size} trade bytes, past the engine's own" +
            " $MAX_TRADE_COMM_BYTES, dropped"
      }
      return
    }
    table.touch()
    sessions.getByCharacterId(peerId)?.send(event.packet)
  }

  /** c2s 0x52: this side picked a party slot. The peer sees the record, never the slot. */
  fun onSelect(event: PacketEvent<TradeSelectMonPacket>) {
    val session = event.session
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    val table = tables[charId] ?: return
    table.touch()
    val slot = event.packet.slotIndex
    if (slot < 0 || slot >= MAX_PARTY_SIZE) return
    val self = store.getCharacter(charId) ?: return
    val mon =
        self.pokemon.firstOrNull {
          it.container == PokemonContainer.PARTY && it.containerSlot.toInt() == slot
        }
    if (mon == null) {
      session.send(notice("That slot is empty."))
      return
    }
    table.selected[charId] = mon.id
    // Either side changing its pick unmakes the agreement for both.
    table.confirmed.clear()
    val peerId = table.peerOf(charId) ?: return
    sessions.getByCharacterId(peerId)?.send(TradeListEntryPacket(mon))
    log.info { "Trade char=$charId offers monster=${mon.id} (dex ${mon.dexId})" }
  }

  /** Withdraw whatever this character has open when the session goes. */
  fun onDisconnect(session: SessionContext) {
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    offers.remove(charId)?.let { offer ->
      sessions.getByCharacterId(offer.fromId)?.send(notice("Your trade offer lapsed."))
    }
    offers.entries.removeIf { it.value.fromId == charId }
    val table = tables[charId] ?: return
    close(table)
    val peerId = table.peerOf(charId)
    if (peerId != null) {
      sessions.getByCharacterId(peerId)?.let {
        it.send(stateOnly(TradeStatePacket.STATE_CANCELLED))
        it.send(notice("The other player left the trade."))
      }
    }
  }

  /** True while this character sits at a trade table. */
  fun inTrade(charId: Long): Boolean = tables.containsKey(charId)

  private fun stateOnly(state: Byte): TradeStatePacket =
      TradeStatePacket(state = state, role = 0, peerGender = 0, peerName = "")

  private fun busy(charId: Long): Boolean =
      tables.containsKey(charId) || battles.inBattle(charId) || duels.inLinkBattle(charId)

  private fun accept(session: SessionContext, charId: Long) {
    sweepLapsed()
    val offer = offers.remove(charId)
    if (offer == null) {
      session.send(notice("Nobody is asking you to trade."))
      return
    }
    val fromSession = sessions.getByCharacterId(offer.fromId)
    if (fromSession == null) {
      session.send(notice("${offer.fromName} is no longer here."))
      return
    }
    open(fromSession, offer.fromId, session, charId)
  }

  private fun open(
      aSession: SessionContext,
      aId: Long,
      bSession: SessionContext,
      bId: Long,
  ) {
    if (busy(aId) || busy(bId)) {
      aSession.send(notice(TRADE_OPEN_FAILED))
      bSession.send(notice(TRADE_OPEN_FAILED))
      return
    }
    val a = store.getCharacter(aId) ?: return
    val b = store.getCharacter(bId) ?: return
    // Re-checked at the open: a party can have emptied between the offer and the
    // answer, and a table where one chair can never offer would just hang.
    if (a.pokemon.none { it.container == PokemonContainer.PARTY } ||
        b.pokemon.none { it.container == PokemonContainer.PARTY }) {
      aSession.send(notice(TRADE_OPEN_FAILED))
      bSession.send(notice(TRADE_OPEN_FAILED))
      return
    }
    val table = TradeTable(aId, bId)
    tables[aId] = table
    tables[bId] = table
    // Neither seat may move a monster between containers while the table stands.
    seated(aId, true)
    seated(bId, true)
    // Role 0 is the chair whose ask opened the table; the engine scene answers it as its comm
    // net id, and net id 0 is the side the official client's table treats as the exchange's server.
    aSession.send(
        TradeStatePacket(
            state = TradeStatePacket.STATE_OPEN,
            role = 0,
            peerGender = (b.info.rivalSex.toInt() and 0xFF).toByte(),
            peerName = b.info.name))
    bSession.send(
        TradeStatePacket(
            state = TradeStatePacket.STATE_OPEN,
            role = 1,
            peerGender = (a.info.rivalSex.toInt() and 0xFF).toByte(),
            peerName = a.info.name))
    aSession.send(notice("Trading with ${b.info.name}."))
    bSession.send(notice("Trading with ${a.info.name}."))
    log.info { "Trade open char=$aId (${a.info.name}) <-> char=$bId (${b.info.name})" }
  }

  private suspend fun confirm(session: SessionContext, charId: Long) {
    val table = tables[charId] ?: return
    table.touch()
    val peerId = table.peerOf(charId) ?: return
    if (table.selected[charId] == null || table.selected[peerId] == null) {
      session.send(notice("Both sides have to offer a monster first."))
      return
    }
    table.confirmed.add(charId)
    val peerSession = sessions.getByCharacterId(peerId)
    peerSession?.send(stateOnly(TradeStatePacket.STATE_PEER_CONFIRMED))
    if (table.confirmed.containsAll(listOf(table.aId, table.bId))) {
      settle(table)
    }
  }

  private fun cancel(session: SessionContext, charId: Long) {
    val table = tables[charId]
    if (table == null) {
      // No table yet: walking away from the offer itself, from either side of it.
      offers.remove(charId)?.let { offer ->
        sessions.getByCharacterId(offer.fromId)?.send(notice("Your trade offer was declined."))
      }
      offers.entries.removeIf { it.value.fromId == charId }
      return
    }
    close(table)
    session.send(stateOnly(TradeStatePacket.STATE_CANCELLED))
    val peerId = table.peerOf(charId)
    if (peerId != null) {
      sessions.getByCharacterId(peerId)?.let {
        it.send(stateOnly(TradeStatePacket.STATE_CANCELLED))
        it.send(notice("The trade was cancelled."))
      }
    }
    log.info { "Trade cancelled by char=$charId" }
  }

  /**
   * Both sides confirmed the pair of monsters as they stand. Re-read both from the cache by id, a
   * selection is only a claim until here, then swap in place on one side, then the other, and undo
   * the first if the second refuses.
   */
  private suspend fun settle(table: TradeTable) {
    if (!table.settling.compareAndSet(false, true)) return
    try {
      settleOnce(table)
    } finally {
      table.settling.set(false)
    }
  }

  private suspend fun settleOnce(table: TradeTable) {
    val aMonIdPicked = table.selected[table.aId]
    val bMonIdPicked = table.selected[table.bId]
    // The table stays open: the official client's scene walks straight back to it for the next
    // trade, and
    // the relay under that walk has to stay live. Only a cancel, a disconnect or the idle
    // sweep closes a table, but the agreement is spent now, before anything can re-read it.
    table.selected.clear()
    table.confirmed.clear()
    table.touch()
    val aId = table.aId
    val bId = table.bId
    val aSession = sessions.getByCharacterId(aId)
    val bSession = sessions.getByCharacterId(bId)
    val aMonId = aMonIdPicked
    val bMonId = bMonIdPicked
    val a = store.getCharacter(aId)
    val b = store.getCharacter(bId)
    if (aMonId == null || bMonId == null || a == null || b == null) return
    val aMon = a.pokemon.firstOrNull { it.id == aMonId && it.container == PokemonContainer.PARTY }
    val bMon = b.pokemon.firstOrNull { it.id == bMonId && it.container == PokemonContainer.PARTY }
    if (aMon == null || bMon == null) {
      close(table)
      aSession?.send(stateOnly(TradeStatePacket.STATE_CANCELLED))
      bSession?.send(stateOnly(TradeStatePacket.STATE_CANCELLED))
      aSession?.send(notice("An offered monster moved; the trade was cancelled."))
      bSession?.send(notice("An offered monster moved; the trade was cancelled."))
      return
    }
    val fromA = store.swapPartyMonster(aId, aMon.id, bMon)
    if (fromA == null) {
      close(table)
      refuse(aSession, bSession)
      return
    }
    val fromB = store.swapPartyMonster(bId, bMon.id, fromA)
    if (fromB == null) {
      close(table)
      // Undo the half that landed. The put-back can only fail if A's write path itself is failing,
      // which is worth a scream: A then holds B's monster and B still holds its own.
      if (store.swapPartyMonster(aId, bMon.id, fromA) == null) {
        log.error { "Trade char=$aId<->$bId: half-applied and the undo did not persist" }
      }
      refuse(aSession, bSession)
      return
    }
    aSession?.let { resendParty(it, aId) }
    bSession?.let { resendParty(it, bId) }
    aSession?.send(TradeStatePacket(TradeStatePacket.STATE_COMPLETED, 0, 0, b.info.name))
    bSession?.send(TradeStatePacket(TradeStatePacket.STATE_COMPLETED, 0, 0, a.info.name))
    aSession?.send(notice("Traded ${aMon.nick()} for ${bMon.nick()}!"))
    bSession?.send(notice("Traded ${bMon.nick()} for ${aMon.nick()}!"))
    log.info {
      "Trade done: char=$aId monster=${aMon.id} (dex ${aMon.dexId})" +
          " <-> char=$bId monster=${bMon.id} (dex ${bMon.dexId})"
    }
  }

  private fun refuse(aSession: SessionContext?, bSession: SessionContext?) {
    aSession?.send(stateOnly(TradeStatePacket.STATE_CANCELLED))
    bSession?.send(stateOnly(TradeStatePacket.STATE_CANCELLED))
    aSession?.send(notice("The trade could not be written."))
    bSession?.send(notice("The trade could not be written."))
  }

  private fun resendParty(session: SessionContext, charId: Long) {
    val stored = store.getCharacter(charId) ?: return
    session.send(
        PokemonContainerPacket(
            container = PokemonContainer.PARTY,
            hasChange = true,
            delete = false,
            pokemon = stored.pokemon,
        ))
  }

  private fun close(table: TradeTable) {
    tables.remove(table.aId, table)
    tables.remove(table.bId, table)
    seated(table.aId, false)
    seated(table.bId, false)
  }

  /** Mark a seat as at a table, or no longer at one, wherever its session still is. */
  private fun seated(charId: Long, at: Boolean) {
    sessions.getByCharacterId(charId)?.attributes?.get(PLAYER_STATE)?.atTradeTable = at
  }

  /** Close any table nobody has spoken at. Both chairs are told, and both are free again. */
  private fun sweepIdleTables() {
    val now = System.currentTimeMillis()
    for (table in tables.values.toSet()) {
      if (now - table.lastTouch <= TABLE_IDLE_MS) continue
      close(table)
      for (id in listOf(table.aId, table.bId)) {
        sessions.getByCharacterId(id)?.let {
          it.send(stateOnly(TradeStatePacket.STATE_CANCELLED))
          it.send(notice("The trade lapsed."))
        }
      }
      log.info { "Trade table char=${table.aId}<->char=${table.bId} lapsed idle" }
    }
  }

  private fun sweepLapsed() {
    val now = System.currentTimeMillis()
    offers.entries.removeIf { (_, offer) ->
      val lapsed = now - offer.sentAt > INVITE_TTL_MS
      if (lapsed) {
        sessions.getByCharacterId(offer.fromId)?.send(notice("Your trade offer lapsed."))
      }
      lapsed
    }
  }

  private fun Pokemon.nick(): String = nickname.ifEmpty { "No. $dexId" }
}
