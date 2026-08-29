package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.ContestRank
import de.fiereu.openmmo.common.ContestType
import de.fiereu.openmmo.net.game.packets.ContestCommPacket
import de.fiereu.openmmo.server.game.session.CLIENT_RUNS_SCRIPTS
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import java.io.ByteArrayOutputStream
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

private val log = KotlinLogging.logger {}

/** The engine's own contestant count. Human seats above this do not exist. */
private const val SEATS = ContestCommPacket.CONTEST_PARTICIPANTS

/** A queue that has held anyone this long starts with whoever it has. */
private const val LOBBY_FILL_MS = 20_000L

/** Below this nobody is waiting for anybody; the engine fills the rest with rivals. */
private const val MIN_HUMANS = 2

private data class Waiting(
    val charId: Long,
    val rank: Int,
    val type: Int,
    val partySlot: Int,
    val since: Long,
)

/**
 * One running link contest. [seats] is in seat order and a seat's index is its net id, which is
 * what every relayed frame is keyed by.
 */
private class Contest(
    val id: Int,
    val rank: Int,
    val type: Int,
    val seats: List<Long>,
) {
  /** Seats whose player has left. A barrier stops waiting for one. */
  val gone = ConcurrentHashMap.newKeySet<Int>()

  /** What each seat says the placements were, once it has said. */
  val reported = ConcurrentHashMap<Int, List<Int>>()

  /** Set once the result has been taken (or refused), so a late report changes nothing. */
  val settled = AtomicBoolean(false)

  fun seatOf(charId: Long): Int = seats.indexOf(charId)
}

