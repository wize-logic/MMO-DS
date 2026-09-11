package de.fiereu.openmmo.server.game.offline.verify

import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.PokemonStat
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.server.game.offline.OfflineMonster
import de.fiereu.openmmo.server.game.offline.OfflineMove
import de.fiereu.openmmo.server.game.offline.OfflineSaveWire
import de.fiereu.openmmo.server.game.offline.WirePosition
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.ExportVerdict
import de.fiereu.openmmo.server.game.storage.ImportRecord
import de.fiereu.openmmo.server.game.storage.InMemoryChainRepository
import de.fiereu.openmmo.server.game.storage.InMemoryExportRepository
import de.fiereu.openmmo.server.game.storage.InMemoryImportRepository
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.nulls.shouldBeNull
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain
import io.kotest.matchers.types.shouldBeInstanceOf
import java.security.MessageDigest
import java.time.LocalDateTime
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.test.runTest

/**
 * The offline copy a session sends as it leaves: kept, booted, compared, and then either the anchor
 * a replay starts from or nothing at all.
 */
class ExportAnchorServiceTest :
    FunSpec({
      val now = LocalDateTime.of(2026, 9, 4, 18, 0, 0)

      fun sha(bytes: ByteArray): String =
          MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

      fun pokemon(
          seed: Int,
          slot: Short = 0,
          container: PokemonContainer = PokemonContainer.PARTY
      ) =
          Pokemon(
              id = EntityIdService().newMonsterId(),
              ownerId = 0,
              container = container,
              containerSlot = slot,
              dexId = 387,
              seed = seed,
              ot = "Probe",
              nickname = "Turty",
              level = 12,
              hp = 30,
              xp = 1728,
              eVs = EVs().also { it.hp = 4 },
              iVs = IVs().also { it.atk = 31 },
              moves = listOf(PokemonMove(33, 35), PokemonMove(45, 40), PokemonMove(0, 0)),
              isShiny = false,
              hasHiddenAbility = false,
              isAlpha = false,
              isSecret = false,
              isFatefulEncounter = false,
              isRaidEncounter = false,
              caughtAt = now,
              heldItemId = 0,
          )

      /** The report the game's own reader would write of a copy holding [p], seated by the game. */
      fun offline(p: Pokemon) =
          OfflineMonster(
              pid = p.seed,
              dexId = p.dexId,
              form = p.form,
              level = p.level.toInt(),
              xp = p.xp,
              ivs = PokemonStat.entries.associateWith { (p.iVs[it] ?: 0).toInt() },
              evs = PokemonStat.entries.associateWith { (p.eVs[it] ?: 0).toInt() },
              moves =
                  p.moves.filter { it.id.toInt() != 0 }.map { OfflineMove(it.id.toInt(), 30, 0) },
              nickname = "another name entirely",
              otName = p.ot,
              otId = 7,
              abilityId = 0,
              hasHiddenAbility = false,
              natureByte = 0,
              isShiny = false,
              heldItemId = p.heldItemId,
              friendship = 200,
              isEgg = p.isEgg,
              eggCyclesLeft = 0,
              container = p.container,
              containerSlot = p.containerSlot.toInt(),
          )

      fun report(saveSha: String, money: Int, monsters: List<OfflineMonster>): ByteArray =
          OfflineSaveWire.encode(
              OfflineSaveWire(
                  trainerId = 7,
                  money = money,
                  badges = 0,
                  playTimeSeconds = 60,
                  position = WirePosition(1, 86, 4, 4),
                  blackOutWarpId = 0,
                  monsters = monsters,
                  bag = emptyList(),
                  dexSeen = emptySet(),
                  dexCaught = emptySet(),
                  flagIds = emptyList(),
                  varIds = emptyList(),
                  blocks = emptyMap(),
                  saveSha256 = saveSha,
                  clientRevision = 12,
              ))

      /**
       * A copy is just bytes here; what it "holds" is whatever [reportOf] says the boot reports.
       */
      class Rig(
          scope: kotlinx.coroutines.CoroutineScope,
          revisions: Set<Int> = setOf(12),
          val reportOf: (ByteArray) -> ByteArray? = { null },
          val breaks: Boolean = false,
      ) {
        val exports = InMemoryExportRepository()
        val chains = InMemoryChainRepository()
        val imports = InMemoryImportRepository()
        val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
        val booted = mutableListOf<ByteArray?>()
        val pending = mutableListOf<suspend () -> Unit>()
        val runner =
            object : ReplayRunner {
              override fun revisions(): Set<Int> = revisions

              override fun run(run: ReplayRun): ReplayOutcome {
                booted += run.bootImage
                if (breaks) return ReplayOutcome.Broke("the game exited 139")
                val image = run.bootImage ?: ByteArray(0)
                return ReplayOutcome.Quit(image, sha(image), reportOf(image))
              }
            }
        val service =
            ExportAnchorService(
                exports,
                store,
                runner,
                EntityIdService(),
                scope,
                kotlinx.coroutines.Dispatchers.Unconfined,
                clock = { now },
                background = { pending += it })

        /** Run the checks the session put behind it, in order. */
        suspend fun drain() {
          while (pending.isNotEmpty()) pending.removeAt(0)()
        }
      }

      val copy = ByteArray(1024) { it.toByte() }

      test("a copy that holds the character becomes its anchor") {
        runTest {
          val mon = pokemon(0x1234_5678)
          val rig =
              Rig(backgroundScope, reportOf = { report(sha(it), 30_000, listOf(offline(mon))) })
          val who = rig.store.createCharacter(1, "Anchored", CharacterGender.MALE, Region.SINNOH)
          checkNotNull(rig.store.addPokemon(who.info.id, mon))

          val kept = rig.service.keep(who.info.id, copy).shouldBeInstanceOf<KeepOutcome.Kept>()
          checkNotNull(rig.exports.find(kept.exportId)).verdict shouldBe ExportVerdict.PENDING
          // Not an anchor until the boot has agreed.
          rig.service.anchorOf(who.info.id).shouldBeNull()

          rig.drain()
          rig.booted.single()!!.contentEquals(copy) shouldBe true
          checkNotNull(rig.exports.find(kept.exportId)).verdict shouldBe ExportVerdict.CHECKED
          rig.service.anchorOf(who.info.id) shouldBe sha(copy)
        }
      }

      test(
          "a copy without the egg or the species past the engine's tables still holds the character") {
            runTest {
              val mon = pokemon(0x1234_5678)
              val egg = pokemon(0x0E66_0E66, slot = 1).copy(isEgg = true)
              val past =
                  pokemon(0x0595_0595, slot = 2)
                      .copy(dexId = ExportAnchorService.ENGINE_SPECIES + 2)
              val rig =
                  Rig(backgroundScope, reportOf = { report(sha(it), 30_000, listOf(offline(mon))) })
              val who =
                  rig.store.createCharacter(1, "Hatching", CharacterGender.MALE, Region.SINNOH)
              checkNotNull(rig.store.addPokemon(who.info.id, mon))
              checkNotNull(rig.store.addPokemon(who.info.id, egg))
              checkNotNull(rig.store.addPokemon(who.info.id, past))

              // The field seat gives the engine neither, so the file the copy is never held them.
              val kept = rig.service.keep(who.info.id, copy).shouldBeInstanceOf<KeepOutcome.Kept>()
              rig.drain()
              checkNotNull(rig.exports.find(kept.exportId)).verdict shouldBe ExportVerdict.CHECKED
            }
          }

      test("a copy with more money than the character is no anchor") {
        runTest {
          val mon = pokemon(0x1234_5678)
          val rig =
              Rig(backgroundScope, reportOf = { report(sha(it), 30_001, listOf(offline(mon))) })
          val who = rig.store.createCharacter(1, "Richer", CharacterGender.MALE, Region.SINNOH)
          checkNotNull(rig.store.addPokemon(who.info.id, mon))

          val kept = rig.service.keep(who.info.id, copy).shouldBeInstanceOf<KeepOutcome.Kept>()
          rig.drain()
          val row = checkNotNull(rig.exports.find(kept.exportId))
          row.verdict shouldBe ExportVerdict.MISMATCH
          checkNotNull(row.verdictReason) shouldContain "money"
          rig.service.anchorOf(who.info.id).shouldBeNull()
        }
      }

      test("a copy holding a monster the character does not is no anchor") {
        runTest {
          val mon = pokemon(0x1234_5678)
          val extra = pokemon(0x0BAD_F00D, slot = 1)
          val rig =
              Rig(
                  backgroundScope,
                  reportOf = { report(sha(it), 30_000, listOf(offline(mon), offline(extra))) })
          val who = rig.store.createCharacter(1, "Padded", CharacterGender.MALE, Region.SINNOH)
          checkNotNull(rig.store.addPokemon(who.info.id, mon))

          val kept = rig.service.keep(who.info.id, copy).shouldBeInstanceOf<KeepOutcome.Kept>()
          rig.drain()
          val row = checkNotNull(rig.exports.find(kept.exportId))
          row.verdict shouldBe ExportVerdict.MISMATCH
          checkNotNull(row.verdictReason) shouldContain "1 in the copy are not here"
        }
      }

      test("a copy whose monster was retouched is no anchor, and one renamed is") {
        runTest {
          val mon = pokemon(0x1234_5678)
          var iv = 31
          val rig =
              Rig(
                  backgroundScope,
                  reportOf = {
                    val touched =
                        offline(mon).let { m -> m.copy(ivs = m.ivs + (PokemonStat.ATTACK to iv)) }
                    report(sha(it), 30_000, listOf(touched))
                  })
          val who = rig.store.createCharacter(1, "Touched", CharacterGender.MALE, Region.SINNOH)
          checkNotNull(rig.store.addPokemon(who.info.id, mon))

          // The nickname, pp and friendship in the report already differ from the record (see
          // `offline`), and that is an honest export. An attack iv that is not the record's is not.
          val same = rig.service.keep(who.info.id, copy).shouldBeInstanceOf<KeepOutcome.Kept>()
          rig.drain()
          checkNotNull(rig.exports.find(same.exportId)).verdict shouldBe ExportVerdict.CHECKED

          iv = 30
          val other = ByteArray(1024) { (it + 1).toByte() }
          val edited = rig.service.keep(who.info.id, other).shouldBeInstanceOf<KeepOutcome.Kept>()
          rig.drain()
          checkNotNull(rig.exports.find(edited.exportId)).verdict shouldBe ExportVerdict.MISMATCH
          // The older, checked copy is still the anchor: a bad copy does not unseat a good one.
          rig.service.anchorOf(who.info.id) shouldBe sha(copy)
        }
      }

      test("a report of some other file is no anchor") {
        runTest {
          val mon = pokemon(0x1234_5678)
          val rig =
              Rig(
                  backgroundScope,
                  reportOf = { report("f".repeat(64), 30_000, listOf(offline(mon))) })
          val who = rig.store.createCharacter(1, "Swapped", CharacterGender.MALE, Region.SINNOH)
          checkNotNull(rig.store.addPokemon(who.info.id, mon))

          val kept = rig.service.keep(who.info.id, copy).shouldBeInstanceOf<KeepOutcome.Kept>()
          rig.drain()
          val row = checkNotNull(rig.exports.find(kept.exportId))
          row.verdict shouldBe ExportVerdict.MISMATCH
          checkNotNull(row.verdictReason) shouldContain "not the copy"
        }
      }

      test(
          "a boot that falls over leaves the copy unchecked, and nothing is said about the player") {
            runTest {
              val rig = Rig(backgroundScope, breaks = true)
              val who = rig.store.createCharacter(1, "Unlucky", CharacterGender.MALE, Region.SINNOH)
              val kept = rig.service.keep(who.info.id, copy).shouldBeInstanceOf<KeepOutcome.Kept>()
              rig.drain()
              checkNotNull(rig.exports.find(kept.exportId)).verdict shouldBe ExportVerdict.UNCHECKED
              rig.service.anchorOf(who.info.id).shouldBeNull()
            }
          }

      test("a server with no worker keeps nothing and says so") {
        runTest {
          val rig = Rig(backgroundScope, revisions = emptySet())
          val who = rig.store.createCharacter(1, "Unkept", CharacterGender.MALE, Region.SINNOH)
          val out = rig.service.keep(who.info.id, copy).shouldBeInstanceOf<KeepOutcome.Declined>()
          out.why shouldContain "not checking"
          rig.exports.listFor(who.info.id, 10) shouldBe emptyList()
          rig.pending shouldBe emptyList()
        }
      }

      test("a copy whose monster differs from the character's in one field names the field") {
        runTest {
          val mon = pokemon(0x1234_5678)
          val rig =
              Rig(
                  backgroundScope,
                  reportOf = { report(sha(it), 30_000, listOf(offline(mon).copy(xp = 9_999))) })
          val who = rig.store.createCharacter(1, "Edited", CharacterGender.MALE, Region.SINNOH)
          checkNotNull(rig.store.addPokemon(who.info.id, mon))

          val kept = rig.service.keep(who.info.id, copy).shouldBeInstanceOf<KeepOutcome.Kept>()
          rig.drain()

          val row = checkNotNull(rig.exports.find(kept.exportId))
          row.verdict shouldBe ExportVerdict.MISMATCH
          checkNotNull(row.verdictReason) shouldContain "xp 1728 here, 9999 in the copy"
        }
      }

      test("a character keeps its newest few copies, and every one it kept still anchors") {
        runTest {
          val rig = Rig(backgroundScope, reportOf = { report(sha(it), 30_000, emptyList()) })
          val who = rig.store.createCharacter(1, "Prolific", CharacterGender.MALE, Region.SINNOH)
          val copies =
              (0 until ExportAnchorService.KEEP + 2).map { n ->
                ByteArray(64) { (it + n).toByte() }
              }
          for (c in copies) {
            rig.service.keep(who.info.id, c).shouldBeInstanceOf<KeepOutcome.Kept>()
            rig.drain()
          }
          val kept = rig.exports.listFor(who.info.id, 64)
          kept.size shouldBe ExportAnchorService.KEEP
          rig.service.anchorOf(who.info.id) shouldBe sha(copies.last())
          // Not only the newest: every copy still here anchors. A saved game carried to
          // another machine is played from the copy it left on and comes home naming that one,
          // by which time this character may have carried another out from here.
          rig.service.anchorsOf(who.info.id) shouldBe kept.map { it.sha256 }.toSet()
          rig.service.anchorsOf(who.info.id).size shouldBe ExportAnchorService.KEEP
          rig.service.anchorsOf(who.info.id).contains(sha(copies.last())) shouldBe true
        }
      }

      test("the first session of a chain is replayed from the checked copy, not from a New Game") {
        runTest {
          val rig = Rig(backgroundScope, reportOf = { report(sha(it), 30_000, emptyList()) })
          val who = rig.store.createCharacter(1, "Chained", CharacterGender.MALE, Region.SINNOH)
          rig.service.keep(who.info.id, copy).shouldBeInstanceOf<KeepOutcome.Kept>()
          rig.drain()
          rig.booted.clear()

          rig.imports.record(
              ImportRecord(
                  id = 77L,
                  characterId = who.info.id,
                  importedAt = now,
                  playTimeSeconds = 100,
                  saveSha256 = "a".repeat(64),
                  clientRevision = 12,
                  trainerId = 7,
                  partyCount = 0,
                  boxCount = 0,
                  speciesCount = 0,
                  levelTotal = 0,
                  levelMax = 0,
                  moneyBefore = 0,
                  moneyAfter = 0,
                  badgesBefore = 0,
                  badgesAfter = 0,
                  verdicts = emptyList(),
                  snapshotVersion = 1),
              ByteArray(0))
          val verification =
              ReplayVerificationService(
                  rig.chains,
                  rig.imports,
                  rig.store,
                  rig.runner,
                  ReplayLimits(),
                  EntityIdService(),
                  rig.exports,
                  Dispatchers.Unconfined,
                  clock = { now })

          val recording = "0 keys none\n120 keys A\n".toByteArray()
          // The fake runner "replays" a session by handing back the image it booted from, so the
          // one honest quit hash for a session booted from the copy is the copy's own.
          val link =
              (SessionLinkFormat.parse(
                      "version 1\nrevision 12\nrtc 2026-09-04 12:00:00\nboot-sha256 ${sha(copy)}\n" +
                          "recording sessions/20260904-120000.inp\nrecording-sha256 " +
                          "${sha(recording)}\nquit-sha256 ${sha(copy)}\nend-frame 600\n")
                      as LinkReading.Read)
                  .link
          val chain = SessionChain(sha(copy), listOf(link), listOf(recording))

          verification
              .offer(ChainOffer(who.info.id, 77L, chain, sha(copy)))
              .shouldBeInstanceOf<OfferOutcome.Kept>()
          // Kept is not run: the ask is. Everything, here.
          verification.request(who.info.id, null).shouldBeInstanceOf<RequestOutcome.Queued>()
          verification.runNext() shouldBe true

          // The replay's one boot, from the copy and nothing else.
          rig.booted.single()!!.contentEquals(copy) shouldBe true
          checkNotNull(rig.imports.find(77L)).replayVerdict shouldBe "VERIFIED"
        }
      }

      test("a chain claiming a copy this server did not check is refused") {
        runTest {
          val rig = Rig(backgroundScope, breaks = true)
          val who = rig.store.createCharacter(1, "Claimant", CharacterGender.MALE, Region.SINNOH)
          rig.service.keep(who.info.id, copy).shouldBeInstanceOf<KeepOutcome.Kept>()
          rig.drain()
          val verification =
              ReplayVerificationService(
                  rig.chains,
                  rig.imports,
                  rig.store,
                  rig.runner,
                  ReplayLimits(),
                  EntityIdService(),
                  rig.exports,
                  Dispatchers.Unconfined,
                  clock = { now })
          val recording = "0 keys none\n".toByteArray()
          val link =
              (SessionLinkFormat.parse(
                      "version 1\nrevision 12\nrtc 2026-09-04 12:00:00\nboot-sha256 ${sha(copy)}\n" +
                          "recording sessions/20260904-120000.inp\nrecording-sha256 " +
                          "${sha(recording)}\nquit-sha256 ${sha(copy)}\nend-frame 600\n")
                      as LinkReading.Read)
                  .link
          val out =
              verification.offer(
                  ChainOffer(
                      who.info.id,
                      1L,
                      SessionChain(sha(copy), listOf(link), listOf(recording)),
                      sha(copy)))
          out.shouldBeInstanceOf<OfferOutcome.Unverifiable>().why shouldContain "did not write"
        }
      }
    })
