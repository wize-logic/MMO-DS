package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.server.game.storage.CharacterStore
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Reads and writes a character's story progression: boolean flags and named integer vars. This
 * is the game agnostic story state.
 */
@Singleton
class StoryService @Inject constructor(private val characterStore: CharacterStore) {

  fun isFlagSet(characterId: Long, flag: String): Boolean =
      characterStore.getCharacter(characterId)?.storyFlags?.contains(flag) ?: false

  fun setFlag(characterId: Long, flag: String) = characterStore.setStoryFlag(characterId, flag)

  fun clearFlag(characterId: Long, flag: String) = characterStore.clearStoryFlag(characterId, flag)

  fun getVar(characterId: Long, key: String): Int =
      characterStore.getCharacter(characterId)?.storyVars?.get(key) ?: 0

  fun setVar(characterId: Long, key: String, value: Int) =
      characterStore.setStoryVar(characterId, key, value)
}
