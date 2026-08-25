# Vendored third-party code

svpview itself is GPL-3.0-or-later (see `../LICENSE`). The files in this
directory are **not** ours and keep their own licences, all of which are
GPL-compatible:

| File | Origin | Licence |
|---|---|---|
| `nuklear.h` | [Immediate-Mode-UI/Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) by Micha Mettke | MIT **or** Unlicense (public domain), at your option. Embeds `stb_textedit`, `stb_truetype` and `stb_rect_pack` by Sean Barrett (public domain / MIT) and ProggyClean.ttf by Tristan Grimmer (MIT) |
| `nuklear_sdl_renderer.h` | the same project's SDL2 renderer backend | same as `nuklear.h` |

SDL2 is linked, not vendored, and is under the zlib licence.

`../tools/linux/font/DejaVuSans.ttf` is carried for the same reason a third-
party file would be: the Linux AppImage ships it so the interface looks the
same on a machine with no fonts installed, where the program would otherwise
fall back to Nuklear's built-in bitmap face. It is under the DejaVu Fonts
licence — the Bitstream Vera terms, free to use, redistribute and modify —
and the full text travels with it as `DejaVu-LICENSE.txt`, inside the
AppImage at `usr/share/doc/svpview/`. The Windows and native Linux builds do
not use it; they take the system face.

`nuklear_sdl_renderer.h` carries **one local modification**, marked in the file:
upstream hard-codes the vertex index size as 2 bytes in its
`SDL_RenderGeometryRaw` call, which is wrong when `NK_UINT_DRAW_INDEX` is
defined — SDL then reads 32-bit indices as 16-bit and draws garbage. Worth
sending upstream. Do not re-vendor this file without re-applying that fix.
