package de.fiereu.openmmo.server.game.script

/** The gym badges, eight per region, in each cartridge's own `BADGE_ID_*` order. */
enum class Badge(val home: String) {
  COAL("sinnoh"),
  FOREST("sinnoh"),
  COBBLE("sinnoh"),
  FEN("sinnoh"),
  RELIC("sinnoh"),
  MINE("sinnoh"),
  ICICLE("sinnoh"),
  BEACON("sinnoh"),
  ZEPHYR("johto"),
  HIVE("johto"),
  PLAIN("johto"),
  FOG("johto"),
  STORM("johto"),
  MINERAL("johto"),
  GLACIER("johto"),
  RISING("johto"),
  BOULDER("kanto"),
  CASCADE("kanto"),
  THUNDER("kanto"),
  RAINBOW("kanto"),
  SOUL("kanto"),
  MARSH("kanto"),
  VOLCANO("kanto"),
  EARTH("kanto");

  /** The story key this badge is held under for a player in [regionName]. */
  fun keyIn(regionName: String): String = "$regionName/BADGE_ID_$name"

  /** The story key this badge is held under in its own region, which is where it is earned. */
  val key: String
    get() = keyIn(home)

  companion object {
    /** The wire id of a badge: Sinnoh's `BADGE_ID_*` value, then HeartGold's plus eight. */
    fun wireId(badge: Badge): Short = badge.ordinal.toShort()

    /** How many a region has, and the first wire id of each region's eight. */
    const val PER_REGION = 8
  }
}
