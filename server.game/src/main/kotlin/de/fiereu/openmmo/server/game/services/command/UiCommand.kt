package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.net.game.packets.ListWindowPagePacket
import de.fiereu.openmmo.net.game.packets.ListWindowRow
import de.fiereu.openmmo.net.game.packets.OptionListWindowPacket
import de.fiereu.openmmo.net.game.packets.OptionMenuEntry
import de.fiereu.openmmo.net.game.packets.RequestConfirmationPromptPacket
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class UiCommand @Inject constructor() : ChatCommand {
  override val name = "ui"
  override val usage = "/ui"
  override val description = "sends a confirm, an option list and a labelled page"
  override val role = AccountRole.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    ctx.session.send(
        OptionListWindowPacket(
            kind = 1,
            options =
                listOf(
                    OptionMenuEntry(1, 0, 0, 0, 0, 0, 0),
                    OptionMenuEntry(2, 0, 0, 0, 0, 0, 0),
                ),
        ))
    ctx.session.send(
        ListWindowPagePacket(
            windowId = 1,
            firstPage = true,
            lastPage = true,
            headerA = 0,
            headerB = 0,
            headerC = 0,
            rows = listOf(ListWindowRow(1, 0, 0, 0, 0, 0, "Potion", 0)),
        ))
    ctx.session.send(
        RequestConfirmationPromptPacket(
            visible = true,
            entityId = 0L,
            requestTimeoutSeconds = 30,
            responseTimeoutSeconds = 30,
        ))
  }
}
