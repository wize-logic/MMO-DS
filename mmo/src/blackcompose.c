/* See the header. Moved out of the plugin unchanged, so the
 * fill and the live compositor cannot drift into composing differently. */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "blackcompose.h"

u8 mmo_black_canvas[CANVAS * CANVAS];
int mmo_black_minx, mmo_black_miny, mmo_black_maxx, mmo_black_maxy;

/* The plugin's own names for these, so the bodies below read as they did. */
#define s_canvas mmo_black_canvas
#define s_cminx  mmo_black_minx
#define s_cminy  mmo_black_miny
#define s_cmaxx  mmo_black_maxx
#define s_cmaxy  mmo_black_maxy

static u8 s_node[CANVAS * CANVAS];

/* --------------------------------------------------------------- readers */

static u16 rd16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u32 rd32(const u8 *p) { return (u32)(p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24); }

/* A Nitro resource: 16-byte file header, then sections of name, size, body. */
static const u8 *section(const u8 *blob, u32 len, const char *magic, u32 *out_size)
{
    u32 off = 16;

    while (off + 8 <= len) {
        u32 size = rd32(blob + off + 4);

        if (size < 8 || off + size > len)
            return NULL;
        if (memcmp(blob + off, magic, 4) == 0) {
            *out_size = size - 8;
            return blob + off + 8;
        }
        off += size;
    }
    return NULL;
}

static int read_charmap(struct face *f, const u8 *blob, u32 len)
{
    u32 size, dsize, doff;
    const u8 *s = section(blob, len, "RAHC", &size);

    if (s == NULL || size < 24)
        return 0;
    f->rows = rd16(s);
    f->cols = rd16(s + 2);
    if (rd32(s + 4) != 3 || !(rd32(s + 12) & 1))
        return 0;                           /* not a linear 4bpp bitmap */
    dsize = rd32(s + 16);
    doff = rd32(s + 20);
    if (doff > size || dsize > size - doff || dsize < f->cols * 4 * f->rows * 8)
        return 0;
    f->map = malloc(dsize);
    if (f->map == NULL)
        return 0;
    memcpy(f->map, s + doff, dsize);
    return 1;
}

