package de.fiereu.openmmo.codegen.item

import io.kotest.assertions.throwables.shouldThrow
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldContainExactly
import io.kotest.matchers.shouldBe
import java.io.File

private fun dataDir(
    names: List<String>,
    prices: Map<Int, Int>,
    holdEffects: Map<Int, Int> = emptyMap(),
): File {
  val dir = kotlin.io.path.createTempDirectory("item-data").toFile()
  dir.resolve("item_names.json").writeText(names.joinToString(",", "[", "]") { "\"$it\"" })
  dir.resolve("items.json")
      .writeText(
          prices.entries.joinToString(",", "[", "]") { (index, price) ->
            """{"index":$index,"price":$price,"hold_effect":${holdEffects[index] ?: 0}}"""
          })
  return dir
}

class ItemDataParserTest :
    FunSpec({
      test("reads a name and its price into the region 5 id block") {
        val items =
            ItemDataParser(
                    dataDir(
                        listOf("None", "Master Ball", "Ultra Ball", "Poké Ball"),
                        mapOf(1 to 0, 2 to 1200, 3 to 200)))
                .parseAll()

        items.single { it.identifier == "POKE_BALL" } shouldBe
            ParsedItem("POKE_BALL", "Poké Ball", 200, 0, listOf(5003))
      }

      test("carries the hold effect the item table gives a slot") {
        val items =
            ItemDataParser(
                    dataDir(
                        listOf("None", "Potion", "Everstone"),
                        mapOf(1 to 300, 2 to 200),
                        mapOf(2 to 64)))
                .parseAll()

        items.single { it.identifier == "EVERSTONE" }.holdEffect shouldBe 64
        items.single { it.identifier == "POTION" }.holdEffect shouldBe 0
      }

      test("collects every slot sharing a name into one item with several ids") {
        val items =
            ItemDataParser(
                    dataDir(
                        listOf("None", "Potion", "Antidote", "Potion"),
                        mapOf(1 to 300, 2 to 100, 3 to 300)))
                .parseAll()

        items.single { it.identifier == "POTION" }.ids shouldContainExactly listOf(5001, 5003)
      }

      test("skips index 0 and the placeholder slots") {
        val items =
            ItemDataParser(dataDir(listOf("None", "?????", "-", "", "Potion"), mapOf(4 to 300)))
                .parseAll()

        items.map { it.identifier } shouldContainExactly listOf("POTION")
      }

      test("derives identifiers that are legal Kotlin names") {
        val names = listOf("None", "Poké Ball", "Parlyz Heal", "Up-Grade", "10 Coins", "X Attack")
        val items = ItemDataParser(dataDir(names, emptyMap())).parseAll()

        items.map { it.identifier } shouldContainExactly
            listOf("POKE_BALL", "PARLYZ_HEAL", "UP_GRADE", "_10_COINS", "X_ATTACK")
      }

      test("prices an item the table has no entry for at zero") {
        val items = ItemDataParser(dataDir(listOf("None", "Parcel"), emptyMap())).parseAll()

        items.single().price shouldBe 0
      }

      test("carries field use, use class and heal amount from the table") {
        val dir = kotlin.io.path.createTempDirectory("item-use").toFile()
        dir.resolve("item_names.json").writeText("""["None","Poké Ball","Potion"]""")
        dir.resolve("items.json")
            .writeText(
                """
                [
                  {"index":1,"price":200,"hold_effect":0,"amount":0,"field_use":"none","use_class":"ball"},
                  {"index":2,"price":300,"hold_effect":0,"amount":20,"field_use":"medicine","use_class":0}
                ]
                """
                    .trimIndent())

        val items = ItemDataParser(dir).parseAll()

        val ball = items.single { it.identifier == "POKE_BALL" }
        ball.useClass shouldBe "ball"
        ball.fieldUse shouldBe "none"
        ball.amount shouldBe 0

        val potion = items.single { it.identifier == "POTION" }
        potion.fieldUse shouldBe "medicine"
        potion.useClass shouldBe "0"
        potion.amount shouldBe 20
      }

      test("fails loudly when the vendored data is missing") {
        val empty = kotlin.io.path.createTempDirectory("item-data-empty").toFile()

        shouldThrow<IllegalArgumentException> { ItemDataParser(empty).parseAll() }
      }
    })
