package de.fiereu.openmmo.items

/** Not a data class on purpose. The catalogue instance is the identity, so equality is by it. */
class ItemDef(
    val name: String,
    val price: Int,
    val holdEffect: Int = HOLD_EFFECT_NONE,
    val fieldUse: String = FIELD_USE_NONE,
    val useClass: String = USE_CLASS_NONE,
    val amount: Int = 0,
) {

  /** The Everstone condition: what the game asks a held item before a monster evolves. */
  val preventsEvolution: Boolean
    get() = holdEffect == HOLD_EFFECT_NO_EVOLVE

  /** A Poké Ball and its cousins. The table writes `use_class: "ball"`. */
  val isBall: Boolean
    get() = useClass == USE_CLASS_BALL

  /**
   * Restores hp outside and inside battle. `amount` is the table's heal; 255 means the whole
   * bar (Max Potion, Full Restore).
   */
  val healsHp: Boolean
    get() = fieldUse == FIELD_USE_MEDICINE && amount > 0

  /** How many hp this item restores, or [maxHp] when the table says 255. */
  fun healAmount(maxHp: Int): Int = if (amount >= FULL_HEAL_AMOUNT) maxHp else amount

  override fun toString(): String = name

  companion object {
    const val HOLD_EFFECT_NONE = 0

    /**
     * The DS decomp names its hold effects and the Gen 5 table the item catalogue is built
     * from numbers them, and the two are the same numbering: over the 445 items both tables
     * carry, every one of them agrees.
     */
    const val HOLD_EFFECT_NO_EVOLVE = 64

    const val FIELD_USE_NONE = "none"
    const val FIELD_USE_MEDICINE = "medicine"
    const val USE_CLASS_NONE = "0"
    const val USE_CLASS_BALL = "ball"

    /** The table's value for a full-bar heal (Max Potion, Full Restore). */
    const val FULL_HEAL_AMOUNT = 255
  }
}