/** A link Super Contest: the group, and the wire between it. */
@Singleton
class ContestService
@Inject
constructor(
    private val sessions: SessionRegistry,
    private val store: CharacterStore,
) {
  /** Queued players, keyed by character. One player is in at most one queue. */
  private val waiting = ConcurrentHashMap<Long, Waiting>()

  /** Running contests, keyed by their id, and the seat each character holds. */
  private val running = ConcurrentHashMap<Int, Contest>()
  private val seatedIn = ConcurrentHashMap<Long, Int>()
  private val nextId = AtomicInteger(1)

  /** Lobbies with a re-check already scheduled, keyed by rank and type. */
  private val pending = ConcurrentHashMap.newKeySet<Int>()
  private val lobbyScope = CoroutineScope(Dispatchers.Default + SupervisorJob())

  private fun lobbyKey(rank: Int, type: Int) = rank * 256 + type

  fun onContestComm(event: PacketEvent<ContestCommPacket>) {
    val session = event.session
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    val msg = event.packet

    when (msg.kind) {
      ContestCommPacket.KIND_QUEUE -> onQueue(session, charId, msg)
      ContestCommPacket.KIND_CANCEL -> {
        waiting.remove(charId)
        log.debug { "char=$charId left the contest queue" }
      }
      ContestCommPacket.KIND_LEAVE -> onLeave(charId)
      ContestCommPacket.KIND_RESULT -> onResult(charId, msg)
      ContestCommPacket.KIND_DATA,
      ContestCommPacket.KIND_SYNC -> relay(charId, msg)
      else ->
          log.warn { "char=$charId sent contest kind ${msg.kind}, which is not one a client sends" }
    }
  }

  /** A session went away: out of the queue, and out of any contest it was in. */
  fun onDisconnect(session: SessionContext) {
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    waiting.remove(charId)
    onLeave(charId)
  }

  /** Refuse a queue request, out loud. */
  private fun refuse(session: SessionContext, why: String?) {
    if (why != null) session.send(notice(why))
    session.send(
        ContestCommPacket(
            kind = ContestCommPacket.KIND_CANCEL,
            sessionId = 0,
            seat = -1,
            payload = ByteArray(0),
        ))
  }

  private fun onQueue(session: SessionContext, charId: Long, msg: ContestCommPacket) {
    // Only an engine-running client can hold one of these. There is nothing to fall back to the
    // way a duel falls back to the server turn engine: the contest is the engine's scene.
    if (session.attributes[CLIENT_RUNS_SCRIPTS] != true) {
      refuse(session, "This client cannot hold a link contest.")
      return
    }
    if (seatedIn.containsKey(charId)) {
      refuse(session, "You are already in a contest.")
      return
    }
    if (msg.payload.size < 3) {
      refuse(session, null)
      return
    }
    val rank = msg.payload[0].toInt() and 0xFF
    val type = msg.payload[1].toInt() and 0xFF
    val slot = msg.payload[2].toInt() and 0xFF
    if (type !in ContestType.entries.indices || rank !in ContestRank.entries.indices) {
      log.warn { "char=$charId queued for contest rank $rank type $type, which is neither" }
      refuse(session, null)
      return
    }
    val stored = store.getCharacter(charId)
    if (stored == null) {
      refuse(session, null)
      return
    }
    if (stored.pokemon.none { it.containerSlot.toInt() == slot && !it.isEgg }) {
      refuse(session, "That party slot has nothing to enter.")
      return
    }
    waiting[charId] = Waiting(charId, rank, type, slot, System.currentTimeMillis())
    log.info { "char=$charId queued for a link contest, rank $rank type $type" }
    fill(rank, type)
  }

  /**
   * Start a contest out of the queue for one rank and type if it can. Called on every join and
   * meant to be called on a timer too; both are safe, because the decision is only ever made from
   * the queue as it stands.
   */
  fun fill(rank: Int, type: Int) {
    val now = System.currentTimeMillis()
    // Longest-waiting first, so nobody is left behind by a later arrival.
    val group =
        waiting.values
            .filter { it.rank == rank && it.type == type }
            .filter { sessions.getByCharacterId(it.charId) != null }
            .sortedBy { it.since }
            .take(SEATS)
    if (group.size < MIN_HUMANS) return
    // Four starts at once; fewer waits, in case a third or fourth is a moment away.
    if (group.size < SEATS && now - group.first().since < LOBBY_FILL_MS) {
      for (w in group) {
        sessions
            .getByCharacterId(w.charId)
            ?.send(
                waitingFrame(group.size, SEATS),
            )
      }
      scheduleFill(rank, type, LOBBY_FILL_MS - (now - group.first().since))
      return
    }
    start(group)
  }

  /** Come back and look at this lobby once its window has passed, if nobody already will. */
  private fun scheduleFill(rank: Int, type: Int, inMs: Long) {
    val key = lobbyKey(rank, type)
    if (!pending.add(key)) return
    lobbyScope.launch {
      delay(if (inMs < 0) 0 else inMs)
      pending.remove(key)
      // Straight back through the same decision, made from the queue as it stands then: a lobby
      // that filled or emptied in the meantime needs no special case here.
      fill(rank, type)
    }
  }

  private fun start(group: List<Waiting>) {
    val contest =
        Contest(nextId.getAndIncrement(), group[0].rank, group[0].type, group.map { it.charId })
    for (w in group) {
      waiting.remove(w.charId)
      seatedIn[w.charId] = contest.id
    }
    running[contest.id] = contest

    val payload = seatPayload(contest)
    for ((seat, charId) in contest.seats.withIndex()) {
      sessions
          .getByCharacterId(charId)
          ?.send(
              ContestCommPacket(
                  kind = ContestCommPacket.KIND_SEAT,
                  sessionId = contest.id,
                  seat = seat,
                  payload = payload,
              ))
    }
    log.info {
      "Link contest ${contest.id} seated: ${contest.seats.size} human seat(s)," +
          " rank ${contest.rank} type ${contest.type}"
    }
  }

  /**
   * The seat frame's body: rank, type, human count, then per seat a flags byte, a gender byte and a
   * UTF-16 null-terminated name.
   */
  private fun seatPayload(contest: Contest): ByteArray {
    val out = ByteArrayOutputStream()
    out.write(contest.rank)
    out.write(contest.type)
    out.write(contest.seats.size)
    for (charId in contest.seats) {
      val stored = store.getCharacter(charId)
      // Both flags are zero, deliberately, and filling them is a bigger job than it looks.
      val flags = 0
      out.write(flags)
      out.write(stored?.info?.rivalSex?.toInt() ?: 0)
      val name = stored?.info?.name ?: ""
      for (ch in name) {
        out.write(ch.code and 0xFF)
        out.write((ch.code shr 8) and 0xFF)
      }
      out.write(0)
      out.write(0)
    }
    return out.toByteArray()
  }

  private fun waitingFrame(have: Int, want: Int) =
      ContestCommPacket(
          kind = ContestCommPacket.KIND_WAITING,
          sessionId = 0,
          seat = -1,
          payload = byteArrayOf(have.toByte(), want.toByte()),
      )

  /**
   * One of the contest's own comm commands, or a barrier, to everybody else. The sender's seat is
   * stamped on the way down because a four-party relay cannot infer it, and the payload is not
   * read: it is the engine's protocol and its meaning belongs to the engines.
   */
  private fun relay(charId: Long, msg: ContestCommPacket) {
    val contest = running[seatedIn[charId] ?: return] ?: return
    if (msg.sessionId != contest.id) return
    val seat = contest.seatOf(charId)
    if (seat < 0) return
    if (msg.payload.size > ContestCommPacket.MAX_DATA_BYTES) {
      log.warn {
        "char=$charId relayed ${msg.payload.size} bytes in contest ${contest.id}," +
            " past the engine's own ${ContestCommPacket.MAX_DATA_BYTES}, dropped"
      }
      return
    }
    val out = msg.copy(seat = seat)
    for ((other, otherId) in contest.seats.withIndex()) {
      if (other == seat || other in contest.gone) continue
      sessions.getByCharacterId(otherId)?.send(out)
    }
  }

  private fun onLeave(charId: Long) {
    val id = seatedIn.remove(charId) ?: return
    val contest = running[id] ?: return
    val seat = contest.seatOf(charId)
    if (seat < 0) return
    contest.gone += seat
    val out =
        ContestCommPacket(
            kind = ContestCommPacket.KIND_LEAVE,
            sessionId = contest.id,
            seat = seat,
            payload = ByteArray(0),
        )
    for ((other, otherId) in contest.seats.withIndex()) {
      if (other == seat || other in contest.gone) continue
      sessions.getByCharacterId(otherId)?.send(out)
    }
    log.info { "char=$charId left link contest ${contest.id} from seat $seat" }
    if (contest.gone.size >= contest.seats.size) {
      running.remove(contest.id)
      log.info { "Link contest ${contest.id} is empty and is over" }
    }
  }

  /**
   * A client's own answer to who won. Recorded, and acted on only when every seat still present has
   * sent the same one.
   */
  private fun onResult(charId: Long, msg: ContestCommPacket) {
    val contest = running[seatedIn[charId] ?: return] ?: return
    if (msg.sessionId != contest.id || contest.settled.get()) return
    val seat = contest.seatOf(charId)
    if (seat < 0 || seat in contest.gone) return
    val placement = msg.payload.map { it.toInt() and 0xFF }
    if (placement.size != contest.seats.size) {
      log.warn {
        "char=$charId reported ${placement.size} placement(s) for a contest of" +
            " ${contest.seats.size}, refused"
      }
      return
    }
    contest.reported[seat] = placement

    // Read each seat's answer once and keep it, rather than testing for presence and reading again:
    // a seat can leave between the two, and then the set that decided this was complete is not the
    // set being compared.
    val present = contest.seats.indices.filter { it !in contest.gone }
    val answers = present.map { contest.reported[it] ?: return }.distinct()

    // Exactly one thread settles, whichever gets here first with the set complete.
    if (!contest.settled.compareAndSet(false, true)) return
    if (answers.size != 1) {
      log.warn {
        "Link contest ${contest.id} ended with ${answers.size} different placement vectors" +
            " ($answers), nothing recorded"
      }
    } else {
      // The board itself lives in the game's own LINK_CONTEST_RECORDS save block, which each
      // client keeps and reports; what this server does here is agree that the contest happened
      // and say who won it, so a later argument about a record has one place to be checked.
      log.info { "Link contest ${contest.id} agreed: placements ${answers.first()}" }
    }
    running.remove(contest.id)
    for (id in contest.seats) seatedIn.remove(id)
  }
}
