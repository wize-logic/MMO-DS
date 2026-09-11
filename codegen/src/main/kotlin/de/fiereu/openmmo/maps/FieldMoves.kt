package de.fiereu.openmmo.maps

import de.fiereu.openmmo.items.ItemDef

/** One tile of a headbutt tree on a ported map. */
data class HeadbuttTree(val x: Int, val y: Int, val tree: Int, val secret: Boolean)

/** What a smashed rock may leave behind on a ported map. */
data class RockSmashRubble(val odds: Int, val items: List<ItemDef>)
