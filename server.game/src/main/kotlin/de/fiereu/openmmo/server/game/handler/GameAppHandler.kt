package de.fiereu.openmmo.server.game.handler

import de.fiereu.network.Side
import de.fiereu.network.coroutines.CoroutineProtocolHandler
import de.fiereu.openmmo.common.enums.ChatType
import de.fiereu.openmmo.common.enums.Language
import de.fiereu.openmmo.net.game.GameProtocol
import de.fiereu.openmmo.net.game.packets.AddFriendPacket
import de.fiereu.openmmo.net.game.packets.BagDeltaPacket
import de.fiereu.openmmo.net.game.packets.BattleOutcomePacket
import de.fiereu.openmmo.net.game.packets.BlockPlayerPacket
import de.fiereu.openmmo.net.game.packets.CancelSocialInteractionPacket
import de.fiereu.openmmo.net.game.packets.ChatMessagePacket
import de.fiereu.openmmo.net.game.packets.ChatMessageSendPacket
import de.fiereu.openmmo.net.game.packets.ClientScriptOwnershipPacket
import de.fiereu.openmmo.net.game.packets.ContestCommPacket
import de.fiereu.openmmo.net.game.packets.CreateCharacterPacket
import de.fiereu.openmmo.net.game.packets.CreateMarketListingPacket
import de.fiereu.openmmo.net.game.packets.DeleteCharacterPacket
import de.fiereu.openmmo.net.game.packets.DialogChoicePacket
import de.fiereu.openmmo.net.game.packets.DialogOptionPacket
import de.fiereu.openmmo.net.game.packets.EntityInteractPacket
import de.fiereu.openmmo.net.game.packets.ExchangeItemRequestPacket
import de.fiereu.openmmo.net.game.packets.FaceDirectionPacket
import de.fiereu.openmmo.net.game.packets.InGameChallengeResponsePacket
import de.fiereu.openmmo.net.game.packets.JoinPacket
import de.fiereu.openmmo.net.game.packets.KeepAlivePacket
import de.fiereu.openmmo.net.game.packets.LeaveMatchmakingQueuePacket
import de.fiereu.openmmo.net.game.packets.LinkBattleDataPacket
import de.fiereu.openmmo.net.game.packets.LinkKickMemberPacket
import de.fiereu.openmmo.net.game.packets.MailComposeSendPacket
import de.fiereu.openmmo.net.game.packets.MailDeletePacket
import de.fiereu.openmmo.net.game.packets.MailDetailRequestPacket
import de.fiereu.openmmo.net.game.packets.MailPageRequestPacket
import de.fiereu.openmmo.net.game.packets.MapLoadedAckPacket
import de.fiereu.openmmo.net.game.packets.MarketListingsRequestPacket
import de.fiereu.openmmo.net.game.packets.MarketSearchFilterPacket
import de.fiereu.openmmo.net.game.packets.MatchmakingLanguagePrefsPacket
import de.fiereu.openmmo.net.game.packets.MoneyDeltaPacket
import de.fiereu.openmmo.net.game.packets.MovementPacket
import de.fiereu.openmmo.net.game.packets.NullPacket
import de.fiereu.openmmo.net.game.packets.PokemonMovePacket
import de.fiereu.openmmo.net.game.packets.PokemonReleasePacket
import de.fiereu.openmmo.net.game.packets.RegisteredItemPacket
import de.fiereu.openmmo.net.game.packets.RemoveFriendPacket
import de.fiereu.openmmo.net.game.packets.RequestCharactersPacket
import de.fiereu.openmmo.net.game.packets.RequestPlayerPacket
import de.fiereu.openmmo.net.game.packets.RequestSocialProfilePacket
import de.fiereu.openmmo.net.game.packets.ScriptGrantPacket
import de.fiereu.openmmo.net.game.packets.ScriptStatePacket
import de.fiereu.openmmo.net.game.packets.ScriptWarpArrivedPacket
import de.fiereu.openmmo.net.game.packets.SelectCharacterPacket
import de.fiereu.openmmo.net.game.packets.SendChatCommandPacket
import de.fiereu.openmmo.net.game.packets.ShopSellRequestPacket
import de.fiereu.openmmo.net.game.packets.StringCommandPacket
import de.fiereu.openmmo.net.game.packets.TileInteractPacket
import de.fiereu.openmmo.net.game.packets.TradeActionPacket
import de.fiereu.openmmo.net.game.packets.TradeCommPacket
import de.fiereu.openmmo.net.game.packets.TradeSelectMonPacket
import de.fiereu.openmmo.net.game.packets.UnblockPlayerPacket
import de.fiereu.openmmo.net.game.packets.UndergroundTalkPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleActionPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleActionSelectPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleAppearancePacket
import de.fiereu.openmmo.net.game.packets.battle.BattleCancelRequestPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleChatMessagePacket
import de.fiereu.openmmo.net.game.packets.battle.BattleLeavePacket
import de.fiereu.openmmo.net.game.packets.battle.BattlePartySlotSelectPacket
import de.fiereu.openmmo.net.game.packets.battle.BattlePartySwitchPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleReadyPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleRewardSelectPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleSequencePacket
import de.fiereu.openmmo.net.game.packets.battle.BattleSimulationRequestPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleSlotActionPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleSwitchSelectionsPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleTargetPickPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleTierSelectPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleTransitionReadyPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleUseItemPacket
import de.fiereu.openmmo.net.game.packets.battle.moves.MoveLearnReplyPacket
import de.fiereu.openmmo.net.game.packets.dialog.DialogActionResponsePacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlClaimPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlConfirmPurchasePacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlCreateListingPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlListingActionPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlListingCancelPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlListingSearchPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlListingsPageRequestPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlMarketListingsRequestPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlOpenSessionPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlPriceChangePacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlPurchaseListingPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlPurchasePacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlSearchFilterPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlSearchPageRequestPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlTradeLogRequestPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildActivityLogPageRequestPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildCreatePacket
import de.fiereu.openmmo.net.game.packets.guild.GuildDisbandPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildInvitePacket
import de.fiereu.openmmo.net.game.packets.guild.GuildLeavePacket
import de.fiereu.openmmo.net.game.packets.guild.GuildMemberKickPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildMemberRankAssignPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildMotdUpdatePacket
import de.fiereu.openmmo.net.game.packets.guild.GuildRankLabelUpdatePacket
import de.fiereu.openmmo.net.game.packets.guild.GuildRankPermissionUpdatePacket
import de.fiereu.openmmo.net.game.packets.matchmaking.MatchmakingSignupPacket
import de.fiereu.openmmo.server.game.matchmaking.MatchmakingService
import de.fiereu.openmmo.server.game.script.ScriptRunner
import de.fiereu.openmmo.server.game.services.BattleService
import de.fiereu.openmmo.server.game.services.ChatService
import de.fiereu.openmmo.server.game.services.ContestService
import de.fiereu.openmmo.server.game.services.DialogService
import de.fiereu.openmmo.server.game.services.DuelService
import de.fiereu.openmmo.server.game.services.GtlService
import de.fiereu.openmmo.server.game.services.GuildService
import de.fiereu.openmmo.server.game.services.InteractionService
import de.fiereu.openmmo.server.game.services.ItemUseService
import de.fiereu.openmmo.server.game.services.LinkService
import de.fiereu.openmmo.server.game.services.LocalScriptService
import de.fiereu.openmmo.server.game.services.LoginService
import de.fiereu.openmmo.server.game.services.MailService
import de.fiereu.openmmo.server.game.services.MovementService
import de.fiereu.openmmo.server.game.services.MultiplayerService
import de.fiereu.openmmo.server.game.services.PokemonStorageService
import de.fiereu.openmmo.server.game.services.PresenceService
import de.fiereu.openmmo.server.game.services.ShopService
import de.fiereu.openmmo.server.game.services.SocialService
import de.fiereu.openmmo.server.game.services.TradeService
import de.fiereu.openmmo.server.game.services.UndergroundTalkService
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.session.SCRIPT_SCOPE
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.world.UndergroundMap
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.cancel

