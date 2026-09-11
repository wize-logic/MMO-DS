package de.fiereu.openmmo.server.game.di

import com.zaxxer.hikari.HikariConfig
import com.zaxxer.hikari.HikariDataSource
import dagger.Module
import dagger.Provides
import de.fiereu.openmmo.common.auth.SessionTokenVerifier
import de.fiereu.openmmo.common.io.PemKeyLoader
import de.fiereu.openmmo.common.io.pemStream
import de.fiereu.openmmo.pokemon.EvolutionRegistry
import de.fiereu.openmmo.server.game.config.GameServerConfig
import de.fiereu.openmmo.server.game.offline.verify.ExportAnchorService
import de.fiereu.openmmo.server.game.offline.verify.NoReplayRunner
import de.fiereu.openmmo.server.game.offline.verify.ProcessReplayRunner
import de.fiereu.openmmo.server.game.offline.verify.ReplayLimits
import de.fiereu.openmmo.server.game.offline.verify.ReplayRunner
import de.fiereu.openmmo.server.game.offline.verify.ReplayStores
import de.fiereu.openmmo.server.game.offline.verify.ReplayVerificationService
import de.fiereu.openmmo.server.game.offline.verify.ReplayWorker
import de.fiereu.openmmo.server.game.offline.verify.ReplayWorkerConfig
import de.fiereu.openmmo.server.game.script.ScriptRegistry
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.ChainRepository
import de.fiereu.openmmo.server.game.storage.CharacterPresence
import de.fiereu.openmmo.server.game.storage.CharacterRepository
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.ExportRepository
import de.fiereu.openmmo.server.game.storage.GtlRepository
import de.fiereu.openmmo.server.game.storage.GuildRepository
import de.fiereu.openmmo.server.game.storage.ImportRepository
import de.fiereu.openmmo.server.game.storage.JooqChainRepository
import de.fiereu.openmmo.server.game.storage.JooqCharacterRepository
import de.fiereu.openmmo.server.game.storage.JooqExportRepository
import de.fiereu.openmmo.server.game.storage.JooqGtlRepository
import de.fiereu.openmmo.server.game.storage.JooqGuildRepository
import de.fiereu.openmmo.server.game.storage.JooqImportRepository
import de.fiereu.openmmo.server.game.storage.JooqMailRepository
import de.fiereu.openmmo.server.game.storage.JooqOfflineItemRepository
import de.fiereu.openmmo.server.game.storage.JooqPcRepository
import de.fiereu.openmmo.server.game.storage.JooqRequestRepository
import de.fiereu.openmmo.server.game.storage.JooqSaveBlockRepository
import de.fiereu.openmmo.server.game.storage.JooqSocialRepository
import de.fiereu.openmmo.server.game.storage.JooqViolationRepository
import de.fiereu.openmmo.server.game.storage.MailRepository
import de.fiereu.openmmo.server.game.storage.OfflineItemRepository
import de.fiereu.openmmo.server.game.storage.PcRepository
import de.fiereu.openmmo.server.game.storage.RequestRepository
import de.fiereu.openmmo.server.game.storage.SaveBlockRepository
import de.fiereu.openmmo.server.game.storage.SocialRepository
import de.fiereu.openmmo.server.game.storage.ViolationRepository
import de.fiereu.openmmo.server.game.world.interest.InterestPolicy
import de.fiereu.openmmo.server.game.world.interest.PassThroughInterestPolicy
import io.netty.channel.EventLoopGroup
import io.netty.channel.MultiThreadIoEventLoopGroup
import io.netty.channel.nio.NioIoHandler
import java.security.interfaces.ECPrivateKey
import javax.inject.Named
import javax.inject.Singleton
import javax.sql.DataSource
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import org.jooq.DSLContext
import org.jooq.SQLDialect
import org.jooq.impl.DSL

@Module
object GameServerModule {

  @Provides
  @Singleton
  fun rootKey(config: GameServerConfig): ECPrivateKey =
      PemKeyLoader.loadEcPrivate(
          pemStream(config.rootKey, config.rootKeyFile, config.rootKeyResource))

  @Provides
  @Singleton
  fun tokenVerifier(config: GameServerConfig): SessionTokenVerifier =
      SessionTokenVerifier(config.sessionSecret, config.sessionTokenMaxAge)

  @Provides
  @Singleton
  @Named("boss")
  fun bossGroup(): EventLoopGroup = MultiThreadIoEventLoopGroup(1, NioIoHandler.newFactory())

  @Provides
  @Singleton
  @Named("worker")
  fun workerGroup(): EventLoopGroup = MultiThreadIoEventLoopGroup(0, NioIoHandler.newFactory())

