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
import de.fiereu.openmmo.server.game.storage.Containers
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.ImportRecord
import de.fiereu.openmmo.server.game.storage.InMemoryChainRepository
import de.fiereu.openmmo.server.game.storage.InMemoryImportRepository
import de.fiereu.openmmo.server.game.storage.InMemoryRequestRepository
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.nulls.shouldBeNull
import io.kotest.matchers.shouldBe
import io.kotest.matchers.shouldNotBe
import io.kotest.matchers.string.shouldContain
import io.kotest.matchers.types.shouldBeInstanceOf
import java.security.MessageDigest
import java.time.LocalDateTime
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.test.runTest

/** Replaying the play behind an import, and what it costs when the answer is no. */
class ReplayVerificationTest :
    FunSpec({
      fun sha(bytes: ByteArray): String =
          MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

      val now = LocalDateTime.of(2026, 9, 4, 18, 0, 0)

      /** What the launcher writes, in the order it writes it. */
      fun record(
          rtc: String = "2026-09-04 11:30:15",
          revision: String = "12",
          boot: String = "none",
          quit: String = "b".repeat(64),
          recordingHash: String = "c".repeat(64),
          endFrame: String? = "27168",
      ) = buildString {
        append("version 1\n")
        append("revision $revision\n")
        append("rtc $rtc\n")
        append("boot-sha256 $boot\n")
        append("recording sessions/20260904-113015.inp\n")
        append("recording-sha256 $recordingHash\n")
        append("quit-sha256 $quit\n")
        if (endFrame != null) append("end-frame $endFrame\n")
      }

      fun readOne(text: String): SessionLink =
          (SessionLinkFormat.parse(text) as LinkReading.Read).link

      /** A chain of [quits] sessions, each one booting from the last one's quit. */
      fun chain(
          quits: List<String>,
          anchor: String? = null,
          endFrames: List<Long?> = quits.map { 1_000L },
          scripts: List<String> = quits.map { "0 keys none\n900 keys A\n906 keys none\n" },
      ): SessionChain {
        val recordings = scripts.map { it.toByteArray() }
        val links =
            quits.indices.map { i ->
              readOne(
                  record(
                      rtc = "2026-09-0${i + 1} 10:00:00",
                      boot = if (i == 0) (anchor ?: "none") else quits[i - 1],
                      quit = quits[i],
                      recordingHash = sha(recordings[i]),
                      endFrame = endFrames[i]?.toString(),
                  ))
            }
        return SessionChain(anchor, links, recordings)
      }

      fun state(anchor: String? = null, revisions: Set<Int> = setOf(12)) =
          VerifierState(setOfNotNull(anchor), revisions)

      fun states(vararg anchors: String, revisions: Set<Int> = setOf(12)) =
          VerifierState(anchors.toSet(), revisions)

      fun ask(
          spent: Long = 0,
          free: Long = ReplayLimits().freeFrames,
          inFlight: Int = 0,
          depth: Int = 0,
      ) = RequestState(spent, free, inFlight, depth)

      context("the session record") {
        test("reads back every line the front door writes") {
          val link = readOne(record())
          link.version shouldBe 1
          link.revision shouldBe 12
          link.rtc shouldBe LocalDateTime.of(2026, 9, 4, 11, 30, 15)
          link.bootSha256 shouldBe null
          link.quitSha256 shouldBe "b".repeat(64)
          link.endFrame shouldBe 27168L
        }

        test("a session that crashed has no end frame, which is an answer and not a fault") {
          readOne(record(endFrame = null)).endFrame shouldBe null
        }

        test("an install that cannot name its build says so rather than claiming build zero") {
          readOne(record(revision = "unknown")).revision shouldBe null
        }

        test("a version this server does not know is unreadable, never a refusal of the player") {
          val reading = SessionLinkFormat.parse(record().replace("version 1", "version 9"))
          reading.shouldBeInstanceOf<LinkReading.Unreadable>()
          reading.why shouldContain "version 9"
        }

        test("an end frame of zero is not a frame") {
          // PC_FRAMES=0 is the port's own "no limit", so a zero taken at face value would be an
          // unbounded run rather than an empty one. It is refused at the door instead.
          SessionLinkFormat.parse(record(endFrame = "0"))
              .shouldBeInstanceOf<LinkReading.Unreadable>()
        }
      }

      context("a recording, read the way the game reads it") {
        val cap = ReplayLimits().recordingBytes

        test("the three verbs and a chord") {
          val reading =
              ReplayScript.read(
                  "# recorded by PC_RECORD_INPUT\n0 keys none\n10 keys A B\n20 touch 128 96\n30 release\n"
                      .toByteArray(),
                  cap)
          reading.shouldBeInstanceOf<ScriptReading.Ok>()
          reading.events shouldBe 4
          reading.lastFrame shouldBe 30L
        }

        test("frames that go backwards are refused, the way the port refuses them") {
          val reading = ReplayScript.read("10 keys A\n5 keys none\n".toByteArray(), cap)
          reading.shouldBeInstanceOf<ScriptReading.Refused>()
          reading.why shouldContain "back in time"
        }

        test("a button the keypad does not have") {
          ReplayScript.read("0 keys TURBO\n".toByteArray(), cap)
              .shouldBeInstanceOf<ScriptReading.Refused>()
        }

        test("a verb the port has never had") {
          ReplayScript.read("0 sleep 400\n".toByteArray(), cap)
              .shouldBeInstanceOf<ScriptReading.Refused>()
        }

        test("a line longer than the game's own buffer, which it would split in two") {
          val long = "0 keys" + " A".repeat(200) + "\n"
          val reading = ReplayScript.read(long.toByteArray(), cap)
          reading.shouldBeInstanceOf<ScriptReading.Refused>()
          reading.why shouldContain "past the game's own"
        }

        test("a frame with a sign in front of it, which C's own conversion would wrap") {
          ReplayScript.read("-5 keys none\n".toByteArray(), cap)
              .shouldBeInstanceOf<ScriptReading.Refused>()
        }

        test("the cap is the file's, because the port has none of its own") {
          val reading = ReplayScript.read("0 keys none\n".toByteArray(), 4)
          reading.shouldBeInstanceOf<ScriptReading.Refused>()
          reading.why shouldContain "over the 4"
        }
      }

      context("what is refused before a single frame is replayed") {
        val limits = ReplayLimits()
        val save = "1".repeat(64)

        test("a chain that ends on a different save than the one that was imported") {
          val c = chain(listOf("b".repeat(64)))
          val answer = ReplayAdmission.check(c, save, state(), limits, now)
          answer.shouldBeInstanceOf<Admission.Unverifiable>()
          answer.why shouldContain "not the file that was imported"
        }

        test("a session that did not start from the one in front of it") {
          val c = chain(listOf("b".repeat(64), save))
          val torn =
              c.copy(
                  links =
                      c.links.mapIndexed { i, l ->
                        if (i == 1) l.copy(bootSha256 = "d".repeat(64)) else l
                      })
          val answer = ReplayAdmission.check(torn, save, state(), limits, now)
          answer.shouldBeInstanceOf<Admission.Unverifiable>()
          answer.why shouldContain "did not start from the save"
        }

        test("a clock that does not move forward, so one seed cannot be replayed link after link") {
          val c = chain(listOf("b".repeat(64), save))
          val frozen = c.copy(links = c.links.map { it.copy(rtc = c.links.first().rtc) })
          val answer = ReplayAdmission.check(frozen, save, state(), limits, now)
          answer.shouldBeInstanceOf<Admission.Unverifiable>()
          answer.why shouldContain "by its own clock"
        }

        test("a session played later than this server's own clock") {
          val c = chain(listOf(save))
          val ahead = c.copy(links = c.links.map { it.copy(rtc = now.plusDays(1)) })
          ReplayAdmission.check(ahead, save, state(), limits, now)
              .shouldBeInstanceOf<Admission.Unverifiable>()
        }

        test("a build this server has no binary for") {
          val answer =
              ReplayAdmission.check(
                  chain(listOf(save)), save, state(revisions = setOf(9)), limits, now)
          answer.shouldBeInstanceOf<Admission.Unverifiable>()
          answer.why shouldContain "cannot run"
        }

        test("an end frame that claims the session stopped before its last button") {
          val c = chain(listOf(save), endFrames = listOf(10L))
          val answer = ReplayAdmission.check(c, save, state(), limits, now)
          answer.shouldBeInstanceOf<Admission.Unverifiable>()
          answer.why shouldContain "before its last button"
        }

        test("one line claiming a trillion frames is refused by the cap, not run for fifty years") {
          val c =
              chain(
                  listOf(save),
                  endFrames = listOf(1_000_000_000_000L),
                  scripts = listOf("1000000000000 keys none\n"))
          val answer = ReplayAdmission.check(c, save, state(), limits, now)
          answer.shouldBeInstanceOf<Admission.Unverifiable>()
          answer.why shouldContain "over the ${limits.linkFrames}"
        }

        test("a recording that is not the one the record wrote down") {
          val c = chain(listOf(save))
          val swapped = c.copy(recordings = listOf("0 keys A\n".toByteArray()))
          val answer = ReplayAdmission.check(swapped, save, state(), limits, now)
          answer.shouldBeInstanceOf<Admission.Unverifiable>()
          answer.why shouldContain "not the one it wrote down"
        }

        test("a chain that starts from a save this server never handed out") {
          val c = chain(listOf(save), anchor = "e".repeat(64))
          val answer = ReplayAdmission.check(c, save, state(anchor = null), limits, now)
          answer.shouldBeInstanceOf<Admission.Unverifiable>()
          answer.why shouldContain "anchor"
        }

        test("a chain that says it started from no file, from a character that was handed one") {
          val c = chain(listOf(save))
          val answer = ReplayAdmission.check(c, save, state(anchor = "e".repeat(64)), limits, now)
          answer.shouldBeInstanceOf<Admission.Unverifiable>()
          answer.why shouldContain "started from no saved game"
        }

        /*
         * The one a carried saved game needs. A game taken to another machine is played from
         * the copy it left on, and by the time it comes back this character may well have
         * carried another out here, so the anchor the chain names is honestly an older one.
         */
        test("a chain anchored on an older copy this server still holds is admitted") {
          val older = "e".repeat(64)
          val c = chain(listOf(save), anchor = older)
          val answer = ReplayAdmission.check(c, save, states(older, "f".repeat(64)), limits, now)
          answer.shouldBeInstanceOf<Admission.Kept>()
        }

        test("an honest chain is kept, and its frames are what it would cost to run whole") {
          val answer = ReplayAdmission.check(chain(listOf(save)), save, state(), limits, now)
          answer.shouldBeInstanceOf<Admission.Kept>()
          answer.frames shouldBe 1_000L
        }

        test("a session that crashed is run to its last button plus the margin") {
          val c = chain(listOf(save), endFrames = listOf(null))
          val answer = ReplayAdmission.check(c, save, state(), limits, now)
          answer.shouldBeInstanceOf<Admission.Kept>()
          answer.frames shouldBe 906L + limits.crashMargin
        }

        test(
            "verification switched off keeps nothing and takes no ask, and that is not an outage") {
              val off = ReplayLimits(cores = 0)
              ReplayAdmission.check(chain(listOf(save)), save, state(), off, now)
                  .shouldBeInstanceOf<Admission.Unverifiable>()
              ReplayAdmission.request(1_000, ask(), off)
                  .shouldBeInstanceOf<RequestAdmission.Refused>()
            }

        test("a second ask while one is still waiting") {
          val answer = ReplayAdmission.request(1_000, ask(inFlight = 1), limits)
          answer.shouldBeInstanceOf<RequestAdmission.Refused>()
          answer.why shouldContain "already have a check waiting"
        }

        test("a full queue") {
          ReplayAdmission.request(1_000, ask(depth = limits.queueDepth), limits)
              .shouldBeInstanceOf<RequestAdmission.Refused>()
        }

        test("a week already spent") {
          val answer =
              ReplayAdmission.request(1_000, ask(spent = limits.accountFramesPerWeek), limits)
          answer.shouldBeInstanceOf<RequestAdmission.Refused>()
          answer.why shouldContain "week"
        }

        test("an ask inside the free allowance costs nothing") {
          val answer = ReplayAdmission.request(1_000, ask(), limits)
          answer.shouldBeInstanceOf<RequestAdmission.Queued>()
          answer.frames shouldBe 1_000L
          answer.fee shouldBe 0
        }

        test("past the free allowance the player pays by the hour, rounded up") {
          val answer = ReplayAdmission.request(ReplayLimits.hours(2) + 1, ask(free = 0), limits)
          answer.shouldBeInstanceOf<RequestAdmission.Queued>()
          answer.fee shouldBe 3 * limits.feePerHour
        }
      }

      context("replaying forward from a trusted anchor") {
        val limits = ReplayLimits()
        val save = "1".repeat(64)

        /** Answers with whatever image the map says, and remembers what it was booted from. */
        class Fake(private val quits: Map<Int, ByteArray?>) : ReplayRunner {
          val bootedFrom = mutableListOf<ByteArray?>()
          var runs = 0

          override fun revisions(): Set<Int> = setOf(12)

          override fun run(run: ReplayRun): ReplayOutcome {
            bootedFrom += run.bootImage
            val image = quits[runs++] ?: return ReplayOutcome.Broke("the game exited 139")
            return ReplayOutcome.Quit(image, sha(image))
          }
        }

        test(
            "each session boots from the image the one before it PRODUCED, never from the record") {
              val first = "the first quit".toByteArray()
              val second = "the second quit".toByteArray()
              val c = chain(listOf(sha(first), sha(second)))
              val fake = Fake(mapOf(0 to first, 1 to second))

              val result = ReplayVerifier(fake, limits).verify(c, Frontier(-1, null), 1)

              result.verdict shouldBe ReplayVerdict.VERIFIED
              fake.bootedFrom[0] shouldBe null
              fake.bootedFrom[1]!!.decodeToString() shouldBe "the first quit"
              result.frontier.link shouldBe 1
            }

        test("a save the recorded input does not produce is DIVERGED at the session it is in") {
          val first = "the first quit".toByteArray()
          val c = chain(listOf(sha(first), save))
          val fake = Fake(mapOf(0 to first, 1 to "something else".toByteArray()))

          val result = ReplayVerifier(fake, limits).verify(c, Frontier(-1, null), 1)

          result.verdict shouldBe ReplayVerdict.DIVERGED
          result.divergedLink shouldBe 1
          // The frontier stays at the last session this server believes. State after a session it
          // does not believe is not state anything later may stand on.
          result.frontier.link shouldBe 0
        }

        test("a port that falls over says nothing about the player") {
          val c = chain(listOf(save))
          val result =
              ReplayVerifier(Fake(mapOf(0 to null)), limits).verify(c, Frontier(-1, null), 0)
          result.verdict shouldBe ReplayVerdict.INCONCLUSIVE
        }

        test("a frontier that has already been reached is not replayed again") {
          val second = "the second quit".toByteArray()
          val c = chain(listOf("a".repeat(64), sha(second)))
          val fake = Fake(mapOf(0 to second))

          val result = ReplayVerifier(fake, limits).verify(c, Frontier(0, "held".toByteArray()), 1)

          result.verdict shouldBe ReplayVerdict.VERIFIED
          fake.runs shouldBe 1
          fake.bootedFrom.single()!!.decodeToString() shouldBe "held"
        }
      }

      context("what a verdict costs") {
        test("the fee is kept when the replay answered about the save") {
          refundFor(ReplayVerdict.VERIFIED, 5000) shouldBe 0
          refundFor(ReplayVerdict.DIVERGED, 5000) shouldBe 0
        }

        test("and goes back when it answered about this server") {
          refundFor(ReplayVerdict.INCONCLUSIVE, 5000) shouldBe 5000
          refundFor(ReplayVerdict.UNVERIFIABLE, 5000) shouldBe 5000
        }

        test("a chain that verified without saying what it held answered about this server too") {
          refundFor(ReplayVerdict.VERIFIED, 5000, saidWhatItHeld = false) shouldBe 5000
          refundFor(ReplayVerdict.DIVERGED, 5000, saidWhatItHeld = false) shouldBe 0
        }

        test("only a verified chain takes the mark off anything") {
          ReplayVerdict.entries.filter { clearsTheMark(it) } shouldBe listOf(ReplayVerdict.VERIFIED)
        }
      }

      context("end to end, over the stores") {
        val limits = ReplayLimits(freeHours = 0)
        val save = "1".repeat(64)
        val hour = ReplayLimits.hours(1)

        /**
         * Replays by handing back the image the test says each run ends on, and "boots" an image (a
         * run with nothing pressed) by handing it straight back; either way the report is what
         * [reportOf] says that image holds.
         */
        class Rig(
            scope: kotlinx.coroutines.CoroutineScope,
            quits: Map<Int, ByteArray?>,
            val reportOf: (ByteArray) -> ByteArray? = { null },
        ) {
          val chains = InMemoryChainRepository()
          val imports = InMemoryImportRepository()
          val requests = InMemoryRequestRepository()
          val repository = FakeCharacterRepository()
          val store = CharacterStore(repository, EntityIdService(), scope)
          val runner =
              object : ReplayRunner {
                var runs = 0
                val booted = mutableListOf<ByteArray?>()

                override fun revisions(): Set<Int> = setOf(12)

                override fun run(run: ReplayRun): ReplayOutcome {
                  if (run.recording.isEmpty()) {
                    val image = run.bootImage ?: ByteArray(0)
                    booted += image
                    return ReplayOutcome.Quit(image, sha(image), reportOf(image))
                  }
                  val image = quits[runs++] ?: return ReplayOutcome.Broke("the game exited 139")
                  return ReplayOutcome.Quit(image, sha(image), reportOf(image))
                }
              }
          val service =
              ReplayVerificationService(
                  chains,
                  imports,
                  store,
                  runner,
                  limits,
                  EntityIdService(),
                  blocking = Dispatchers.Unconfined,
                  requests = requests,
                  clock = { now })

          suspend fun character(name: String): Long =
              store.createCharacter(1, name, CharacterGender.MALE, Region.SINNOH).info.id

          suspend fun money(who: Long): Int = checkNotNull(store.getCharacter(who)).info.money

          suspend fun marked(who: Long, seed: Int): Boolean =
              checkNotNull(store.getCharacter(who))
                  .let { it.pokemon + it.pcStorage }
                  .single { it.seed == seed }
                  .offlineOrigin
        }

        suspend fun importRow(rig: Rig, characterId: Long, id: Long) =
            rig.imports.record(
                ImportRecord(
                    id = id,
                    characterId = characterId,
                    importedAt = now,
                    playTimeSeconds = 100,
                    saveSha256 = "a".repeat(64),
                    clientRevision = 12,
                    trainerId = 1,
                    partyCount = 1,
                    boxCount = 0,
                    speciesCount = 1,
                    levelTotal = 5,
                    levelMax = 5,
                    moneyBefore = 0,
                    moneyAfter = 0,
                    badgesBefore = 0,
                    badgesAfter = 0,
                    verdicts = emptyList(),
                    snapshotVersion = 1),
                ByteArray(0))

        /** A monster carrying the offline mark. */
        fun monster(owner: Long, seed: Int, dexId: Int = 387, atk: Int = 31, ot: String = "Probe") =
            Pokemon(
                id = EntityIdService().newMonsterId(),
                ownerId = owner,
                container = PokemonContainer.PARTY,
                containerSlot = 0,
                dexId = dexId,
                seed = seed,
                ot = ot,
                nickname = "Turty",
                level = 5,
                hp = 20,
                xp = 135,
                eVs = EVs(),
                iVs = IVs().also { it.atk = atk },
                moves = listOf(PokemonMove(33, 35)),
                isShiny = false,
                hasHiddenAbility = false,
                isAlpha = false,
                isSecret = false,
                isFatefulEncounter = false,
                isRaidEncounter = false,
                caughtAt = now,
                heldItemId = 0,
                offlineOrigin = true,
            )

        /** The same monster as the game's exit reporter writes it at a quit. */
        fun born(p: Pokemon, dexId: Int = p.dexId) =
            OfflineMonster(
                pid = p.seed,
                dexId = dexId,
                form = 0,
                level = 5,
                xp = 135,
                ivs = PokemonStat.entries.associateWith { (p.iVs[it] ?: 0).toInt() },
                evs = PokemonStat.entries.associateWith { 0 },
                moves = listOf(OfflineMove(33, 30, 0)),
                nickname = "Turty",
                otName = p.ot,
                otId = 7,
                abilityId = 0,
                hasHiddenAbility = false,
                natureByte = 0,
                isShiny = false,
                heldItemId = 0,
                friendship = 70,
                isEgg = false,
                eggCyclesLeft = 0,
                container = PokemonContainer.PARTY,
                containerSlot = 0,
            )

        fun report(image: ByteArray, monsters: List<OfflineMonster>): ByteArray =
            OfflineSaveWire.encode(
                OfflineSaveWire(
                    trainerId = 7,
                    money = 0,
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
                    saveSha256 = sha(image),
                    clientRevision = 12,
                ))

        /** Reports keyed by the image they are of. */
        fun reports(
            vararg pairs: Pair<ByteArray, List<OfflineMonster>>
        ): (ByteArray) -> ByteArray? {
          val byImage =
              pairs.associate { (image, mons) -> image.decodeToString() to report(image, mons) }
          return { image -> byImage[image.decodeToString()] }
        }

        /** An import and the chain of [quits] sessions behind it, each an hour long, kept. */
        suspend fun onFile(rig: Rig, who: Long, importId: Long, quits: List<ByteArray>): Long {
          importRow(rig, who, importId)
          val c = chain(quits.map { sha(it) }, endFrames = quits.map { hour })
          val kept = rig.service.offer(ChainOffer(who, importId, c, sha(quits.last())))
          return kept.shouldBeInstanceOf<OfferOutcome.Kept>().chainId
        }

        test("a chain is kept unpaid at the import, and nothing runs until somebody asks") {
          runTest {
            val quit = "the only quit".toByteArray()
            val rig = Rig(backgroundScope, mapOf(0 to quit))
            val who = rig.character("Kept")
            rig.store.addMoney(who, 100_000)
            val before = rig.money(who)

            val chainId = onFile(rig, who, 4242L, listOf(quit))

            checkNotNull(rig.chains.find(chainId)).verdict shouldBe ReplayVerdict.HELD
            checkNotNull(rig.imports.find(4242L)).replayVerdict shouldBe "HELD"
            rig.service.claimNext().shouldBeNull()
            rig.runner.runs shouldBe 0
            rig.money(who) shouldBe before
          }
        }

        test("asking for everything replays to the end, clears what it held, and costs what ran") {
          runTest {
            val quit = "the only quit".toByteArray()
            val mon = monster(0, 0x1111)
            val rig = Rig(backgroundScope, mapOf(0 to quit), reports(quit to listOf(born(mon))))
            val who = rig.character("Whole")
            rig.store.addPokemon(who, mon)
            rig.store.addMoney(who, 100_000)
            val before = rig.money(who)
            val chainId = onFile(rig, who, 4343L, listOf(quit))

            val asked = rig.service.request(who, null).shouldBeInstanceOf<RequestOutcome.Queued>()
            asked.fee shouldBe limits.feePerHour
            asked.frames shouldBe hour
            checkNotNull(rig.imports.find(4343L)).replayVerdict shouldBe "PENDING"
            rig.service.runNext() shouldBe true

            val done = checkNotNull(rig.requests.find(asked.requestId))
            done.verdict shouldBe ReplayVerdict.VERIFIED
            done.framesRun shouldBe hour
            done.feeRefunded shouldBe 0
            checkNotNull(rig.chains.find(chainId)).verdict shouldBe ReplayVerdict.VERIFIED
            checkNotNull(rig.imports.find(4343L)).replayVerdict shouldBe "VERIFIED"
            rig.marked(who, 0x1111) shouldBe false
            rig.money(who) shouldBe before - limits.feePerHour
          }
        }

        test("a marked monster the play never held keeps its mark when the chain is verified") {
          runTest {
            val quit = "the only quit".toByteArray()
            val mine = monster(0, 0x1111)
            val boarder = monster(0, 0x9999)
            val rig = Rig(backgroundScope, mapOf(0 to quit), reports(quit to listOf(born(mine))))
            val who = rig.character("Boarder")
            rig.store.addPokemon(who, mine)
            // An earlier import's monster, sitting in the day care, which an import replaces the
            // party and the boxes around, so it outlives the play it arrived with.
            rig.store.rearrangeMonsters(who) { party, pc, daycare ->
              Containers(
                  party,
                  pc,
                  daycare + boarder.copy(ownerId = who, container = PokemonContainer.DAYCARE))
            } shouldBe true
            rig.store.addMoney(who, 100_000)
            onFile(rig, who, 4848L, listOf(quit))

            val asked = rig.service.request(who, null).shouldBeInstanceOf<RequestOutcome.Queued>()
            rig.service.runNext() shouldBe true

            checkNotNull(rig.requests.find(asked.requestId)).verdict shouldBe ReplayVerdict.VERIFIED
            rig.marked(who, 0x1111) shouldBe false
            checkNotNull(rig.store.getCharacter(who)).daycare.single().offlineOrigin shouldBe true
          }
        }

        test("a verified chain whose last quit will not read takes no mark off at all") {
          runTest {
            val quit = "the only quit".toByteArray()
            val mon = monster(0, 0x1111)
            // The replay agrees session for session, but the worker cannot say what it ended
            // holding; a mark taken off on that would be taken off on nothing.
            val rig = Rig(backgroundScope, mapOf(0 to quit))
            val who = rig.character("Unread")
            rig.store.addPokemon(who, mon)
            rig.store.addMoney(who, 100_000)
            val before = rig.money(who)
            val chainId = onFile(rig, who, 4949L, listOf(quit))

            val asked = rig.service.request(who, null).shouldBeInstanceOf<RequestOutcome.Queued>()
            asked.fee shouldBe limits.feePerHour
            rig.service.runNext() shouldBe true

            val done = checkNotNull(rig.requests.find(asked.requestId))
            done.verdict shouldBe ReplayVerdict.VERIFIED
            checkNotNull(rig.chains.find(chainId)).verdict shouldBe ReplayVerdict.VERIFIED
            rig.marked(who, 0x1111) shouldBe true
            // Nothing came off, so the hours proved nothing the player can use and the fee is not
            // earned.
            done.feeRefunded shouldBe limits.feePerHour
            rig.money(who) shouldBe before
          }
        }

        test("and the ask is still open, so a worker that can read the play clears it") {
          runTest {
            val quit = "the only quit".toByteArray()
            val mon = monster(0, 0x1111)
            var reads = false
            val rig =
                Rig(backgroundScope, mapOf(0 to quit)) { image ->
                  if (reads) report(image, listOf(born(mon))) else null
                }
            val who = rig.character("Again")
            rig.store.addPokemon(who, mon)
            rig.store.addMoney(who, 100_000)
            onFile(rig, who, 5050L, listOf(quit))
            rig.service.request(who, null).shouldBeInstanceOf<RequestOutcome.Queued>()
            rig.service.runNext() shouldBe true
            rig.marked(who, 0x1111) shouldBe true
            val before = rig.money(who)

            // The chain is checked to its end, so the second ask replays nothing and costs nothing;
            // it only boots the image the frontier stands on and reads it again.
            reads = true
            val again = rig.service.request(who, null).shouldBeInstanceOf<RequestOutcome.Queued>()
            again.frames shouldBe 0
            again.fee shouldBe 0
            rig.service.runNext() shouldBe true

            checkNotNull(rig.requests.find(again.requestId)).verdict shouldBe ReplayVerdict.VERIFIED
            rig.marked(who, 0x1111) shouldBe false
            rig.money(who) shouldBe before
            // And with nothing left wearing the mark there is nothing left to ask.
            rig.service
                .request(who, null)
                .shouldBeInstanceOf<RequestOutcome.Refused>()
                .why shouldContain "checked to its end"
          }
        }

        test("asking for one monster stops at its birth, clears only it, and gives back the rest") {
          runTest {
            val q0 = "quit one".toByteArray()
            val q1 = "quit two".toByteArray()
            val q2 = "quit three".toByteArray()
            val a = monster(0, 0xAAAA)
            val b = monster(0, 0xBBBB)
            val c = monster(0, 0xCCCC)
            val rig =
                Rig(
                    backgroundScope,
                    mapOf(0 to q0, 1 to q1, 2 to q2),
                    reports(
                        q0 to listOf(born(a)),
                        q1 to listOf(born(a), born(b)),
                        q2 to listOf(born(a), born(b), born(c))))
            val who = rig.character("Picky")
            for (mon in listOf(a, b, c)) rig.store.addPokemon(who, mon)
            rig.store.addMoney(who, 100_000)
            val before = rig.money(who)
            val chainId = onFile(rig, who, 4444L, listOf(q0, q1, q2))

            val askedB =
                rig.service.request(who, 0xBBBB).shouldBeInstanceOf<RequestOutcome.Queued>()
            askedB.fee shouldBe 3 * limits.feePerHour
            rig.service.runNext() shouldBe true

            val doneB = checkNotNull(rig.requests.find(askedB.requestId))
            doneB.verdict shouldBe ReplayVerdict.VERIFIED
            doneB.stoppedLink shouldBe 1
            doneB.framesRun shouldBe 2 * hour
            doneB.feeRefunded shouldBe limits.feePerHour
            rig.money(who) shouldBe before - 2 * limits.feePerHour
            rig.runner.runs shouldBe 2
            checkNotNull(rig.chains.find(chainId)).verifiedLink shouldBe 1
            checkNotNull(rig.chains.find(chainId)).verdict shouldBe ReplayVerdict.HELD
            checkNotNull(rig.imports.find(4444L)).replayVerdict shouldBe "HELD"
            rig.marked(who, 0xBBBB) shouldBe false
            rig.marked(who, 0xAAAA) shouldBe true
            rig.marked(who, 0xCCCC) shouldBe true

            // Born before the frontier: read off the image this server already holds, no replay,
            // and the whole budget back.
            val askedA =
                rig.service.request(who, 0xAAAA).shouldBeInstanceOf<RequestOutcome.Queued>()
            askedA.fee shouldBe limits.feePerHour
            rig.service.runNext() shouldBe true
            val doneA = checkNotNull(rig.requests.find(askedA.requestId))
            doneA.verdict shouldBe ReplayVerdict.VERIFIED
            doneA.framesRun shouldBe 0
            doneA.feeRefunded shouldBe limits.feePerHour
            rig.runner.runs shouldBe 2
            rig.runner.booted.single().contentEquals(q1) shouldBe true
            rig.marked(who, 0xAAAA) shouldBe false

            // The last one walks the last session, which finishes the chain.
            rig.service.request(who, 0xCCCC).shouldBeInstanceOf<RequestOutcome.Queued>()
            rig.service.runNext() shouldBe true
            rig.marked(who, 0xCCCC) shouldBe false
            checkNotNull(rig.chains.find(chainId)).verdict shouldBe ReplayVerdict.VERIFIED
            rig.money(who) shouldBe before - 3 * limits.feePerHour
          }
        }

        test("a monster the play never produced is unverifiable, and costs nothing") {
          runTest {
            val quit = "the only quit".toByteArray()
            val a = monster(0, 0xAAAA)
            val z = monster(0, 0x2222)
            val rig = Rig(backgroundScope, mapOf(0 to quit), reports(quit to listOf(born(a))))
            val who = rig.character("Stranger")
            rig.store.addPokemon(who, a)
            rig.store.addPokemon(who, z)
            rig.store.addMoney(who, 100_000)
            val before = rig.money(who)
            val chainId = onFile(rig, who, 4545L, listOf(quit))

            val asked = rig.service.request(who, 0x2222).shouldBeInstanceOf<RequestOutcome.Queued>()
            rig.service.runNext() shouldBe true

            val done = checkNotNull(rig.requests.find(asked.requestId))
            done.verdict shouldBe ReplayVerdict.UNVERIFIABLE
            checkNotNull(done.verdictReason) shouldContain "no such monster"
            rig.money(who) shouldBe before
            rig.marked(who, 0x2222) shouldBe true
            // The play itself agreed to its end, which is a fact about the chain, not the ask.
            checkNotNull(rig.chains.find(chainId)).verdict shouldBe ReplayVerdict.VERIFIED
          }
        }

        test("a monster whose hidden stats are not the ones it was born with is named") {
          runTest {
            val quit = "the only quit".toByteArray()
            val bornWith = monster(0, 0xAAAA, atk = 31)
            val heldNow = monster(0, 0xAAAA, atk = 30)
            val rig =
                Rig(backgroundScope, mapOf(0 to quit), reports(quit to listOf(born(bornWith))))
            val who = rig.character("Edited")
            rig.store.addPokemon(who, heldNow)
            rig.store.addMoney(who, 100_000)
            val before = rig.money(who)
            onFile(rig, who, 4646L, listOf(quit))

            val asked = rig.service.request(who, 0xAAAA).shouldBeInstanceOf<RequestOutcome.Queued>()
            rig.service.runNext() shouldBe true

            val done = checkNotNull(rig.requests.find(asked.requestId))
            done.verdict shouldBe ReplayVerdict.DIVERGED
            checkNotNull(done.verdictReason) shouldContain "hidden stats"
            rig.marked(who, 0xAAAA) shouldBe true
            // Kept: the answer was about the save.
            rig.money(who) shouldBe before - limits.feePerHour
          }
        }

        test("a monster that has evolved since is still the one born") {
          runTest {
            val quit = "the only quit".toByteArray()
            val turtwig = monster(0, 0xAAAA, dexId = 387)
            val grotle = monster(0, 0xAAAA, dexId = 388)
            val rig = Rig(backgroundScope, mapOf(0 to quit), reports(quit to listOf(born(turtwig))))
            val who = rig.character("Grown")
            rig.store.addPokemon(who, grotle)
            rig.store.addMoney(who, 100_000)
            onFile(rig, who, 4747L, listOf(quit))

            val asked = rig.service.request(who, 0xAAAA).shouldBeInstanceOf<RequestOutcome.Queued>()
            rig.service.runNext() shouldBe true

            checkNotNull(rig.requests.find(asked.requestId)).verdict shouldBe ReplayVerdict.VERIFIED
            rig.marked(who, 0xAAAA) shouldBe false
          }
        }

        test("a divergence ends the chain, and later asks are refused with the session named") {
          runTest {
            val q0 = "quit one".toByteArray()
            val q1 = "quit two".toByteArray()
            val rig = Rig(backgroundScope, mapOf(0 to q0, 1 to "something else".toByteArray()))
            val who = rig.character("Torn")
            rig.store.addMoney(who, 100_000)
            val chainId = onFile(rig, who, 4848L, listOf(q0, q1))

            val asked = rig.service.request(who, null).shouldBeInstanceOf<RequestOutcome.Queued>()
            rig.service.runNext() shouldBe true

            checkNotNull(rig.requests.find(asked.requestId)).verdict shouldBe ReplayVerdict.DIVERGED
            val chain = checkNotNull(rig.chains.find(chainId))
            chain.verdict shouldBe ReplayVerdict.DIVERGED
            chain.divergedLink shouldBe 1
            chain.verifiedLink shouldBe 0
            checkNotNull(rig.imports.find(4848L)).replayVerdict shouldBe "DIVERGED"

            val again = rig.service.request(who, null)
            again.shouldBeInstanceOf<RequestOutcome.Refused>().why shouldContain "session 2"
          }
        }

        test("one ask in flight per character, and a port that falls over gives the money back") {
          runTest {
            val rig = Rig(backgroundScope, mapOf(0 to null))
            val who = rig.character("Broken")
            rig.store.addMoney(who, 100_000)
            val before = rig.money(who)
            onFile(rig, who, 4949L, listOf("the only quit".toByteArray()))

            val asked = rig.service.request(who, null).shouldBeInstanceOf<RequestOutcome.Queued>()
            rig.money(who) shouldBe before - asked.fee
            rig.service
                .request(who, null)
                .shouldBeInstanceOf<RequestOutcome.Refused>()
                .why shouldContain "already have a check waiting"

            rig.service.runNext() shouldBe true

            checkNotNull(rig.requests.find(asked.requestId)).verdict shouldBe
                ReplayVerdict.INCONCLUSIVE
            checkNotNull(rig.imports.find(4949L)).replayVerdict shouldBe "HELD"
            rig.money(who) shouldBe before
          }
        }

        test("an ask nobody can pay for takes no money and joins no queue") {
          runTest {
            val rig = Rig(backgroundScope, mapOf(0 to ByteArray(1)))
            val who = rig.character("Broke")
            importRow(rig, who, 5050L)
            // Eight hours of replay at five thousand an hour, against a wallet that starts on
            // thirty: the fee is a rate limit as much as a sink, and this is what it limits.
            val c = chain(listOf("f".repeat(64)), endFrames = listOf(ReplayLimits.hours(8)))
            rig.service
                .offer(ChainOffer(who, 5050L, c, "f".repeat(64)))
                .shouldBeInstanceOf<OfferOutcome.Kept>()

            rig.service.request(who, null).shouldBeInstanceOf<RequestOutcome.CannotAfford>()
            rig.requests.depth() shouldBe 0
          }
        }

        test("an ask a lane holds is neither handed out again nor aged out from under it") {
          runTest {
            val rig = Rig(backgroundScope, emptyMap())
            val ids = listOf(5100L, 5101L)
            for ((i, name) in listOf("Aleph", "Beth").withIndex()) {
              val who = rig.character(name)
              onFile(rig, who, ids[i], listOf("quit $name".toByteArray()))
              rig.service.request(who, null).shouldBeInstanceOf<RequestOutcome.Queued>()
            }
            val first = checkNotNull(rig.service.claimNext())
            val second = checkNotNull(rig.service.claimNext(setOf(first.id)))
            second.id shouldNotBe first.id
            rig.service.claimNext(setOf(first.id, second.id)).shouldBeNull()
            // Both still waiting: a claim is a look, not a verdict.
            rig.requests.depth() shouldBe 2
          }
        }

        test("an ask that has waited past the queue's days answers about the queue") {
          runTest {
            val rig = Rig(backgroundScope, emptyMap())
            var at = now
            val service =
                ReplayVerificationService(
                    rig.chains,
                    rig.imports,
                    rig.store,
                    rig.runner,
                    limits,
                    EntityIdService(),
                    blocking = Dispatchers.Unconfined,
                    requests = rig.requests,
                    clock = { at })
            val who = rig.character("Patient")
            rig.store.addMoney(who, 100_000)
            val before = rig.money(who)
            onFile(rig, who, 5200L, listOf("the only quit".toByteArray()))
            val asked = service.request(who, null).shouldBeInstanceOf<RequestOutcome.Queued>()
            asked.fee shouldBe limits.feePerHour

            at = now.plusDays(limits.queueDays.toLong()).plusMinutes(1)
            service.claimNext().shouldBeNull()

            val aged = checkNotNull(rig.requests.find(asked.requestId))
            aged.verdict shouldBe ReplayVerdict.UNVERIFIABLE
            aged.verdictReason shouldBe "the queue"
            rig.money(who) shouldBe before
            rig.runner.runs shouldBe 0

            // Put back, it waits from now, not from the ask: the next look runs it.
            service.requeue(aged, "Mod").shouldBeInstanceOf<RequeueOutcome.Queued>()
            checkNotNull(service.claimNext()).id shouldBe asked.requestId
          }
        }

        test("an ask put back resumes from the frontier, and goes back only once") {
          runTest {
            val q0 = "quit one".toByteArray()
            val q1 = "quit two".toByteArray()
            // Session 1 agrees, session 2 breaks the port; put back, session 2 alone is run.
            val rig = Rig(backgroundScope, mapOf(0 to q0, 1 to null, 2 to q1))
            val who = rig.character("Twice")
            rig.store.addMoney(who, 100_000)
            val before = rig.money(who)
            val chainId = onFile(rig, who, 5300L, listOf(q0, q1))

            val asked = rig.service.request(who, null).shouldBeInstanceOf<RequestOutcome.Queued>()
            asked.fee shouldBe 2 * limits.feePerHour
            rig.service.runNext() shouldBe true
            val broken = checkNotNull(rig.requests.find(asked.requestId))
            broken.verdict shouldBe ReplayVerdict.INCONCLUSIVE
            broken.feeRefunded shouldBe 2 * limits.feePerHour
            rig.money(who) shouldBe before
            checkNotNull(rig.chains.find(chainId)).verifiedLink shouldBe 0

            rig.service.requeue(broken, "Mod").shouldBeInstanceOf<RequeueOutcome.Queued>()
            checkNotNull(rig.requests.find(asked.requestId)).requeuedBy shouldBe "Mod"
            rig.service.runNext() shouldBe true

            rig.runner.runs shouldBe 3
            val done = checkNotNull(rig.requests.find(asked.requestId))
            done.verdict shouldBe ReplayVerdict.VERIFIED
            // Already given back once: nothing is taken twice, and nothing is owed.
            done.feeRefunded shouldBe 2 * limits.feePerHour
            rig.money(who) shouldBe before
            checkNotNull(rig.chains.find(chainId)).verdict shouldBe ReplayVerdict.VERIFIED

            val again = rig.service.requeue(done, "Mod")
            again.shouldBeInstanceOf<RequeueOutcome.Refused>().why shouldContain "verified"
          }
        }

        test("an ask refunded once is not refunded again when put back and it fails again") {
          runTest {
            val rig = Rig(backgroundScope, mapOf(0 to null, 1 to null))
            val who = rig.character("Unlucky")
            rig.store.addMoney(who, 100_000)
            val before = rig.money(who)
            onFile(rig, who, 5400L, listOf("the only quit".toByteArray()))
            val asked = rig.service.request(who, null).shouldBeInstanceOf<RequestOutcome.Queued>()
            asked.fee shouldBe limits.feePerHour

            rig.service.runNext() shouldBe true
            rig.money(who) shouldBe before
            rig.service
                .requeue(checkNotNull(rig.requests.find(asked.requestId)), "Mod")
                .shouldBeInstanceOf<RequeueOutcome.Queued>()
            rig.service.runNext() shouldBe true

            val twice = checkNotNull(rig.requests.find(asked.requestId))
            twice.verdict shouldBe ReplayVerdict.INCONCLUSIVE
            twice.feeRefunded shouldBe limits.feePerHour
            rig.money(who) shouldBe before
            val third = rig.service.requeue(twice, "Mod")
            third.shouldBeInstanceOf<RequeueOutcome.Refused>().why shouldContain
                "already put back once"
          }
        }

        test("an ask still waiting is not put back") {
          runTest {
            val rig = Rig(backgroundScope, mapOf(0 to null))
            val who = rig.character("Queued")
            onFile(rig, who, 5500L, listOf("the only quit".toByteArray()))
            val asked = rig.service.request(who, null).shouldBeInstanceOf<RequestOutcome.Queued>()

            val waiting =
                rig.service.requeue(checkNotNull(rig.requests.find(asked.requestId)), "Mod")
            waiting.shouldBeInstanceOf<RequeueOutcome.Refused>().why shouldContain "still waiting"
          }
        }

        test("a verdict that lands while the player is offline still takes the mark off") {
          runTest {
            val quit = "the only quit".toByteArray()
            val mon = monster(0, 0x1111)
            val rig = Rig(backgroundScope, mapOf(0 to quit), reports(quit to listOf(born(mon))))
            val who = rig.character("Away")
            rig.store.addPokemon(who, mon)
            onFile(rig, who, 5600L, listOf(quit))
            rig.service.request(who, 0x1111).shouldBeInstanceOf<RequestOutcome.Queued>()

            // The player logs out: the character is written and dropped from the cache, which is
            // where every verdict used to find nothing to write to.
            rig.store.unloadCharacterAsync(who)
            testScheduler.runCurrent()
            rig.store.getCharacter(who).shouldBeNull()
            checkNotNull(rig.repository.saved[who]).pokemon.single().offlineOrigin shouldBe true

            rig.service.runNext() shouldBe true
            testScheduler.runCurrent()

            checkNotNull(rig.repository.saved[who]).pokemon.single().offlineOrigin shouldBe false
            // And let go again: nobody is online to hold it.
            rig.store.getCharacter(who).shouldBeNull()
          }
        }

        test("money given back to a player who is offline reaches them") {
          runTest {
            val rig = Rig(backgroundScope, mapOf(0 to null))
            val who = rig.character("Owed")
            rig.store.addMoney(who, 100_000)
            val before = rig.money(who)
            onFile(rig, who, 5700L, listOf("the only quit".toByteArray()))
            val asked = rig.service.request(who, null).shouldBeInstanceOf<RequestOutcome.Queued>()
            asked.fee shouldBe limits.feePerHour

            rig.store.unloadCharacterAsync(who)
            testScheduler.runCurrent()
            rig.store.getCharacter(who).shouldBeNull()
            checkNotNull(rig.repository.saved[who]).info.money shouldBe before - asked.fee

            rig.service.runNext() shouldBe true
            testScheduler.runCurrent()

            checkNotNull(rig.requests.find(asked.requestId)).verdict shouldBe
                ReplayVerdict.INCONCLUSIVE
            checkNotNull(rig.repository.saved[who]).info.money shouldBe before
            rig.store.getCharacter(who).shouldBeNull()
          }
        }
      }
    })
