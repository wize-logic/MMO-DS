package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.maps.WarpTile
import de.fiereu.openmmo.server.game.services.WarpService
import de.fiereu.openmmo.server.game.storage.CharacterStore
import javax.inject.Inject
import javax.inject.Singleton

/** Put a character on a map by its wire address, without a warp tile to stand on. */
@Singleton
class WarpCommand
@Inject
constructor(
    private val warpService: WarpService,
    private val characterStore: CharacterStore,
    private val mapManager: MapManager,
) : ChatCommand {
  override val name = "warp"
  override val usage = "/warp <bank> <map> [x] [y] [region]"
  override val description = "put yourself on a map by its bank and map number"
  override val role = AccountRole.DEVELOPER

  override suspend fun run(ctx: CommandContext) {
    var args = ctx.args
    if (args.isEmpty()) {
      ctx.reply("usage: $usage, /handbook <name> finds one, /pos prints the one you are on")
      return
    }

    /*
     * A name instead of the two numbers. The wire has no names, so this resolves one to an
     * address and everything below carries on as if it had been typed, coordinates included,
     * so `/warp jubilife_city 20 20` means what it says.
     */
    var named: MapDef? = null
    if (args[0].toIntOrNull() == null) {
      val wanted = args[0]
      val found = mapManager.search(wanted).filter { drawable(it) }
      named =
          found.firstOrNull { key(it.name) == key(wanted) }
              ?: found.singleOrNull()
              ?: run {
                when {
                  found.isNotEmpty() ->
                      ctx.reply(
                          "'$wanted' matches ${found.size}: " +
                              found.take(6).joinToString(", ") { it.name } +
                              ", /handbook $wanted for the rest")
                  mapManager.search(wanted).isNotEmpty() ->
                      ctx.reply(
                          "'$wanted' only names maps in a region this client cannot draw, " +
                              "only Sinnoh is playable")
                  else -> ctx.reply("No map is called '$wanted', try /handbook $wanted")
                }
                return
              }
      /* A MapDef stores its address as bytes and Sinnoh's map numbers pass 127, so a bare
       * toInt() went negative and the range check below refused every such map, Twinleaf is
       * map 155. Masked, the number is the one the wire carries. */
      args =
          listOf(
              (named.bankId.toInt() and 0xFF).toString(),
              (named.mapId.toInt() and 0xFF).toString(),
          ) + args.drop(1)
    }

    if (args.size < 2) {
      ctx.reply("usage: $usage, /pos prints the address you are on now")
      return
    }
    val bank = args[0].toIntOrNull()
    val map = args[1].toIntOrNull()
    if (bank == null || map == null || bank !in 0..255 || map !in 0..255) {
      ctx.reply("bank and map are each a byte (0-255); a header id is not one number here")
      return
    }
    val region: Byte =
        (args.getOrNull(4)?.toIntOrNull()
                ?: named?.regionId?.let { it.toInt() and 0xFF }
                ?: ctx.state.regionId.toInt())
            .toByte()
    /* The half of the undrawable-region refusal that covers the numeric form. Without it the
     * server moves the character, the client refuses the map, and the saved position is a strand:
     * the character logs in somewhere the client cannot draw, for ever after. */
    if ((region.toInt() and 0xFF) != DRAWABLE_REGION) {
      ctx.reply(
          "region $region is not one the client can draw, only Sinnoh ($DRAWABLE_REGION) is; " +
              "warping there would strand this character")
      return
    }
    val target = mapManager.getMap(region, bank.toByte(), map.toByte())

    val asked = args.getOrNull(2)?.toIntOrNull()
    val askedY = args.getOrNull(3)?.toIntOrNull()
    val x: Int
    val y: Int
    if (asked != null && askedY != null) {
      val why = target?.let { standable(it, asked, askedY) }
      if (why != null) {
        ctx.reply("($asked, $askedY) on $bank:$map $why, pick another, or pass none for a guess")
        return
      }
      x = asked
      y = askedY
    } else {
      /*
       * Where the game itself would put someone: its own fly destination for this town, which
       * is a tile it has land for and a player can stand on. Everywhere else still gets the
       * search below, which is the best this can do without a table.
       */
      val fly = if (region.toInt() == FLY_POINT_REGION) FLY_POINTS[(bank shl 8) or map] else null
      val guess = fly?.let { it.x to it.y } ?: target?.let { middleOfTheLargestWalkableRegion(it) }
      if (guess == null && target?.terrain != null) {
        ctx.reply("no standable tile on $bank:$map, nothing to warp onto")
        return
      }
      x = guess?.first ?: 0
      y = guess?.second ?: 0
    }

    val charId = ctx.characterId
    warpService.executeWarp(
        ctx.session,
        charId,
        WarpTile(
            x = 0,
            y = 0,
            targetRegionId = region,
            targetBankId = bank.toByte(),
            targetMapId = map.toByte(),
            targetX = x,
            targetY = y,
            /*
             * The height the matrix gives that cell. A DS map is a grid of chunks each sitting
             * at its own altitude, and a warp that says nothing says zero, which on a town
             * built above zero is the player standing inside the ground.
             */
            targetElevation = target?.terrain?.altitudeAt(x, y) ?: 0,
            exitFacing = Direction.DOWN,
        ),
    )
    characterStore.flushCharacterAsync(charId)
    ctx.reply("warping to region $region bank $bank map $map at ($x, $y)")
  }

  /**
   * The header this map is on the wire, which is what a matrix labels its cells with: a Platinum
   * header is split across the two bytes a packet has for it, so bank 2 map 82 is header 594.
   */
  private fun headerOf(map: MapDef): Int =
      ((map.bankId.toInt() and 0xFF) shl 8) or (map.mapId.toInt() and 0xFF)

  /** Whether the client's engine draws this map's region, see `mmo/src/region.c`'s roster. */
  private fun drawable(map: MapDef): Boolean = (map.regionId.toInt() and 0xFF) == DRAWABLE_REGION

  companion object {
    /** Sinnoh, the one region the client's engine draws (`mmo/src/region.c`). */
    const val DRAWABLE_REGION = 3

    /** Names compare without case or separators, the way MapManager normalizes them. */
    private fun key(s: String) = s.filter { it.isLetterOrDigit() }.lowercase()
  }

  /** Whether the tile belongs to this map at all. */
  private fun owns(map: MapDef, x: Int, y: Int): Boolean =
      map.terrain?.headerAt(x, y)?.let { it == headerOf(map) } ?: true

  /** Why a player on foot cannot stand here, or null when they can. Null too with no terrain. */
  private fun standable(map: MapDef, x: Int, y: Int): String? {
    val terrain = map.terrain ?: return null
    if (!owns(map, x, y)) {
      val owner = terrain.headerAt(x, y)
      return "is on header $owner, not this map"
    }
    val tile = terrain.tileAt(x, y) ?: return "is off its ${terrain.width}x${terrain.height} plane"
    if (tile.collision.toInt() != 0) return "is a wall"
    if (tile.behavior.isWater) return "is open water"
    return null
  }

  /**
   * A tile to land on when none was named: the one nearest the middle of the map that is part of
   * the largest place a player can walk.
   */
  private fun middleOfTheLargestWalkableRegion(map: MapDef): Pair<Int, Int>? {
    val terrain = map.terrain ?: return null
    val w = terrain.width
    val h = terrain.height
    /*
     * Ownership first, and not only for correctness: it is a per cell lookup, where standable
     * decodes a whole 2048 byte chunk to answer. Asking it first keeps this to the map's own
     * handful of chunks instead of all nine hundred the overworld is made of.
     */
    val walkable =
        BooleanArray(w * h) { owns(map, it % w, it / w) && standable(map, it % w, it / w) == null }
    val seen = BooleanArray(w * h)
    var best: List<Int> = emptyList()
    for (start in walkable.indices) {
      if (!walkable[start] || seen[start]) continue
      val region = mutableListOf<Int>()
      val queue = ArrayDeque(listOf(start))
      seen[start] = true
      while (queue.isNotEmpty()) {
        val at = queue.removeFirst()
        region += at
        val x = at % w
        val y = at / w
        if (x > 0) push(at - 1, walkable, seen, queue)
        if (x < w - 1) push(at + 1, walkable, seen, queue)
        if (y > 0) push(at - w, walkable, seen, queue)
        if (y < h - 1) push(at + w, walkable, seen, queue)
      }
      if (region.size > best.size) best = region
    }
    if (best.isEmpty()) return null
    /* The middle of what was found, not of the plane: on a shared matrix the plane's centre is
     * some other town. */
    val cx = (best.minOf { it % w } + best.maxOf { it % w }) / 2
    val cy = (best.minOf { it / w } + best.maxOf { it / w }) / 2
    val pick = best.minBy { kotlin.math.abs(it % w - cx) + kotlin.math.abs(it / w - cy) }
    return pick % w to pick / w
  }

  private fun push(at: Int, walkable: BooleanArray, seen: BooleanArray, queue: ArrayDeque<Int>) {
    if (walkable[at] && !seen[at]) {
      seen[at] = true
      queue.addLast(at)
    }
  }
}
