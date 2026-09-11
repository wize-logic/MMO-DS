package de.fiereu.openmmo.server.game.offline.verify

import de.fiereu.openmmo.server.game.storage.ChainRepository
import de.fiereu.openmmo.server.game.storage.ExportRepository
import de.fiereu.openmmo.server.game.storage.ImportRepository
import de.fiereu.openmmo.server.game.storage.RequestRepository
import javax.inject.Inject

/** The four tables verification reads and writes, gathered so a provider has one thing to name. */
class ReplayStores
@Inject
constructor(
    val chains: ChainRepository,
    val imports: ImportRepository,
    val exports: ExportRepository,
    val requests: RequestRepository,
)
