/* Music and sfx volumes on the engine's sequence players. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc_spu.h"   /* pc_spu_set_output: the mixer's own output mode */

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

/* The output RATE, before anybody can read the wrong one. */
static void mixer_output_mode(void)
{
    static const char *const names[] = { "none", "linear", "cosine", "cubic" };
    const char *rate = getenv("PC_AUDIO_RATE");
    const char *interp = getenv("PC_AUDIO_INTERP");
    uint32_t hz = 0;
    enum pc_spu_interp mode = PC_SPU_INTERP_NONE;
    unsigned i;

    if ((rate == NULL || rate[0] == '\0')
        && (interp == NULL || interp[0] == '\0'))
        return;                 /* nothing asked for: the port's own default */

    if (rate != NULL && rate[0] != '\0')
        hz = (uint32_t)strtoul(rate, NULL, 10);
    if (interp != NULL && interp[0] != '\0') {
        for (i = 0; i < sizeof names / sizeof names[0]; i++) {
            if (strcmp(interp, names[i]) == 0) {
                mode = (enum pc_spu_interp)i;
                break;
            }
        }
    }
    if (pc_spu_set_output(hz, mode) != 0) {
        /* Out of range. Left to the port to report and to clamp on its own
         * first advance, which is where that message already lives. */
        return;
    }
    printf("openmmo: mixer output %u Hz, %s\n", (unsigned)pc_spu_rate(),
           names[(unsigned)pc_spu_interp() % 4]);
}

void openmmo_mixer_apply(void)
{
    mixer_output_mode();
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

/* The CHANNELS A carried track plays on. */
#include <nnsys/snd.h>

#define OPENMMO_PORTED_SEQ_FIRST  2133      /* this game's SDAT holds 2133 sequences */
#define OPENMMO_PORTED_BGM_CHANNELS 0xA7FFu /* HeartGold's 0xA7FE, and channel 0 */

void openmmo_bgm_channels_widen(int seqID)
{
    const NNSSndArcSeqInfo *info;

    if (seqID < OPENMMO_PORTED_SEQ_FIRST)
        return;
    info = NNS_SndArcGetSeqInfo(seqID);
    if (info == NULL)
        return;
    NNS_SndPlayerSetAllocatableChannel(info->param.playerNo,
                                       OPENMMO_PORTED_BGM_CHANNELS);
    {
        static int last = -1;

        if (last != seqID) {
            last = seqID;
            printf("openmmo: track %d plays on HeartGold's channels (player %d)\n",
                   seqID, (int)info->param.playerNo);
        }
    }
}
