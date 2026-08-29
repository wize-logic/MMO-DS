package de.fiereu.openmmo.net.game.packets.gtl

import de.fiereu.bytecodec.Codec
import de.fiereu.bytecodec.ReadBuffer
import de.fiereu.bytecodec.S16LE
import de.fiereu.bytecodec.S32LE
import de.fiereu.bytecodec.S64LE
import de.fiereu.bytecodec.S8
import de.fiereu.bytecodec.U8
import de.fiereu.bytecodec.WriteBuffer
import de.fiereu.openmmo.net.game.codecs.PokemonCodec

sealed interface GtlSearchListing {
  val listingId: Long
  val price: Int
  val listedAt: Int
  val expiresAt: Int
  val quantity: Short
  val own: GtlOwnInfo?
}

/**
 * The tail an OWN_LISTINGS row carries (ours; no captures of a non-empty own page exists, and the
 * write path always self-described, the design notes): where the listing stands, how many units are
 * still up, and how many sold units await Claim.
 */
data class GtlOwnInfo(val state: Byte, val remaining: Short, val unclaimedUnits: Short)

// Every row names its own kind, so an own listings page can mix monsters and items. An
// OWN_LISTINGS page's rows also carry a tail (GtlOwnInfo), ours, since no captures of a
// non-empty own page exists and this wire's both ends are this repository.
internal fun gtlSearchListingCodec(pageKind: GtlListKind): Codec<GtlSearchListing> =
    object : Codec<GtlSearchListing> {
      override fun read(buf: ReadBuffer): GtlSearchListing {
        val listingId = S64LE.read(buf)
        val rowKind = GtlListKindCodec.read(buf)
        val price = S32LE.read(buf)
        val listedAt = S32LE.read(buf)
        val expiresAt = S32LE.read(buf)
        val quantity = S16LE.read(buf)
        val row: GtlSearchListing =
            if (rowKind == GtlListKind.ITEM) {
              val itemId = S16LE.read(buf)
              val itemState = S8.read(buf)
              GtlItemListing(listingId, price, listedAt, expiresAt, quantity, itemId, itemState)
            } else {
              val hasRecord = U8.read(buf) == 1
              val pokemon = if (hasRecord) PokemonCodec.read(buf) else null
              val stats = if (hasRecord) List(6) { S16LE.read(buf) } else emptyList()
              GtlPokemonListing(listingId, price, listedAt, expiresAt, quantity, pokemon, stats)
            }
        if (pageKind != GtlListKind.OWN_LISTINGS) return row
        val own = GtlOwnInfo(S8.read(buf), S16LE.read(buf), S16LE.read(buf))
        return when (row) {
          is GtlItemListing -> row.copy(own = own)
          is GtlPokemonListing -> row.copy(own = own)
        }
      }

      override fun write(buf: WriteBuffer, value: GtlSearchListing) {
        S64LE.write(buf, value.listingId)
        GtlListKindCodec.write(
            buf,
            if (value is GtlItemListing) GtlListKind.ITEM else GtlListKind.POKEMON,
        )
        S32LE.write(buf, value.price)
        S32LE.write(buf, value.listedAt)
        S32LE.write(buf, value.expiresAt)
        S16LE.write(buf, value.quantity)
        when (value) {
          is GtlItemListing -> {
            S16LE.write(buf, value.itemId)
            S8.write(buf, value.itemState)
          }
          is GtlPokemonListing -> {
            U8.write(buf, if (value.pokemon == null) 0 else 1)
            value.pokemon?.let { PokemonCodec.write(buf, it) }
            value.stats.forEach { S16LE.write(buf, it) }
          }
        }
        if (pageKind == GtlListKind.OWN_LISTINGS) {
          val own = value.own ?: GtlOwnInfo(0, value.quantity, 0)
          S8.write(buf, own.state)
          S16LE.write(buf, own.remaining)
          S16LE.write(buf, own.unclaimedUnits)
        }
      }
    }
