package de.fiereu.openmmo.server.web

/**
 * The handful of pages this process renders itself: what a visitor sees the moment they submit the
 * form. Everything else is static and served by apache, so these only have to match that
 * stylesheet, not reproduce it.
 */
object Pages {

  fun escape(value: String): String = buildString {
    value.forEach {
      when (it) {
        '&' -> append("&amp;")
        '<' -> append("&lt;")
        '>' -> append("&gt;")
        '"' -> append("&quot;")
        '\'' -> append("&#39;")
        else -> append(it)
      }
    }
  }

  fun page(title: String, body: String): String =
      """
      <!doctype html>
      <html lang="en">
      <head>
      <meta charset="utf-8">
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <title>${escape(title)} &middot; OpenMMO DS</title>
      <meta name="robots" content="noindex, follow">
      <link rel="icon" href="/favicon.svg" type="image/svg+xml">
      <link rel="stylesheet" href="/style.css">
      </head>
      <body>
      <header class="bar">
        <a class="brand" href="/"><span class="mark"></span>OpenMMO DS</a>
        <nav>
          <a href="/">Home</a>
          <a href="https://github.com/wize-logic/OpenMMO-DS">Download</a>
          <a class="only-wide" href="https://github.com/openmmo-org/OpenMMO">Source</a>
          <a class="cta" href="/register.html">Play now</a>
        </nav>
      </header>
      <main class="shell narrow">
      $body
      </main>
      <footer class="bar foot">
        <span>OpenMMO DS is a fan project in development. Not affiliated with PokeMMO or Nintendo.</span>
      </footer>
      </body>
      </html>
      """
          .trimIndent()

  fun created(username: String): String =
      page(
          "You're in",
          """
          <div class="card ok">
            <h1>You're in, ${escape(username)}.</h1>
            <p>That name is yours. Get the client and sign in.</p>
            <p class="muted">Capitals don't matter when you sign in.</p>
            <p><a class="button" href="https://github.com/wize-logic/OpenMMO-DS">Get the client &rarr;</a></p>
          </div>
          """
              .trimIndent())

  fun rejected(reason: String): String =
      page(
          "Not quite",
          """
          <div class="card bad">
            <h1>Not quite</h1>
            <p>${escape(reason)}.</p>
            <p><a class="button" href="/register.html">Try again</a></p>
          </div>
          """
              .trimIndent())

  fun notFound(): String =
      page(
          "Page not found",
          """
          <div class="card">
            <h1>Page not found</h1>
            <p>There's no page here.</p>
            <p><a class="button" href="/">Home</a></p>
          </div>
          """
              .trimIndent())
}
