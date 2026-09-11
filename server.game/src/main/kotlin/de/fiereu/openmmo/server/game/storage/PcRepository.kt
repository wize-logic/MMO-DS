package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.db.game.tables.references.CHARACTER_PCS
import java.time.LocalDateTime
import javax.inject.Inject
import javax.inject.Named
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import org.jooq.DSLContext

/** One PC a character has used, and the tile they used it from. */
data class PcVisit(
    val characterId: Long,
    val region: Int,
    val bank: Int,
    val map: Int,
    val x: Int,
    val y: Int,
    val elevation: Int,
    val lastUsed: LocalDateTime,
) {
  /** The map header the wire address names: bank above, map below. */
  val header: Int
    get() = ((bank and 0xFF) shl 8) or (map and 0xFF)

  fun samePlace(other: PcVisit): Boolean =
      (region and 0xFF) == (other.region and 0xFF) &&
          (bank and 0xFF) == (other.bank and 0xFF) &&
          (map and 0xFF) == (other.map and 0xFF)
}

interface PcRepository {
  /** Record a use of the PC at [visit]'s place; a repeat only refreshes the tile and the time. */
  suspend fun touch(visit: PcVisit)

  /** Every PC the character has used, most recent first. */
  suspend fun list(characterId: Long): List<PcVisit>
}

class JooqPcRepository
@Inject
constructor(
    private val dsl: DSLContext,
    @param:Named("db") private val dispatcher: CoroutineDispatcher,
) : PcRepository {

  override suspend fun touch(visit: PcVisit) =
      withContext(dispatcher) {
        dsl.insertInto(CHARACTER_PCS)
            .set(CHARACTER_PCS.CHARACTER_ID, visit.characterId)
            .set(CHARACTER_PCS.REGION, visit.region)
            .set(CHARACTER_PCS.BANK, visit.bank)
            .set(CHARACTER_PCS.MAP, visit.map)
            .set(CHARACTER_PCS.X, visit.x)
            .set(CHARACTER_PCS.Y, visit.y)
            .set(CHARACTER_PCS.ELEVATION, visit.elevation)
            .set(CHARACTER_PCS.LAST_USED, visit.lastUsed)
            .onConflict(
                CHARACTER_PCS.CHARACTER_ID,
                CHARACTER_PCS.REGION,
                CHARACTER_PCS.BANK,
                CHARACTER_PCS.MAP,
            )
            .doUpdate()
            .set(CHARACTER_PCS.X, visit.x)
            .set(CHARACTER_PCS.Y, visit.y)
            .set(CHARACTER_PCS.ELEVATION, visit.elevation)
            .set(CHARACTER_PCS.LAST_USED, visit.lastUsed)
            .execute()
        Unit
      }

  override suspend fun list(characterId: Long): List<PcVisit> =
      withContext(dispatcher) {
        dsl.selectFrom(CHARACTER_PCS)
            .where(CHARACTER_PCS.CHARACTER_ID.eq(characterId))
            .orderBy(CHARACTER_PCS.LAST_USED.desc())
            .fetch { r ->
              PcVisit(
                  characterId = r[CHARACTER_PCS.CHARACTER_ID]!!,
                  region = r[CHARACTER_PCS.REGION]!!,
                  bank = r[CHARACTER_PCS.BANK]!!,
                  map = r[CHARACTER_PCS.MAP]!!,
                  x = r[CHARACTER_PCS.X]!!,
                  y = r[CHARACTER_PCS.Y]!!,
                  elevation = r[CHARACTER_PCS.ELEVATION]!!,
                  lastUsed = r[CHARACTER_PCS.LAST_USED]!!,
              )
            }
      }
}
