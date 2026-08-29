package de.fiereu.openmmo.server.web

import java.nio.file.Files
import java.nio.file.Path

/**
 * Serves the site straight from the working tree, which is what a developer run does. In production
 * apache serves these files and this class is never constructed.
 */
class StaticFiles(private val root: Path) {
  data class Served(val bytes: ByteArray, val contentType: String) {
    override fun equals(other: Any?): Boolean =
        other is Served && bytes.contentEquals(other.bytes) && contentType == other.contentType

    override fun hashCode(): Int = bytes.contentHashCode() * 31 + contentType.hashCode()
  }

  fun read(requestPath: String): Served? {
    val relative = requestPath.removePrefix("/").ifEmpty { "index.html" }
    // normalize() collapses any ".." the caller sent; the containment check below is what makes
    // the result safe, because a path can also climb out with symlinks.
    val candidate = root.resolve(relative).normalize()
    if (!candidate.startsWith(root)) return null
    val file = if (Files.isDirectory(candidate)) candidate.resolve("index.html") else candidate
    if (!Files.isRegularFile(file) || !file.toRealPath().startsWith(root.toRealPath())) return null
    return Served(Files.readAllBytes(file), contentType(file))
  }

  private fun contentType(file: Path): String =
      when (file.fileName.toString().substringAfterLast('.', "")) {
        "html" -> "text/html; charset=utf-8"
        "css" -> "text/css; charset=utf-8"
        "js" -> "text/javascript; charset=utf-8"
        "svg" -> "image/svg+xml"
        "png" -> "image/png"
        "ico" -> "image/x-icon"
        "txt" -> "text/plain; charset=utf-8"
        else -> "application/octet-stream"
      }
}
