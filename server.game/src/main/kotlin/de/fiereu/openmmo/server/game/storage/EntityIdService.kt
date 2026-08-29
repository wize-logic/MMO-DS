package de.fiereu.openmmo.server.game.storage

import java.util.concurrent.ThreadLocalRandom
import java.util.concurrent.atomic.AtomicLong
import javax.inject.Inject
import javax.inject.Singleton

const val CHARACTER_ID_TAG = 0x9000L
const val MONSTER_ID_TAG = 0xC000L

/** Creates entity ids without a database roundtrip, safe to call from any thread. */
@Singleton
class EntityIdService @Inject constructor() {

  private val counter = AtomicLong(ThreadLocalRandom.current().nextLong(256))

  fun newCharacterId(): Long = newId(CHARACTER_ID_TAG)

  fun newMonsterId(): Long = newId(MONSTER_ID_TAG)

  private fun newId(tag: Long): Long {
    val millis = System.currentTimeMillis() and 0x7F_FFFF_FFFFL
    val head = (millis shl 8) or (counter.getAndIncrement() and 0xFF)
    return (head shl 16) or tag
  }
}
