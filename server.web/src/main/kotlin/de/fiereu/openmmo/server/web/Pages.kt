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
      <title>${escape(title)} &middot; MMO-DS</title>
      <meta name="robots" content="noindex, follow">
      <meta name="theme-color" content="#73ace2">
      <link rel="icon" href="/favicon.svg?v=3" type="image/svg+xml">
      <link rel="stylesheet" href="/style.css?v=9">
      </head>
      <body>
      <div class="sky" aria-hidden="true"><div></div><div></div><div></div></div>
      <header class="bar">
        <a class="brand" href="/"><span class="mark"></span>MMO-DS</a>
        <nav>
          <a href="/">Home</a>
          <a href="/download.html">Download</a>
          <a class="cta" href="/register.html">Play now</a>
        </nav>
      </header>
      <main class="shell narrow">
      $body
      </main>
      <footer class="foot">
        <div class="footer-legal">
          MMO-DS is a free fan project in development.<br>
          Not affiliated with Nintendo or The Pok&eacute;mon Company. No ROMs or copyrighted
          assets are distributed.<br>
          All logos, trademarks, and trade names used herein are the property of their respective
          owners.
        </div>
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
            <p><a class="button" href="/download.html">Get the client &rarr;</a></p>
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
