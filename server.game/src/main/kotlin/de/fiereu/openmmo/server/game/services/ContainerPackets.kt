package de.fiereu.openmmo.server.game.services

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket

/** How many records one container packet carries. */
internal const val RECORDS_PER_CONTAINER_PACKET = 200

/**
 * One container as the packets that carry it: the first replaces the container, the rest merge into
 * it by monster id, which is how the client applies a container that does not fit in one.
 */
internal fun containerPackets(
    container: PokemonContainer,
    pokemon: List<Pokemon>,
): List<PokemonContainerPacket> =
    pokemon
        .chunked(RECORDS_PER_CONTAINER_PACKET)
        .ifEmpty { listOf(emptyList()) }
        .mapIndexed { i, chunk ->
          PokemonContainerPacket(
              container = container,
              hasChange = i == 0,
              delete = false,
              pokemon = chunk,
          )
        }

/** Sends [pokemon] as [container], chunked. */
internal fun SessionContext.sendContainer(container: PokemonContainer, pokemon: List<Pokemon>) {
  containerPackets(container, pokemon).forEach { send(it) }
}
