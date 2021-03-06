/* Goxel 3D voxels editor
 *
 * copyright (c) 2015 Guillaume Chereau <guillaume@noctua-software.com>
 *
 * Goxel is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.

 * Goxel is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.

 * You should have received a copy of the GNU General Public License along with
 * goxel.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "goxel.h"

static void print_history(const image_t *img)
{
    if (!DEBUG) return;
    const image_t *im;
    int i = 0;
    LOG_V("hist");
    DL_FOREACH2(img->history, im, history_next) {
        LOG_V("%s %d (%p)", im == img->history_current ? "*" : " ", i, im);
        i++;
    }
}

static layer_t *layer_new(const char *name)
{
    layer_t *layer;
    layer = calloc(1, sizeof(*layer));
    // XXX: potential bug here.
    strncpy(layer->name, name, sizeof(layer->name));
    layer->mesh = mesh_new();
    layer->mat = mat4_identity;
    return layer;
}

static layer_t *layer_copy(layer_t *other)
{
    layer_t *layer;
    layer = calloc(1, sizeof(*layer));
    memcpy(layer->name, other->name, sizeof(layer->name));
    layer->visible = other->visible;
    layer->mesh = mesh_copy(other->mesh);
    layer->image = texture_copy(other->image);
    layer->mat = other->mat;
    return layer;
}

static void layer_delete(layer_t *layer)
{
    mesh_delete(layer->mesh);
    texture_delete(layer->image);
    free(layer);
}

image_t *image_new(void)
{
    layer_t *layer;
    image_t *img = calloc(1, sizeof(*img));
    img->export_width = 256;
    img->export_height = 256;
    layer = layer_new("background");
    layer->visible = true;
    DL_APPEND(img->layers, layer);
    img->active_layer = layer;
    image_history_push(img);
    return img;
}

image_t *image_copy(image_t *other)
{
    image_t *img;
    layer_t *layer, *other_layer;
    img = calloc(1, sizeof(*img));
    *img = *other;
    img->layers = NULL;
    img->active_layer = NULL;
    DL_FOREACH(other->layers, other_layer) {
        layer = layer_copy(other_layer);
        DL_APPEND(img->layers, layer);
        if (other_layer == other->active_layer)
            img->active_layer = layer;
    }
    assert(img->active_layer);
    return img;
}

void image_delete(image_t *img)
{
    layer_t *layer, *tmp;
    DL_FOREACH_SAFE(img->layers, layer, tmp) {
        DL_DELETE(img->layers, layer);
        layer_delete(layer);
    }
    free(img->path);
    free(img);
}

layer_t *image_add_layer(image_t *img)
{
    layer_t *layer;
    layer = layer_new("unamed");
    layer->visible = true;
    DL_APPEND(img->layers, layer);
    img->active_layer = layer;
    return layer;
}

void image_delete_layer(image_t *img, layer_t *layer)
{
    DL_DELETE(img->layers, layer);
    if (layer == img->active_layer) img->active_layer = NULL;
    layer_delete(layer);
    if (img->layers == NULL) {
        layer = layer_new("unamed");
        layer->visible = true;
        DL_APPEND(img->layers, layer);
    }
    if (!img->active_layer) img->active_layer = img->layers->prev;
}

void image_move_layer(image_t *img, layer_t *layer, int d)
{
    assert(d == -1 || d == +1);
    layer_t *other = NULL;
    if (d == -1) {
        other = layer->next;
        SWAP(other, layer);
    } else if (layer != img->layers) {
        other = layer->prev;
    }
    if (!other || !layer) return;
    DL_DELETE(img->layers, layer);
    DL_PREPEND_ELEM(img->layers, other, layer);
}

layer_t *image_duplicate_layer(image_t *img, layer_t *other)
{
    layer_t *layer;
    layer = layer_copy(other);
    layer->visible = true;
    DL_APPEND(img->layers, layer);
    img->active_layer = layer;
    return layer;
}

void image_merge_visible_layers(image_t *img)
{
    layer_t *layer, *last = NULL;
    DL_FOREACH(img->layers, layer) {
        if (!layer->visible) continue;
        if (last) {
            mesh_merge(layer->mesh, last->mesh);
            DL_DELETE(img->layers, last);
            layer_delete(last);
        }
        last = layer;
    }
    if (last) img->active_layer = last;
}

void image_set(image_t *img, image_t *other)
{
    layer_t *layer, *tmp, *other_layer;
    DL_FOREACH_SAFE(img->layers, layer, tmp) {
        DL_DELETE(img->layers, layer);
        layer_delete(layer);
    }
    DL_FOREACH(other->layers, other_layer) {
        layer = layer_copy(other_layer);
        DL_APPEND(img->layers, layer);
        if (other_layer == other->active_layer)
            img->active_layer = layer;
    }
}

void image_history_push(image_t *img)
{
    image_t *snap = image_copy(img);
    if (!img->history_current) img->history_current = img->history;
    // Discard previous undo.
    // XXX: also need to delete the images!
    while (img->history != img->history_current)
        DL_DELETE2(img->history, img->history, history_prev, history_next);
    DL_PREPEND2(img->history, snap, history_prev, history_next);
    img->history_current = img->history;
    print_history(img);
}

void image_undo(image_t *img)
{
    if (!img->history_current->history_next) return;
    img->history_current = img->history_current->history_next;
    image_set(img, img->history_current);
    goxel_update_meshes(goxel(), true);
    print_history(img);
}

void image_redo(image_t *img)
{
    image_t *cur = img->history_current;
    if (!cur || cur == img->history) return;
    img->history_current = cur->history_prev;
    image_set(img, img->history_current);
    goxel_update_meshes(goxel(), true);
    print_history(img);
}

ACTION_REGISTER(img_new_layer,
    .help = "Add a new layer to the image",
    .func = image_add_layer,
    .sig = SIG(TYPE_LAYER, ARG("image", TYPE_IMAGE)),
)

ACTION_REGISTER(img_del_layer,
    .help = "Delete the active layer",
    .func = image_delete_layer,
    .sig = SIG(TYPE_VOID, ARG("image", TYPE_IMAGE),
                          ARG("layer", TYPE_LAYER)),
)

ACTION_REGISTER(img_move_layer,
    .help = "Move the active layer",
    .func = image_move_layer,
    .sig = SIG(TYPE_VOID, ARG("image", TYPE_IMAGE),
                          ARG("layer", TYPE_LAYER),
                          ARG("ofs", TYPE_INT)),
)

ACTION_REGISTER(img_duplicate_layer,
    .help = "Duplicate the active layer",
    .func = image_duplicate_layer,
    .sig = SIG(TYPE_LAYER, ARG("image", TYPE_IMAGE),
                           ARG("layer", TYPE_LAYER)),
)

ACTION_REGISTER(img_merge_visible_layers,
    .help = "Merge all the visible layers",
    .func = image_merge_visible_layers,
    .sig = SIG(TYPE_VOID, ARG("image", TYPE_IMAGE)),
)
