package de.fiereu.openmmo.common.enums

/**
 * What kind of tile the player is standing on, normalized across games from the decomp metatile
 * behaviors. Only the behaviors we act on are named, everything else is [NORMAL].
 */
enum class TileBehavior {
  NORMAL,
  TALL_GRASS,
  LONG_GRASS,
  JUMP_EAST,
  JUMP_WEST,
  JUMP_NORTH,
  JUMP_SOUTH,
  DOOR,
  /** Cave and water doors. They look like doors but warp like a ladders. */
  NON_ANIMATED_DOOR,
  /** Ladders, escalators and warp pads, which warp as soon as the player steps on them. */
  LADDER,
  STAIR_WARP_EAST,
  STAIR_WARP_WEST,
  /** Arrow warps are named after the direction the player walks to use them. */
  NORTH_ARROW_WARP,
  SOUTH_ARROW_WARP,
  EAST_ARROW_WARP,
  WEST_ARROW_WARP,
  /**
   * The HM walls, which are a tile rather than an object: **42,346** Sinnoh tiles are surfable
   * water, **55** are a waterfall and **247** are a rock-climb face.
   */
  SURFABLE_WATER,
  WATERFALL,
  ROCK_CLIMB_NORTH_SOUTH,
  ROCK_CLIMB_EAST_WEST;

  /** The direction the player must walk while standing here to be warped. */
  val warpsWhenWalking: Direction?
    get() =
        when (this) {
          STAIR_WARP_EAST,
          EAST_ARROW_WARP -> Direction.RIGHT
          STAIR_WARP_WEST,
          WEST_ARROW_WARP -> Direction.LEFT
          NORTH_ARROW_WARP -> Direction.UP
          SOUTH_ARROW_WARP -> Direction.DOWN
          else -> null
        }

  val warpsOnStep: Boolean
    get() = this == LADDER || this == NON_ANIMATED_DOOR

  /** True on a tile only a field move gets the player past or onto. */
  val needsFieldMove: Boolean
    get() =
        this == SURFABLE_WATER ||
            this == WATERFALL ||
            this == ROCK_CLIMB_NORTH_SOUTH ||
            this == ROCK_CLIMB_EAST_WEST

  /** True on a tile a player can only be on while surfing: open water and the falls themselves. */
  val isWater: Boolean
    get() = this == SURFABLE_WATER || this == WATERFALL

  /** True when a player facing this rock wall from [facing] can climb it. */
  fun rockClimbAllows(facing: Direction): Boolean =
      when (this) {
        ROCK_CLIMB_NORTH_SOUTH -> facing == Direction.UP || facing == Direction.DOWN
        ROCK_CLIMB_EAST_WEST -> facing == Direction.LEFT || facing == Direction.RIGHT
        else -> false
      }
}
