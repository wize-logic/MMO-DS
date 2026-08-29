package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.CharacterPermissions
import de.fiereu.openmmo.net.game.packets.HighScoreAppearance
import de.fiereu.openmmo.net.game.packets.HighScoreBoardPacket
import de.fiereu.openmmo.net.game.packets.HighScoreEntry
import de.fiereu.openmmo.net.game.packets.MatchmakingRentalPreview
import de.fiereu.openmmo.net.game.packets.MatchmakingRentalsPacket
import de.fiereu.openmmo.net.game.packets.TournamentMatchup
import de.fiereu.openmmo.net.game.packets.TournamentMatchupsPacket
import de.fiereu.openmmo.net.game.packets.matchmaking.TournamentEntrantCountPacket
import de.fiereu.openmmo.net.game.packets.matchmaking.TournamentPagePacket
import de.fiereu.openmmo.net.game.packets.matchmaking.TournamentRecord
import javax.inject.Inject
import javax.inject.Singleton

/** Sends one of each competitive packet so a client can be watched reassembling them. */
@Singleton
class CompeteCommand @Inject constructor() : ChatCommand {
  override val name = "compete"
  override val usage = "/compete"
  override val description = "sends a rental offer, a tournament page, a bracket and a score board"
  override val permission = CharacterPermissions.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    ctx.session.send(
        MatchmakingRentalsPacket(
            mode = 2,
            param1 = 0,
            param2 = 0,
            param3 = 0,
            previews =
                listOf(
                    MatchmakingRentalPreview(speciesId = 25, level = 50),
                    MatchmakingRentalPreview(speciesId = 6, level = 50),
                ),
            pokemon = null,
        ))
    ctx.session.send(
        TournamentPagePacket(
            tab = 0,
            active = false,
            page = 1,
            total = 1,
            tournaments =
                listOf(
                    TournamentRecord(
                        id = 7L,
                        kind = 0,
                        type = 0,
                        name = "Test Cup",
                        capacity = 8,
                        formatId = 0,
                        startTime = 0L,
                        byte1 = 0,
                        int1 = 0,
                        byte2 = 15,
                        flag1 = false,
                        flag2 = false,
                    )),
        ))
    ctx.session.send(TournamentEntrantCountPacket(tournamentId = 7L, entered = 4, checkedIn = 2))
    ctx.session.send(
        TournamentMatchupsPacket(
            matchups =
                listOf(
                    TournamentMatchup(
                        value = -1, type = 0, entityId = 100L, moveRefA = null, moveRefB = null),
                    TournamentMatchup(
                        value = 0, type = 1, entityId = 101L, moveRefA = 0, moveRefB = 1),
                )))
    ctx.session.send(
        HighScoreBoardPacket(
            category = 0,
            entries =
                listOf(
                    HighScoreEntry(
                        entityId = 11L,
                        rank = 1,
                        score = 1234,
                        appearances =
                            listOf(
                                HighScoreAppearance(
                                    name = "Pika",
                                    gender = 0,
                                    id = 25,
                                    kind = 0,
                                    palettePack = 0,
                                    slots = listOf(1, 2, 3, 4),
                                ))))))
  }
}
