package de.fiereu.openmmo.server.game.handler

import de.fiereu.openmmo.net.game.packets.AddFriendPacket
import de.fiereu.openmmo.net.game.packets.BlockPlayerPacket
import de.fiereu.openmmo.net.game.packets.CancelSocialInteractionPacket
import de.fiereu.openmmo.net.game.packets.DeleteCharacterPacket
import de.fiereu.openmmo.net.game.packets.JoinPacket
import de.fiereu.openmmo.net.game.packets.KeepAlivePacket
import de.fiereu.openmmo.net.game.packets.LinkKickMemberPacket
import de.fiereu.openmmo.net.game.packets.MovementPacket
import de.fiereu.openmmo.net.game.packets.PokemonMovePacket
import de.fiereu.openmmo.net.game.packets.RegisteredItemPacket
import de.fiereu.openmmo.net.game.packets.RemoveFriendPacket
import de.fiereu.openmmo.net.game.packets.RequestSocialProfilePacket
import de.fiereu.openmmo.net.game.packets.SendChatCommandPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleActionPacket
import de.fiereu.openmmo.server.game.config.GameServerConfig
import de.fiereu.openmmo.server.game.di.DaggerGameServerComponent
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class GameAppHandlerTest :
    FunSpec({
      test("dagger graph builds and handler registers expected packet types") {
        val config =
            GameServerConfig(
                host = "127.0.0.1",
                port = 0,
                checksumSize = 2,
                rootKeyResource = "game.private.pem",
                sessionSecret = "test-secret".toByteArray(),
            )
        val component = DaggerGameServerComponent.factory().create(config)
        val handler = component.handlerProvider().get()
        handler.isRegistered(JoinPacket::class) shouldBe true
        handler.isRegistered(MovementPacket::class) shouldBe true
        handler.isRegistered(PokemonMovePacket::class) shouldBe true
        handler.isRegistered(KeepAlivePacket::class) shouldBe true
        handler.isRegistered(AddFriendPacket::class) shouldBe true
        handler.isRegistered(RemoveFriendPacket::class) shouldBe true
        handler.isRegistered(BlockPlayerPacket::class) shouldBe true
        handler.isRegistered(DeleteCharacterPacket::class) shouldBe true
        handler.isRegistered(BattleActionPacket::class) shouldBe true
        handler.isRegistered(SendChatCommandPacket::class) shouldBe true
        handler.isRegistered(LinkKickMemberPacket::class) shouldBe true
        handler.isRegistered(CancelSocialInteractionPacket::class) shouldBe true
        handler.isRegistered(RequestSocialProfilePacket::class) shouldBe true
        handler.isRegistered(RegisteredItemPacket::class) shouldBe true
      }
    })
