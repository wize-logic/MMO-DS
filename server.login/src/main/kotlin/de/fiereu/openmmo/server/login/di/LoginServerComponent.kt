package de.fiereu.openmmo.server.login.di

import dagger.BindsInstance
import dagger.Component
import de.fiereu.openmmo.server.login.LoginServer
import de.fiereu.openmmo.server.login.auth.AdminAccountBootstrap
import de.fiereu.openmmo.server.login.auth.JooqUserStore
import de.fiereu.openmmo.server.login.auth.RememberMeTokens
import de.fiereu.openmmo.server.login.auth.UserService
import de.fiereu.openmmo.server.login.config.LoginServerConfig
import de.fiereu.openmmo.server.login.db.DatabaseBootstrap
import de.fiereu.openmmo.server.login.handler.LoginAppHandler
import de.fiereu.openmmo.server.login.update.ClientRevisionFloor
import javax.inject.Provider
import javax.inject.Singleton

@Singleton
@Component(modules = [LoginServerModule::class])
interface LoginServerComponent {

  fun server(): LoginServer

  fun handlerProvider(): Provider<LoginAppHandler>

  fun databaseBootstrap(): DatabaseBootstrap

  /**
   * Asked for in main() before the port opens, for the reason migrate() is: a provider is built on
   * first use, and first use here is the first client that connects. A named feed that cannot be
   * read has to stop the server, not the login it should have refused.
   */
  fun clientRevisionFloor(): ClientRevisionFloor

  fun adminAccountBootstrap(): AdminAccountBootstrap

  fun users(): UserService

  fun rememberMeTokens(): RememberMeTokens

  /**
   * The store itself rather than the interface, because rewriting the rows written the old way is a
   * thing only the database-backed one has to do.
   */
  fun userUpgrade(): JooqUserStore

  @Component.Factory
  fun interface Factory {
    fun create(@BindsInstance config: LoginServerConfig): LoginServerComponent
  }
}
