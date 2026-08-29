package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.CharacterPermissions
import de.fiereu.openmmo.net.game.packets.ObjectiveProgressPacket
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class QuestCommand @Inject constructor() : ChatCommand {
  override val name = "quest"
  override val usage = "/quest"
  override val description = "sends one objective (id 1, value 3, count 5)"
  override val permission = CharacterPermissions.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    ctx.session.send(ObjectiveProgressPacket(id = 1.toByte(), value = 3, count = 5.toShort()))
  }
}
