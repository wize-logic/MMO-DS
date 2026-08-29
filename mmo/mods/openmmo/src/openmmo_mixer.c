/* Music and sfx volumes on the engine's sequence players. */

#include <stdlib.h>

#include "sound.h"
#include "constants/sound_volume.h"

#define MIX_PCT_MAX 100

static int sMusic = -1; /* 0..SOUND_VOLUME_MAX, -1 unread */
static int sSfx   = -1;

static int pct_from_env(const char *e)
{
    char *end;
    long n;

    if (e == NULL || e[0] == '\0')
        return MIX_PCT_MAX;
    n = strtol(e, &end, 10);
    if (end == e || *end != '\0')
        return MIX_PCT_MAX;
    if (n < 0)
        n = 0;
    if (n > MIX_PCT_MAX)
        n = MIX_PCT_MAX;
    return (int)n;
}

static int pct_to_volume(int pct)
{
    return (pct * SOUND_VOLUME_MAX) / MIX_PCT_MAX;
}

static void mix_load(void)
{
    if (sMusic >= 0)
        return;
    sMusic = pct_to_volume(pct_from_env(getenv("OPENMMO_MUSIC")));
    sSfx   = pct_to_volume(pct_from_env(getenv("OPENMMO_SFX")));
}

static int mix_is_music(int playerID)
{
    return playerID == PLAYER_FIELD
        || playerID == PLAYER_BGM
        || playerID == PLAYER_ME;
}

void openmmo_mix_player_volume(int playerID, int volume)
{
    int scale;

    mix_load();
    if (volume < SOUND_VOLUME_MIN)
        volume = SOUND_VOLUME_MIN;
    if (volume > SOUND_VOLUME_MAX)
        volume = SOUND_VOLUME_MAX;
    scale = mix_is_music(playerID) ? sMusic : sSfx;
    NNS_SndPlayerSetPlayerVolume(playerID,
        (volume * scale) / SOUND_VOLUME_MAX);
}

/*
 * Every player to zero, for the ended-session notice: the field keeps ticking behind it (the
 * Android park never leaves the process), and town music under CONNECTION LOST reads like the
 * app has not noticed.
 */
void openmmo_mixer_hush(void)
{
    static const int players[] = {
        PLAYER_PV, PLAYER_FIELD, PLAYER_ME, PLAYER_SE_1,
        PLAYER_SE_2, PLAYER_SE_3, PLAYER_SE_4, PLAYER_BGM,
    };
    unsigned i;

    for (i = 0; i < sizeof players / sizeof players[0]; i++)
        NNS_SndPlayerSetPlayerVolume(players[i], 0);
}

void openmmo_mixer_apply(void)
{
    mix_load();
    openmmo_mix_player_volume(PLAYER_PV,    SOUND_VOLUME_MAX);
    openmmo_mix_player_volume(PLAYER_FIELD, SOUND_VOLUME_MAX);
    openmmo_mix_player_volume(PLAYER_ME,    SOUND_VOLUME_MAX);
    openmmo_mix_player_volume(PLAYER_SE_1,  SOUND_VOLUME_MAX);
    openmmo_mix_player_volume(PLAYER_SE_2,  SOUND_VOLUME_MAX);
    openmmo_mix_player_volume(PLAYER_SE_3,  SOUND_VOLUME_MAX);
    openmmo_mix_player_volume(PLAYER_SE_4,  SOUND_VOLUME_MAX);
    openmmo_mix_player_volume(PLAYER_BGM,   SOUND_VOLUME_MAX);
}
