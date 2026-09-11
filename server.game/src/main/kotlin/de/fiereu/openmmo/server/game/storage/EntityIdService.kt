package de.fiereu.openmmo.server.game.storage

import java.util.concurrent.ThreadLocalRandom
import java.util.concurrent.atomic.AtomicLong
import javax.inject.Inject
import javax.inject.Singleton

const val CHARACTER_ID_TAG = 0x9000L
const val MONSTER_ID_TAG = 0xC000L

/** Never on the wire: an import row is a server-side record with no packet that names it. */
const val IMPORT_ID_TAG = 0xD000L

/**
 * Nor is a chain of offline sessions: it is the evidence behind an import, not a thing in a game.
 */
const val CHAIN_ID_TAG = 0xD100L

/** Nor is an offline copy a session sent as it left: the image a chain of sessions starts from. */
const val EXPORT_ID_TAG = 0xD200L

/** A request to have offline play checked. */
const val REQUEST_ID_TAG = 0xD300L

/** Creates entity ids without a database roundtrip, safe to call from any thread. */
@Singleton
class EntityIdService @Inject constructor() {

  fun newCharacterId(): Long = newId(CHARACTER_ID_TAG)

  fun newMonsterId(): Long = newId(MONSTER_ID_TAG)

  fun newImportId(): Long = newId(IMPORT_ID_TAG)

  fun newChainId(): Long = newId(CHAIN_ID_TAG)

  fun newExportId(): Long = newId(EXPORT_ID_TAG)

  fun newRequestId(): Long = newId(REQUEST_ID_TAG)

  private fun newId(tag: Long): Long {
    val millis = System.currentTimeMillis() and 0x7F_FFFF_FFFFL
    val head = (millis shl 8) or (counter.getAndIncrement() and 0xFF)
    return (head shl 16) or tag
  }

  private companion object {
    /**
     * Shared by every instance in the process, because the counter is the only thing separating two
     * ids made in the same millisecond and it is eight bits wide.
     */
    val counter = AtomicLong(ThreadLocalRandom.current().nextLong(256))
  }
}
