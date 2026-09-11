/* A ported species' cry, played as the wave it is. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sound.h"
#include "sound_system.h"
#include "sound_playback.h"
#include "constants/heap.h"

#include "../../../include/endpoint.h"
#include "../../../include/species_port.h"

#define CRIES_MAGIC "OCRY"

struct cry_row {
    unsigned short species;
    unsigned short rate;
    unsigned int samples;
    unsigned int offset;
    unsigned int pad;
};

static FILE *s_file;
static struct cry_row *s_rows;
static int s_nrows = -1; /* -1: not looked yet; 0: looked, nothing there */
/*
 * GUEST-ADDRESSED ON PURPOSE, sized for the largest cry the pack holds (46,968 bytes
 * measured): SOUNDxSAD keeps 27 bits, so a wave the SPU plays must live at a guest address
 * (pc_spu.c, decision 17).
 */
#define CRY_BUFFER_SAMPLES 24576
static short *s_pcm;

static void bind_once(void)
{
    const char *dir = getenv("PC_MODS_DIR");
    char path[1024];
    unsigned char head[8];
    int n;

    if (s_nrows >= 0) {
        return;
    }
    s_nrows = 0;
    if (dir == NULL || dir[0] == '\0') {
        return;
    }
    snprintf(path, sizeof path, "%s/imports/cries.bin", dir);
    s_file = fopen(path, "rb");
    if (s_file == NULL) {
        printf("openmmo: no ported cries (%s); species past %d keep the "
               "engine's stand-in\n", path, mmo_species_port_max());
        return;
    }
    if (fread(head, 1, sizeof head, s_file) != sizeof head
            || memcmp(head, CRIES_MAGIC, 4) != 0) {
        fclose(s_file);
        s_file = NULL;
        printf("openmmo: cries.bin is not a cry pack; ignored\n");
        return;
    }
    n = head[4] | (head[5] << 8);
    s_rows = malloc(sizeof *s_rows * (unsigned)n);
    if (s_rows == NULL
            || fread(s_rows, sizeof *s_rows, (size_t)n, s_file) != (size_t)n) {
        free(s_rows);
        s_rows = NULL;
        fclose(s_file);
        s_file = NULL;
        return;
    }
    {
        extern void *pc_guest_window_alloc(unsigned int bytes, const char *what);

        s_pcm = pc_guest_window_alloc(CRY_BUFFER_SAMPLES * 2u, "ported cries");
    }
    if (s_pcm == NULL) {
        s_nrows = 0;
        printf("openmmo: no guest room for a cry buffer; cries stay the stand-in\n");
        return;
    }
    s_nrows = n;
    printf("openmmo: %d ported cries bound\n", n);
}

/* The debug door the other prompts have: OPENMMO_CRY=<species> plays that
 * cry once the field settles, so a headless run can put a cry on the ring
 * with nobody in a battle. A vanilla species goes through the engine's own
 * sequence path, which is the positive control. */
void openmmo_cries_debug_tick(int settled)
{
    static int done;
    const char *want = openmmo_dev_env("OPENMMO_CRY");
    int species;

    if (done || !settled || want == NULL || want[0] == '\0')
        return;
    done = 1;
    species = atoi(want);
    printf("openmmo: debug cry %d\n", species);
    Sound_PlayPokemonCryEx(POKECRY_NORMAL, (u16)species, 0, 127,
                           HEAP_ID_FIELD2, 0);
}

/* Play the fill's cry for a species past the engine's shelf. 1 when the wave
 * is on its way; 0 sends the caller back to its own clamp. */
int openmmo_cry_play(int species, int pan, int volume)
{
    const struct cry_row *row = NULL;
    WaveOutParam param;
    u8 *primary;
    /* The engine asks with its own id, Victini and Snivy live at 650 and
     * 651 past the egg slots, and the pack is keyed by the wire's, which
     * is also Black's own shelf order. One map exists for exactly this. */
    int wire = mmo_species_port_wire_id(species);
    int i;

    bind_once();
    for (i = 0; i < s_nrows; i++) {
        if (s_rows[i].species == wire) {
            row = &s_rows[i];
            break;
        }
    }
    if (row == NULL || s_file == NULL) {
        if (s_nrows > 0)
            printf("openmmo: no ported cry for species %d (wire %d)\n", species, wire);
        return 0;
    }
    if (row->samples > (unsigned)CRY_BUFFER_SAMPLES) {
        printf("openmmo: cry %d is %u samples, past the %u this buffer holds\n",
               species, row->samples, (unsigned)CRY_BUFFER_SAMPLES);
        return 0;
    }
    if (fseek(s_file, (long)row->offset, SEEK_SET) != 0
            || fread(s_pcm, 2, row->samples, s_file) != row->samples) {
        return 0;
    }

    /* The channel the recorded Chatot cry uses, taken the way
     * Sound_PlayPokemonCryEx takes it: stop and free whatever holds it. */
    primary = SoundSystem_GetParam(SOUND_SYSTEM_PARAM_WAVE_OUT_PRIMARY_ALLOCATED);
    if (*primary == TRUE) {
        Sound_StopWaveOut(WAVE_OUT_CHANNEL_PRIMARY);
        Sound_FreeWaveOutChannel(WAVE_OUT_CHANNEL_PRIMARY);
    }
    if (Sound_AllocateWaveOutChannel(WAVE_OUT_CHANNEL_PRIMARY) == FALSE) {
        printf("openmmo: cry %d refused a wave-out channel\n", species);
        return 0;
    }

    param.handle = Sound_GetWaveOutHandle(WAVE_OUT_CHANNEL_PRIMARY);
    param.format = NNS_SND_WAVE_FORMAT_PCM16;
    param.data = s_pcm;
    param.loop = FALSE;
    param.loopStartSample = 0;
    param.samples = (int)row->samples;
    param.sampleRate = row->rate;
    param.volume = volume;
    param.speed = WAVE_OUT_SPEED(1.0);
    param.pan = WAVE_OUT_PAN_CENTER + (pan / 2);

    if (Sound_PlayWaveOut(&param, WAVE_OUT_CHANNEL_PRIMARY) == FALSE) {
        printf("openmmo: cry %d would not start\n", species);
        return 0;
    }
    Sound_SetWaveOutVolume(WAVE_OUT_CHANNEL_PRIMARY, volume);
    printf("openmmo: cry %d as a wave, %u samples at %u Hz (pan %d vol %d)\n",
           species, row->samples, row->rate, pan, volume);
    return 1;
}
