package de.fiereu.openmmo.server.login.di

import dagger.BindsInstance
import dagger.Component
import de.fiereu.openmmo.server.login.LoginServer
import de.fiereu.openmmo.server.login.auth.AdminAccountBootstrap
import de.fiereu.openmmo.server.login.auth.JooqUserStore
import de.fiereu.openmmo.server.login.auth.UserService
import de.fiereu.openmmo.server.login.config.LoginServerConfig
import de.fiereu.openmmo.server.login.db.DatabaseBootstrap
import de.fiereu.openmmo.server.login.handler.LoginAppHandler
import javax.inject.Provider
import javax.inject.Singleton

@Singleton
@Component(modules = [LoginServerModule::class])
interface LoginServerComponent {

  fun server(): LoginServer

  fun handlerProvider(): Provider<LoginAppHandler>

  fun databaseBootstrap(): DatabaseBootstrap

  fun adminAccountBootstrap(): AdminAccountBootstrap

  fun users(): UserService

  /** The store itself, because rewriting old rows is a thing only the database-backed one does. */
  fun userUpgrade(): JooqUserStore

  @Component.Factory
  fun interface Factory {
    fun create(@BindsInstance config: LoginServerConfig): LoginServerComponent
  }
}