private val log = KotlinLogging.logger {}

/** How much of a ping's token this server hands back. The client writes one byte and reads one. */
private const val MAX_KEEPALIVE_BYTES = 8

class GameAppHandler
@Inject
constructor(
    private val loginService: LoginService,
    private val movementService: MovementService,
    private val dialogService: DialogService,
    private val interactionService: InteractionService,
    private val multiplayerService: MultiplayerService,
    private val presenceService: PresenceService,
    private val socialService: SocialService,
    private val guildService: GuildService,
    private val linkService: LinkService,
    private val mailService: MailService,
    private val battleService: BattleService,
    private val duelService: DuelService,
    private val matchmakingService: MatchmakingService,
    private val tradeService: TradeService,
    private val gtlService: GtlService,
    private val undergroundTalkService: UndergroundTalkService,
    private val contestService: ContestService,
    private val chatService: ChatService,
    private val shopService: ShopService,
    private val itemUseService: ItemUseService,
    private val pokemonStorageService: PokemonStorageService,
    private val scriptRunner: ScriptRunner,
    private val localScriptService: LocalScriptService,
    private val sessionRegistry: SessionRegistry,
    private val characterStore: CharacterStore,
    scope: CoroutineScope,
) : CoroutineProtocolHandler<GameProtocol>(GameProtocol, Side.SERVER, scope) {

  init {
    onSuspend<JoinPacket> { event -> loginService.onJoinGame(event) }
    onSuspend<CreateCharacterPacket> { event -> loginService.onCreateCharacter(event) }
    onSuspend<RequestCharactersPacket> { event -> loginService.onCharacterRequest(event) }
    onSuspend<SelectCharacterPacket> { event -> loginService.onCharacterSelected(event) }
    onSuspend<DeleteCharacterPacket> { event -> loginService.onDeleteCharacter(event) }
    onSuspend<RequestPlayerPacket> { event -> loginService.onRequestPlayer(event) }

    onSuspend<MovementPacket> { event -> movementService.onMovement(event) }
    onSuspend<FaceDirectionPacket> { event -> movementService.onFaceDirection(event) }

    // The client's own script VM ran a scene; these two are its record, not a
    // request (LocalScriptService).
    on<ClientScriptOwnershipPacket> { event -> localScriptService.onScriptOwnership(event) }
    onSuspend<ScriptStatePacket> { event -> localScriptService.onScriptState(event) }
    on<ScriptWarpArrivedPacket> { event -> localScriptService.onScriptWarpArrived(event) }
    onSuspend<ScriptGrantPacket> { event -> localScriptService.onScriptGrant(event) }
    on<BattleOutcomePacket> { event -> localScriptService.onBattleOutcome(event) }
    onSuspend<BagDeltaPacket> { event -> localScriptService.onBagDelta(event) }
    onSuspend<MoneyDeltaPacket> { event -> localScriptService.onMoneyDelta(event) }
    onSuspend<PokemonReleasePacket> { event -> localScriptService.onPokemonRelease(event) }
    on<RegisteredItemPacket> { event -> localScriptService.onRegisteredItem(event) }

    onSuspend<EntityInteractPacket> { event -> interactionService.onEntityInteract(event) }
    onSuspend<TileInteractPacket> { event -> interactionService.onTileInteract(event) }
    onSuspend<DialogActionResponsePacket> { event -> dialogService.onInteractive(event) }
    onSuspend<DialogChoicePacket> { event -> dialogService.onDialogChoice(event) }
    onSuspend<DialogOptionPacket> { event -> itemUseService.onUse(event) }
    onSuspend<ExchangeItemRequestPacket> { event -> shopService.onBuy(event) }
    onSuspend<ShopSellRequestPacket> { event -> shopService.onSell(event) }

    on<AddFriendPacket> { event -> socialService.onAddFriend(event) }
    on<RemoveFriendPacket> { event -> socialService.onRemoveFriend(event) }
    on<BlockPlayerPacket> { event -> socialService.onBlockPlayer(event) }
    on<UnblockPlayerPacket> { event -> socialService.onUnblockPlayer(event) }
    on<SendChatCommandPacket> { event -> linkService.onInvite(event) }
    on<LinkKickMemberPacket> { event -> linkService.onKick(event) }
    on<CancelSocialInteractionPacket> { event -> linkService.onLeave(event) }
    on<RequestSocialProfilePacket> { event -> linkService.onAssignCaptain(event) }

    onSuspend<GuildCreatePacket> { event -> guildService.onCreateGuild(event) }
    onSuspend<GuildInvitePacket> { event -> guildService.onGuildInvite(event) }
    onSuspend<GuildRankPermissionUpdatePacket> { event ->
      guildService.onRankPermissionUpdate(event)
    }
    onSuspend<GuildMemberRankAssignPacket> { event -> guildService.onRankAssign(event) }
    onSuspend<GuildMemberKickPacket> { event -> guildService.onKick(event) }
    onSuspend<GuildLeavePacket> { event -> guildService.onLeave(event) }
    onSuspend<GuildDisbandPacket> { event -> guildService.onDisband(event) }
    onSuspend<GuildMotdUpdatePacket> { event -> guildService.onMotdUpdate(event) }
    onSuspend<GuildRankLabelUpdatePacket> { event -> guildService.onRankLabelUpdate(event) }
    onSuspend<GuildActivityLogPageRequestPacket> { event ->
      guildService.onActivityLogPageRequest(event)
    }

    onSuspend<MailComposeSendPacket> { event -> mailService.onCompose(event) }
    onSuspend<MailPageRequestPacket> { event -> mailService.onPageRequest(event) }
    onSuspend<MailDetailRequestPacket> { event -> mailService.onDetailRequest(event) }
    onSuspend<MailDeletePacket> { event -> mailService.onDelete(event) }

    onSuspend<MoveLearnReplyPacket> { event -> battleService.onMoveLearnReply(event) }
    on<BattlePartySwitchPacket> { event -> battleService.onBattlePacket(event) }
    on<BattleActionPacket> { event -> battleService.onBattlePacket(event) }
    onSuspend<BattleActionSelectPacket> { event -> battleService.onBattleAction(event) }
    on<BattleLeavePacket> { event -> battleService.onBattlePacket(event) }
    on<BattleSequencePacket> { event -> battleService.onBattlePacket(event) }
    on<BattleSlotActionPacket> { event -> battleService.onBattlePacket(event) }
    on<BattleSwitchSelectionsPacket> { event -> battleService.onBattlePacket(event) }
    on<BattleUseItemPacket> { event -> battleService.onBattlePacket(event) }
    on<BattleAppearancePacket> { event -> battleService.onBattlePacket(event) }
    on<BattleCancelRequestPacket> { event -> battleService.onBattlePacket(event) }
    on<BattleSimulationRequestPacket> { event -> battleService.onBattlePacket(event) }
    on<BattleReadyPacket> { event -> battleService.onBattlePacket(event) }
    on<MatchmakingSignupPacket> { event -> matchmakingService.onSignup(event) }
    on<LeaveMatchmakingQueuePacket> { event -> matchmakingService.onWithdraw(event) }
    on<MatchmakingLanguagePrefsPacket> { event -> matchmakingService.onLanguagePrefs(event) }
    on<BattleTierSelectPacket> { event -> battleService.onBattlePacket(event) }
    on<BattleChatMessagePacket> { event -> battleService.onBattleChat(event) }
    on<BattlePartySlotSelectPacket> { event -> battleService.onBattlePacket(event) }
    on<BattleTargetPickPacket> { event -> battleService.onBattlePacket(event) }
    on<BattleTransitionReadyPacket> { event -> battleService.onBattlePacket(event) }
    on<BattleRewardSelectPacket> { event -> battleService.onBattlePacket(event) }
    onSuspend<MapLoadedAckPacket> { event -> battleService.onClientReady(event) }

    // A challenge is an offer: the challenged player answers it, and only an accept opens the
    // native link battle whose blobs this server relays without reading.
    on<InGameChallengeResponsePacket> { event -> duelService.onResponse(event) }
    on<LinkBattleDataPacket> { event -> duelService.onLinkData(event) }

    // A trade is the same shape as a challenge: an offer by name, a two-sided table, and nothing
    // moves before both sides confirm the pair as it stands.
    on<StringCommandPacket> { event -> tradeService.onRequest(event) }
    onSuspend<TradeActionPacket> { event -> tradeService.onAction(event) }
    on<TradeSelectMonPacket> { event -> tradeService.onSelect(event) }
    on<TradeCommPacket> { event -> tradeService.onComm(event) }

    // The global trade link. Everything that moves money or monsters suspends on the durable
    // store; the rest are reads of the shelf.
    onSuspend<GtlOpenSessionPacket> { event -> gtlService.onOpenSession(event) }
    onSuspend<GtlListingsPageRequestPacket> { event -> gtlService.onListingsPage(event) }
    onSuspend<GtlSearchPageRequestPacket> { event -> gtlService.onSearchPage(event) }
    on<GtlCreateListingPacket> { event -> gtlService.onLegacyCreate(event) }
    onSuspend<GtlConfirmPurchasePacket> { event -> gtlService.onConfirmPurchase(event) }
    onSuspend<GtlPurchaseListingPacket> { event -> gtlService.onPurchaseListing(event) }
    onSuspend<GtlPurchasePacket> { event -> gtlService.onPurchase(event) }
    onSuspend<GtlListingCancelPacket> { event -> gtlService.onListingCancel(event) }
    onSuspend<GtlClaimPacket> { event -> gtlService.onClaim(event) }
    onSuspend<GtlPriceChangePacket> { event -> gtlService.onPriceChange(event) }
    onSuspend<GtlListingSearchPacket> { event -> gtlService.onListingSearch(event) }
    onSuspend<GtlTradeLogRequestPacket> { event -> gtlService.onTradeLog(event) }
    on<GtlMarketListingsRequestPacket> { event -> gtlService.onMarketListings(event) }
    on<GtlSearchFilterPacket> { event -> gtlService.onSearchFilter(event) }
    on<GtlListingActionPacket> { event -> gtlService.onListingAction(event) }
    on<MarketListingsRequestPacket> { event -> gtlService.onMarketBoard(event) }
    on<MarketSearchFilterPacket> { event -> gtlService.onMarketSearch(event) }
    onSuspend<CreateMarketListingPacket> { event -> gtlService.onCreateListing(event) }

    // Pressing A on someone in the Underground, and the conversation it opens. This server is the
    // console that decides the pairing; the conversation itself crosses unread.
    on<UndergroundTalkPacket> { event -> undergroundTalkService.onPacket(event) }
    // The link Super Contest: the queue, the relay and the agreed result.
    on<ContestCommPacket> { event -> contestService.onContestComm(event) }

    on<PokemonMovePacket> { event -> pokemonStorageService.onMove(event) }

    // The client sends an empty heartbeat packet.
    on<NullPacket> {}
    // A ping, answered with the same token so the client can time the round trip. The codec takes
    // the whole rest of the frame, and this handed every byte back before the session had
    // authenticated, so the reply is trimmed to what a ping is.
    on<KeepAlivePacket> { event ->
      val data = event.packet.sessionData
      if (data.size > MAX_KEEPALIVE_BYTES) {
        log.warn { "A ping carried ${data.size} bytes of token; answering the first one" }
      }
      event.session.send(
          if (data.size <= MAX_KEEPALIVE_BYTES) event.packet
          else event.packet.copy(sessionData = data.copyOf(MAX_KEEPALIVE_BYTES)))
    }
    // What the client sends when the player types. The text rides in target unless the mode
    // carries a message of its own.
    onSuspend<ChatMessageSendPacket> { event -> chatService.onSend(event.session, event.packet) }
  }

  override fun onInactive() {
    // A cancelled scope left behind would make every later launch a silent no-op.
    session.attributes.remove(SCRIPT_SCOPE)?.cancel()
    val state = session.attributes[PLAYER_STATE] ?: return
    log.info { "Player ${state.characterId} disconnected." }
    val charId = state.characterId
    if (charId != null) {
      // The battle flush must land before the unload evicts the character from the cache, and
      // before the rollback, which would otherwise be overwritten by the party it persists.
      battleService.onDisconnect(session)
      duelService.onDisconnect(session)
      matchmakingService.onDisconnect(session)
      tradeService.onDisconnect(session)
      undergroundTalkService.onDisconnect(session)
      contestService.onDisconnect(session)
      // Undo the interrupted script here rather than leaving it to the coroutine's own
      // cleanup, which runs on another thread and would race the flush below.
      val unfinishedScript = scriptRunner.takeRollback(session)
      state.inDialog = false
      presenceService.leave(session)
      characterStore.getCharacter(charId)?.info?.name?.let { name ->
        socialService.announcePresence(name, false)
      }
      guildService.announcePresence(charId, false)
      linkService.onDisconnect(charId)
      surfaceIfUnderground(charId, state)
      sessionRegistry.unbindCharacter(charId, session)
      characterStore.unloadCharacterAsync(charId, unfinishedScript)
    }
    sessionRegistry.unregister(session)
    multiplayerService.broadcastMessage(
        ChatMessagePacket(
            type = ChatType.GAME_NOTIFICATIONS,
            language = Language.EN,
            message = "A player left the game.",
            sender = "",
        ),
    )
  }

  /** Put a character who is still in the Underground back on the tile they went down from. */
  private fun surfaceIfUnderground(charId: Long, state: PlayerState) {
    val exit = state.undergroundExit ?: return
    val info = characterStore.getCharacter(charId)?.info ?: return
    if (!UndergroundMap.isUnderground(
        info.positionRegionId, info.positionBankId, info.positionMapId)) {
      return
    }
    characterStore.updateCharacter(
        info.copy(
            positionRegionId = exit.regionId,
            positionBankId = exit.bankId,
            positionMapId = exit.mapId,
            positionX = exit.x,
            positionY = exit.y,
            positionFacing = exit.facing,
        ))
    characterStore.flushCharacterAsync(charId)
    state.undergroundExit = null
    log.info {
      "Character $charId left the game underground; put back at" +
          " ${exit.regionId}:${exit.bankId}:${exit.mapId} (${exit.x}, ${exit.y})"
    }
  }
}
