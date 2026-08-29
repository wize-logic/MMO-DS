package de.fiereu.openmmo.server.game.storage

import java.util.concurrent.ThreadLocalRandom
import java.util.concurrent.atomic.AtomicLong
import javax.inject.Inject
import javax.inject.Singleton

const val CHARACTER_ID_TAG = 0x9000L
const val MONSTER_ID_TAG = 0xC000L

/**
 * Creates entity ids without a database roundtrip, safe to call from any thread. An id is a head
 * shifted left 16 with the kind tag in the low bits, and the head is unix millis plus an eight bit
 * counter, so ids are unique in a process while fewer than 256 are made in the same millisecond.
 */
@Singleton
class EntityIdService @Inject constructor() {

  fun newCharacterId(): Long = newId(CHARACTER_ID_TAG)

  fun newMonsterId(): Long = newId(MONSTER_ID_TAG)

  private fun newId(tag: Long): Long {
    val millis = System.currentTimeMillis() and 0x7F_FFFF_FFFFL
    val head = (millis shl 8) or (counter.getAndIncrement() and 0xFF)
    return (head shl 16) or tag
  }

  private companion object {
    /**
     * Shared by every instance in the process. It is eight bits wide, so held per instance two
     * services picked independent starts and handed out the same id about once in 256. The start
     * stays random so two processes on one database do not line up.
     */
    val counter = AtomicLong(ThreadLocalRandom.current().nextLong(256))
  }
}
