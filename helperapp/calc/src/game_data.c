/* The decomp's own data tables, compiled in unmodified. Each is pure data
 * (designated initializers referencing only include/constants values), so
 * it builds as-is against the host-side struct redeclarations in
 * emerald_calc.h -- zero transcription, and it can't drift from the ROM. */
#include "emerald_calc.h"

/* include/global.h's versions (that header itself isn't include-safe --
 * see battle_state.h). The game text macro only has to produce a valid
 * array initializer here; nothing reads the Pokedex strings. */
#define _(x) {x}
#define min(a, b) ((a) < (b) ? (a) : (b))

/* src/data/battle_moves.h -- gBattleMoves[MOVES_COUNT]. */
#include "data/battle_moves.h"

/* src/data/pokemon/species_info.h -- gSpeciesInfo[]. */
#include "data/pokemon/species_info.h"

/* include/pokedex.h struct PokedexEntry, for pokedex_entries.h below. */
struct PokedexEntry {
    u8 categoryName[12];
    u16 height;
    u16 weight;
    const u8 *description;
    u16 unused;
    u16 pokemonScale;
    u16 pokemonOffset;
    u16 trainerScale;
    u16 trainerOffset;
};

/* src/data/pokemon/pokedex_text.h is only here because
 * pokedex_entries.h's .description initializers point into it. */
#include "data/pokemon/pokedex_text.h"
#include "data/pokemon/pokedex_entries.h"

/* sSpeciesToNationalPokedexNum isn't in a data header -- it's a static
 * table inside src/pokemon.c, which can't be compiled here. CMake copies
 * its initializer out of pokemon.c verbatim into this generated header
 * (helperapp/CMakeLists.txt), with pokemon.c's own macro for it: */
#define SPECIES_TO_NATIONAL(name) [SPECIES_##name - 1] = NATIONAL_DEX_##name
#include "species_to_national_dex.h"

/* src/pokemon.c */
u16 SpeciesToNationalPokedexNum(u16 species)
{
    if (!species)
        return 0;

    return sSpeciesToNationalPokedexNum[species - 1];
}

/* src/pokedex.c, plus one bounds check the original doesn't have: the
 * unused SPECIES_OLD_UNOWN_B..Z placeholders map to national dex numbers
 * past the end of gPokedexEntries, where the game reads whatever follows
 * the table in ROM. They never appear in a real battle; here they read as
 * 0 instead of out of bounds. */
u16 GetPokedexHeightWeight(u16 dexNum, u8 data)
{
    if (dexNum >= sizeof(gPokedexEntries) / sizeof(gPokedexEntries[0]))
        return 0;

    switch (data)
    {
    case 0: // height
        return gPokedexEntries[dexNum].height;
    case 1: // weight
        return gPokedexEntries[dexNum].weight;
    default:
        return 1;
    }
}
