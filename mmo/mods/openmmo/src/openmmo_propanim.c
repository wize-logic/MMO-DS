/* One animated prop model, placed in a room many times. */

#include "overlay005/map_prop_animation.h"

#include <nitro.h>

/* The field's one manager. MapPropAnimationManager_ReleaseRenderObj is reached
 * from the chunk unload with only a prop in hand, and the manager it belongs to
 * is not on the way; the field builds exactly one and takes it down with the
 * map, so recording it here is the whole of the lookup. */
static MapPropAnimationManager *sManager;

void openmmo_prop_anim_bind(MapPropAnimationManager *manager)
{
    int i;

    for (i = 0; i < MAP_PROP_ANIMATION_MANAGER_MAX_ANIMATIONS; i++) {
        manager->animations[i].texture = NULL;
        manager->animations[i].cloneCount = 0;
    }

    sManager = manager;
}

void openmmo_prop_anim_unbind(MapPropAnimationManager *manager)
{
    if (sManager == manager) {
        sManager = NULL;
    }
}

/* A slot has just been loaded against a model and a texture set. The clones are
 * built from both, so the texture is kept beside the object. */
void openmmo_prop_anim_slot_bound(MapPropAnimation *animation, NNSG3dResTex *texture)
{
    animation->texture = texture;
    animation->cloneCount = 0;
}

/* A fresh animation object for one placed prop, off the slot's own file, model
 * and texture set. NULL once the slot's clones are spent, so that placement
 * stands still, and NULL again for a render object that already holds a clone
 * of this slot, so a second add is not made. */
static NNSG3dAnmObj *clone_for(MapPropAnimation *animation, NNSG3dRenderObj *renderObj, NNSFndAllocator *allocator)
{
    void *anim;
    NNSG3dResMdl *model;
    NNSG3dAnmObj *clone;
    int i;

    for (i = 0; i < animation->cloneCount; i++) {
        if (animation->cloneOwner[i] == renderObj) {
            return NULL;
        }
    }

    if (animation->cloneCount >= MAP_PROP_ANIMATION_CLONES) {
        return NULL;
    }

    anim = NNS_G3dGetAnmByIdx(animation->animationFile, 0);
    model = NNS_G3dRenderObjGetResMdl(renderObj);

    if (anim == NULL || model == NULL) {
        return NULL;
    }

    clone = NNS_G3dAllocAnmObj(allocator, anim, model);

    if (clone == NULL) {
        return NULL;
    }

    NNS_G3dAnmObjInit(clone, anim, model, animation->texture);
    clone->frame = animation->animationObj->frame;
    animation->clones[animation->cloneCount] = clone;
    animation->cloneOwner[animation->cloneCount] = renderObj;
    animation->cloneCount++;

    return clone;
}

/* Which animation object this render object should be given for this slot, or
 * NULL for "add nothing". */
NNSG3dAnmObj *openmmo_prop_anim_object(MapPropAnimation *animation, NNSG3dRenderObj *renderObj, NNSFndAllocator *allocator, BOOL isBicycleSlope)
{
    if (isBicycleSlope) {
        return animation->animationObj;
    }

    return clone_for(animation, renderObj, allocator);
}

/* The clones follow the slot's frame. */
void openmmo_prop_anim_sync(MapPropAnimation *animation)
{
    int i;

    for (i = 0; i < animation->cloneCount; i++) {
        animation->clones[i]->frame = animation->animationObj->frame;
    }
}

/* The slot is going away; so is every clone of it, wherever it is linked. */
void openmmo_prop_anim_free_clones(MapPropAnimation *animation, NNSFndAllocator *allocator)
{
    while (animation->cloneCount > 0) {
        animation->cloneCount--;
        NNS_G3dFreeAnmObj(allocator, animation->clones[animation->cloneCount]);
    }
}

void MapPropAnimationManager_ReleaseRenderObj(NNSG3dRenderObj *renderObj)
{
    MapPropAnimationManager *manager = sManager;
    int i, k;

    if (manager == NULL || renderObj == NULL) {
        return;
    }

    for (i = 0; i < MAP_PROP_ANIMATION_MANAGER_MAX_ANIMATIONS; i++) {
        MapPropAnimation *animation = &manager->animations[i];

        if (!animation->loaded) {
            continue;
        }

        for (k = 0; k < animation->cloneCount; k++) {
            if (animation->cloneOwner[k] != renderObj) {
                continue;
            }

            NNS_G3dRenderObjRemoveAnmObj(renderObj, animation->clones[k]);
            NNS_G3dFreeAnmObj(&manager->allocator, animation->clones[k]);
            animation->cloneCount--;
            animation->clones[k] = animation->clones[animation->cloneCount];
            animation->cloneOwner[k] = animation->cloneOwner[animation->cloneCount];
            k--;
        }
    }
}
