package de.fiereu.openmmo.server.game.services

/*
 * The key items. They are story progress and one-per-save equipment, not goods, and two walls
 * are built on knowing which ids they are: the GTL refuses to carry them, and the registered-
 * item slot accepts nothing else.
 */
val KEY_ITEM_IDS = 5428..5467

/*
 * Where the engine's own item table starts on this wire. An item's id here is this base plus
 * its index in the engine's `generated/items.txt`, where an entry's index is its line less one,
 * the same join the key items pocket above is cut from.
 */
const val ITEM_ID_BASE = 5000

/* The TMs and HMs. */
val TMHM_ITEM_IDS = 5328..5427

/*
 * How deep a bag stack goes, from the engine's `src/bag.c`: BAG_MAX_QUANTITY_ITEM for every
 * pocket but the TMs and HMs, which get a tenth of it.
 */
const val BAG_MAX_QUANTITY_ITEM = 999
const val BAG_MAX_QUANTITY_TMHM = 99

/** The deepest stack of [itemId] the client's bag will take. */
fun bagStackLimit(itemId: Int): Int =
    if (itemId in TMHM_ITEM_IDS) BAG_MAX_QUANTITY_TMHM else BAG_MAX_QUANTITY_ITEM
