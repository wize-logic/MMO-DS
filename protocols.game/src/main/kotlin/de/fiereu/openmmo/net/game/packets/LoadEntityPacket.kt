package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.EntityStatus
import de.fiereu.openmmo.net.game.codecs.DefaultSkinSetCodec
import de.fiereu.openmmo.net.game.codecs.SkinSet

data class LoadEntityPacket(
    val entityId: Long,
    /** Which body the client draws the entity's sprite from: 0 male, 1 female. */
    val gender: Byte,
    val skin: SkinSet,
    val name: String,
    val regionId: Int,
    val bankId: Int,
    val mapId: Int,
    val x: Int,
    val y: Int,
    val z: Int,
    val facing: Direction,
    val transportation: Int = 0,
    val entityNameplateType: Int = 0,
    val status: EntityStatus = EntityStatus.NONE,
    val hasFollower: Boolean,
    val followerDexId: Short,
    /**
     * What the follower looks like, which the official client's own packet cannot say: its dex id
     * is the whole of the field.
     */
    val followerForm: Int = 0,
    val followerFemale: Boolean = false,
    val followerShiny: Boolean = false,
)

object LoadEntityPacketCodec : PacketCodec<LoadEntityPacket>() {
  override fun CodecScope<LoadEntityPacket>.body(): LoadEntityPacket {
    val entityId = field(S64LE, LoadEntityPacket::entityId)
    val gender = field(S8, LoadEntityPacket::gender)
    val skin = field(DefaultSkinSetCodec, LoadEntityPacket::skin)
    val name = field(Utf16LeNullTerminated, LoadEntityPacket::name)
    val regionId = field(U8, LoadEntityPacket::regionId)
    val bankId = field(U8, LoadEntityPacket::bankId)
    val mapId = field(U8, LoadEntityPacket::mapId)
    val x = field(S16LE) { it.x.toShort() }.toInt()
    val y = field(S16LE) { it.y.toShort() }.toInt()
    val z = field(U8, LoadEntityPacket::z)
    // Only the low two bits of this byte are the heading; 0x08 is a separate flag the client
    // matches against a tile's own when it binds the entity to a tile.
    val facing = Direction.entries[field(U8) { it.facing.ordinal and 0x03 } and 0x03]
    // The client reads the first of these bitwise and the second as a small enum;
    // neither meaning is established, so both stay 0 until one is.
    val transportation = field(U8, LoadEntityPacket::transportation)
    val entityNameplateType = field(U8, LoadEntityPacket::entityNameplateType)
    // 0x20 is ours and rides behind every one of the official client's; it says nothing at all
    // unless there is
    // a follower for it to describe.
    val flags = field(U8) { if (it.hasFollower) 0x04 or 0x20 else 0 }
    if (flags and 0x01 != 0) field(S8) { 0 }
    if (flags and 0x02 != 0) {
      field(S8) { 0 }
      field(U16LE) { 0 }
    }
    val hasFollower = (flags and 0x04) != 0
    val followerDexId: Short = if (hasFollower) field(S16LE, LoadEntityPacket::followerDexId) else 0
    if (flags and 0x08 != 0) field(S8) { 0 }
    // A prefix the client draws in brackets before the name. It is a bare string:
    // there is no length or id in front of it.
    if (flags and 0x10 != 0) field(Utf16LeNullTerminated) { "" }
    // Three bytes of our own behind every one of the official client's; a packet without the bit
    // asks for the
    // ordinary male non-shiny coat, which is the picture the official client draws.
    var followerForm = 0
    var followerFemale = false
    var followerShiny = false
    if (flags and 0x20 != 0) {
      followerForm = field(U8, LoadEntityPacket::followerForm)
      followerFemale = field(U8) { if (it.followerFemale) 1 else 0 } != 0
      followerShiny = field(U8) { if (it.followerShiny) 1 else 0 } != 0
    }
    return LoadEntityPacket(
        entityId = entityId,
        gender = gender,
        skin = skin,
        name = name,
        regionId = regionId,
        bankId = bankId,
        mapId = mapId,
        x = x,
        y = y,
        z = z,
        facing = facing,
        transportation = transportation,
        entityNameplateType = entityNameplateType,
        status = EntityStatus.NONE,
        hasFollower = hasFollower,
        followerDexId = followerDexId,
        followerForm = followerForm,
        followerFemale = followerFemale,
        followerShiny = followerShiny,
    )
  }
}
