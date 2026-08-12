/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Hugh Frater
 *
 * This file is part of svpview. svpview is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version. It is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License in LICENSE for details.
 */
#ifndef SV_UI_H
#define SV_UI_H

#include <SDL2/SDL.h>
#include "sv_app.h"

typedef struct SvUi SvUi;

SvUi *sv_ui_create(SDL_Window *win, SDL_Renderer *ren, SvApp *app);
void  sv_ui_destroy(SvUi *ui);

/* Resize and recentre the window for the display it opened on. base_w/base_h
 * are the size the layout is designed for at UI scale 1; the UI knows the
 * scale actually in force, so only it can turn that into a sensible window.
 * Also sets the minimum size, below which the fixed regions stop fitting. */
void  sv_ui_fit_window(SvUi *ui, int base_w, int base_h);

/* Bracket the SDL event pump; every event goes to sv_ui_handle_event()
 * between these two calls. */
void  sv_ui_input_begin(SvUi *ui);
void  sv_ui_input_end(SvUi *ui);

/* Feed one SDL event. Returns true if the UI consumed it. */
bool  sv_ui_handle_event(SvUi *ui, SDL_Event *e);

/* Call between event pumping and rendering. */
void  sv_ui_frame(SvUi *ui, int win_w, int win_h);
void  sv_ui_render(SvUi *ui);

/* Background colour for SDL_RenderClear, so the frame has no flash of a
 * different colour behind the UI. */
void  sv_ui_clear_colour(const SvUi *ui, Uint8 *r, Uint8 *g, Uint8 *b);

bool  sv_ui_quit_requested(const SvUi *ui);

#endif /* SV_UI_H */
