package de.fiereu.openmmo.server.game.services.command

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.auth.AccountRoles
import de.fiereu.openmmo.server.game.services.notice
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

private const val PREFIX = '/'

@Singleton
class ChatCommandService
@Inject
constructor(
    private val characterStore: CharacterStore,
    registered: Set<@JvmSuppressWildcards ChatCommand>,
) {

  private val commands = registered.associateBy { it.name.lowercase() }

  /** Handles a chat line. Returns false when it is ordinary chat the caller should broadcast. */
  suspend fun tryHandle(session: SessionContext, message: String): Boolean {
    val text = message.trim()
    if (!text.startsWith(PREFIX)) return false

    val parts = tokenizeCommandLine(text.drop(1))
    val name = parts.firstOrNull()?.lowercase()
    if (name == null) {
      session.send(notice("Type /help to see what you can run."))
      return true
    }

    val state = session.attributes[PLAYER_STATE]
    val character = state?.characterId?.let(characterStore::getCharacter)
    if (state == null || character == null) {
      session.send(notice("You are not in the world yet."))
      return true
    }

    // Answered by the account's roles, which arrived signed in the join ticket, not by anything
    // the character carries: a person is trusted, not one of their save files.
    val command = commands[name]
    if (command == null || !state.roles.allow(command)) {
      if (command != null) {
        log.info { "user=${state.userId} (${state.roles}) may not run /$name" }
      }
      // The same answer either way, so a player cannot map out the commands they do not have by
      // reading which ones are refused rather than unknown.
      session.send(notice("Unknown command: $name. Try /help."))
      return true
    }

    val ctx =
        CommandContext(
            session = session,
            state = state,
            character = character,
            args = parts.drop(1),
            commands = commands.values.filter { state.roles.allow(it) },
        )
    try {
      command.run(ctx)
    } catch (e: Exception) {
      log.error(e) { "/$name failed for char=${character.info.id}" }
      ctx.reply("/$name failed.")
    }
    return true
  }
}

private fun AccountRoles.allow(command: ChatCommand): Boolean {
  val needed = command.role ?: return true
  return has(needed)
}
