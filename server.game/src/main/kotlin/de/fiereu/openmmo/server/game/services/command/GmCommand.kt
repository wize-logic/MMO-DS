package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.CharacterPermissions
import de.fiereu.openmmo.net.game.packets.GmAccountInfo
import de.fiereu.openmmo.net.game.packets.GmCharacterListing
import de.fiereu.openmmo.net.game.packets.GmCharacterSession
import de.fiereu.openmmo.net.game.packets.GmExtraDetail
import de.fiereu.openmmo.net.game.packets.GmPanelEntryPacket
import de.fiereu.openmmo.net.game.packets.GmPanelOption
import de.fiereu.openmmo.net.game.packets.GmPanelRow
import de.fiereu.openmmo.net.game.packets.GmPanelVariantPacket
import de.fiereu.openmmo.net.game.packets.GmPlayerLookupPacket
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Sends one of each staff packet so a client can be watched decoding them. None of these is
 * addressed to an ordinary player and nothing here is a real lookup: the scalars are fixed and only
 * the shapes matter.
 */
@Singleton
class GmCommand @Inject constructor() : ChatCommand {
  override val name = "gm"
  override val usage = "/gm"
  override val description = "sends a player lookup, a panel body and a panel row"
  override val permission = CharacterPermissions.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    ctx.session.send(
        GmPlayerLookupPacket(
            found = true,
            session =
                GmCharacterSession(
                    entityId = 1L,
                    name = "Test",
                    secondaryName = "test",
                    intA = 0,
                    byteA = 0,
                    intB = 0,
                    longA = 0L,
                    intC = 0,
                    intD = 0,
                    intE = 0,
                    shortA = 0,
                    intF = 0,
                    byteB = 0,
                    byteC = 0,
                    byteD = 0,
                    shortB = 0,
                    byteE = 0,
                    intG = 0,
                    byteF = 0,
                    byteG = 0,
                    byteH = 0,
                    byteI = 0,
                    byteJ = 0,
                    byteK = 0,
                    shortC = 0,
                    shortD = 0,
                    shortE = 0,
                    shortF = 0,
                    statusByte = 0,
                    shortG = 0,
                    shortH = 0,
                    statusList = listOf(0, 1),
                ),
            rank = 1,
            account =
                GmAccountInfo(
                    rankExtraA = 0,
                    rankExtraB = 0,
                    rankExtraC = 0,
                    ipAddress = "127.0.0.1",
                    playtime = 3600L,
                    accountId = 1,
                    stringA = "",
                    stringB = "",
                    stringC = "",
                ),
            extra = GmExtraDetail(value = 0, text = "note"),
            characters =
                listOf(
                    GmCharacterListing(entityId = 1L, value = 0, label = "Sinnoh", name = "Test"))))
    ctx.session.send(
        GmPanelVariantPacket(
            variant = GmPanelVariantPacket.MENU,
            rows =
                listOf(
                    GmPanelRow(
                        label = "Actions",
                        options =
                            listOf(
                                GmPanelOption(label = "Kick", kind = 0),
                                GmPanelOption(label = "Mute", kind = 1),
                            )))))
    ctx.session.send(
        GmPanelEntryPacket(clearFlag = 0, label = "Status", value = "online", count = 1))
  }
}
