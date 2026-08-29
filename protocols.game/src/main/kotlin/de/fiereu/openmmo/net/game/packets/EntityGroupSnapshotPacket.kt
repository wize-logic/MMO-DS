package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*

data class EntityAppearanceInfo(
    val name: String,
    val gender: Byte,
    val formId: Int,
    val kind: Byte,
    val palettePack: Byte,
    val slots: List<Short>,
)

/* One f/m80 as the official client reads it: S64LE, S16LE, U8, U8, S16LE. */
data class GroupListFrame(
    val entityId: Long,
    val typeId: Short,
    val byteA: Byte,
    val byteB: Byte,
    val packed2: Short,
)

data class GroupListFrameSet(
    val listType: Byte?,
    val frames: List<GroupListFrame>,
)

data class EntityGroupMember(
    val entityId: Long,
    val appearance: EntityAppearanceInfo,
    val frames: GroupListFrameSet,
)

data class EntityGroupSnapshotPacket(
    val present: Boolean,
    val leaderId: Long?,
    val members: List<EntityGroupMember>?,
)

internal val EntityAppearanceInfoCodec: Codec<EntityAppearanceInfo> =
    object : PacketCodec<EntityAppearanceInfo>() {
      override fun CodecScope<EntityAppearanceInfo>.body(): EntityAppearanceInfo {
        val name = field(Utf16LeNullTerminated) { it.name }
        val gender = field(S8) { it.gender }
        val formId = field(S32LE) { it.formId }
        val kind = field(S8) { it.kind }
        val palettePack = field(S8) { it.palettePack }
        val slots = field(S16LE.repeat(4)) { it.slots }
        return EntityAppearanceInfo(name, gender, formId, kind, palettePack, slots)
      }
    }

internal val GroupListFrameCodec: Codec<GroupListFrame> =
    object : PacketCodec<GroupListFrame>() {
      override fun CodecScope<GroupListFrame>.body(): GroupListFrame {
        val entityId = field(S64LE) { it.entityId }
        val typeId = field(S16LE) { it.typeId }
        val byteA = field(S8) { it.byteA }
        val byteB = field(S8) { it.byteB }
        val packed2 = field(S16LE) { it.packed2 }
        return GroupListFrame(entityId, typeId, byteA, byteB, packed2)
      }
    }

internal val GroupListFrameSetCodec: Codec<GroupListFrameSet> =
    object : PacketCodec<GroupListFrameSet>() {
      override fun CodecScope<GroupListFrameSet>.body(): GroupListFrameSet {
        val count = field(U8) { it.frames.size }
        val listType = if (count >= 1) field(S8) { it.listType ?: 0 } else null
        val frames = (0 until count).map { i -> field(GroupListFrameCodec) { it.frames[i] } }
        return GroupListFrameSet(listType, frames)
      }
    }

internal val EntityGroupMemberCodec: Codec<EntityGroupMember> =
    object : PacketCodec<EntityGroupMember>() {
      override fun CodecScope<EntityGroupMember>.body(): EntityGroupMember {
        val entityId = field(S64LE) { it.entityId }
        val appearance = field(EntityAppearanceInfoCodec) { it.appearance }
        val frames = field(GroupListFrameSetCodec) { it.frames }
        return EntityGroupMember(entityId, appearance, frames)
      }
    }

object EntityGroupSnapshotPacketCodec : PacketCodec<EntityGroupSnapshotPacket>() {
  override fun CodecScope<EntityGroupSnapshotPacket>.body(): EntityGroupSnapshotPacket {
    val present = field(U8) { if (it.present) 1 else 0 } == 1
    val leaderId = if (present) field(S64LE) { it.leaderId!! } else null
    val members =
        if (present) field(EntityGroupMemberCodec.listPrefixed(U8)) { it.members!! } else null
    return EntityGroupSnapshotPacket(present, leaderId, members)
  }
}
