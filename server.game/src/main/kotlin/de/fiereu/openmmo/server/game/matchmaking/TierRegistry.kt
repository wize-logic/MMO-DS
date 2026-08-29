package de.fiereu.openmmo.server.game.matchmaking

import de.fiereu.openmmo.common.pvp.TierGroup
import io.github.oshai.kotlinlogging.KotlinLogging
import java.nio.file.Files
import java.nio.file.Path
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** Where the packaged table lives, and the variable that points at another copy of it. */
private const val PACKAGED = "/pvp/tiers.tsv"
private const val OVERRIDE_ENV = "OPENMMO_PVP_TIERS"

/** A species absent from the table sits here, and this permits every queue. */
private val UNTIERED = setOf(TierGroup.UNTIERED)

/** One row: which groups a species, or one of its formes, belongs to. */
private data class TierKey(val dexId: Int, val form: Int)

/** Which competitive groups a species belongs to, and therefore which queues will take it. */
@Singleton
class TierRegistry @Inject constructor() {
  private val rows: Map<TierKey, Set<TierGroup>>

  init {
    rows = load()
  }

  /** Every group this species sits in. Empty of an entry means untiered. */
  fun groupsOf(dexId: Int, form: Int = 0): Set<TierGroup> =
      rows[TierKey(dexId, form)] ?: rows[TierKey(dexId, ANY_FORM)] ?: UNTIERED

  /**
   * The strictest group this species sits in, which is the one that decides what it may enter.
   */
  fun strictestGroup(dexId: Int, form: Int = 0): TierGroup =
      groupsOf(dexId, form).minByOrNull { it.id.toInt() } ?: TierGroup.UNTIERED

  /** Whether a queue checking against [group] will take this species. */
  fun permits(group: TierGroup, dexId: Int, form: Int = 0): Boolean {
    if (!group.speciesTier || group == TierGroup.UNLIMITED) return true
    return strictestGroup(dexId, form).id >= group.id
  }

  /** How many species the table covers, per group, for the startup line. */
  fun describe(): String {
    if (rows.isEmpty()) return "no species are tiered; every queue takes everything"
    val counts =
        TierGroup.entries
            .filter { it.speciesTier }
            .associateWith { g -> rows.values.count { it.contains(g) } }
            .filterValues { it > 0 }
    val empty =
        TierGroup.entries
            .filter { it.speciesTier && it != TierGroup.UNTIERED }
            .filter { g -> rows.values.none { it.contains(g) } }
    val covered = counts.entries.joinToString(", ") { "${it.key} ${it.value}" }
    return if (empty.isEmpty()) covered
    else
        "$covered; nothing is assigned to ${empty.joinToString(", ")}, so those queues take" +
            " whatever the looser ones do"
  }

  private fun load(): Map<TierKey, Set<TierGroup>> {
    val override = System.getenv(OVERRIDE_ENV)
    val text =
        if (!override.isNullOrBlank()) {
          val path = Path.of(override)
          if (Files.isReadable(path)) {
            log.info { "Tier table from $path" }
            Files.readString(path)
          } else {
            log.warn { "$OVERRIDE_ENV points at $path, which is not readable; using the packaged" }
            packaged()
          }
        } else {
          packaged()
        }
    return parse(text)
  }

  private fun packaged(): String =
      TierRegistry::class.java.getResourceAsStream(PACKAGED)?.bufferedReader()?.readText()
          ?: run {
            log.error { "No tier table at $PACKAGED; every queue will take everything" }
            ""
          }

  private fun parse(text: String): Map<TierKey, Set<TierGroup>> {
    val out = HashMap<TierKey, Set<TierGroup>>()
    text.lineSequence().forEachIndexed { i, raw ->
      val line = raw.substringBefore('#').trim()
      if (line.isEmpty()) return@forEachIndexed
      val cells = line.split('\t').map { it.trim() }.filter { it.isNotEmpty() }
      if (cells.size < 3) {
        log.warn { "Tier table line ${i + 1} has ${cells.size} columns, not 3 or more; skipped" }
        return@forEachIndexed
      }
      val dexId = cells[0].toIntOrNull()
      val form = cells[1].toIntOrNull()
      if (dexId == null || form == null) {
        log.warn { "Tier table line ${i + 1} does not start with two numbers; skipped" }
        return@forEachIndexed
      }
      val groups =
          cells[2]
              .split(',')
              .mapNotNull { name ->
                val g = TierGroup.entries.firstOrNull { it.name == name.trim().uppercase() }
                if (g == null) log.warn { "Tier table line ${i + 1} names no group '$name'" }
                g
              }
              .toSet()
      if (groups.isEmpty()) return@forEachIndexed
      out[TierKey(dexId, form)] = groups
    }
    return out
  }

  companion object {
    /** A row's form column, when the row covers every forme of the species. */
    const val ANY_FORM: Int = -1
  }
}
