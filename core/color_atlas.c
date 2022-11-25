// -------------------------------------------------------------
//  Cubzh Core
//  color_atlas.c
//  Created by Mina Pecheux on July 19, 2022.
// -------------------------------------------------------------

#include "color_atlas.h"
#include "color_palette.h"

#include "cclog.h"
#include "config.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void _color_atlas_add_index_to_dirty_slice(ColorAtlas *a, ATLAS_COLOR_INDEX_INT_T index) {
    const ATLAS_COLOR_INDEX_INT_T x = index % a->size;
    const ATLAS_COLOR_INDEX_INT_T y = index / a->size;
    if (a->dirty_slice_width > 0 && a->dirty_slice_height > 0) {
        const ATLAS_COLOR_INDEX_INT_T fromX = minimum(a->dirty_slice_origin_x, x);
        const ATLAS_COLOR_INDEX_INT_T fromY = minimum(a->dirty_slice_origin_y, y);
        const ATLAS_COLOR_INDEX_INT_T toX = maximum(a->dirty_slice_origin_x + a->dirty_slice_width -
                                                        1,
                                                    x);
        const ATLAS_COLOR_INDEX_INT_T toY = maximum(a->dirty_slice_origin_y +
                                                        a->dirty_slice_height - 1,
                                                    y);

        a->dirty_slice_origin_x = fromX;
        a->dirty_slice_origin_y = fromY;
        a->dirty_slice_width = toX - fromX + 1;
        a->dirty_slice_height = toY - fromY + 1;
    } else {
        a->dirty_slice_origin_x = x;
        a->dirty_slice_origin_y = y;
        a->dirty_slice_width = a->dirty_slice_height = 1;
    }
}

ColorAtlas *color_atlas_new() {
    ColorAtlas *color_atlas = (ColorAtlas *)malloc(sizeof(ColorAtlas));

    color_atlas->wptr = NULL;
    color_atlas->availableIndices = fifo_list_new();
    color_atlas->count = 0;
    color_atlas->size = COLOR_ATLAS_SIZE;
    color_atlas->dirty_slice_origin_x = 0;
    color_atlas->dirty_slice_origin_y = 0;
    color_atlas->dirty_slice_width = 0;
    color_atlas->dirty_slice_height = 0;

    // color + complementary : half as many unique colors
    const uint32_t nbColors = color_atlas->size * color_atlas->size / 2;
    color_atlas->colors = (RGBAColor *)malloc(sizeof(RGBAColor) * nbColors);
    color_atlas->complementaryColors = (RGBAColor *)malloc(sizeof(RGBAColor) * nbColors);

    return color_atlas;
}

void color_atlas_free(ColorAtlas *a) {
    if (a != NULL) {
        weakptr_invalidate(a->wptr);
        free(a->colors);
        free(a->complementaryColors);
        fifo_list_free(a->availableIndices);
    }
    free(a);
}

Weakptr *color_atlas_get_weakptr(ColorAtlas *a) {
    if (a->wptr == NULL) {
        a->wptr = weakptr_new(a);
    }
    return a->wptr;
}

Weakptr *color_atlas_get_and_retain_weakptr(ColorAtlas *a) {
    if (a->wptr == NULL) {
        a->wptr = weakptr_new(a);
    }
    if (weakptr_retain(a->wptr)) {
        return a->wptr;
    } else { // this can only happen if weakptr ref count is at max
        return NULL;
    }
}

ATLAS_COLOR_INDEX_INT_T color_atlas_check_and_add_color(ColorAtlas *a, RGBAColor color) {
    // get an available index below count, or expand
    ATLAS_COLOR_INDEX_INT_T index;
    void *pop = fifo_list_pop(a->availableIndices);
    if (pop != NULL) {
        index = *((ATLAS_COLOR_INDEX_INT_T *)pop);
        free(pop);
    } else if (a->count >= ATLAS_COLOR_INDEX_MAX_COUNT) {
        return ATLAS_COLOR_INDEX_ERROR; // atlas at max capacity
    } else {
        index = a->count++;
    }

    // add color + complementary
#if DEBUG_MARK_OPERATIONS
    a->colors[index] = (RGBAColor){ 0, 255, 0, 255 };
    a->complementaryColors[index] = (RGBAColor){ 0, 0, 255, 255 };
#else
    a->colors[index] = color;
    a->complementaryColors[index] = color_compute_complementary(color);
#endif

    _color_atlas_add_index_to_dirty_slice(a, index);

    return index;
}

void color_atlas_remove_color(ColorAtlas *a, ATLAS_COLOR_INDEX_INT_T index) {
    if (index >= ATLAS_COLOR_INDEX_MAX_COUNT) {
        return;
    }

    // index becomes available
    ATLAS_COLOR_INDEX_INT_T *push = (ATLAS_COLOR_INDEX_INT_T *)malloc(
        sizeof(ATLAS_COLOR_INDEX_INT_T));
    *push = index;
    fifo_list_push(a->availableIndices, push);

    // note: removed color do not need to be set dirty, it simply becomes available and won't be
    // used in the meantime

#if DEBUG_MARK_OPERATIONS
    a->colors[index] = (RGBAColor){ 255, 0, 0, 255 };
    a->complementaryColors[index] = (RGBAColor){ 255, 0, 255, 255 };

    _color_atlas_add_index_to_dirty_slice(a, index);
#endif
}

void color_atlas_remove_palette(ColorAtlas *a, const ColorPalette *p) {
    const uint8_t nbColors = color_palette_get_count(p);
    ATLAS_COLOR_INDEX_INT_T idx;
    for (int i = 0; i < nbColors; ++i) {
        idx = color_palette_get_atlas_index(p, i);
        if (idx != ATLAS_COLOR_INDEX_ERROR &&
            color_palette_get_color_use_count((ColorPalette *)p, i) > 0) {
            color_atlas_remove_color(a, idx);
        }
    }
}

void color_atlas_set_color(ColorAtlas *a, ATLAS_COLOR_INDEX_INT_T index, RGBAColor color) {
    if (index >= ATLAS_COLOR_INDEX_MAX_COUNT || colors_are_equal(&(a->colors[index]), &color)) {
        return;
    }

#if DEBUG_MARK_OPERATIONS
    a->colors[index] = (RGBAColor){ 0, 128, 0, 255 };
    a->complementaryColors[index] = (RGBAColor){ 0, 0, 128, 255 };
#else
    a->colors[index] = color;
    a->complementaryColors[index] = color_compute_complementary(color);
#endif

    _color_atlas_add_index_to_dirty_slice(a, index);
}

RGBAColor *color_atlas_get_color(const ColorAtlas *a, ATLAS_COLOR_INDEX_INT_T index) {
    if (index >= ATLAS_COLOR_INDEX_MAX_COUNT) {
        return NULL;
    }
    return &(a->colors[index]);
}

void color_atlas_force_dirty_slice(ColorAtlas *a) {
    a->dirty_slice_origin_x = a->dirty_slice_origin_y = 0;
    a->dirty_slice_width = a->count % a->size;
    a->dirty_slice_height = maximum(a->count / a->size, 1);
}

void color_atlas_flush_slice(ColorAtlas *a) {
    a->dirty_slice_origin_x = a->dirty_slice_origin_y = 0;
    a->dirty_slice_width = a->dirty_slice_height = 0;
}
