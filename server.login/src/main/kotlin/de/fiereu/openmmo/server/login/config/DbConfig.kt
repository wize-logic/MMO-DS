package de.fiereu.openmmo.server.login.config

data class DbConfig(
    val host: String = "localhost",
    val port: Int = 20011,
    val name: String = "openmmo_login_db",
    val user: String = "openmmo_login_user",
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