  @Provides
  @Singleton
  fun coroutineScope(): CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

  @Provides @Singleton fun interestPolicy(impl: PassThroughInterestPolicy): InterestPolicy = impl

  @Provides @Singleton fun scriptRegistry(): ScriptRegistry = ScriptRegistry.generated()

  @Provides
  @Singleton
  fun characterRepository(impl: JooqCharacterRepository): CharacterRepository = impl

  @Provides @Singleton fun gtlRepository(impl: JooqGtlRepository): GtlRepository = impl

  @Provides @Singleton fun mailRepository(impl: JooqMailRepository): MailRepository = impl

  @Provides @Singleton fun socialRepository(impl: JooqSocialRepository): SocialRepository = impl

  @Provides @Singleton fun guildRepository(impl: JooqGuildRepository): GuildRepository = impl

  @Provides @Singleton fun pcRepository(impl: JooqPcRepository): PcRepository = impl

  @Provides
  @Singleton
  fun saveBlockRepository(impl: JooqSaveBlockRepository): SaveBlockRepository = impl

  @Provides @Singleton fun importRepository(impl: JooqImportRepository): ImportRepository = impl

  @Provides
  @Singleton
  fun offlineItemRepository(impl: JooqOfflineItemRepository): OfflineItemRepository = impl

  @Provides @Singleton fun chainRepository(impl: JooqChainRepository): ChainRepository = impl

  @Provides @Singleton fun exportRepository(impl: JooqExportRepository): ExportRepository = impl

  @Provides @Singleton fun requestRepository(impl: JooqRequestRepository): RequestRepository = impl

  @Provides @Singleton fun replayLimits(): ReplayLimits = ReplayLimits.fromEnvironment()

  /** The replay worker, or the one that holds no builds. */
  @Provides
  @Singleton
  fun replayRunner(): ReplayRunner =
      ReplayWorkerConfig.fromEnvironment()?.let { ProcessReplayRunner(it) } ?: NoReplayRunner

  @Provides
  @Singleton
  fun replayVerification(
      stores: ReplayStores,
      characters: CharacterStore,
      runner: ReplayRunner,
      limits: ReplayLimits,
      entityIds: EntityIdService,
      evolutions: EvolutionRegistry,
  ): ReplayVerificationService =
      ReplayVerificationService(
          stores.chains,
          stores.imports,
          characters,
          runner,
          limits,
          entityIds,
          stores.exports,
          Dispatchers.IO,
          stores.requests,
          evolutions)

  /**
   * The lanes that replay the queue. Asked for by name in `main()`, before the port opens, so that
   * the first player bringing a save online is not what builds it.
   */
  @Provides
  @Singleton
  fun replayWorker(
      service: ReplayVerificationService,
      limits: ReplayLimits,
      scope: CoroutineScope,
  ): ReplayWorker = ReplayWorker(service, limits, scope)

  /**
   * The keeper of offline copies, which boots each one in the same worker the replays run in. With
   * no worker it keeps nothing and says so, which is the state every character was in.
   */
  @Provides
  @Singleton
  fun exportAnchors(
      exports: ExportRepository,
      characters: CharacterStore,
      runner: ReplayRunner,
      limits: ReplayLimits,
      entityIds: EntityIdService,
      scope: CoroutineScope,
  ): ExportAnchorService =
      ExportAnchorService(
          exports, characters, runner, entityIds, scope, Dispatchers.IO, limits.cores)

  /** Who is online, for the store's own decision about letting a character go. */
  @Provides
  @Singleton
  fun characterPresence(sessions: SessionRegistry): CharacterPresence = sessions

  @Provides
  @Singleton
  fun violationRepository(impl: JooqViolationRepository): ViolationRepository = impl

  // Never touch the database while building the Dagger graph. The explicit
  // migrate() call in main() is the fail-fast connection check.
  @Provides
  @Singleton
  fun dataSource(config: GameServerConfig): DataSource =
      HikariDataSource(
          HikariConfig().apply {
            jdbcUrl = config.db.jdbcUrl
            username = config.db.user
            password = config.db.password
            maximumPoolSize = config.db.poolSize
            initializationFailTimeout = -1
          })

  @Provides
  @Singleton
  fun dslContext(dataSource: DataSource): DSLContext = DSL.using(dataSource, SQLDialect.POSTGRES)

  @Provides
  @Singleton
  @Named("db")
  fun dbDispatcher(config: GameServerConfig): CoroutineDispatcher =
      Dispatchers.IO.limitedParallelism(config.db.poolSize)
}
