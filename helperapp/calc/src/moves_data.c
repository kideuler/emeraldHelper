/* Pulls in the real, unmodified move table verbatim -- see
 * emerald_calc.h's header comment for why struct BattleMove is redeclared
 * there instead of using include/pokemon.h's. src/data/battle_moves.h is
 * pure data (designated initializers referencing only the constants
 * #included by emerald_calc.h), so it compiles as-is against that
 * redeclaration with zero transcription. */
#include "emerald_calc.h"

#include "data/battle_moves.h"
