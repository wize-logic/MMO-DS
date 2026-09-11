package de.fiereu.network.coroutines

import de.fiereu.network.HandlerRegistrationException
import de.fiereu.network.PacketEvent
import de.fiereu.network.Protocol
import de.fiereu.network.Side
import de.fiereu.network.TypedProtocolHandler
import io.github.oshai.kotlinlogging.KotlinLogging
import io.netty.channel.ChannelHandlerContext
import kotlin.reflect.KClass
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull

private val log = KotlinLogging.logger {}

/**
 * How long the mailbox may take to finish what is already in it once the channel has gone. Long
 * enough for a database write, short enough that a handler which will never finish is noticed.
 */
private const val DRAIN_TIMEOUT_MS = 5_000L

abstract class CoroutineProtocolHandler<P : Protocol>(
    protocol: P,
    side: Side,
    private val coroutineScope: CoroutineScope,
) : TypedProtocolHandler<P>(protocol, side) {

  private val suspendHandlers = mutableMapOf<KClass<*>, suspend (PacketEvent<*>) -> Unit>()

  private val mailbox: Channel<PacketEvent<*>> = Channel(Channel.UNLIMITED)

  @Volatile private var consumerJob: Job? = null

  protected inline fun <reified T : Any> onSuspend(
      noinline handler: suspend (PacketEvent<T>) -> Unit,
  ) {
    registerSuspend(T::class, handler)
  }

  @PublishedApi
  internal fun <T : Any> registerSuspend(
      type: KClass<T>,
      handler: suspend (PacketEvent<T>) -> Unit,
  ) {
    if (isRegistered(type) || suspendHandlers.containsKey(type)) {
      throw HandlerRegistrationException(
          "Duplicate handler registration for ${type.simpleName}",
      )
    }
    @Suppress("UNCHECKED_CAST")
    suspendHandlers[type] = handler as suspend (PacketEvent<*>) -> Unit
  }

  override fun handlerAdded(ctx: ChannelHandlerContext) {
    super.handlerAdded(ctx)
    consumerJob =
        coroutineScope.launch {
          for (event in mailbox) {
            val handler = suspendHandlers[event.packet::class] ?: continue
            try {
              handler(event)
            } catch (t: Throwable) {
              log.warn(t) { "Suspend handler for ${event.packet::class.simpleName} threw" }
              ctx.executor().execute { onError(t) }
            }
          }
        }
  }

  /** Let the mailbox finish what is already in it. */
  override fun handlerRemoved(ctx: ChannelHandlerContext) {
    mailbox.close()
    val job = consumerJob ?: return
    consumerJob = null
    coroutineScope.launch {
      if (withTimeoutOrNull(DRAIN_TIMEOUT_MS) { job.join() } == null) {
        log.warn { "Packet handlers did not finish within $DRAIN_TIMEOUT_MS ms; cancelling" }
        job.cancel()
      }
    }
  }

  override fun isRegistered(type: KClass<*>): Boolean =
      super.isRegistered(type) || suspendHandlers.containsKey(type)

  final override fun tryHandleAlternate(event: PacketEvent<*>): Boolean {
    if (event.packet::class !in suspendHandlers) return false
    val result = mailbox.trySend(event)
    if (!result.isSuccess) {
      log.warn { "Mailbox rejected packet ${event.packet::class.simpleName}" }
      onError(IllegalStateException("Coroutine mailbox closed"))
    }
    return true
  }
}
