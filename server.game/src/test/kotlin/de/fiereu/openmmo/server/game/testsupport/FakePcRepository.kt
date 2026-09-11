package de.fiereu.openmmo.server.game.testsupport

import de.fiereu.openmmo.server.game.storage.PcRepository
import de.fiereu.openmmo.server.game.storage.PcVisit
import java.util.concurrent.ConcurrentHashMap

/** The PC table as a map keyed the way its primary key is. */
class FakePcRepository : PcRepository {
  val rows = ConcurrentHashMap<List<Int>, PcVisit>()

  override suspend fun touch(visit: PcVisit) {
    rows[listOf(visit.characterId.toInt(), visit.region, visit.bank, visit.map)] = visit
  }

  override suspend fun list(characterId: Long): List<PcVisit> =
      rows.values.filter { it.characterId == characterId }.sortedByDescending { it.lastUsed }
}
