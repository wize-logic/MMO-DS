package de.fiereu.openmmo.codegen.gen5

import de.fiereu.openmmo.common.enums.Ability
import de.fiereu.openmmo.common.enums.BodyColor
import de.fiereu.openmmo.common.enums.EggGroup
import de.fiereu.openmmo.common.enums.EvolutionMethod
import de.fiereu.openmmo.common.enums.GrowthRate
import de.fiereu.openmmo.common.enums.MoveEffect
import de.fiereu.openmmo.common.enums.MoveFlag
import de.fiereu.openmmo.common.enums.MoveTarget
import de.fiereu.openmmo.common.enums.PokemonType
import java.io.File
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.boolean
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/** The last id Platinum has, per class. A Gen 5 row must be past its own. */
const val LAST_GEN4_SPECIES = 493
const val LAST_GEN4_MOVE = 467

/** Genesect, the last species Black has and so the last row the whole-dex tables must cover. */
const val LAST_GEN5_SPECIES = 649

/** The species and moves Black adds, which no decompilation this repo reads can supply. */
class Gen5Tables(private val dir: File) {

  private val json = Json { ignoreUnknownKeys = true }

  init {
    checkEnumTail("abilities", Ability.entries.map { it.name })
    checkEnumTail("move_effects", MoveEffect.entries.map { it.name })
  }

  val species: List<Gen5Species> by lazy {
    read("species.json", "species").map { row ->
      val obj = row.jsonObject
      val id = obj.getValue("id").jsonPrimitive.int
      require(id > LAST_GEN4_SPECIES) {
        "gen5/species.json holds species $id, which Platinum already has"
      }
      val stats = obj.getValue("base_stats").jsonObject
      val evs = obj.getValue("ev_yields").jsonObject
      val items = obj.getValue("held_items").jsonObject
      val types = obj.getValue("types").jsonArray.map { it.jsonPrimitive.int }
      val abilities = obj.getValue("abilities").jsonArray.map { it.jsonPrimitive.int }
      val groups = obj.getValue("egg_groups").jsonArray.map { it.jsonPrimitive.int }
      require(types.size == 2 && abilities.size == 2 && groups.size == 2) {
        "species $id has ${types.size} types, ${abilities.size} abilities and " +
            "${groups.size} egg groups, the game holds two of each"
      }
      Gen5Species(
          id = id,
          name = obj.getValue("name").jsonPrimitive.content,
          baseHp = stats.getValue("hp").jsonPrimitive.int,
          baseAttack = stats.getValue("attack").jsonPrimitive.int,
          baseDefense = stats.getValue("defense").jsonPrimitive.int,
          baseSpeed = stats.getValue("speed").jsonPrimitive.int,
          baseSpAttack = stats.getValue("special_attack").jsonPrimitive.int,
          baseSpDefense = stats.getValue("special_defense").jsonPrimitive.int,
          type1 = ordinal<PokemonType>(id, types[0]),
          type2 = ordinal<PokemonType>(id, types[1]),
          catchRate = obj.getValue("catch_rate").jsonPrimitive.int,
          expYield = obj.getValue("base_exp_reward").jsonPrimitive.int,
          evYieldHp = evs.getValue("hp").jsonPrimitive.int,
          evYieldAttack = evs.getValue("attack").jsonPrimitive.int,
          evYieldDefense = evs.getValue("defense").jsonPrimitive.int,
          evYieldSpeed = evs.getValue("speed").jsonPrimitive.int,
          evYieldSpAttack = evs.getValue("special_attack").jsonPrimitive.int,
          evYieldSpDefense = evs.getValue("special_defense").jsonPrimitive.int,
          itemCommon = items.getValue("common").jsonPrimitive.int,
          itemRare = items.getValue("rare").jsonPrimitive.int,
          genderRatio = obj.getValue("gender_ratio").jsonPrimitive.int,
          eggCycles = obj.getValue("hatch_cycles").jsonPrimitive.int,
          friendship = obj.getValue("base_friendship").jsonPrimitive.int,
          growthRate = ordinal<GrowthRate>(id, obj.getValue("exp_rate").jsonPrimitive.int),
          eggGroup1 = ordinal<EggGroup>(id, groups[0]),
          eggGroup2 = ordinal<EggGroup>(id, groups[1]),
          ability1 = ordinal<Ability>(id, abilities[0]),
          ability2 = ordinal<Ability>(id, abilities[1]),
          escapeRate = obj.getValue("escape_rate").jsonPrimitive.int,
          bodyColor = ordinal<BodyColor>(id, obj.getValue("body_color").jsonPrimitive.int),
          flipSprite = obj.getValue("flip_sprite").jsonPrimitive.boolean,
          offspringSpeciesId = obj.getValue("offspring").jsonPrimitive.int,
          learnset =
              obj.getValue("learnset").jsonArray.map {
                val entry = it.jsonObject
                Gen5LevelUpMove(
                    level = entry.getValue("level").jsonPrimitive.int,
                    moveId = entry.getValue("move").jsonPrimitive.int,
                )
              },
          machineMoveIds = obj.getValue("machine_moves").jsonArray.map { it.jsonPrimitive.int },
          evolutions =
              obj.getValue("evolutions").jsonArray.map {
                val entry = it.jsonObject
                Gen5Evolution(
                    method = evolutionMethod(id, entry.getValue("method").jsonPrimitive.int),
                    param = entry.getValue("param").jsonPrimitive.int,
                    targetSpeciesId = entry.getValue("target").jsonPrimitive.int,
                )
              },
      )
    }
  }

