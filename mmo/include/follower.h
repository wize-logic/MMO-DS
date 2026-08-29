/* Which picture walks behind a player. */
#ifndef OPENMMO_FOLLOWER_H
#define OPENMMO_FOLLOWER_H

/* The graphics id that draws this species walking, or -1 when nothing should. */
int mmo_follower_gfx(int species, int form, int gender, int shiny);

/* The number of species this build's table covers, so a caller can say why it
 * was refused without reading the generated header. */
int mmo_follower_species_max(void);

/* The first graphics id of the follower band, which is what a filled package
 * allocates from. A caller that wants to know whether a graphics id is a
 * follower at all compares against this and the count. */
int mmo_follower_gfx_base(void);
int mmo_follower_gfx_count(void);

#endif /* OPENMMO_FOLLOWER_H */
