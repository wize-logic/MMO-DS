package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*

/** One iv the egg might get in a stat, and how likely it is. */
data class BreedingStatContribution(
    val value: Byte,
    val chance: Float,
    val label: Int,
)

/**
 * One stat's forecast. Entries come in the stat order hp, atk, def, speed, spAtk, spDef, the
 * order a monster record's IVs are already in, so the position in the list is the stat and
 * there is no stat id on the wire.
 */
data class BreedingStatEntry(
    val guaranteed: Boolean,
    val itemId: Short,
    val contributions: List<BreedingStatContribution>,
)

/** What a pairing would produce, in answer to an [AssignBreedingSlotPacket]. */
data class BreedingForecastPacket(
    val parentA: Long,
    val parentB: Long,
    val hasPreview: Boolean,
    val species: Short,
    val form: Byte,
    val statEntries: List<BreedingStatEntry>,
    val shininessTypes: List<Byte>,
    val moveIds: List<Short>,
    val moveSources: List<Byte>,
    val otStatus: Byte,
    val nature: Short,
    val genderSelectable: Boolean,
    val genderCostFirst: Int,
    val genderCostSecond: Int,
)

private object BreedingStatContributionCodec : PacketCodec<BreedingStatContribution>() {
  override fun CodecScope<BreedingStatContribution>.body(): BreedingStatContribution {
    val value = field(S8, BreedingStatContribution::value)
    val chance = field(F32LE, BreedingStatContribution::chance)
    val label = field(S32LE, BreedingStatContribution::label)
    return BreedingStatContribution(value, chance, label)
  }
}

private val BreedingStatContributionListPrefixedU8: Codec<List<BreedingStatContribution>> =
    object : Codec<List<BreedingStatContribution>> {
      override fun read(buf: ReadBuffer): List<BreedingStatContribution> {
        val n = U8.read(buf)
        return List(n) { BreedingStatContributionCodec.read(buf) }
      }

      override fun write(buf: WriteBuffer, value: List<BreedingStatContribution>) {
        U8.write(buf, value.size)
        value.forEach { BreedingStatContributionCodec.write(buf, it) }
      }
    }

private object BreedingStatEntryCodec : PacketCodec<BreedingStatEntry>() {
  override fun CodecScope<BreedingStatEntry>.body(): BreedingStatEntry {
    val guaranteed = field(Bool, BreedingStatEntry::guaranteed)
    val itemId = field(S16LE, BreedingStatEntry::itemId)
    val contributions =
        field(BreedingStatContributionListPrefixedU8, BreedingStatEntry::contributions)
    return BreedingStatEntry(guaranteed, itemId, contributions)
  }
}

object BreedingForecastPacketCodec : PacketCodec<BreedingForecastPacket>() {
  override fun CodecScope<BreedingForecastPacket>.body(): BreedingForecastPacket {
    val parentA = field(S64LE, BreedingForecastPacket::parentA)
    val parentB = field(S64LE, BreedingForecastPacket::parentB)
    val hasPreview = field(Bool, BreedingForecastPacket::hasPreview)
    if (!hasPreview) {
      return BreedingForecastPacket(
          parentA,
          parentB,
          hasPreview,
          0,
          0,
          emptyList(),
          emptyList(),
          emptyList(),
          emptyList(),
          0,
          0,
          false,
          0,
          0,
      )
    }
    val species = field(S16LE, BreedingForecastPacket::species)
    val form = field(S8, BreedingForecastPacket::form)
    val statCount = field(U8) { it.statEntries.size }
    val statEntries = List(statCount) { i -> field(BreedingStatEntryCodec) { it.statEntries[i] } }
    val shininessCount = field(U8) { it.shininessTypes.size }
    val shininessTypes = List(shininessCount) { i -> field(S8) { it.shininessTypes[i] } }
    val moveCount = field(U8) { it.moveIds.size }
    val moveIds = List(moveCount) { i -> field(S16LE) { it.moveIds[i] } }
    val moveSources = List(moveCount) { i -> field(S8) { it.moveSources[i] } }
    val otStatus = field(S8, BreedingForecastPacket::otStatus)
    val nature = field(S16LE, BreedingForecastPacket::nature)
    val genderSelectable = field(Bool, BreedingForecastPacket::genderSelectable)
    val genderCostFirst = field(S32LE, BreedingForecastPacket::genderCostFirst)
    val genderCostSecond = field(S32LE, BreedingForecastPacket::genderCostSecond)
    return BreedingForecastPacket(
        parentA,
        parentB,
        hasPreview,
        species,
        form,
        statEntries,
        shininessTypes,
        moveIds,
        moveSources,
        otStatus,
        nature,
        genderSelectable,
        genderCostFirst,
        genderCostSecond,
    )
  }
}
