/* How far back the host holds the field camera. */

#include <nitro.h>

#include <stdio.h>
#include <stdlib.h>

#include "camera.h"

#define OPENMMO_CAMERA_MIN 100
#define OPENMMO_CAMERA_MAX 130

#define OPENMMO_PORTED_CAMERA_COUNT 17

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

/* The look-at offset each of those carries, applied to the camera and its
 * target alike; this engine's own templates have none. */
static const VecFx32 sPortedCameraOffsets[OPENMMO_PORTED_CAMERA_COUNT] = {
    { 0, 0, 0 },
    { 0, 151552, -61440 },
    { 0, 0, 0 },
    { 0, 125381, -49353 },
    { 0, 0, 0 },
    { 0, 0, -98304 },
    { 0, 0, 0 },
    { 0, 97371, -92702 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 50673, -154740 },
    { 0, -32768, 0 },
    { 0, 0, -131072 },
    { 0, 125381, -123081 },
    { 0, 0, 0 },
    { 0, 0, -188416 },
    { 0, 0, 0 },
};

/*
 * How far the field camera stands from its target, and how far its far plane is, once the
 * host's stance is applied.
 */
void openmmo_camera_scale_distance(fx32 *distance, fx32 *farPlane)
{
    int percent = openmmo_camera_distance_percent();
    fx32 scaled;

    if (percent == 100) {
        return;
    }

    scaled = *distance * percent / 100;
    *farPlane += scaled - *distance;
    *distance = scaled;
}

/*
 * The source's Camera_OffsetLookAtPosAndTarget: the camera and its target both move by the
 * ported template's offset, once. The field camera follows its target by deltas from then on
 * (camera.c), so the offset holds for the map.
 */
void openmmo_camera_offset_lookat(Camera *camera, int index)
{
    const VecFx32 *offset;

    if (camera == NULL || index < 0 || index >= OPENMMO_PORTED_CAMERA_COUNT) {
        return;
    }

    offset = &sPortedCameraOffsets[index];
    VEC_Add(&camera->lookAt.position, offset, &camera->lookAt.position);
    VEC_Add(&camera->lookAt.target, offset, &camera->lookAt.target);
}
