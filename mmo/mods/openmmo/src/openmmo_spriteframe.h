/*
 * The page a battle draws its mon sprites through.
 * See openmmo_spriteframe.c.
 */
#ifndef OPENMMO_SPRITEFRAME_H
#define OPENMMO_SPRITEFRAME_H

/* One arrangement of the sprite manager's 256x256 texture page: how large a
 * frame is, where each slot's two frames sit, and where the 80x80 shadow block
 * is copied. Positions are page pixels; a slot the layout does not place has
 * x = -1 and is never active while the layout is. */
struct openmmo_spriteframe_layout {
    const char *name;
    int frameW, frameH;
    int slots;
    int pos[4][2][2];
    int shadowX, shadowY;
};

/* The layout a manager draws through; the cartridge's 80x80 for one that never
 * chose (NULL is a manager too). */
const struct openmmo_spriteframe_layout *openmmo_spriteframe_of(const void *man);

/* Choose a manager's layout: wide is the two-slot 128x88, else the cartridge's.
 * Call once, after PokemonSpriteManager_New and before any sprite is created. */
void openmmo_spriteframe_set(void *man, int wide);
void openmmo_spriteframe_free(void *man);

/* What MON_SPRITE_FRAME_WIDTH / HEIGHT read: the frame of the layout in force. */
int openmmo_mon_frame_w(void);
int openmmo_mon_frame_h(void);

/* Texture rectangle of a slot's frame, and of a shadow of one of the four
 * sizes: { u0, v0, u1, v1 }. */
void openmmo_spriteframe_uv(const void *man, int slot, int frame, int uv[4]);
void openmmo_spriteframe_shadow_uv(const void *man, int size, int uv[4]);
/* Byte offset in the page where the 80x80 shadow block is copied. */
unsigned openmmo_spriteframe_shadow_offset(const void *man);

/* Lay a just-buffered sheet into the page for a layout that is not the
 * cartridge's. rawPair is the sheet's decrypted first 160x80 pair. Answers 0
 * for the cartridge's layout, whose own copy code then runs untouched. */
int openmmo_spriteframe_buffer(void *man, int slot, const unsigned char *rawPair);

/* The compositor's full-size picture for a slot (8bpp palette indices,
 * row-major, w x h), set when it composes and cleared when it does not. */
void openmmo_spriteframe_composed_set(int slot, const unsigned char *pix, int w, int h);
int openmmo_spriteframe_composed(int slot, const unsigned char **pix, int *w, int *h);

#endif
