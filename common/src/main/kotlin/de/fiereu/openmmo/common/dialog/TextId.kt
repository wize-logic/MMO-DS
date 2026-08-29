package de.fiereu.openmmo.common.dialog

import de.fiereu.openmmo.common.enums.Region

/** How a text string is named on the wire. */
object TextId {
  const val REGION_SHIFT = 28
  const val BANK_SHIFT = 16

  /** 4095: what a bank index has to fit in. The DS archive holds 724 banks. */
  const val BANK_MAX = (1 shl (REGION_SHIFT - BANK_SHIFT)) - 1

  /** 65535: what a message index has to fit in. The largest DS bank holds 1269 messages. */
  const val ENTRY_MAX = (1 shl BANK_SHIFT) - 1

  /** A DS string, named by the bank and message index the decomp gives it. */
  fun ds(region: Region, bank: Int, entry: Int): Int {
    require(bank in 0..BANK_MAX) { "text bank $bank does not fit in the id's bank field" }
    require(entry in 0..ENTRY_MAX) { "text entry $entry does not fit in the id's entry field" }
    return region(region) or (bank shl BANK_SHIFT) or entry
  }

  /** A GBA string, named by where it sits in the ROM. */
  fun gba(region: Region, romOffset: Int): Int {
    require(romOffset in 0..(1 shl REGION_SHIFT) - 1) { "ROM offset $romOffset has no room here" }
    return region(region) or romOffset
  }

  fun regionOf(textId: Int): Int = (textId ushr REGION_SHIFT) and 0xF

  fun bankOf(textId: Int): Int = (textId ushr BANK_SHIFT) and BANK_MAX

  fun entryOf(textId: Int): Int = textId and ENTRY_MAX

  private fun region(region: Region): Int = region.wireValue.toInt() shl REGION_SHIFT
}
