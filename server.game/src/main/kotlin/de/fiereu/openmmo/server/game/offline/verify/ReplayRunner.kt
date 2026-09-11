package de.fiereu.openmmo.server.game.offline.verify

import io.github.oshai.kotlinlogging.KotlinLogging
import java.nio.file.Files
import java.nio.file.Path
import java.security.MessageDigest
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter
import java.util.concurrent.TimeUnit
import kotlin.io.path.deleteRecursively

private val log = KotlinLogging.logger {}

/** One session to replay, exactly as the worker will run it. */
class ReplayRun(
    val revision: Int,
    /** The image this link boots from, always one a replay produced, never one that arrived. */
    val bootImage: ByteArray?,
    val rtc: LocalDateTime,
    val recording: ByteArray,
    val frames: Long,
)

sealed interface ReplayOutcome {
  /** The port ran to its frame and left an image. */
  data class Quit(val image: ByteArray, val sha256: String, val report: ByteArray? = null) :
      ReplayOutcome

  /**
   * The port never got there. Evidence of a bug or a lost file, and of nothing about the player.
   */
  data class Broke(val why: String) : ReplayOutcome
}

interface ReplayRunner {
  /** The builds this worker holds. A link naming any other cannot be replayed. */
  fun revisions(): Set<Int>

  fun run(run: ReplayRun): ReplayOutcome
}

/** The runner a server with no worker configured has, which holds no builds and replays nothing. */
object NoReplayRunner : ReplayRunner {
  override fun revisions(): Set<Int> = emptySet()

  override fun run(run: ReplayRun): ReplayOutcome =
      ReplayOutcome.Broke("this server has no replay worker")
}

/** Where the worker's binaries live and what it is allowed to run them inside. */
data class ReplayWorkerConfig(
    val binaries: Path,
    val sandbox: List<String>,
    val rom: Path,
    val mods: Path,
    /**
     * Address space one replay may have, in kilobytes. The guest is 55 MB; the rest is the host.
     */
    val addressSpaceKb: Long = 2L * 1024 * 1024,
) {
  companion object {
    fun fromEnvironment(env: (String) -> String? = System::getenv): ReplayWorkerConfig? {
      val binaries = env("VERIFY_BINARIES")?.trim().orEmpty()
      val sandbox = env("VERIFY_SANDBOX")?.trim().orEmpty()
      val rom = env("VERIFY_ROM")?.trim().orEmpty()
      val mods = env("VERIFY_MODS")?.trim().orEmpty()
      if (binaries.isEmpty() && sandbox.isEmpty() && rom.isEmpty() && mods.isEmpty()) return null
      val missing = buildList {
        if (binaries.isEmpty()) add("VERIFY_BINARIES")
        if (sandbox.isEmpty()) add("VERIFY_SANDBOX")
        if (rom.isEmpty()) add("VERIFY_ROM")
        if (mods.isEmpty()) add("VERIFY_MODS")
      }
      if (missing.isNotEmpty()) {
        log.error {
          "replay verification is configured but will not run: ${missing.joinToString(", ")} is" +
              " unset. The sandbox in particular has no default, a replay is untrusted input" +
              " through a 32-bit recompilation, so it runs confined or not at all."
        }
        return null
      }
      return ReplayWorkerConfig(
          binaries = Path.of(binaries),
          sandbox = sandbox.split(" ").filter { it.isNotEmpty() },
          rom = Path.of(rom),
          mods = Path.of(mods),
      )
    }
  }
}

/**
 * The worker: one replay, in its own directory, under the operator's sandbox, bounded four ways.
 */
