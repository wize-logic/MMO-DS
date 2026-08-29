package de.fiereu.openmmo.server.game.services.command

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.server.game.services.notice
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.storage.StoredCharacter

/** One chat command, matched on [name] without the slash and without case. */
interface ChatCommand {
  val name: String
  val usage: String
  val description: String

  /** The least an account must be to run this. Null when anybody may. */
  val role: AccountRole?
    get() = null

  // Runs on the thread the packet arrived on. Anything that waits for the client needs its own
  // scope first.
  suspend fun run(ctx: CommandContext)
}

class CommandContext(
    val session: SessionContext,
    val state: PlayerState,
    val character: StoredCharacter,
    val args: List<String>,
    /** Already filtered to what this caller may run. */
    val commands: List<ChatCommand>,
) {
  val characterId: Long
    get() = character.info.id

  /**
   * A notice, in as many messages as it needs. The client keeps 128 bytes of a chat line and drops
   * the rest on the floor, so one long answer used to arrive cut mid-word with no sign anything was
   * missing.
   */
  fun reply(message: String) {
    var rest = message
    while (rest.length > CHUNK) {
      var cut = rest.lastIndexOf(' ', CHUNK)
      if (cut <= 0) cut = CHUNK
      session.send(notice(rest.substring(0, cut)))
      rest = rest.substring(cut).trimStart()
    }
    if (rest.isNotEmpty()) session.send(notice(rest))
  }

  private companion object {
    /** Under the client's 128-byte line, with room for the window's own framing. */
    const val CHUNK = 120
  }
}
