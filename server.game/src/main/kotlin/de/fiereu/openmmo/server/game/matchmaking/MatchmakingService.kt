package de.fiereu.openmmo.server.game.matchmaking

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.pvp.MatchmakingQueue
import de.fiereu.openmmo.common.pvp.SignupOutcome
import de.fiereu.openmmo.net.game.packets.LeaveMatchmakingQueuePacket
import de.fiereu.openmmo.net.game.packets.MatchmakingLanguagePrefsPacket
import de.fiereu.openmmo.net.game.packets.matchmaking.MatchmakingSignupClearedPacket
import de.fiereu.openmmo.net.game.packets.matchmaking.MatchmakingSignupPacket
import de.fiereu.openmmo.net.game.packets.matchmaking.MatchmakingSignupResultPacket
import de.fiereu.openmmo.net.game.packets.matchmaking.MatchmakingWindowPacket
import de.fiereu.openmmo.net.game.packets.matchmaking.QueueAvailability
import de.fiereu.openmmo.net.game.packets.matchmaking.QueueSignup
import de.fiereu.openmmo.net.game.packets.matchmaking.TournamentSignup
import de.fiereu.openmmo.server.game.battle.BattleRegistry
import de.fiereu.openmmo.server.game.services.DuelService
import de.fiereu.openmmo.server.game.services.notice
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** Matchmaking: whether a party may enter a queue, and who it meets when a round comes round. */
@Singleton
class MatchmakingService
@Inject
constructor(
    private val store: CharacterStore,
    private val validator: TeamValidator,
    private val sessions: SessionRegistry,
    private val duels: DuelService,
    private val battles: BattleRegistry,
) {
  private val standing = LinkedHashMap<Long, Set<MatchmakingQueue>>()
  private val languages = ConcurrentHashMap<Long, List<Byte>>()
  private val lock = Any()

  /** The queues this character is signed up for, empty when it is not. */
  fun signupOf(charId: Long): Set<MatchmakingQueue> =
      synchronized(lock) { standing[charId] ?: emptySet() }

  /** Everyone waiting in [queue], in the order they signed up. */
  fun waitingIn(queue: MatchmakingQueue): List<Long> =
      synchronized(lock) { standing.filterValues { it.contains(queue) }.keys.toList() }

  fun onSignup(event: PacketEvent<MatchmakingSignupPacket>) {
    val session = event.session
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    when (val request = event.packet.request) {
      is TournamentSignup -> {
        // Nothing here runs a tournament, and saying so is better than an unknown error.
        log.info { "char=$charId asked for tournament ${request.tournamentId}; none are running" }
        session.send(refusal(SignupOutcome.TOURNAMENT_INVALID))
      }

      is QueueSignup -> {
        // A real client cannot send this: the leading byte is the count and the discriminator
        // both, so an empty list is read as a tournament signup instead. Treating it as a
        // withdrawal is defensive, not a route anybody takes, withdrawing is c2s 0x47.
        if (request.selections.isEmpty()) {
          withdraw(session, charId)
          return
        }
        answerQueueSignup(session, charId, request)
      }
    }
  }

  /** The player pressed the button again, or the window closed. */
  fun onWithdraw(event: PacketEvent<LeaveMatchmakingQueuePacket>) {
    val charId = event.session.attributes[PLAYER_STATE]?.characterId ?: return
    withdraw(event.session, charId)
  }

  fun onDisconnect(session: SessionContext) {
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    languages.remove(charId)
    if (synchronized(lock) { standing.remove(charId) } != null) {
      log.info { "char=$charId left the queue by disconnecting" }
    }
  }

  /** The languages this player is willing to be matched with. */
  fun onLanguagePrefs(event: PacketEvent<MatchmakingLanguagePrefsPacket>) {
    val charId = event.session.attributes[PLAYER_STATE]?.characterId ?: return
    languages[charId] = event.packet.languageIds
  }

  /** What this player has said they will be matched with. Empty means they have not said. */
  fun languagesOf(charId: Long): List<Byte> = languages[charId] ?: emptyList()

  /**
   * Run one round of one queue: pair off everyone waiting, in the order they arrived, and seat each
   * pair. An odd one out keeps their place and is told the round could not place them.
   */
  fun runRound(queue: MatchmakingQueue): Int {
    val waiting = waitingIn(queue).filter { canStillFight(it) && stillLegalFor(queue, it) }
    if (waiting.size < 2) {
      waiting.forEach { tellUnpaired(it) }
      return 0
    }
    var made = 0
    var i = 0
    while (i + 1 < waiting.size) {
      val a = waiting[i]
      val b = waiting[i + 1]
      val aSession = sessions.getByCharacterId(a)
      val bSession = sessions.getByCharacterId(b)
      if (aSession == null || bSession == null) {
        // Somebody left between the snapshot and here. Drop the pair rather than half-seat it.
        i += 2
        continue
      }
      // Take both out of every queue before seating, so a seat that opens a scene cannot be
      // raced into a second one by the next queue's round.
      synchronized(lock) {
        standing.remove(a)
        standing.remove(b)
      }
      if (duels.seat(aSession, a, bSession, b)) {
        made++
        log.info { "Round of $queue paired char=$a with char=$b" }
      } else {
        log.warn { "Round of $queue could not seat char=$a with char=$b" }
      }
      i += 2
    }
    if (waiting.size % 2 == 1) tellUnpaired(waiting.last())
    return made
  }

  /** Run a round of every queue this server opens. */
  fun runAllRounds(): Int = QueueRules.ALL.sumOf { runRound(it.queue) }

  /** What the matchmaking window is told when it opens, and whenever a queue's state changes. */
  fun window(): MatchmakingWindowPacket =
      MatchmakingWindowPacket(
          open = true,
          tournamentPage = false,
          tournaments = emptyList(),
          queues =
              QueueRules.ALL.map {
                QueueAvailability(queueId = it.queue.id, enabled = true, nextRoundAt = 0L)
              },
          definitions = emptyList(),
          notices = emptyList(),
      )

  /** Still online, still has a party, and is not already in a fight. */
  private fun canStillFight(charId: Long): Boolean {
    if (sessions.getByCharacterId(charId) == null) return false
    if (duels.inLinkBattle(charId)) return false
    if (battles.byChar(charId) != null) return false
    val character = store.getCharacter(charId) ?: return false
    return character.pokemon.any { it.container == PokemonContainer.PARTY && !it.isEgg }
  }

  /**
   * Whether the party this character holds now still passes the queue it signed up for. The team
   * was checked once, at signup, and nothing looked again, so a player could pass a capped tier
   * with a legal six and swap in level 100s before the round. The fight is run by the two clients,
   * so nothing downstream ever sees the team either.
   */
  private fun stillLegalFor(queue: MatchmakingQueue, charId: Long): Boolean {
    val rules = QueueRules.of(queue) ?: return true
    val character = store.getCharacter(charId) ?: return false
    val party = character.pokemon.filter { it.container == PokemonContainer.PARTY }
    if (validator.validate(party, rules, character.info.name) is TeamVerdict.Refused) {
      log.warn { "char=$charId leaves the $queue queue: its team no longer passes" }
      sessions.getByCharacterId(charId)?.let { withdraw(it, charId) }
      return false
    }
    return true
  }

  private fun tellUnpaired(charId: Long) {
    sessions
        .getByCharacterId(charId)
        ?.send(notice("No suitable match could be found for this round."))
  }

  private fun answerQueueSignup(session: SessionContext, charId: Long, request: QueueSignup) {
    if (signupOf(charId).isNotEmpty()) {
      session.send(refusal(SignupOutcome.ALREADY_REGISTERED))
      return
    }
    val character = store.getCharacter(charId) ?: return
    val party = character.pokemon.filter { it.container == PokemonContainer.PARTY }

    val wanted = LinkedHashSet<MatchmakingQueue>()
    for (selection in request.selections) {
      val queue = MatchmakingQueue.fromId(selection.queueId)
      val rules = queue?.let { QueueRules.of(it) }
      if (queue == null || rules == null) {
        log.info { "char=$charId asked for queue ${selection.queueId}, which is not open here" }
        session.send(refusal(SignupOutcome.UNKNOWN))
        return
      }
      when (val verdict = validator.validate(party, rules, character.info.name)) {
        is TeamVerdict.Refused -> {
          log.info { "char=$charId refused from $queue: ${verdict.outcome}" }
          session.send(
              MatchmakingSignupResultPacket(
                  queueCount = 0,
                  queues = emptyList(),
                  tournamentId = 0L,
                  value = verdict.value,
                  outcome = verdict.outcome.id,
                  clause = verdict.clause?.id,
              ))
          return
        }

        TeamVerdict.Accepted -> wanted.add(queue)
      }
    }

    synchronized(lock) { standing[charId] = wanted }
    log.info { "char=$charId signed up for ${wanted.joinToString(", ")}" }
    session.send(
        MatchmakingSignupResultPacket(
            queueCount = wanted.size.toByte(),
            queues = wanted.map { it.id },
            tournamentId = 0L,
            value = 0,
            outcome = SignupOutcome.ACCEPTED.id,
            clause = null,
        ))
  }

  private fun withdraw(session: SessionContext, charId: Long) {
    synchronized(lock) { standing.remove(charId) }
    session.send(MatchmakingSignupClearedPacket())
  }

  private fun refusal(outcome: SignupOutcome, value: Int = 0) =
      MatchmakingSignupResultPacket(
          queueCount = 0,
          queues = emptyList(),
          tournamentId = 0L,
          value = value,
          outcome = outcome.id,
          clause = null,
      )
}