class ProcessReplayRunner(
    private val config: ReplayWorkerConfig,
    /** Frames a second this host manages. Measured by [calibrate]; the bound follows the host. */
    private val rate: Long = 300,
    private val workRoot: Path = Path.of(System.getProperty("java.io.tmpdir")),
) : ReplayRunner {

  override fun revisions(): Set<Int> =
      runCatching {
            Files.list(config.binaries).use { entries ->
              entries
                  .map { it.fileName.toString() }
                  .filter { it.toIntOrNull() != null }
                  .map { it.toInt() }
                  .filter { Files.isExecutable(binaryOf(it)) }
                  .toList()
                  .toSet()
            }
          }
          .getOrElse {
            log.warn(it) { "no replay binaries under ${config.binaries}" }
            emptySet()
          }

  @OptIn(kotlin.io.path.ExperimentalPathApi::class)
  override fun run(run: ReplayRun): ReplayOutcome {
    if (run.frames <= 0) {
      // PC_FRAMES=0 is the port's "no limit", so a zero here would be an unbounded run rather than
      // an empty one. The admission check refuses it first; this is the belt.
      return ReplayOutcome.Broke("a session with no frames to run")
    }
    val binary = binaryOf(run.revision)
    if (!Files.isExecutable(binary)) return ReplayOutcome.Broke("no build ${run.revision} here")

    val dir = Files.createTempDirectory(workRoot, "replay-")
    try {
      val script = dir.resolve("input.txt")
      val save = dir.resolve("link.sav")
      Files.write(script, run.recording)
      if (run.bootImage != null) Files.write(save, run.bootImage)

      val process =
          ProcessBuilder(command(binary))
              .directory(dir.toFile())
              .redirectErrorStream(true)
              .redirectOutput(dir.resolve("log.txt").toFile())
              .also { it.environment().clear() }
              .also { it.environment().putAll(environment(run, script, save)) }
              .start()
      process.outputStream.close()

      // The wall clock inside the command is the real bound; this one only stops the worker
      // waiting for ever on a sandbox that never reaped anything.
      if (!process.waitFor(wallClockSeconds(run.frames) + GRACE_SECONDS, TimeUnit.SECONDS)) {
        process.destroyForcibly()
        return ReplayOutcome.Broke("the replay outlived even its own killer")
      }
      val code = process.exitValue()
      if (code == TIMEOUT_EXIT) {
        return ReplayOutcome.Broke(
            "the replay ran past ${wallClockSeconds(run.frames)}s of wall clock")
      }
      if (code != 0) return ReplayOutcome.Broke("the game exited $code")
      if (!Files.exists(save)) return ReplayOutcome.Broke("the replay wrote no save at all")
      val image = Files.readAllBytes(save)
      val report = dir.resolve("link.sav.report").takeIf { Files.isRegularFile(it) }
      return ReplayOutcome.Quit(image, sha256(image), report?.let { Files.readAllBytes(it) })
    } catch (e: java.io.IOException) {
      return ReplayOutcome.Broke("the replay could not be started: ${e.message}")
    } finally {
      runCatching { dir.deleteRecursively() }
    }
  }

  /**
   * The command, kept apart from running it so a check can read it without a ROM on the machine.
   */
  fun command(binary: Path): List<String> =
      config.sandbox +
          listOf(
              "/bin/sh",
              "-c",
              "ulimit -v ${config.addressSpaceKb}; exec timeout -k 5 \$PC_WALL_SECONDS \"\$0\"",
              binary.toString(),
          )

  fun environment(run: ReplayRun, script: Path, save: Path): Map<String, String> =
      mapOf(
          "PC_ROM" to config.rom.toString(),
          "PC_MODS_DIR" to config.mods.toString(),
          "PC_SAVE" to save.toString(),
          "PC_INPUT" to script.toString(),
          "PC_RTC" to RTC.format(run.rtc),
          // No pacing, no window, no session: a replay is the guest and nothing else. One thread,
          // because many single-threaded replays beat one banded replay on the same cores.
          "PC_PACE" to "0",
          "PC_THREADS" to "1",
          "PC_FRAMES" to run.frames.toString(),
          "PC_WALL_SECONDS" to wallClockSeconds(run.frames).toString(),
          "HOME" to save.parent.toString(),
          "PATH" to "/usr/bin:/bin",
      )

  /** Three times what this host measured itself doing, and never less than a minute. */
  fun wallClockSeconds(frames: Long): Long =
      ((frames * 3) / rate.coerceAtLeast(1)).coerceAtLeast(60)

  private fun binaryOf(revision: Int): Path =
      config.binaries.resolve(revision.toString()).resolve("pokeplatinum")

  companion object {
    /** What `timeout` exits with when it had to kill something. */
    const val TIMEOUT_EXIT = 124

    private const val GRACE_SECONDS = 30L

    private val RTC: DateTimeFormatter = DateTimeFormatter.ofPattern("uuuu-MM-dd HH:mm:ss")

    fun sha256(bytes: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

    /** What this host replays at, measured on it rather than assumed. */
    fun calibrate(runner: ProcessReplayRunner, run: ReplayRun): Long {
      val started = System.nanoTime()
      val outcome = runner.run(run)
      val seconds = (System.nanoTime() - started) / 1_000_000_000.0
      if (outcome is ReplayOutcome.Broke || seconds <= 0.0) {
        log.warn { "replay calibration did not finish; keeping the conservative rate" }
        return 1
      }
      return (run.frames / seconds).toLong().coerceAtLeast(1)
    }
  }
}
