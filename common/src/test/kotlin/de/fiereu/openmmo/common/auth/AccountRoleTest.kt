package de.fiereu.openmmo.common.auth

import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

/**
 * The roles are a ranking, so most of what matters here is which of them reach which. A check asks
 * for the least it needs and anything above it passes.
 */
class AccountRoleTest :
    FunSpec({
      test("a role reaches itself") {
        for (role in AccountRole.entries) {
          AccountRoles.of(role).has(role) shouldBe true
        }
      }

      test("a developer reaches every role") {
        val developer = AccountRoles.of(AccountRole.DEVELOPER)

        for (role in AccountRole.entries) {
          developer.has(role) shouldBe true
        }
      }

      test("a moderator reaches nothing above it") {
        val moderator = AccountRoles.of(AccountRole.MODERATOR)

        moderator.has(AccountRole.MODERATOR) shouldBe true
        moderator.has(AccountRole.ADMIN) shouldBe false
        moderator.has(AccountRole.DEVELOPER) shouldBe false
      }

      test("no role at all reaches nothing") {
        for (role in AccountRole.entries) {
          AccountRoles.NONE.has(role) shouldBe false
        }
      }

      test("granting and revoking are the two directions of one thing") {
        var roles = AccountRoles.NONE

        roles += AccountRole.ADMIN
        roles.has(AccountRole.ADMIN) shouldBe true

        roles -= AccountRole.ADMIN
        roles shouldBe AccountRoles.NONE
      }

      test("revoking a role that was never held changes nothing") {
        val roles = AccountRoles.of(AccountRole.ADMIN)

        (roles - AccountRole.MODERATOR) shouldBe roles
      }

      test("granted lists what was actually given, not what it implies") {
        AccountRoles.of(AccountRole.DEVELOPER).granted() shouldBe listOf(AccountRole.DEVELOPER)
      }

      /** A stored or signed value must not be able to mean more than the roles that exist. */
      test("bits no role uses are dropped") {
        AccountRoles.ofMask(-1) shouldBe
            AccountRoles.of(AccountRole.MODERATOR, AccountRole.ADMIN, AccountRole.DEVELOPER)
        AccountRoles.ofMask(1 shl 30) shouldBe AccountRoles.NONE
      }

      test("every role has a bit of its own") {
        AccountRole.entries.map { it.bit }.toSet().size shouldBe AccountRole.entries.size
      }

      test("a role is read from what a person typed, in any case") {
        AccountRole.parse("developer") shouldBe AccountRole.DEVELOPER
        AccountRole.parse("ADMIN") shouldBe AccountRole.ADMIN
        AccountRole.parse("Moderator") shouldBe AccountRole.MODERATOR
        AccountRole.parse("wizard") shouldBe null
      }

      test("an operator sees the roles by name") {
        AccountRoles.NONE.toString() shouldBe "none"
        AccountRoles.of(AccountRole.DEVELOPER).toString() shouldBe "developer"
        AccountRoles.of(AccountRole.MODERATOR, AccountRole.ADMIN).toString() shouldBe
            "moderator,admin"
      }
    })
