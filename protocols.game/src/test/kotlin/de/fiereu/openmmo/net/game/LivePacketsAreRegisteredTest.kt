package de.fiereu.openmmo.net.game

import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import java.io.File

/** Nothing in `packets/` is off the wire. */
class LivePacketsAreRegisteredTest :
    FunSpec({
      val declaration =
          Regex(
              """^(?:internal\s+)?(?:data\s+)?(?:class|object)\s+(\w*Packet)\b""",
              RegexOption.MULTILINE)

      test("every packet type outside packets/spec has an opcode") {
        val live = File("src/main/kotlin/de/fiereu/openmmo/net/game/packets")
        live.isDirectory shouldBe true

        val declared =
            live
                .walkTopDown()
                .onEnter { it.name != "spec" }
                .filter { it.isFile && it.extension == "kt" }
                .flatMap { file -> declaration.findAll(file.readText()).map { it.groupValues[1] } }
                .toSet()
        val registered = GameProtocol.registrations.mapNotNull { it.type.simpleName }.toSet()

        (declared - registered).sorted() shouldBe emptyList()
      }
    })
