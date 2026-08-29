package de.fiereu.openmmo.server.web

import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain

class PagesTest :
    FunSpec({
      test("a name is escaped wherever a page repeats it back") {
        Pages.escape("""<img src=x onerror="alert('1')">""") shouldBe
            "&lt;img src=x onerror=&quot;alert(&#39;1&#39;)&quot;&gt;"
      }

      test("the success page names the account and links onwards") {
        val page = Pages.created("ash")

        page shouldContain "You're in, ash."
        page shouldContain """href="https://github.com/wize-logic/OpenMMO-DS""""
      }
    })
