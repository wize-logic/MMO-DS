package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.dialog.DialogLine
import de.fiereu.openmmo.common.dialog.TextId
import de.fiereu.openmmo.net.game.packets.DialogChoicePacket
import de.fiereu.openmmo.net.game.packets.DialogStatePacket
import de.fiereu.openmmo.net.game.packets.dialog.DialogActionPacket
import de.fiereu.openmmo.net.game.packets.dialog.DialogActionResponsePacket
import de.fiereu.openmmo.net.game.packets.dialog.DialogMessageArg
import de.fiereu.openmmo.server.game.session.PENDING_DIALOG
import de.fiereu.openmmo.server.game.session.PENDING_DIALOG_RESPONSE
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.PlayerState
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.CompletableDeferred

private val log = KotlinLogging.logger {}

internal val CLOSE_DIALOG_ACTION =
    DialogActionPacket(
        flags = 0,
        actionType = 0x64,
        textId = 0,
        entityId = -1,
        contextValue = 0,
        messageArgs = emptyList(),
        detail = ByteArray(0),
    )

data class DialogPresentation(
    val messageArgs: List<DialogMessageArg> = emptyList(),
    val contextValue: Int = 0,
    val detail: ByteArray = byteArrayOf(0),
)

@Singleton
class DialogService @Inject constructor() {

  /** Claim the avatar lock and tell the client, the way the official client's s2c 0x0E does. */
  fun claimLock(session: SessionContext, state: PlayerState) {
    if (state.inDialog) return
    state.inDialog = true
    session.send(DialogStatePacket(true))
  }

  /** Emerald starter picker ROM ids. */
  suspend fun chooseHoennStarter(session: SessionContext, state: PlayerState): Int {
    while (true) {
      val choice =
          showChoiceAndWait(
                  session = session,
                  state = state,
                  textId = HOENN_STARTER_PICK_TEXT,
                  actionType = STARTER_PICK,
                  entityId = NO_ENTITY,
                  presentation =
                      DialogPresentation(
                          contextValue = STARTER_CONTEXT,
                          detail =
                              byteArrayOf(
                                  3,
                                  (TREECKO and 0xFF).toByte(),
                                  (TREECKO shr 8).toByte(),
                                  (TORCHIC and 0xFF).toByte(),
                                  (TORCHIC shr 8).toByte(),
                                  (MUDKIP and 0xFF).toByte(),
                                  (MUDKIP shr 8).toByte(),
                              ),
                      ),
              )
              .unk
      if (choice !in 1..3) continue

      val accepted =
          showChoiceAndWait(
                  session = session,
                  state = state,
                  textId = HOENN_STARTER_CONFIRM_TEXT,
                  actionType = YES_NO,
                  entityId = NO_ENTITY,
                  presentation = DialogPresentation(contextValue = STARTER_CONTEXT),
              )
              .unk != 0
      if (accepted) return listOf(TREECKO, TORCHIC, MUDKIP)[choice - 1]
    }
  }

  /**
   * Platinum starter picker ROM ids. The wire is the same 0x23 the Emerald one uses, a count and
   * that many species ids, which the client looks up in its own species-name bank, so the only
   * Sinnoh things here are the three species and the text.
   */
  suspend fun chooseSinnohStarter(session: SessionContext, state: PlayerState): Int {
    val species = listOf(TURTWIG, CHIMCHAR, PIPLUP)
    val confirmText =
        listOf(
            SINNOH_STARTER_CONFIRM_TURTWIG,
            SINNOH_STARTER_CONFIRM_CHIMCHAR,
            SINNOH_STARTER_CONFIRM_PIPLUP)
    while (true) {
      val choice =
          showChoiceAndWait(
                  session = session,
                  state = state,
                  textId = SINNOH_STARTER_PICK_TEXT,
                  actionType = STARTER_PICK,
                  entityId = NO_ENTITY,
                  presentation =
                      DialogPresentation(
                          contextValue = STARTER_CONTEXT,
                          detail =
                              byteArrayOf(species.size.toByte()) +
                                  species.fold(ByteArray(0)) { acc, id ->
                                    acc + byteArrayOf((id and 0xFF).toByte(), (id shr 8).toByte())
                                  },
                      ),
              )
              .unk
      if (choice !in 1..species.size) continue

      val accepted =
          showChoiceAndWait(
                  session = session,
                  state = state,
                  textId = confirmText[choice - 1],
                  actionType = YES_NO,
                  entityId = NO_ENTITY,
                  presentation = DialogPresentation(contextValue = STARTER_CONTEXT),
              )
              .unk != 0
      if (accepted) return species[choice - 1]
    }
  }

