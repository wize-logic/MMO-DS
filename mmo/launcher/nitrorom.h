/* Reading a Nintendo DS cartridge image, once. */
#ifndef MMO_NITROROM_H
#define MMO_NITROROM_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

/* A run of bytes and its length. Owned by whoever allocated it; the members
 * of an archive point into the archive's own blob and are not freed. */
typedef struct {
    uint8_t *p;
    uint32_t len;
} MmoBlob;

typedef struct {
    MmoBlob *m;
    uint32_t n;
} MmoMembers;

/* Arm the failure path: where a refusal jumps and where its sentence is
 * written. Call it after setjmp(jb) and before the first read. `err` may be
 * NULL, in which case a refusal still jumps and says nothing. */
void mmo_nitro_catch(jmp_buf *jb, char *err, size_t cap);

/* Refuse, with the sentence a player reads. Never returns. */
void mmo_nitro_die(const char *fmt, ...);

/* malloc and realloc that refuse rather than return NULL. */
void *mmo_nitro_alloc(size_t n);
void *mmo_nitro_grow(void *p, size_t n);

/* Little-endian, the only order a DS image is written in. */
uint16_t mmo_nitro_rd16(const uint8_t *p);
uint32_t mmo_nitro_rd32(const uint8_t *p);
void mmo_nitro_wr16(uint8_t *p, uint16_t v);
void mmo_nitro_wr32(uint8_t *p, uint32_t v);

/* A copy of `len` bytes, owned by the caller. */
MmoBlob mmo_nitro_dup(const uint8_t *p, uint32_t len);

/* A whole file, owned by the caller. */
MmoBlob mmo_nitro_read_file(const char *path);

/*
 * The bytes of one file inside an image, by its NitroFS path, the porter's
 * NitroRom.file_bytes. A official image has its archive names stripped, so a path is usually an
 * id like "a/0/8/1" and the walk builds exactly that.
 */
MmoBlob mmo_nitro_file(MmoBlob rom, const char *want, const char *label,
                       const char *hint);

/*
 * An archive's members, NitroRom.narc_members. The members point into `arc`, so the caller
 * keeps it alive for as long as it reads them.
 */
MmoMembers mmo_nitro_narc(MmoBlob arc, const char *what);

#endif
