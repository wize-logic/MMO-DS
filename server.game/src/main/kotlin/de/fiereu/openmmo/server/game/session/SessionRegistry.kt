package de.fiereu.openmmo.server.game.session

import de.fiereu.network.SessionContext
import io.netty.channel.Channel
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class SessionRegistry @Inject constructor() {
  private val sessionsByChannel = ConcurrentHashMap<Channel, SessionContext>()
  private val sessionsByCharacter = ConcurrentHashMap<Long, SessionContext>()
  private val sessionsByUser = ConcurrentHashMap<Int, SessionContext>()

  fun register(ctx: SessionContext) {
    sessionsByChannel[ctx.channel] = ctx
  }

  fun unregister(ctx: SessionContext) {
    sessionsByChannel.remove(ctx.channel)
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId
    if (charId != null) {
      unbindCharacter(charId, ctx)
    }
    releaseUser(state.userId, ctx)
  }

  /**
   * Makes [ctx] the one session holding [userId], and answers with the session that held it
   * before. An account is played by one client at a time, so a second join displaces the first
   * rather than running beside it.
   */
  fun claimUser(userId: Int, ctx: SessionContext): SessionContext? = sessionsByUser.put(userId, ctx)

  /**
   * Drops [ctx]'s hold on [userId]. Value-checked: a displaced session is cleaned up after the one
   * that replaced it has already claimed the account, and must not take the new claim with it.
   */
  fun releaseUser(userId: Int, ctx: SessionContext) {
    sessionsByUser.remove(userId, ctx)
  }

  fun sessionForUser(userId: Int): SessionContext? = sessionsByUser[userId]

  fun bindCharacter(ctx: SessionContext, characterId: Long) {
    sessionsByCharacter[characterId] = ctx
  }

  /** Value-checked for the same reason as [releaseUser]. */
  fun unbindCharacter(characterId: Long, ctx: SessionContext) {
    sessionsByCharacter.remove(characterId, ctx)
  }

  fun getByCharacterId(id: Long): SessionContext? = sessionsByCharacter[id]

  fun onlineCharacterIds(): Set<Long> = sessionsByCharacter.keys

  fun forEachBound(block: (SessionContext) -> Unit) {
    sessionsByCharacter.values.forEach(block)
  }
}
