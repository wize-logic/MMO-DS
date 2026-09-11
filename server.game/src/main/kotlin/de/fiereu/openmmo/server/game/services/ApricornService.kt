package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.items.ItemDef
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.items.generated.Items
import de.fiereu.openmmo.net.game.packets.UndergroundTalkPacket
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** An Apricorn tree on a ported map, answered. */
@Singleton
class ApricornService
@Inject
constructor(
    private val npcService: NpcService,
    private val characterStore: CharacterStore,
    private val storyService: StoryService,
    private val storyPlayerService: StoryPlayerService,
    private val worldClock: WorldClock,
    private val items: ItemRegistry,
) {

  suspend fun onPick(event: PacketEvent<UndergroundTalkPacket>) {
    val session = event.session
    val state = session.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val stored = characterStore.getCharacter(charId) ?: return
    val index = event.packet.payload.firstOrNull()?.toInt()?.and(0xFF) ?: return
    // The tree is the one in front of the player, at the position and facing this server keeps
    // for the session, the same tile a rod cast lands on and a sign press reads.
    val facing = state.facingDirection
    val npc =
        npcService.visibleNpcAt(
            session,
            state.regionId,
            state.bankId,
            state.mapId,
            state.x + facing.dx,
            state.y + facing.dy,
        )
    val tree = npc?.script?.let { TREE.matchEntire(it) }
    if (tree == null || tree.groupValues[1].toInt() != index) {
      log.info { "char=$charId picks apricorn tree $index, which is not the tree faced" }
      return answer(session, NO_BOX)
    }
    val kind = tree.groupValues[2].toInt()
    val fruit = FRUIT.getOrNull(kind - 1)
    if (fruit == null) {
      log.warn { "char=$charId picks apricorn tree $index of kind $kind, which is not a fruit" }
      return answer(session, NO_BOX)
    }
    val boxHeld = items.idsOf(Items.APRICORN_BOX).any { (stored.items[it] ?: 0) > 0 }
    if (!boxHeld) return answer(session, NO_BOX)
    val today = worldClock.now().toLocalDate().toEpochDay().toInt()
    val key = "apricorn/$index"
    if (storyService.getVar(charId, key) == today) return answer(session, NOTHING_TODAY)
    if (!storyPlayerService.giveItem(session, state, fruit, 1)) {
      log.info { "char=$charId picks a ${fruit.name} the bag cannot take" }
      return answer(session, NOTHING_TODAY)
    }
    storyService.setVar(charId, key, today)
    log.info { "char=$charId picks a ${fruit.name} from apricorn tree $index" }
    answer(session, PICKED + (kind - 1))
  }

  private fun answer(session: SessionContext, verdict: Int) {
    session.send(
        UndergroundTalkPacket(
            UndergroundTalkPacket.KIND_APRICORN_VERDICT, 0, byteArrayOf(verdict.toByte())))
  }

  companion object {
    private val TREE = Regex("""apricorn:(\d+):(\d+)""")

    /** The seven fruits in the source's own order of kinds, red to black. */
    private val FRUIT: List<ItemDef> =
        listOf(
            Items.RED_APRICORN,
            Items.YLW_APRICORN,
            Items.BLU_APRICORN,
            Items.GRN_APRICORN,
            Items.PNK_APRICORN,
            Items.WHT_APRICORN,
            Items.BLK_APRICORN,
        )
    const val NO_BOX = 0
    const val NOTHING_TODAY = 1
    const val PICKED = 2
  }
}
