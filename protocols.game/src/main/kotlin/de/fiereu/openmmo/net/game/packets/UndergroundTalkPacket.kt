package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S64LE
import de.fiereu.bytecodec.U16LE
import de.fiereu.bytecodec.U8
import de.fiereu.bytecodec.bytesPrefixed

/**
 * Talking to another player in Sinnoh's Underground.
 *
 * @param kind which of the five below.
 * @param entityId the player this is about. c2s on a [KIND_REQUEST] it is the player being faced;
 * @param payload the kind's own bytes; see each constant.
 */
data class UndergroundTalkPacket(
    val kind: Int,
    val entityId: Long,
    val payload: ByteArray,
) {
  override fun equals(other: Any?): Boolean =
      other is UndergroundTalkPacket &&
          kind == other.kind &&
          entityId == other.entityId &&
          payload.contentEquals(other.payload)

  override fun hashCode(): Int = (kind * 31 + entityId.hashCode()) * 31 + payload.contentHashCode()

  companion object {
    /** c2s: "I pressed A facing [entityId]." No payload. */
    const val KIND_REQUEST = 0

    /**
     * c2s: this player's availability changed. `payload[0]` is one of [STATE_FREE], [STATE_BUSY],
     * [STATE_MINING].
     */
    const val KIND_STATE = 1

    /**
     * bidi: one of the conversation's own comm commands, relayed verbatim to the other party.
     * `payload[0]` is the command id (75, 76, 78, 80 or 82, the broadcast half of
     * `src/underground/player_talk.c` and `src/underground/records.c`) and the rest is its body.
     */
    const val KIND_DATA = 2

    /**
     * s2c: the answer to a [KIND_REQUEST]. `payload[0]` is the engine's `enum TalkResult`,
     * [TALK_SUCCESS], [TALK_FAIL] or [TALK_MINING], and `payload[1]` is which end of a successful
     * pairing this player is: [ROLE_INITIATOR] or [ROLE_RESPONDER].
     */
    const val KIND_RESULT = 3

    /**
     * bidi: the conversation is over. c2s when this player's menu closed; s2c when the other party
     * dropped, or left the cavern with a box still open on this screen.
     */
    const val KIND_END = 4

    /**
     * [KIND_END]: an orderly goodbye, which needs no telling. Both exits in `player_talk.c`
     * announce themselves to the other side through the relay first, and the menu still on the
     * other screen is printing "OK, see you!" and waiting for its own A press.
     */
    const val END_DONE = 0

    /**
     * [KIND_END]: this client would not take the pairing. Availability is a report, so it is a
     * round trip old, and the player who was faced may have opened a menu or started digging since.
     */
    const val END_REFUSED = 1

    /** [KIND_STATE]: free to be talked to. */
    const val STATE_FREE = 0

    /** [KIND_STATE]: in a menu, mid-step, trapped or already talking. */
    const val STATE_BUSY = 1

    /** [KIND_STATE]: inside the mining game, which has its own refusal line. */
    const val STATE_MINING = 2

    /** `TALK_RESULT_SUCCESS`: the conversation opens on both screens. */
    const val TALK_SUCCESS = 1

    /** `TALK_RESULT_FAIL`: "That person seems occupied." */
    const val TALK_FAIL = 2

    /** `TALK_RESULT_MINING`: "I'm digging right now!" */
    const val TALK_MINING = 4

    /** The player who pressed A. Runs `UndergroundTalk_Start`. */
    const val ROLE_INITIATOR = 0

    /** The player who was faced. Runs `UndergroundTalkResponse_Start`. */
    const val ROLE_RESPONDER = 1

    /* Fishing rides here too. */

    /** c2s: the rod was cast at the tile the player faces. `payload[0]` is the rod, 1..3. */
    const val KIND_FISHING_CAST = 5

    /** c2s: the minigame's reel-in landed; the held roll becomes the battle. No payload. */
    const val KIND_FISHING_HOOKED = 6

    /** c2s: the fish got away or the line was reeled in early; the held roll is dropped. */
    const val KIND_FISHING_LOST = 7

    /** s2c: the cast's answer. `payload[0]` is 1 for a bite worth playing for, 0 for nothing. */
    const val KIND_FISHING_VERDICT = 8

    /**
     * c2s: A on an Apricorn tree, with the tree's index in `payload[0]`. The lines are the client's
     * own; the record of who picked what today, and the fruit, are the server's.
     */
    const val KIND_APRICORN_PICK = 9

    /**
     * s2c: the pick's answer: `payload[0]` is 0 for no Apricorn Box, 1 for a tree already picked
     * today, 2 plus the kind (0 red .. 6 black) for a fruit now in the bag.
     */
    const val KIND_APRICORN_VERDICT = 10

    /**
     * c2s: a rock in front of the player was smashed, on a ported map. No payload: the rock is the
     * one the player faces, which this server knows.
     */
    const val KIND_ROCK_SMASH = 11

    /**
     * s2c: the smash's answer. `payload[0]` is [ROCK_NOTHING], [ROCK_BATTLE] for a wild battle the
     * server has opened, or [ROCK_ITEM] with the item's id in the next two bytes, little- endian,
     * an item the client's own obtain routine then reports, as a gift would.
     */
    const val KIND_ROCK_SMASH_VERDICT = 12

    /**
     * c2s: the tree in front of the player was headbutted, on a ported map. No payload, for the
     * same reason: the tree is the one faced.
     */
    const val KIND_HEADBUTT = 13

    /** s2c: the headbutt's answer. `payload[0]` is [ROCK_NOTHING] or [ROCK_BATTLE]. */
    const val KIND_HEADBUTT_VERDICT = 14

    /**
     * c2s: the scripted wild fight the server dealt on a press (a static site) ended won, or with
     * the Pokemon caught, on the client's engine, which is where a wild fight is fought; the
     * server's own instance only ever hears run at the end of one. No payload.
     */
    const val KIND_STATIC_WON = 15

    /** [KIND_ROCK_SMASH_VERDICT], [KIND_HEADBUTT_VERDICT]: nothing came of it. */
    const val ROCK_NOTHING = 0

    /** [KIND_ROCK_SMASH_VERDICT], [KIND_HEADBUTT_VERDICT]: a wild battle is on its way. */
    const val ROCK_BATTLE = 1

    /** [KIND_ROCK_SMASH_VERDICT]: an item was in the rubble; its id follows. */
    const val ROCK_ITEM = 2
  }
}

object UndergroundTalkPacketCodec : PacketCodec<UndergroundTalkPacket>() {
  override fun CodecScope<UndergroundTalkPacket>.body(): UndergroundTalkPacket {
    val kind = field(U8) { it.kind }
    val entityId = field(S64LE) { it.entityId }
    val payload = field(bytesPrefixed(U16LE)) { it.payload }
    return UndergroundTalkPacket(kind, entityId, payload)
  }
}