  /** Show a ROM-backed yes/no box and return true for YES. */
  suspend fun askYesNo(
      session: SessionContext,
      state: PlayerState,
      textId: Int,
      entityId: Long,
      messageArgs: List<DialogMessageArg> = emptyList(),
  ): Boolean =
      showChoiceAndWait(
              session,
              state,
              textId,
              YES_NO,
              entityId,
              DialogPresentation(messageArgs = messageArgs),
          )
          .unk != 0

  /**
   * Show a ROM-backed text list and return the 0-based index the client picked. Cancel is
   * [entries.size].
   */
  suspend fun askList(
      session: SessionContext,
      state: PlayerState,
      prompt: DialogLine,
      entries: List<DialogLine>,
      entityId: Long,
      messageArgs: List<DialogMessageArg> = emptyList(),
  ): Int {
    return showChoiceAndWait(
            session,
            state,
            prompt.textId,
            TEXT_LIST,
            entityId,
            DialogPresentation(messageArgs = messageArgs, detail = listMenuDetail(entries)),
        )
        .unk
  }

  /** Send a text list without waiting. Used by `/list` on the chat handler. */
  fun showList(
      session: SessionContext,
      state: PlayerState,
      prompt: DialogLine,
      entries: List<DialogLine>,
      entityId: Long,
  ) {
    show(
        session,
        state,
        prompt.textId,
        TEXT_LIST,
        entityId,
        DialogPresentation(detail = listMenuDetail(entries)),
    )
  }

  /**
   * Sends a dialog box without waiting for the client's reply. Used when the box is only being
   * shown (the advance lives on [showAndWait]).
   */
  fun show(
      session: SessionContext,
      state: PlayerState,
      textId: Int,
      actionType: Int,
      entityId: Long,
      presentation: DialogPresentation = DialogPresentation(),
  ) {
    val seq = state.dialogSeqId
    state.dialogSeqId = seq + 1
    log.debug {
      "Send dialog box seq=$seq actionType=$actionType textId=0x${textId.toString(16)} entity=$entityId"
    }
    session.send(
        DialogActionPacket(
            flags = seq.toByte(),
            actionType = actionType.toByte(),
            textId = textId,
            entityId = entityId,
            contextValue = presentation.contextValue,
            messageArgs = presentation.messageArgs,
            detail = presentation.detail,
        ))
  }

  /**
   * Shows a dialog box and waits for the player to advance or close it. [actionType] is 3 for a
   * sign and 4 for an npc box, [entityId] is the speaking npc or -1.
   */
  suspend fun showAndWait(
      session: SessionContext,
      state: PlayerState,
      textId: Int,
      actionType: Int,
      entityId: Long,
      presentation: DialogPresentation = DialogPresentation(),
  ) {
    val advance = CompletableDeferred<Unit>()
    session.attributes[PENDING_DIALOG] = advance
    val seq = state.dialogSeqId
    state.dialogSeqId = seq + 1
    claimLock(session, state)
    state.dialogNpcEntityId = entityId
    log.debug {
      "Send dialog box seq=$seq actionType=$actionType textId=0x${textId.toString(16)} entity=$entityId"
    }
    session.send(
        DialogActionPacket(
            flags = seq.toByte(),
            actionType = actionType.toByte(),
            textId = textId,
            entityId = entityId,
            contextValue = presentation.contextValue,
            messageArgs = presentation.messageArgs,
            detail = presentation.detail,
        ))
    advance.await()
  }

  /** Show a scene page and wait for the client's 0x21 acknowledgement. */
  suspend fun showScenePageAndWait(
      session: SessionContext,
      state: PlayerState,
      textId: Int,
      actionType: Int,
      contextValue: Int,
      messageArgs: List<DialogMessageArg>,
  ) =
      showAndWait(
          session,
          state,
          textId,
          actionType,
          NO_ENTITY,
          DialogPresentation(messageArgs, contextValue, ByteArray(0)),
      )

  /** Show a scene menu and return its 1-based choice. */
  suspend fun showSceneMenuAndWait(
      session: SessionContext,
      state: PlayerState,
      detail: ByteArray,
  ): Int =
      showChoiceAndWait(
              session,
              state,
              textId = 0,
              actionType = STARTER_PICK,
              entityId = NO_ENTITY,
              presentation = DialogPresentation(detail = detail),
          )
          .unk

  /** Closes the dialog once a script has shown its last box. */
  fun close(session: SessionContext, state: PlayerState) {
    session.attributes.remove(PENDING_DIALOG)
    session.attributes.remove(PENDING_DIALOG_RESPONSE)
    if (state.inDialog) {
      session.send(DialogStatePacket(false))
      state.inDialog = false
      state.dialogNpcEntityId = 0
    }
  }

