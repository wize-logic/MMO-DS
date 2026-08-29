/* How far back the host holds the field camera. */

#include <stdio.h>
#include <stdlib.h>

#define OPENMMO_CAMERA_MIN 100
#define OPENMMO_CAMERA_MAX 130

int openmmo_camera_distance_percent(void)
{
    static int percent = -1;
    const char *v;
    char *end;
    long n;

    if (percent >= 0) {
        return percent;
    }

    percent = 100;
    v = getenv("OPENMMO_CAMERA_DISTANCE");
    if (v == NULL || v[0] == '\0') {
        return percent;
    }

    n = strtol(v, &end, 10);
    if (end == v || *end != '\0' || n < OPENMMO_CAMERA_MIN || n > OPENMMO_CAMERA_MAX) {
        fprintf(stderr,
                "openmmo: OPENMMO_CAMERA_DISTANCE=%s is not a percent in "
                "%d..%d; keeping the official client's camera\n",
                v, OPENMMO_CAMERA_MIN, OPENMMO_CAMERA_MAX);
        return percent;
    }

    percent = (int)n;
    return percent;
}
