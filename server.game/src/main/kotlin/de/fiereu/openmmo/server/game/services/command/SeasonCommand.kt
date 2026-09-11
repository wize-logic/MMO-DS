package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.enums.TimeOfDay
import de.fiereu.openmmo.server.game.services.WorldClock
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class SeasonCommand @Inject constructor(private val worldClock: WorldClock) : ChatCommand {
  override val name = "season"
  override val usage = "/season"
  override val description = "what season it is, and where the daylight stands"

  override suspend fun run(ctx: CommandContext) {
    val season = worldClock.season()
    val now = worldClock.now()
    val bucket = worldClock.timeOfDay()
    val b = TimeOfDay.boundaries(season)
    ctx.reply(
        "It is ${season.name.lowercase()}, %02d:%02d (%s). Daybreak %02d:00, day %02d:00, twilight %02d:00, night %02d:00."
            .format(
                now.hour,
                now.minute,
                bucket.name.lowercase().replace('_', ' '),
                b[0],
                b[1],
                b[2],
                b[3]))
  }
}
