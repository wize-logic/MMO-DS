package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S32LE
import de.fiereu.bytecodec.U8
import de.fiereu.bytecodec.Utf16LeNullTerminated
import de.fiereu.bytecodec.listPrefixed
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.net.game.codecs.DefaultSkinSetCodec
import de.fiereu.openmmo.net.game.codecs.PokemonCodec
import de.fiereu.openmmo.net.game.codecs.SkinSet

/** Seat a native Platinum link battle between two players. */
data class LinkBattleOpenPacket(
    val battleId: Int,
    val netId: Int,
    val peerName: String,
    /**
     * [de.fiereu.openmmo.common.enums.CharacterGender]'s wire value: the sprite the other side is
     * drawn as. Each client draws its own from its own save, so only the opponent's travels.
     */
    val peerGender: Int,
    val party: List<Pokemon>,
    /** The other player's cosmetic slots, the same set [LoadEntityPacket] carries. */
    val peerSkin: SkinSet = SkinSet(),
)

object LinkBattleOpenPacketCodec : PacketCodec<LinkBattleOpenPacket>() {
  override fun CodecScope<LinkBattleOpenPacket>.body(): LinkBattleOpenPacket {
    val battleId = field(S32LE) { it.battleId }
    val netId = field(U8) { it.netId }
    val peerName = field(Utf16LeNullTerminated) { it.peerName }
    val peerGender = field(U8) { it.peerGender }
    val party = field(PokemonCodec.listPrefixed(U8), LinkBattleOpenPacket::party)
    val peerSkin = field(DefaultSkinSetCodec, LinkBattleOpenPacket::peerSkin)
    return LinkBattleOpenPacket(battleId, netId, peerName, peerGender, party, peerSkin)
  }
}
