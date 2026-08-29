package de.fiereu.openmmo.server.game.services.command

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.server.game.services.BattleService
import de.fiereu.openmmo.server.game.services.DuelService
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class ChallengeCommand
@Inject
constructor(
    private val battles: BattleService,
    private val duels: DuelService,
    private val sessions: SessionRegistry,
    private val store: CharacterStore,
) : ChatCommand {
  override val name = "challenge"
  override val usage = "/challenge <name>"
  override val description = "offers a battle to another player on this map"

  override suspend fun run(ctx: CommandContext) {
    val name = ctx.args.joinToString(" ").trim()
    if (name.isEmpty()) {
      ctx.reply("Usage: /challenge <name>")
      return
    }
    if (name.equals(ctx.character.info.name, ignoreCase = true)) {
      ctx.reply("You cannot challenge yourself.")
      return
    }
    val resolved = resolveOnline(name)
    if (resolved == null) {
      ctx.reply("They are not in the world.")
      return
    }
    val (_, foeSession) = resolved
    val foeState = foeSession.attributes[PLAYER_STATE]
    if (foeState == null) {
      ctx.reply("They are not in the world.")
      return
    }
    if (foeState.regionId != ctx.state.regionId ||
        foeState.bankId != ctx.state.bankId ||
        foeState.mapId != ctx.state.mapId) {
      ctx.reply("They are too far away.")
      return
    }
    if (battles.inBattle(ctx.characterId)) {
      ctx.reply("You are already in a battle.")
      return
    }
    // The offer, not the fight: the other player has to accept it before anything opens.
    duels.challenge(ctx.session, foeSession)
  }

  private fun resolveOnline(name: String): Pair<Long, SessionContext>? {
    for (id in sessions.onlineCharacterIds()) {
      val stored = store.getCharacter(id) ?: continue
      if (!stored.info.name.equals(name, ignoreCase = true)) continue
      val session = sessions.getByCharacterId(id) ?: continue
      return id to session
    }
    return null
  }
}
