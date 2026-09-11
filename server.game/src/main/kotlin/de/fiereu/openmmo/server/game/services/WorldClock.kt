package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.common.enums.Season
import de.fiereu.openmmo.common.enums.TimeOfDay
import java.time.Clock
import java.time.LocalDateTime
import javax.inject.Inject
import javax.inject.Singleton

/** The world's one calendar and clock. */
@Singleton
class WorldClock @Inject constructor() {
  private val clock: Clock = Clock.systemDefaultZone()

  fun now(): LocalDateTime = LocalDateTime.now(clock)

  fun season(): Season = Season.forMonth(now().monthValue)

  fun timeOfDay(): TimeOfDay = TimeOfDay.forHour(now().hour, season())
}