static const u8 k_shape_w[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
static const u8 k_shape_h[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };

static int read_cells(struct face *f, const u8 *blob, u32 len)
{
    u32 size, count, attr, array, mapping, csize, heads, oams_at, i, total = 0;
    const u8 *s = section(blob, len, "KBEC", &size);

    if (s == NULL || size < 12)
        return 0;
    count = rd16(s);
    attr = rd16(s + 2);
    array = rd32(s + 4);
    mapping = rd32(s + 8);
    if (mapping != 4)
        return 0;                           /* this reads 2D character mapping only */
    csize = (attr & 1) ? 16 : 8;
    heads = array;
    oams_at = heads + count * csize;
    if (oams_at > size)
        return 0;
    f->cells = calloc(count ? count : 1, sizeof *f->cells);
    if (f->cells == NULL)
        return 0;
    for (i = 0; i < count; i++)
        total += rd16(s + heads + i * csize);
    f->oams = calloc(total ? total : 1, sizeof *f->oams);
    if (f->oams == NULL)
        return 0;
    f->ncells = count;
    f->noams = 0;
    for (i = 0; i < count; i++) {
        u32 n = rd16(s + heads + i * csize);
        u32 off = rd32(s + heads + i * csize + 4);
        u32 j;

        f->cells[i].first = f->noams;
        f->cells[i].n = 0;
        if (oams_at + off + n * 6 > size)
            continue;
        for (j = 0; j < n; j++) {
            const u8 *o = s + oams_at + off + j * 6;
            u32 a0 = rd16(o), a1 = rd16(o + 2), a2 = rd16(o + 4);
            struct oam *m = &f->oams[f->noams++];
            int y = (int)(a0 & 0xFF), x = (int)(a1 & 0x1FF);
            u32 shape = (a0 >> 14) & 3, sz = (a1 >> 14) & 3;
            u32 affine = (a0 >> 8) & 1, dbl = (a0 >> 9) & 1;

            if (y & 0x80)
                y -= 256;
            if (x & 0x100)
                x -= 512;
            if (shape > 2)
                shape = 0;
            m->w = k_shape_w[shape][sz];
            m->h = k_shape_h[shape][sz];
            m->chr = (u16)(a2 & 0x3FF);
            /* Under the affine bit attr1 bits 9..13 are the affine index, never
             * flips; a double-size OBJ stores the corner of its doubled box. */
            m->hf = affine ? 0 : (u8)((a1 >> 12) & 1);
            m->vf = affine ? 0 : (u8)((a1 >> 13) & 1);
            if (affine && dbl) {
                x += m->w / 2;
                y += m->h / 2;
            }
            m->x = (s16)x;
            m->y = (s16)y;
            f->cells[i].n++;
        }
    }
    return 1;
}

static int read_anims(const u8 *blob, u32 len, struct seq **out_seqs, u32 *out_nseqs,
                      struct aframe **out_frames, u32 *out_nframes)
{
    u32 size, count, seq_at, frame_at, content_at, i, total = 0, at = 0;
    const u8 *s = section(blob, len, "KNBA", &size);
    struct seq *seqs;
    struct aframe *frames;

    if (s == NULL || size < 16)
        return 0;
    count = rd16(s);
    seq_at = rd32(s + 4);
    frame_at = rd32(s + 8);
    content_at = rd32(s + 12);
    if (seq_at + count * 16 > size)
        return 0;
    for (i = 0; i < count; i++)
        total += rd16(s + seq_at + i * 16);
    seqs = calloc(count ? count : 1, sizeof *seqs);
    frames = calloc(total ? total : 1, sizeof *frames);
    if (seqs == NULL || frames == NULL) {
        free(seqs);
        free(frames);
        return 0;
    }
    for (i = 0; i < count; i++) {
        const u8 *q = s + seq_at + i * 16;
        u32 n = rd16(q), element = rd32(q + 4) & 0xFFFF, foff = rd32(q + 12), j;

        seqs[i].loop = rd16(q + 2);
        seqs[i].mode = (u16)rd32(q + 8);
        seqs[i].first = at;
        seqs[i].n = 0;
        if (frame_at + foff + n * 8 > size)
            continue;
        for (j = 0; j < n; j++) {
            const u8 *fr = s + frame_at + foff + j * 8;
            u32 content = rd32(fr);
            const u8 *c = s + content_at + content;
            struct aframe *a = &frames[at];

            if (content_at + content + 2 > size)
                break;
            a->duration = rd16(fr + 4);
            a->index = rd16(c);
            a->sx = a->sy = FX32_ONE;
            a->rot = 0;
            a->px = a->py = 0;
            if (element == 1 && content_at + content + 16 <= size) {
                a->rot = rd16(c + 2);
                a->sx = (s32)rd32(c + 4);
                a->sy = (s32)rd32(c + 8);
                a->px = (s16)rd16(c + 12);
                a->py = (s16)rd16(c + 14);
            } else if (element == 2 && content_at + content + 8 <= size) {
                a->px = (s16)rd16(c + 4);
                a->py = (s16)rd16(c + 6);
            }
            at++;
            seqs[i].n++;
        }
    }
    *out_seqs = seqs;
    *out_nseqs = count;
    *out_frames = frames;
    *out_nframes = at;
    return 1;
}

static int read_multicells(struct face *f, const u8 *blob, u32 len)
{
    u32 size, count, array, nodes, i, total = 0, at = 0;
    const u8 *s = section(blob, len, "KBCM", &size);

    if (s == NULL || size < 12)
        return 0;
    count = rd16(s);
    array = rd32(s + 4);
    nodes = rd32(s + 8);
    if (array + count * 8 > size)
        return 0;
    for (i = 0; i < count; i++)
        total += rd16(s + array + i * 8);
    f->mcs = calloc(count ? count : 1, sizeof *f->mcs);
    f->nodes = calloc(total ? total : 1, sizeof *f->nodes);
    if (f->mcs == NULL || f->nodes == NULL)
        return 0;
    f->nmcs = count;
    for (i = 0; i < count; i++) {
        u32 n = rd16(s + array + i * 8), off = rd32(s + array + i * 8 + 4), k;

        f->mcs[i].first = at;
        f->mcs[i].n = 0;
        if (nodes + off + n * 8 > size)
            continue;
        for (k = 0; k < n; k++) {
            const u8 *p = s + nodes + off + k * 8;

            f->nodes[at].seq = rd16(p);
            f->nodes[at].x = (s16)rd16(p + 2);
            f->nodes[at].y = (s16)rd16(p + 4);
            f->nodes[at].attr = rd16(p + 6);
            at++;
            f->mcs[i].n++;
        }
    }
    f->nnodes = at;
    return 1;
}

/* ----------------------------------------------------------- composition */

static u8 map_pixel(const struct face *f, u32 x, u32 y)
{
    u32 i = y * f->cols * 8 + x;
    u8 b = f->map[i >> 1];

    return (i & 1) ? (u8)(b >> 4) : (u8)(b & 0xF);
}

/* The frame a sequence shows at tick t from its start. */
static const struct aframe *frame_at(const struct face *f, const struct seq *q, u32 t)
{
    const struct aframe *fr = f->cframes + q->first;
    u32 n = q->n, total = 0, i;
    int reverse = (q->mode == 3 || q->mode == 4);
    int loop = (q->mode == 2 || q->mode == 4);

    if (n == 0)
        return NULL;
#define AT(i) (&fr[reverse ? (n - 1 - (i)) : (i)])
    for (i = 0; i < n; i++)
        total += AT(i)->duration;
    if (total == 0)
        return AT(0);
    if (t >= total) {
        u32 loop_at, head = 0, span;

        if (!loop)
            return AT(n - 1);
        loop_at = q->loop < n - 1 ? q->loop : n - 1;
        for (i = 0; i < loop_at; i++)
            head += AT(i)->duration;
        span = total - head;
        t = span > 0 ? head + (t - head) % span : head;
    }
    for (i = 0; i < n; i++) {
        if (t < AT(i)->duration)
            return AT(i);
        t -= AT(i)->duration;
    }
    return AT(n - 1);
#undef AT
}

/* Draw one cell at (ox, oy) through the node's own scale and turn about that
 * point. The OBJ with the lower index is in front, so the list is walked back
 * to front and the earlier ones overwrite. */
static void compose_cell(const struct face *f, const struct cell *c, int ox, int oy,
                         s32 sx, s32 sy, u32 rot)
{
    int minx = CANVAS, miny = CANVAS, maxx = -1, maxy = -1;
    u32 j;

    for (j = c->n; j-- > 0;) {
        const struct oam *m = &f->oams[c->first + j];
        u32 tw = m->w / 8, th = m->h / 8, t;

        for (t = 0; t < tw * th; t++) {
            u32 tx = t % tw, ty = t / tw;
            u32 row = m->chr / MAP_PITCH + ty, col = m->chr % MAP_PITCH + tx, k;
            int dx, dy;

            if (row >= f->rows || col >= f->cols)
                continue;                   /* past the map: transparent */
            dx = m->x + (int)(m->hf ? (tw - 1 - tx) * 8 : tx * 8);
            dy = m->y + (int)(m->vf ? (th - 1 - ty) * 8 : ty * 8);
            for (k = 0; k < 64; k++) {
                u8 v = map_pixel(f, col * 8 + k % 8, row * 8 + k / 8);
                int px, py;

                if (!v)
                    continue;
                px = dx + (int)(m->hf ? 7 - k % 8 : k % 8) + ORIGIN;
                py = dy + (int)(m->vf ? 7 - k / 8 : k / 8) + ORIGIN;
                if (px < 0 || px >= CANVAS || py < 0 || py >= CANVAS)
                    continue;
                s_node[py * CANVAS + px] = v;
                if (px < minx) minx = px;
                if (px > maxx) maxx = px;
                if (py < miny) miny = py;
                if (py > maxy) maxy = py;
            }
        }
    }
    if (maxx < 0)
        return;
    if (sx == FX32_ONE && sy == FX32_ONE && rot == 0) {
        int x, y;

        for (y = miny; y <= maxy; y++) {
            for (x = minx; x <= maxx; x++) {
                u8 v = s_node[y * CANVAS + x];
                int X = x + ox, Y = y + oy;

                if (v && X >= 0 && X < CANVAS && Y >= 0 && Y < CANVAS) {
                    s_canvas[Y * CANVAS + X] = v;
                    if (X < s_cminx) s_cminx = X;
                    if (X > s_cmaxx) s_cmaxx = X;
                    if (Y < s_cminy) s_cminy = Y;
                    if (Y > s_cmaxy) s_cmaxy = Y;
                }
            }
        }
    } else {
        float fx = (float)sx / FX32_ONE, fy = (float)sy / FX32_ONE;
        float theta = (float)rot * 2.0f * 3.14159265f / 65536.0f;
        float c = cosf(theta), s = sinf(theta);
        int lx0 = minx - ORIGIN, ly0 = miny - ORIGIN, lx1 = maxx + 1 - ORIGIN, ly1 = maxy + 1 - ORIGIN;
        float cx[4] = { lx0 * fx, lx1 * fx, lx0 * fx, lx1 * fx };
        float cy[4] = { ly0 * fy, ly0 * fy, ly1 * fy, ly1 * fy };
        float tx0 = 1e9f, tx1 = -1e9f, ty0 = 1e9f, ty1 = -1e9f;
        int i, X, Y, X0, X1, Y0, Y1;

        if (fx == 0.0f || fy == 0.0f)
            goto done;
        for (i = 0; i < 4; i++) {
            float rx = cx[i] * c - cy[i] * s, ry = cx[i] * s + cy[i] * c;

            if (rx < tx0) tx0 = rx;
            if (rx > tx1) tx1 = rx;
            if (ry < ty0) ty0 = ry;
            if (ry > ty1) ty1 = ry;
        }
        X0 = (int)floorf(tx0); X1 = (int)ceilf(tx1);
        Y0 = (int)floorf(ty0); Y1 = (int)ceilf(ty1);
        for (Y = Y0; Y <= Y1; Y++) {
            for (X = X0; X <= X1; X++) {
                float dcx = X + 0.5f, dcy = Y + 0.5f;
                float ux = dcx * c + dcy * s, uy = -dcx * s + dcy * c;
                int lx = (int)floorf(ux / fx) + ORIGIN, ly = (int)floorf(uy / fy) + ORIGIN;
                int DX = X + ox + ORIGIN, DY = Y + oy + ORIGIN;
                u8 v;

                if (lx < minx || lx > maxx || ly < miny || ly > maxy)
                    continue;
                v = s_node[ly * CANVAS + lx];
                if (v && DX >= 0 && DX < CANVAS && DY >= 0 && DY < CANVAS) {
                    s_canvas[DY * CANVAS + DX] = v;
                    if (DX < s_cminx) s_cminx = DX;
                    if (DX > s_cmaxx) s_cmaxx = DX;
                    if (DY < s_cminy) s_cminy = DY;
                    if (DY > s_cmaxy) s_cmaxy = DY;
                }
            }
        }
    }
done:
    {
        int y;

        for (y = miny; y <= maxy; y++)
            memset(s_node + y * CANVAS + minx, 0, (size_t)(maxx - minx + 1));
    }
}

/* The picture at tick t of the multi-cell animation's first sequence, onto
 * s_canvas with the cartridge's origin at (ORIGIN, ORIGIN). */
static void compose(const struct face *f, u32 t)
{
    const struct seq *mq;
    const struct aframe *mf = NULL;
    const struct mcell *mc;
    u32 elapsed = 0, i;

    memset(s_canvas, 0, sizeof s_canvas);
    s_cminx = s_cminy = CANVAS;
    s_cmaxx = s_cmaxy = -1;
    if (f->nmseqs == 0 || f->mseqs[0].n == 0)
        return;
    mq = &f->mseqs[0];
    for (i = 0; i < mq->n; i++) {
        mf = &f->mframes[mq->first + i];
        if (t < elapsed + mf->duration || i + 1 == mq->n)
            break;
        elapsed += mf->duration;
    }
    if (mf == NULL || mf->index >= f->nmcs)
        return;
    mc = &f->mcs[mf->index];
    for (i = mc->n; i-- > 0;) {
        const struct node *nd = &f->nodes[mc->first + i];
        const struct aframe *fr;
        u32 local_t;

        if (!((nd->attr >> 5) & 1) || nd->seq >= f->ncseqs)
            continue;
        local_t = ((nd->attr & 0xF) == NODE_RESET) ? t - elapsed : t;
        fr = frame_at(f, &f->cseqs[nd->seq], local_t);
        if (fr == NULL || fr->index >= f->ncells)
            continue;
        compose_cell(f, &f->cells[fr->index], nd->x + fr->px + mf->px, nd->y + fr->py + mf->py,
                     fr->sx, fr->sy, fr->rot);
    }
}

/* Every tick of one period at which the picture can change: each multi-cell
 * frame start, and inside each the keyframes of every node, a RESET node's
 * from the frame start, a continue node's from the animation's own start. */
static void mark_changes(struct face *f)
{
    const struct seq *mq;
    u32 period = 0, start = 0, i;

    f->period = 1;
    if (f->nmseqs == 0 || f->mseqs[0].n == 0)
        return;
    mq = &f->mseqs[0];
    for (i = 0; i < mq->n; i++)
        period += f->mframes[mq->first + i].duration;
    if (period == 0 || period > MAX_PERIOD)
        period = period ? MAX_PERIOD : 1;
    f->period = period;
    f->change = calloc(period, 1);
    if (f->change == NULL) {
        f->period = 1;
        return;
    }
    f->change[0] = 1;
    for (i = 0; i < mq->n; i++) {
        const struct aframe *mf = &f->mframes[mq->first + i];
        u32 duration = mf->duration, k;

        if (duration == 0)
            continue;
        if (start < period)
            f->change[start] = 1;
        if (mf->index < f->nmcs) {
            const struct mcell *mc = &f->mcs[mf->index];

            for (k = 0; k < mc->n; k++) {
                const struct node *nd = &f->nodes[mc->first + k];
                const struct seq *q;
                u32 total = 0, j, base;

                if (!((nd->attr >> 5) & 1) || nd->seq >= f->ncseqs)
                    continue;
                q = &f->cseqs[nd->seq];
                for (j = 0; j < q->n; j++)
                    total += f->cframes[q->first + j].duration;
                if (total == 0)
                    total = 1;
                if ((nd->attr & 0xF) == NODE_RESET) {
                    for (base = 0; base < duration; base += total) {
                        u32 st = 0;

                        for (j = 0; j < q->n; j++) {
                            if (base + st < duration && start + base + st < period)
                                f->change[start + base + st] = 1;
                            st += f->cframes[q->first + j].duration;
                        }
                    }
                } else {
                    for (base = (start / total) * total; base < start + duration; base += total) {
                        u32 st = 0;

                        for (j = 0; j < q->n; j++) {
                            if (base + st >= start && base + st < start + duration && base + st < period)
                                f->change[base + st] = 1;
                            st += f->cframes[q->first + j].duration;
                        }
                    }
                }
            }
        }
        start += duration;
    }
}

/* The loop'S box, over whichever keyframes the caller means by the loop. */
void mmo_black_box(struct face *f, const u32 *ticks, u32 nticks)
{
    u32 marks = 0, step, i, t, seen = 0;
    int minx = CANVAS, miny = CANVAS, maxx = -1, maxy = -1;
    int refy = 0, refh = 1, have_ref = 0;

    if (ticks != NULL) {
        step = 1;
        marks = nticks;
    } else {
        for (t = 0; t < f->period; t++)
            marks += f->change ? f->change[t] : 0;
        step = marks > BBOX_SAMPLES ? (marks + BBOX_SAMPLES - 1) / BBOX_SAMPLES : 1;
    }
    for (i = 0; i < (ticks != NULL ? nticks : f->period); i++) {
        int kminx, kminy, kmaxx, kmaxy;

        t = ticks != NULL ? ticks[i] : i;
        if (ticks == NULL) {
            if (!(f->change ? f->change[t] : t == 0))
                continue;
            if (seen++ % step)
                continue;
        }
        compose(f, t);
        kminx = s_cminx;
        kminy = s_cminy;
        kmaxx = s_cmaxx;
        kmaxy = s_cmaxy;
        if (kmaxx < 0)
            continue;
        if (kminx < minx) minx = kminx;
        if (kmaxx > maxx) maxx = kmaxx;
        if (kminy < miny) miny = kminy;
        if (kmaxy > maxy) maxy = kmaxy;
        if (!have_ref) {
            have_ref = 1;
            refy = kminy;
            refh = kmaxy + 1 - kminy;
        }
    }
    if (maxx < 0) {
        f->x0 = f->y0 = ORIGIN;
        f->w = f->h = 1;
        refy = f->y0;
    } else {
        f->x0 = minx;
        f->y0 = miny;
        f->w = maxx + 1 - minx;
        f->h = maxy + 1 - miny;
    }
    f->refy = refy;
    f->refh = refh;
    f->boxed = 1;
}

/* The whole loop fits, and the byte is the room below the feet. */
void mmo_black_fit(struct face *f, int fw, int fh)
{
    int refbot, dip, rise, rbot, fw2, byte;
    double above;

    if (fw < BC_FRAME || fw > BC_FRAME_MAX_W) fw = BC_FRAME;
    if (fh < BC_FRAME || fh > BC_FRAME_MAX_H) fh = BC_FRAME;
    f->fw = fw;
    f->fh = fh;
    refbot = f->refy + f->refh;
    dip = (f->y0 + f->h) - refbot;
    rise = f->refy - f->y0;
    above = (double)(f->refh + rise);
    f->factor = 1.0;
    if (f->w * f->factor > fw)
        f->factor = (double)fw / f->w;
    byte = f->hasByte ? f->byte : (int)floor(dip * f->factor + 0.5);
    if (byte < 0) byte = 0;
    if (byte > fh - 1) byte = fh - 1;
    if (above > 0.0 && above * f->factor > fh - byte)
        f->factor = (double)(fh - byte) / above;
    if (byte > 0 && dip > 0 && dip * f->factor > byte + 0.5)
        f->factor = (double)byte / dip;
    if (!f->hasByte)
        byte = (int)floor(dip * f->factor + 0.5);
    rbot = (int)floor((refbot - f->y0) * f->factor + 0.5);
    fw2 = (int)floor(f->w * f->factor + 0.5);
    if (fw2 < 1) fw2 = 1;
    f->top = (fh - byte) - rbot;
    f->left = (fw - fw2) / 2;
    f->seat = fh - byte;
    f->byte = byte;
}

/* The height byte for a loop already boxed: the resting pose's dip, at the
 * scale the wide frame earns it. The fill writes this into height.narc and
 * every later fit, this file's live one included, is seated by it. */
int mmo_black_height_byte(const struct face *f)
{
    int dip = (f->y0 + f->h) - (f->refy + f->refh);
    double wide = 1.0;

    if (f->w > BC_FRAME_MAX_W)
        wide = (double)BC_FRAME_MAX_W / f->w;
    if (f->h > BC_FRAME_MAX_H && (double)BC_FRAME_MAX_H / f->h < wide)
        wide = (double)BC_FRAME_MAX_H / f->h;
    if (dip < 0)
        dip = 0;
    return (int)floor(dip * wide + 0.5);
}

void mmo_black_place(struct face *f, int fw, int fh)
{
    if (!f->boxed)
        mmo_black_box(f, NULL, 0);
    mmo_black_fit(f, fw, fh);
}

static int nearest_colour(const u16 *pal, double r, double g, double b)
{
    int best = 1, i;
    double bd = 1e9;

    for (i = 1; i < 16; i++) {
        double cr = (double)(pal[i] & 31), cg = (double)((pal[i] >> 5) & 31), cb = (double)((pal[i] >> 10) & 31);
        double d = (cr - r) * (cr - r) + (cg - g) * (cg - g) + (cb - b) * (cb - b);

        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

/* The canvas as laid out in the face's frame: fw x fh palette indices, zero
 * where nothing was drawn. Scaled down by area average and back onto the
 * sprite's own sixteen colours, up by nearest neighbour the way the hardware
 * doubles a back sprite. */
void mmo_black_frame(const struct face *f, u8 *out)
{
    memset(out, 0, (size_t)f->fw * f->fh);
    if (f->factor == 1.0) {
        int x, y;

        for (y = 0; y < f->h; y++) {
            for (x = 0; x < f->w; x++) {
                u8 v = s_canvas[(f->y0 + y) * CANVAS + f->x0 + x];
                int X = f->left + x, Y = f->top + y;

                if (v && X >= 0 && X < f->fw && Y >= 0 && Y < f->fh)
                    out[Y * f->fw + X] = v;
            }
        }
    } else if (f->factor > 1.0) {
        int fw = (int)floor(f->w * f->factor + 0.5), fh = (int)floor(f->h * f->factor + 0.5), X, Y;

        for (Y = 0; Y < fh; Y++) {
            int sy = (int)(Y / f->factor);
            int DY = f->top + Y;

            if (sy > f->h - 1) sy = f->h - 1;
            if (DY < 0 || DY >= f->fh)
                continue;
            for (X = 0; X < fw; X++) {
                int sx = (int)(X / f->factor);
                int DX = f->left + X;
                u8 v;

                if (sx > f->w - 1) sx = f->w - 1;
                if (DX < 0 || DX >= f->fw)
                    continue;
                v = s_canvas[(f->y0 + sy) * CANVAS + f->x0 + sx];
                if (v)
                    out[DY * f->fw + DX] = v;
            }
        }
    } else {
        int nw = (int)floor(f->w * f->factor + 0.5), nh = (int)floor(f->h * f->factor + 0.5), X, Y;
        double inv = 1.0 / f->factor;

        if (nw < 1) nw = 1;
        if (nh < 1) nh = 1;
        for (Y = 0; Y < nh; Y++) {
            for (X = 0; X < nw; X++) {
                double sx0 = X * inv, sx1 = (X + 1) * inv, sy0 = Y * inv, sy1 = (Y + 1) * inv;
                double cover = 0.0, ar = 0.0, ag = 0.0, ab = 0.0;
                int yy, xx, DX = f->left + X, DY = f->top + Y;

                for (yy = (int)floor(sy0); yy < (int)ceil(sy1); yy++) {
                    double wy = (sy1 < yy + 1 ? sy1 : yy + 1) - (sy0 > yy ? sy0 : yy);

                    for (xx = (int)floor(sx0); xx < (int)ceil(sx1); xx++) {
                        double wx = (sx1 < xx + 1 ? sx1 : xx + 1) - (sx0 > xx ? sx0 : xx);
                        double a = wx * wy;
                        u8 v;
                        u16 c;

                        if (xx >= f->w || yy >= f->h)
                            continue;
                        v = s_canvas[(f->y0 + yy) * CANVAS + f->x0 + xx];
                        if (!v)
                            continue;
                        c = f->pal[v];
                        cover += a;
                        ar += (double)(c & 31) * a;
                        ag += (double)((c >> 5) & 31) * a;
                        ab += (double)((c >> 10) & 31) * a;
                    }
                }
                if (cover >= 0.5 * inv * inv && DX >= 0 && DX < f->fw && DY >= 0 && DY < f->fh)
                    out[DY * f->fw + DX] = (u8)nearest_colour(f->pal, ar / cover, ag / cover, ab / cover);
            }
        }
    }
}

/* ------------------------------------------------------------- the names */
void mmo_black_marks(struct face *f) { mark_changes(f); }
int mmo_black_read_charmap(struct face *f, const u8 *blob, u32 len)
{ return read_charmap(f, blob, len); }
int mmo_black_read_cells(struct face *f, const u8 *blob, u32 len)
{ return read_cells(f, blob, len); }
int mmo_black_read_anims(const u8 *blob, u32 len, struct seq **s, u32 *ns,
                         struct aframe **fr, u32 *nfr)
{ return read_anims(blob, len, s, ns, fr, nfr); }
int mmo_black_read_multicells(struct face *f, const u8 *blob, u32 len)
{ return read_multicells(f, blob, len); }
void mmo_black_compose(const struct face *f, u32 t) { compose(f, t); }
