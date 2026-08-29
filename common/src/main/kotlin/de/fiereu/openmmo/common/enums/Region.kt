package de.fiereu.openmmo.common.enums

/** A world, by the id it carries on the wire. */
enum class Region(val wireValue: Byte, val creatable: Boolean) {
  KANTO(0, creatable = false),
  HOENN(1, creatable = false),
  // 3 is read off the official client: its region helper splits ids into GBA {0,1} and NDS {2,3,4}
  // plus the custom region 10, names them from string 250000+id (250010 is "the official client"),
  // and orders
  // them 0,4,1,3,2, generation order, which puts Kanto 0, Johto 4, Hoenn 1, Sinnoh 3, Unova 2.
  SINNOH(3, creatable = true);

  val displayName: String = name.lowercase().replaceFirstChar { it.uppercase() }

  companion object {
    fun byId(id: Int): Region? = entries.find { it.wireValue.toInt() == id }

    fun byWireValue(wireValue: Byte): Region? = entries.find { it.wireValue == wireValue }
  }
}
