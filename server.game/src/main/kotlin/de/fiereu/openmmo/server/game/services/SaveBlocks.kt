package de.fiereu.openmmo.server.game.services

/**
 * The widest save block this server will store, matching the client's own `MMO_SAVE_BLOCK_BYTES`.
 */
const val MAX_SAVE_BLOCK_BYTES = 8192

/** Distinct save block ids one character may hold. The client keeps fifteen. */
const val MAX_SAVE_BLOCK_IDS = 24
