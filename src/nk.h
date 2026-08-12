/*
 * nk.h — the single place Nuklear's configuration is defined.
 *
 * Every translation unit that touches Nuklear includes this and nothing
 * else, so the feature macros cannot drift between files — a mismatch there
 * produces struct layouts that differ per object file and crashes that look
 * like memory corruption.
 *
 * NK_IMPLEMENTATION itself is defined only in sv_ui.c.
 */
#ifndef SV_NK_H
#define SV_NK_H

#include <stdbool.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT

/*
 * 32-bit vertex indices.
 *
 * Nuklear's default is 16-bit, which caps a frame at 65535 vertices — about
 * 16000 glyphs, or 16000 line segments. Four casts overlaid on the profile
 * plot reach that: it asserted and killed the program inside
 * nk_draw_list_alloc_vertices. sv_plot also decimates its polylines to the
 * pixel grid so a frame's vertex count stays bounded regardless of how many
 * samples a cast holds, but the ceiling had to go as well: a hard limit that
 * aborts is not something to leave one cast away from the operator.
 *
 * This requires the matching fix in nuklear_sdl_renderer.h, which hard-codes
 * the index size as 2 bytes in its SDL_RenderGeometryRaw call. See the comment
 * there — it is an upstream bug, not a local choice.
 */
#define NK_UINT_DRAW_INDEX

#include "nuklear.h"

#endif /* SV_NK_H */
