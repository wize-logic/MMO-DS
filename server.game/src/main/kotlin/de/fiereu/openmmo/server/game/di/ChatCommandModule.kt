package de.fiereu.openmmo.server.game.di

import dagger.Binds
import dagger.Module
import dagger.multibindings.IntoSet
import de.fiereu.openmmo.server.game.services.command.BoxCommand
import de.fiereu.openmmo.server.game.services.command.CatchCommand
import de.fiereu.openmmo.server.game.services.command.ChallengeCommand
import de.fiereu.openmmo.server.game.services.command.ChatCommand
import de.fiereu.openmmo.server.game.services.command.CompeteCommand
import de.fiereu.openmmo.server.game.services.command.DialogCommand
import de.fiereu.openmmo.server.game.services.command.FlagCommand
import de.fiereu.openmmo.server.game.services.command.GiftCommand
import de.fiereu.openmmo.server.game.services.command.GmCommand
import de.fiereu.openmmo.server.game.services.command.HandbookCommand
import de.fiereu.openmmo.server.game.services.command.HelpCommand
import de.fiereu.openmmo.server.game.services.command.ImportsCommand
import de.fiereu.openmmo.server.game.services.command.KitCommand
import de.fiereu.openmmo.server.game.services.command.ListCommand
import de.fiereu.openmmo.server.game.services.command.MenuCommand
import de.fiereu.openmmo.server.game.services.command.MoveCommand
import de.fiereu.openmmo.server.game.services.command.MovesCommand
import de.fiereu.openmmo.server.game.services.command.PartyCommand
import de.fiereu.openmmo.server.game.services.command.PcCommand
import de.fiereu.openmmo.server.game.services.command.PosCommand
import de.fiereu.openmmo.server.game.services.command.QuestCommand
import de.fiereu.openmmo.server.game.services.command.QueueCommand
import de.fiereu.openmmo.server.game.services.command.RequeueImportCommand
import de.fiereu.openmmo.server.game.services.command.RollbackImportCommand
import de.fiereu.openmmo.server.game.services.command.SeasonCommand
import de.fiereu.openmmo.server.game.services.command.ShopCommand
import de.fiereu.openmmo.server.game.services.command.StoryCommand
import de.fiereu.openmmo.server.game.services.command.SyncCommand
import de.fiereu.openmmo.server.game.services.command.TestBattleCommand
import de.fiereu.openmmo.server.game.services.command.UiCommand
import de.fiereu.openmmo.server.game.services.command.UndoImportCommand
import de.fiereu.openmmo.server.game.services.command.VerifyCommand
import de.fiereu.openmmo.server.game.services.command.ViolationsCommand
import de.fiereu.openmmo.server.game.services.command.WarpCommand
import de.fiereu.openmmo.server.game.services.command.YesNoCommand

/**
 * A name must not start with a client side command. The client resolves those itself and never
 * sends them, which is why there is a /pos and no /where, which /w would have swallowed.
 */
@Module
interface ChatCommandModule {
  @Binds @IntoSet fun helpCommand(command: HelpCommand): ChatCommand

  @Binds @IntoSet fun handbookCommand(command: HandbookCommand): ChatCommand

  @Binds @IntoSet fun posCommand(command: PosCommand): ChatCommand

  @Binds @IntoSet fun seasonCommand(command: SeasonCommand): ChatCommand

  @Binds @IntoSet fun pcCommand(command: PcCommand): ChatCommand

  @Binds @IntoSet fun violationsCommand(command: ViolationsCommand): ChatCommand

  @Binds @IntoSet fun importsCommand(command: ImportsCommand): ChatCommand

  @Binds @IntoSet fun rollbackImportCommand(command: RollbackImportCommand): ChatCommand

  @Binds @IntoSet fun requeueImportCommand(command: RequeueImportCommand): ChatCommand

  @Binds @IntoSet fun undoImportCommand(command: UndoImportCommand): ChatCommand

  @Binds @IntoSet fun verifyCommand(command: VerifyCommand): ChatCommand

  @Binds @IntoSet fun testBattleCommand(command: TestBattleCommand): ChatCommand

  @Binds @IntoSet fun challengeCommand(command: ChallengeCommand): ChatCommand

  @Binds @IntoSet fun catchCommand(command: CatchCommand): ChatCommand

  @Binds @IntoSet fun boxCommand(command: BoxCommand): ChatCommand

  @Binds @IntoSet fun partyCommand(command: PartyCommand): ChatCommand

  @Binds @IntoSet fun movesCommand(command: MovesCommand): ChatCommand

  @Binds @IntoSet fun storyCommand(command: StoryCommand): ChatCommand

  @Binds @IntoSet fun warpCommand(command: WarpCommand): ChatCommand

  @Binds @IntoSet fun shopCommand(command: ShopCommand): ChatCommand

  @Binds @IntoSet fun dialogCommand(command: DialogCommand): ChatCommand

  @Binds @IntoSet fun yesNoCommand(command: YesNoCommand): ChatCommand

  @Binds @IntoSet fun menuCommand(command: MenuCommand): ChatCommand

  @Binds @IntoSet fun listCommand(command: ListCommand): ChatCommand

  @Binds @IntoSet fun moveCommand(command: MoveCommand): ChatCommand

  @Binds @IntoSet fun questCommand(command: QuestCommand): ChatCommand

  @Binds @IntoSet fun queueCommand(command: QueueCommand): ChatCommand

  @Binds @IntoSet fun flagCommand(command: FlagCommand): ChatCommand

  @Binds @IntoSet fun giftCommand(command: GiftCommand): ChatCommand

  @Binds @IntoSet fun kitCommand(command: KitCommand): ChatCommand

  @Binds @IntoSet fun uiCommand(command: UiCommand): ChatCommand

  @Binds @IntoSet fun syncCommand(command: SyncCommand): ChatCommand

  @Binds @IntoSet fun competeCommand(command: CompeteCommand): ChatCommand

  @Binds @IntoSet fun gmCommand(command: GmCommand): ChatCommand
}
