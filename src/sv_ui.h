#ifndef SV_UI_H
#define SV_UI_H

#include <SDL2/SDL.h>
#include "sv_app.h"

typedef struct SvUi SvUi;

SvUi *sv_ui_create(SDL_Window *win, SDL_Renderer *ren, SvApp *app);
void  sv_ui_destroy(SvUi *ui);

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
