package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.enums.PokemonContainer

/** The one question five doors ask, and the sentences they answer it with. */

/** How a monster is named in a sentence a player reads back. */
fun Pokemon.nick(): String = nickname.ifEmpty { "No. $dexId" }

/** Whether this character's fighting party holds anything that came out of a save file. */
fun List<Pokemon>.partyHoldsOfflineOrigin(): Boolean = any {
  it.container == PokemonContainer.PARTY && it.offlineOrigin
}

/** The refusal a door gives back, naming the monster that earned it. */
fun refusedForOfflineOrigin(mon: Pokemon): String =
    "${mon.nick()} was brought in from an offline save, so it cannot change hands."

/**
 * What the player is told at the moment a catch of theirs earns the mark, so the trade window is
 * never where they find out.
 */
fun staticSiteMarkedNotice(mon: Pokemon): String =
    "${mon.nick()} was caught with progress brought in from an offline save, so it carries the" +
        " same mark that save's Pokemon do: it plays and battles, and it cannot change hands" +
        " until the play behind that save has been checked."

/** What the other player is told before they answer a challenge. */
fun offlineOriginNotice(trainerName: String): String =
    "$trainerName's team includes Pokemon brought in from an offline save."
