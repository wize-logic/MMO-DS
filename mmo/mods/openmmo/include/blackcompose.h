/* A Gen 5 battle sprite, composed. */
#ifndef MMO_BLACKCOMPOSE_H
#define MMO_BLACKCOMPOSE_H

typedef unsigned char mmo_bc_u8;
typedef unsigned short mmo_bc_u16;
typedef short mmo_bc_s16;
typedef unsigned int mmo_bc_u32;
typedef int mmo_bc_s32;

/* The short names, unless something already spelled them. The plugin defines
 * OPENMMO_BLACKANIM_TYPES and mmo/include/mmo.h (MMO_H) declares the same five
 * off <stdint.h>; the app build has both this header and that one on its path,
 * and a second typedef of the same type is a warning on the NDK's clang. */
#if !defined(OPENMMO_BLACKANIM_TYPES) && !defined(MMO_H)
#define OPENMMO_BLACKANIM_TYPES
typedef mmo_bc_u8 u8;
typedef mmo_bc_u16 u16;
typedef mmo_bc_s16 s16;
typedef mmo_bc_u32 u32;
typedef mmo_bc_s32 s32;
#endif

/* The cartridge's origin sits at the centre of a canvas this big. */
#define CANVAS      320
#define ORIGIN      160
#define MAP_PITCH   32     /* 2D OBJ mapping: character name = row * 32 + column */
#define FX32_ONE    4096
#define MAX_PERIOD  4096
#define BBOX_SAMPLES 128
#define NODE_RESET  0      /* NNSG2dMCAnimationPlayMode: restart at each change */
/* The cartridge's own frame, and the widest a layout draws through (a single
 * battle's, openmmo_spriteframe.c). The fill measures the height byte against
 * the wide one and bakes into the small one; see mmo_black_fit. */
#define BC_FRAME       80
#define BC_FRAME_MAX_W 128
#define BC_FRAME_MAX_H 88

struct oam {
    s16 x, y;
    u8 w, h;
    u16 chr;
    u8 hf, vf;
};

struct cell {
    u32 first, n;
};

struct aframe {
    u16 index, duration;
    s32 sx, sy;
    u16 rot;
    s16 px, py;
};

struct seq {
    u32 first, n;
    u16 loop, mode;
};

struct node {
    u16 seq;
    s16 x, y;
    u16 attr;
};

struct mcell {
    u32 first, n;
};

struct face {
    int character;                   /* the pl_pokegra member this stands for; -1 = nothing */
    int loaded;
    u8 *map;                         /* 4bpp linear, width = cols * 8 */
    u32 cols, rows;
    struct oam *oams;   u32 noams;
    struct cell *cells; u32 ncells;
    struct aframe *cframes; u32 ncframes;
    struct seq *cseqs;  u32 ncseqs;
    struct node *nodes; u32 nnodes;
    struct mcell *mcs;  u32 nmcs;
    struct aframe *mframes; u32 nmframes;
    struct seq *mseqs;  u32 nmseqs;
    u16 pal[16];
    int x0, y0, w, h;                /* the loop's bounding box in cartridge coordinates */
    double factor;                   /* DOUBLE, and the fill is why: two of a sprite's
                                      * sixteen colours can sit all but exactly the same
                                      * distance from an averaged pixel (Ivysaur's back
                                      * has a pair 0.0004 apart in 25), and in float the
                                      * bake and tools/portsprites.py chose differently
                                      * on 332 pixels of four faces. The tool's answer is
                                      * the oracle for this whole fill, so the arithmetic
                                      * under it is the tool's too. */
    int left, top;
    int fw, fh;                      /* the frame place() last laid the loop out for */
    int back;
    int byte, hasByte;               /* the height byte: blank rows under the resting pose */
    int seat;                        /* the first frame row under the resting feet */
    int refy, refh, boxed;           /* the resting keyframe, and whether the loop's
                                      * box has been measured for this face yet */
    int held;                        /* a partial draw is on and the pose for it is chosen */
    u32 period, t;
    u8 *change;                      /* one byte a tick: the picture can change here */
};

/* Where compose() draws, and the box of it that it actually set, the
 * compositor already knows where it wrote, so nothing has to scan the canvas
 * back to find out. */
extern u8 mmo_black_canvas[CANVAS * CANVAS];
extern int mmo_black_minx, mmo_black_miny, mmo_black_maxx, mmo_black_maxy;

/* Each returns 1 on a bank it understands and 0 on one it does not. The face
 * owns the allocations; the caller frees them. */
int mmo_black_read_charmap(struct face *f, const u8 *blob, u32 len);
int mmo_black_read_cells(struct face *f, const u8 *blob, u32 len);
int mmo_black_read_anims(const u8 *blob, u32 len, struct seq **out_seqs,
                         u32 *out_nseqs, struct aframe **out_frames,
                         u32 *out_nframes);
int mmo_black_read_multicells(struct face *f, const u8 *blob, u32 len);

/* The picture at tick `t`, into mmo_black_canvas. */
void mmo_black_compose(const struct face *f, u32 t);

/* And the placement, which is the other half both callers share. */
void mmo_black_marks(struct face *f);
void mmo_black_box(struct face *f, const u32 *ticks, u32 nticks);
void mmo_black_fit(struct face *f, int fw, int fh);
void mmo_black_place(struct face *f, int fw, int fh);
void mmo_black_frame(const struct face *f, u8 *out);

/* The height byte a boxed loop earns: the resting pose's dip at the scale the
 * wide frame gives it. What the fill writes into height.narc. */
int mmo_black_height_byte(const struct face *f);

#endif
