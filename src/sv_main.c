/*
 * sv_main.c — entry point and frame loop
 *
 *   svpview [--sim] [--open FILE]
 *
 * --sim runs against an emulated SWiFT that speaks the real wire protocol,
 * so the whole program can be exercised without an instrument.
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "sv_app.h"
#include "sv_ui.h"
#include "sv_version.h"

/* The size the layout is designed for at UI scale 1. sv_ui_fit_window()
 * turns it into the actual opening size once the UI knows the display's
 * scale, so this is not what a HiDPI screen ends up with. */
#define BASE_W 1280
#define BASE_H  820

static void usage(void)
{
    printf("%s %s — %s\n\n", SVPVIEW_NAME, SVPVIEW_VERSION, SVPVIEW_TAGLINE);
    printf("  svpview [options]\n\n"
           "  --sim          run against a simulated instrument\n"
           "  --open FILE    load a .bin logged file at startup\n"
           "  --help         this message\n\n"
           "Environment:\n"
           "  SVPVIEW_SCALE  override the HiDPI UI scale (e.g. 2)\n");
}

int main(int argc, char **argv)
{
    bool simulate = false;
    const char *open_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sim") == 0) {
            simulate = true;
        } else if (strcmp(argv[i], "--open") == 0 && i + 1 < argc) {
            open_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "svpview: unknown argument '%s'\n", argv[i]);
            usage();
            return 2;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

    /* Created hidden and shown after the interface has sized it, so the
     * window doesn't visibly jump from the base size to its real one.
     *
     * The interface keeps the title current as the instrument connects; this
     * is only what shows before the first frame. */
    SDL_Window *win = SDL_CreateWindow(
        SVPVIEW_NAME " " SVPVIEW_VERSION "  -  " SVPVIEW_TAGLINE,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        BASE_W, BASE_H,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren)
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    SvApp *app = sv_app_create(simulate);
    if (!app) {
        fprintf(stderr, "svpview: out of memory\n");
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    SvUi *ui = sv_ui_create(win, ren, app);
    if (!ui) {
        fprintf(stderr, "svpview: cannot create the interface\n");
        sv_app_destroy(app);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    sv_ui_fit_window(ui, BASE_W, BASE_H);
    SDL_ShowWindow(win);

    if (simulate)
        sv_app_connect(app, "sim");

    if (open_path) {
        const char *err = sv_app_open_file(app, open_path);
        if (err)
            fprintf(stderr, "svpview: %s: %s\n", open_path, err);
    }

    while (!sv_ui_quit_requested(ui)) {
        SDL_Event e;
        sv_ui_input_begin(ui);
        while (SDL_PollEvent(&e))
            sv_ui_handle_event(ui, &e);
        sv_ui_input_end(ui);

        sv_app_poll(app);

        int w = 0, h = 0;
        SDL_GetRendererOutputSize(ren, &w, &h);
        sv_ui_frame(ui, w, h);

        Uint8 r, g, b;
        sv_ui_clear_colour(ui, &r, &g, &b);
        SDL_SetRenderDrawColor(ren, r, g, b, 255);
        SDL_RenderClear(ren);
        sv_ui_render(ui);
        SDL_RenderPresent(ren);
    }

    sv_ui_destroy(ui);
    sv_app_destroy(app);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
