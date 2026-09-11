package de.fiereu.openmmo.server.game.config

data class DbConfig(
    val host: String = "localhost",
    val port: Int = 20021,
    val name: String = "openmmo_game_db",
    val user: String = "openmmo_game_user",
    val password: String = "changeMe!",
    val poolSize: Int = 4,
    val seedDev: Boolean = false,
) {
  // Without this the generated one puts the password in any log line or stack trace that prints
  // the config.
  override fun toString(): String =
      "DbConfig(host=$host, port=$port, name=$name, user=$user, password=***, poolSize=$poolSize," +
          " seedDev=$seedDev)"

  val jdbcUrl: String
    get() = "jdbc:postgresql://$host:$port/$name"
}