  fun onInteractive(event: PacketEvent<DialogActionResponsePacket>) {
    val session = event.session
    val response = session.attributes.remove(PENDING_DIALOG_RESPONSE)
    if (response != null) {
      response.complete(event.packet)
      return
    }
    val advance = session.attributes.remove(PENDING_DIALOG)
    log.debug { "Dialog response id=${event.packet.id} advancing=${advance != null}" }
    if (advance != null) {
      // A script is waiting on this box, let it move on to its next line.
      advance.complete(Unit)
      return
    }
    // No script is driving this dialog, just close whatever is open.
    val state = session.attributes[PLAYER_STATE] ?: return
    if (state.inDialog) {
      session.send(DialogStatePacket(false))
      state.inDialog = false
      state.dialogNpcEntityId = 0
    }
  }

  fun onDialogChoice(event: PacketEvent<DialogChoicePacket>) {
    val session = event.session
    log.info { "Dialog choice received: unk1=${event.packet.unk1}, unk2=${event.packet.unk2}" }
    val advance = session.attributes.remove(PENDING_DIALOG)
    if (advance != null) {
      advance.complete(Unit)
      return
    }
    val state = session.attributes[PLAYER_STATE] ?: return
    if (state.inDialog) {
      session.send(DialogStatePacket(false))
      state.inDialog = false
      state.dialogNpcEntityId = 0
    }
  }

  private suspend fun showChoiceAndWait(
      session: SessionContext,
      state: PlayerState,
      textId: Int,
      actionType: Int,
      entityId: Long,
      presentation: DialogPresentation = DialogPresentation(),
  ): DialogActionResponsePacket {
    val response = CompletableDeferred<DialogActionResponsePacket>()
    session.attributes[PENDING_DIALOG_RESPONSE] = response
    val seq = state.dialogSeqId
    state.dialogSeqId = seq + 1
    claimLock(session, state)
    state.dialogNpcEntityId = entityId
    session.send(
        DialogActionPacket(
            flags = seq.toByte(),
            actionType = actionType.toByte(),
            textId = textId,
            entityId = entityId,
            contextValue = presentation.contextValue,
            messageArgs = presentation.messageArgs,
            detail = presentation.detail,
        ))
    return response.await()
  }

  /**
   * the official client `qM1.b` / iq1 case 9: two unused bytes, S16LE bank, U8 count, S16LE
   * entries.
   */
  private fun listMenuDetail(entries: List<DialogLine>): ByteArray {
    require(entries.isNotEmpty()) { "a text list needs at least one row" }
    require(entries.size <= LIST_MENU_MAX) { "a text list is at most $LIST_MENU_MAX rows" }
    val bank = TextId.bankOf(entries.first().textId)
    require(entries.all { TextId.bankOf(it.textId) == bank }) {
      "a text list's rows must share one text bank"
    }
    val out = ByteArray(5 + entries.size * 2)
    out[0] = 0
    out[1] = 0
    out[2] = (bank and 0xFF).toByte()
    out[3] = ((bank shr 8) and 0xFF).toByte()
    out[4] = entries.size.toByte()
    entries.forEachIndexed { i, line ->
      val entry = TextId.entryOf(line.textId)
      out[5 + i * 2] = (entry and 0xFF).toByte()
      out[6 + i * 2] = ((entry shr 8) and 0xFF).toByte()
    }
    return out
  }

  private companion object {
    const val NO_ENTITY = -1L
    const val YES_NO = 0x05
    const val STARTER_PICK = 0x23
    const val TEXT_LIST = 0x31
    const val LIST_MENU_MAX = 32
    const val STARTER_CONTEXT = 700

    const val TREECKO = 252
    const val TORCHIC = 255
    const val MUDKIP = 258

    const val TURTWIG = 387
    const val CHIMCHAR = 390
    const val PIPLUP = 393

    // Verified against the captured Emerald dialog database.
    const val HOENN_STARTER_PICK_TEXT = 0x105E8C53
    const val HOENN_STARTER_CONFIRM_TEXT = 0x105E8C90

    // Bank 360 of the DS text archive, which is the choose-a-starter scene's own bank: message 7
    // asks which it will be and messages 1-3 confirm one Pokemon each.
    const val SINNOH_STARTER_PICK_TEXT = 0x31680007
    const val SINNOH_STARTER_CONFIRM_TURTWIG = 0x31680001
    const val SINNOH_STARTER_CONFIRM_CHIMCHAR = 0x31680002
    const val SINNOH_STARTER_CONFIRM_PIPLUP = 0x31680003
  }
}
