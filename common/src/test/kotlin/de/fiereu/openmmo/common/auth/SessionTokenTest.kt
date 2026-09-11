package de.fiereu.openmmo.common.auth

import io.kotest.assertions.throwables.shouldThrow
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import java.time.Clock
import java.time.Duration
import java.time.Instant
import java.time.ZoneOffset

class SessionTokenTest :
    FunSpec({
      val secret = ByteArray(32) { it.toByte() }

      test("issuer + verifier roundtrip recovers userId and issuedAt") {
        val fixed = Clock.fixed(Instant.ofEpochSecond(1_700_000_000), ZoneOffset.UTC)
        val issuer = SessionTokenIssuer(secret, fixed)
        val token = issuer.issue(userId = 42L)
        val verified = SessionTokenVerifier(secret, clock = fixed).verify(token.bytes)
        verified.shouldNotBeNull()
        verified.userId shouldBe 42L
        verified.issuedAt shouldBe Instant.ofEpochSecond(1_700_000_000)
      }

      test("tampered bytes fail verification") {
        val token = SessionTokenIssuer(secret).issue(userId = 1L)
        val mutated = token.bytes.copyOf()
        mutated[0] = (mutated[0] + 1).toByte()
        SessionTokenVerifier(secret).verify(mutated) shouldBe null
      }

      test("different secret fails verification") {
        val token = SessionTokenIssuer(secret).issue(userId = 1L)
        val other = ByteArray(32) { (it + 1).toByte() }
        SessionTokenVerifier(other).verify(token.bytes) shouldBe null
      }

      test("wrong length fails verification fast") {
        val size = SessionTokenIssuer(secret).issue(1L).bytes.size
        SessionTokenVerifier(secret).verify(ByteArray(0)) shouldBe null
        SessionTokenVerifier(secret).verify(ByteArray(size - 1)) shouldBe null
        SessionTokenVerifier(secret).verify(ByteArray(size + 1)) shouldBe null
      }

      /**
       * The game server holds no user table, so this ticket is the only thing that tells it what an
       * account may do. The roles have to survive the trip and be as forgeable as the id beside
       * them, which is to say not at all.
       */
      test("the roles the login server signed are the roles that come back") {
        val roles = AccountRoles.of(AccountRole.DEVELOPER)

        val token = SessionTokenIssuer(secret).issue(userId = 9L, roles = roles)
        val verified = SessionTokenVerifier(secret).verify(token.bytes)

        verified.shouldNotBeNull()
        verified.roles shouldBe roles
        verified.userId shouldBe 9L
      }

      test("a token issued without roles carries none") {
        val verified =
            SessionTokenVerifier(secret).verify(SessionTokenIssuer(secret).issue(9L).bytes)

        verified.shouldNotBeNull()
        verified.roles shouldBe AccountRoles.NONE
      }

      test("raising the role bits by hand breaks the token") {
        val token = SessionTokenIssuer(secret).issue(userId = 9L, roles = AccountRoles.NONE)
        val forged = token.bytes.copyOf()
        // The role bits are the last four of the signed prefix.
        forged[19] = (forged[19].toInt() or AccountRole.DEVELOPER.bit).toByte()

        SessionTokenVerifier(secret).verify(forged) shouldBe null
      }

      test("bits no role uses do not survive a token") {
        val token = SessionTokenIssuer(secret).issue(userId = 9L, roles = AccountRoles(-1))

        val verified = SessionTokenVerifier(secret).verify(token.bytes)

        verified.shouldNotBeNull()
        verified.roles shouldBe
            AccountRoles.of(AccountRole.MODERATOR, AccountRole.ADMIN, AccountRole.DEVELOPER)
      }

      test("empty secret rejected") {
        shouldThrow<IllegalArgumentException> { SessionTokenIssuer(ByteArray(0)) }
        shouldThrow<IllegalArgumentException> { SessionTokenVerifier(ByteArray(0)) }
      }

      test("token is accepted right up to its max age") {
        val issuedAt = Instant.ofEpochSecond(1_700_000_000)
        val token = SessionTokenIssuer(secret, Clock.fixed(issuedAt, ZoneOffset.UTC)).issue(7L)
        val atLimit = Clock.fixed(issuedAt.plus(Duration.ofMinutes(5)), ZoneOffset.UTC)

        val verified =
            SessionTokenVerifier(secret, Duration.ofMinutes(5), atLimit).verify(token.bytes)

        verified.shouldNotBeNull()
        verified.userId shouldBe 7L
      }

      test("token past its max age is rejected") {
        val issuedAt = Instant.ofEpochSecond(1_700_000_000)
        val token = SessionTokenIssuer(secret, Clock.fixed(issuedAt, ZoneOffset.UTC)).issue(7L)
        val tooLate = Clock.fixed(issuedAt.plusSeconds(301), ZoneOffset.UTC)

        SessionTokenVerifier(secret, Duration.ofMinutes(5), tooLate).verify(token.bytes) shouldBe
            null
      }

      test("max age is configurable independently of the default") {
        val issuedAt = Instant.ofEpochSecond(1_700_000_000)
        val token = SessionTokenIssuer(secret, Clock.fixed(issuedAt, ZoneOffset.UTC)).issue(7L)
        val later = Clock.fixed(issuedAt.plusSeconds(60), ZoneOffset.UTC)

        SessionTokenVerifier(secret, Duration.ofSeconds(30), later).verify(token.bytes) shouldBe
            null
        SessionTokenVerifier(secret, Duration.ofHours(1), later)
            .verify(token.bytes)
            .shouldNotBeNull()
      }

      test("token dated beyond the skew leeway is rejected") {
        val now = Instant.ofEpochSecond(1_700_000_000)
        val future = Clock.fixed(now.plusSeconds(120), ZoneOffset.UTC)
        val token = SessionTokenIssuer(secret, future).issue(7L)

        SessionTokenVerifier(secret, Duration.ofMinutes(5), Clock.fixed(now, ZoneOffset.UTC))
            .verify(token.bytes) shouldBe null
      }

      test("token inside the skew leeway is accepted") {
        val now = Instant.ofEpochSecond(1_700_000_000)
        val slightlyAhead = Clock.fixed(now.plusSeconds(10), ZoneOffset.UTC)
        val token = SessionTokenIssuer(secret, slightlyAhead).issue(7L)

        SessionTokenVerifier(secret, Duration.ofMinutes(5), Clock.fixed(now, ZoneOffset.UTC))
            .verify(token.bytes)
            .shouldNotBeNull()
      }

      test("an unauthentic token with an out of range timestamp is rejected, not thrown") {
        val bytes = ByteArray(SessionTokenIssuer(secret).issue(1L).bytes.size)
        java.nio.ByteBuffer.wrap(bytes).putLong(1L).putLong(Long.MAX_VALUE)

        SessionTokenVerifier(secret).verify(bytes) shouldBe null
      }

      test("the skew leeway is thirty seconds") {
        val now = Instant.ofEpochSecond(1_700_000_000)
        val verifier =
            SessionTokenVerifier(secret, Duration.ofMinutes(5), Clock.fixed(now, ZoneOffset.UTC))
        fun issuedAhead(seconds: Long) =
            SessionTokenIssuer(secret, Clock.fixed(now.plusSeconds(seconds), ZoneOffset.UTC))
                .issue(7L)
                .bytes

        verifier.verify(issuedAhead(30)).shouldNotBeNull()
        verifier.verify(issuedAhead(31)) shouldBe null
      }

      test("non-positive max age rejected") {
        shouldThrow<IllegalArgumentException> { SessionTokenVerifier(secret, Duration.ZERO) }
        shouldThrow<IllegalArgumentException> {
          SessionTokenVerifier(secret, Duration.ofMinutes(-1))
        }
      }
    })
