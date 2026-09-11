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
  /**
   * The wide gaps in the Distortion World, which are jumped over two tiles rather than one: the
   * cartridge marks the two tiles between the ledges impassable and puts this behaviour on the near
   * one, and `PlayerAvatar_WillJumpTwice` reads exactly that.
   */
  JUMP_NORTH_TWICE,
  JUMP_SOUTH_TWICE,
  JUMP_WEST_TWICE,
  JUMP_EAST_TWICE,
  /**
   * A bike ramp, named after the direction it is ridden. The tile itself is impassable, on foot it
   * is a wall on both halves, and a bike takes it anyway, then is thrown off it by the ramp's own
   * forced movement.
   */
  BIKE_RAMP_EAST,
  BIKE_RAMP_WEST,
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
  ROCK_CLIMB_EAST_WEST,
  /** A shop counter or a desk: the tile between the player and the person behind it. */
  COUNTER;

  /** The direction a ledge is hopped in, or null on anything else. */
  val jumpsWhenWalking: Direction?
    get() =
        when (this) {
          JUMP_NORTH -> Direction.UP
          JUMP_SOUTH -> Direction.DOWN
          JUMP_WEST -> Direction.LEFT
          JUMP_EAST -> Direction.RIGHT
          else -> null
        }

  /** The direction a jump-twice tile is crossed in, or null on anything else. */
  val jumpsTwiceWhenWalking: Direction?
    get() =
        when (this) {
          JUMP_NORTH_TWICE -> Direction.UP
          JUMP_SOUTH_TWICE -> Direction.DOWN
          JUMP_WEST_TWICE -> Direction.LEFT
          JUMP_EAST_TWICE -> Direction.RIGHT
          else -> null
        }

  /** The direction a bike ramp is ridden in, or null on anything else. */
  val rampRidesToward: Direction?
    get() =
        when (this) {
          BIKE_RAMP_EAST -> Direction.RIGHT
          BIKE_RAMP_WEST -> Direction.LEFT
          else -> null
        }

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
