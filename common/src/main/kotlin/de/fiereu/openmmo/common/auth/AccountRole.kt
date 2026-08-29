package de.fiereu.openmmo.common.auth

/**
 * What an account may do beyond playing.
 *
 * These belong to the account, not to a character, so starting a new character keeps them.
 *
 * The order is a ranking: every role carries what the ones before it carry, so a check asks for the
 * least it needs and a developer passes all of them.
 */
enum class AccountRole {
  /** Keeps the peace. Aimed at players rather than at the server. */
  MODERATOR,

  /** Runs the server. */
  ADMIN,

  /** Builds the server. The debug commands. */
  DEVELOPER;

  val bit: Int
    get() = 1 shl ordinal

  companion object {
    /** Reads a role a person typed, for the command line. Null when it is not one. */
    fun parse(text: String): AccountRole? = entries.firstOrNull { it.name.equals(text, true) }

    /** Every name, for a usage line. */
    fun names(): String = entries.joinToString("|") { it.name.lowercase() }
  }
}

/** The roles one account holds, as the bits stored on its row and signed into its join ticket. */
@JvmInline
value class AccountRoles(val mask: Int) {

  /** Whether this account reaches [role], by holding it or by holding one that outranks it. */
  fun has(role: AccountRole): Boolean =
      AccountRole.entries.any { it.ordinal >= role.ordinal && it.bit and mask != 0 }

  /** The roles actually granted, without the ones they imply. */
  fun granted(): List<AccountRole> = AccountRole.entries.filter { it.bit and mask != 0 }

  operator fun plus(role: AccountRole) = AccountRoles(mask or role.bit)

  operator fun minus(role: AccountRole) = AccountRoles(mask and role.bit.inv())

  /** How an operator sees them. */
  override fun toString(): String =
      granted().takeIf { it.isNotEmpty() }?.joinToString(",") { it.name.lowercase() } ?: "none"

  companion object {
    val NONE = AccountRoles(0)

    fun of(vararg roles: AccountRole) = AccountRoles(roles.fold(0) { m, r -> m or r.bit })

    /** Drops bits no role uses, so a stored or signed value cannot mean more than it should. */
    fun ofMask(mask: Int) = AccountRoles(mask and of(*AccountRole.entries.toTypedArray()).mask)
  }
}
