/* The hour and the date everyone shares. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nitro.h>

#include "field_system.h"
#include "unk_020559DC.h"

#include "../../../include/platform.h"

extern FieldSystem *pc_lab_field_system(void);

/* The seat's six bytes: years since 2000, month, day, hour, minute, second. */
#define WORLD_CLOCK_BYTES 6

static int s_have;
static int s_year, s_month, s_day, s_hour, s_min, s_sec;
static long s_at;      /* mmo_plat_seconds() when the seat arrived */

static const u8 kDaysInMonth[13] = {
    0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static int days_in_month(int y, int m)
{
    /* 2000..2099, where every fourth year is a leap year. */
    if (m == 2 && (y % 4) == 0)
        return 29;
    return kDaysInMonth[m];
}

/* Day of week, 0 = Sunday, for a 20yy-mm-dd (Sakamoto). RTCWeek is that
 * same numbering, and the engine's date readers carry the field along. */
static int weekday(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int yy = 2000 + y;

    if (m < 3)
        yy -= 1;
    return (yy + yy / 4 - yy / 100 + yy / 400 + t[m - 1] + d) % 7;
}

/* The server's statement of what time it is in the world. */
void openmmo_world_clock_seat(const u8 *b, int len)
{
    if (b == NULL || len < WORLD_CLOCK_BYTES)
        return;
    if (b[0] > 99 || b[1] < 1 || b[1] > 12 || b[2] < 1 || b[2] > 31
        || b[3] > 23 || b[4] > 59 || b[5] > 59) {
        printf("openmmo: the world clock arrived as %u-%u-%u %u:%u:%u,"
               " which is not a date, kept the one we had\n",
               b[0], b[1], b[2], b[3], b[4], b[5]);
        return;
    }
    s_year = b[0];
    s_month = b[1];
    s_day = b[2];
    s_hour = b[3];
    s_min = b[4];
    s_sec = b[5];
    s_at = mmo_plat_seconds();
    s_have = 1;
    printf("openmmo: the world says it is 20%02d-%02d-%02d %02d:%02d:%02d\n",
           s_year, s_month, s_day, s_hour, s_min, s_sec);
}

/* What a script is actually told, which is not this. */
static void report(int h, int mi)
{
    static int last = -2;
    static int on = -1;
    FieldSystem *fs;
    int now;

    if (on < 0)
        on = getenv("OPENMMO_CLOCK_REPORT") != NULL;
    if (!on)
        return;
    fs = pc_lab_field_system();
    if (fs == NULL)
        return;
    now = (int)FieldSystem_GetTimeOfDay(fs);
    if (now == last)
        return;
    last = now;
    printf("openmmo: the world is at %02d:%02d and a script asking the hour"
           " is told %d\n", h, mi, now);
}

/* What time it is now: the seat plus the seconds since it arrived. Answers 0
 * before the server has said, and the engine keeps its own clock then. */
int openmmo_world_clock(RTCDate *date, RTCTime *time)
{
    long elapsed;
    int y, mo, d, h, mi, s;

    if (!s_have)
        return 0;
    elapsed = mmo_plat_seconds() - s_at;
    if (elapsed < 0)
        elapsed = 0;

    y = s_year;
    mo = s_month;
    d = s_day;
    s = s_sec + (int)(elapsed % 60);
    elapsed /= 60;
    mi = s_min + (int)(elapsed % 60);
    elapsed /= 60;
    h = s_hour + (int)(elapsed % 24);
    elapsed /= 24;

    if (s >= 60) { s -= 60; mi++; }
    if (mi >= 60) { mi -= 60; h++; }
    if (h >= 24) { h -= 24; elapsed++; }
    while (elapsed > 0) {
        int dim = days_in_month(y, mo);
        long step = (long)(dim - d) + 1;

        if (elapsed < step) {
            d += (int)elapsed;
            break;
        }
        elapsed -= step;
        d = 1;
        if (++mo > 12) {
            mo = 1;
            y++;
        }
    }

    if (date != NULL) {
        date->year = (u32)y;
        date->month = (u32)mo;
        date->day = (u32)d;
        date->week = (RTCWeek)weekday(y, mo, d);
    }
    if (time != NULL) {
        time->hour = (u32)h;
        time->minute = (u32)mi;
        time->second = (u32)s;
    }
    report(h, mi);
    return 1;
}