  val moves: List<Gen5Move> by lazy {
    read("moves.json", "moves").map { row ->
      val obj = row.jsonObject
      val id = obj.getValue("id").jsonPrimitive.int
      require(id > LAST_GEN4_MOVE) { "gen5/moves.json holds move $id, which Platinum already has" }
      Gen5Move(
          id = id,
          name = obj.getValue("name").jsonPrimitive.content,
          effect = ordinal<MoveEffect>(id, obj.getValue("effect").jsonPrimitive.int),
          power = obj.getValue("power").jsonPrimitive.int,
          type = ordinal<PokemonType>(id, obj.getValue("type").jsonPrimitive.int),
          accuracy = obj.getValue("accuracy").jsonPrimitive.int,
          pp = obj.getValue("pp").jsonPrimitive.int,
          secondaryEffectChance = obj.getValue("chance").jsonPrimitive.int,
          target = name<MoveTarget>(id, obj.getValue("target").jsonPrimitive.content),
          priority = obj.getValue("priority").jsonPrimitive.int,
          flags =
              obj.getValue("flags").jsonArray.map { name<MoveFlag>(id, it.jsonPrimitive.content) },
      )
    }
  }

  /** The hidden ability of every species in the dex, not only Black's own. */
  val hiddenAbilities: Map<Int, String> by lazy {
    val table =
        json
            .parseToJsonElement(
                File(dir, "hidden_abilities.json")
                    .also { require(it.exists()) { "the Gen 5 tables are missing ${it.path}" } }
                    .readText())
            .jsonObject
            .getValue("hidden_abilities")
            .jsonObject
    val out =
        table.entries.associate { (dex, ability) ->
          val id = dex.toInt()
          id to ordinal<Ability>(id, ability.jsonPrimitive.int)
        }
    val missing = (1..LAST_GEN5_SPECIES).filterNot { it in out }
    require(missing.isEmpty()) {
      "hidden_abilities.json has no row for ${missing.size} species, first ${missing.first()}"
    }
    out
  }

  private fun read(file: String, key: String) =
      json
          .parseToJsonElement(
              File(dir, file)
                  .also { require(it.exists()) { "the Gen 5 tables are missing ${it.path}" } }
                  .readText())
          .jsonObject
          .getValue(key)
          .jsonArray

  /** The entry the cartridge's number picks out, by ordinal. */
  private inline fun <reified E : Enum<E>> ordinal(owner: Int, id: Int): String {
    val entries = enumValues<E>()
    require(id in entries.indices) {
      "$owner names ${E::class.simpleName} $id, and there are only ${entries.size}"
    }
    return entries[id].name
  }

  /** The name, checked against the enum that has to hold it. */
  private inline fun <reified E : Enum<E>> name(owner: Int, token: String): String {
    require(enumValues<E>().any { it.name == token }) {
      "$owner names $token, which ${E::class.simpleName} does not have"
    }
    return token
  }

  /**
   * An evolution method, which is the one enum here that carries its own id rather than taking its
   * ordinal, so it is looked up by that id and not by position.
   */
  private fun evolutionMethod(owner: Int, id: Int): String =
      (EvolutionMethod.byId(id)
              ?: error("$owner evolves by method $id, which EvolutionMethod does not have"))
          .name

  /** The ids Gen 5 added to an enum Gen 4 also numbers, each against the ordinal it must sit at. */
  private fun checkEnumTail(key: String, ours: List<String>) {
    val tail =
        json
            .parseToJsonElement(
                File(dir, "enums.json")
                    .also { require(it.exists()) { "the Gen 5 tables are missing ${it.path}" } }
                    .readText())
            .jsonObject
            .getValue(key)
            .jsonObject
    for ((id, theirs) in tail) {
      val ordinal = id.toInt()
      require(ordinal < ours.size) {
        "the cartridge's $key run reaches $ordinal and our enum stops at ${ours.size - 1}"
      }
      require(ours[ordinal] == theirs.jsonPrimitive.content) {
        "$key $ordinal is ${theirs.jsonPrimitive.content} on the cartridge and " +
            "${ours[ordinal]} here"
      }
    }
  }
}

data class Gen5LevelUpMove(val level: Int, val moveId: Int)

data class Gen5Evolution(val method: String, val param: Int, val targetSpeciesId: Int)

data class Gen5Species(
    val id: Int,
    val name: String,
    val baseHp: Int,
    val baseAttack: Int,
    val baseDefense: Int,
    val baseSpeed: Int,
    val baseSpAttack: Int,
    val baseSpDefense: Int,
    val type1: String,
    val type2: String,
    val catchRate: Int,
    val expYield: Int,
    val evYieldHp: Int,
    val evYieldAttack: Int,
    val evYieldDefense: Int,
    val evYieldSpeed: Int,
    val evYieldSpAttack: Int,
    val evYieldSpDefense: Int,
    val itemCommon: Int,
    val itemRare: Int,
    val genderRatio: Int,
    val eggCycles: Int,
    val friendship: Int,
    val growthRate: String,
    val eggGroup1: String,
    val eggGroup2: String,
    val ability1: String,
    val ability2: String,
    /**
     * Gen 5's own escape rate, which sits in the byte Gen 4 spends on the Great Marsh flee rate.
     */
    val escapeRate: Int,
    val bodyColor: String,
    val flipSprite: Boolean,
    val offspringSpeciesId: Int,
    val learnset: List<Gen5LevelUpMove>,
    /** Every move a machine teaches this species, as move ids and never as machine numbers. */
    val machineMoveIds: List<Int>,
    val evolutions: List<Gen5Evolution>,
)

data class Gen5Move(
    val id: Int,
    val name: String,
    val effect: String,
    val power: Int,
    val type: String,
    val accuracy: Int,
    val pp: Int,
    val secondaryEffectChance: Int,
    val target: String,
    val priority: Int,
    val flags: List<String>,
)
