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

#include "nuklear.h"

#endif /* SV_NK_H */
