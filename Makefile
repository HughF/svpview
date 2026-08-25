# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Hugh Frater
#
# svpview — Valeport SWiFT profiler acquisition and display
#
#   make            build ./svpview
#   make test       build and run the unit tests (ASan + UBSan)
#   make debug      sanitised build of the application itself
#   make windows    cross-compile with mingw-w64
#   make clean
#
# Dependencies: SDL2 only. Nuklear is vendored in third_party/.
#   Debian/Ubuntu   sudo apt install libsdl2-dev
#   Arch            sudo pacman -S sdl2
#   macOS           brew install sdl2

CC      ?= cc
UNAME   := $(shell uname -s)

SRC_DIR  = src
TEST_DIR = tests
OBJ_DIR  = build

WARN    = -Wall -Wextra -Wshadow -Wpointer-arith -Wstrict-prototypes \
          -Wno-unused-parameter

# -MMD -MP generates a .d file per object listing the headers it included.
# Without this, changing a struct in a header rebuilds only some of the
# objects that use it and the rest keep the old layout — which does not fail
# to link, it just silently reads the wrong fields at runtime.
CFLAGS  = -std=c99 -O2 $(WARN) -MMD -MP -I$(SRC_DIR) -Ithird_party
LDLIBS  = -lm

# Nuklear's single-header implementation trips these in code we do not own.
NK_CFLAGS = -Wno-unused-function -Wno-sign-compare -Wno-implicit-fallthrough

ifeq ($(UNAME),Darwin)
    CFLAGS += -D_DARWIN_C_SOURCE
else
    CFLAGS += -D_DEFAULT_SOURCE
endif

SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)

# ---------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------

# Portable core plus the app: no SDL, and the unit tests link against these.
CORE = sv_ocean sv_geo sv_proto sv_binfile sv_profile sv_export sv_vigo \
       sv_config sv_sim sv_app sv_help

ifeq ($(OS),Windows_NT)
    PLAT = plat_win32
else
    PLAT = plat_posix
endif

UI   = sv_theme sv_plot sv_chart sv_ui sv_main

OBJS = $(addprefix $(OBJ_DIR)/,$(addsuffix .o,$(CORE) $(PLAT) $(UI)))

TARGET = svpview

TESTS = test_ocean test_geo test_proto test_binfile test_vigo test_profile

# ---------------------------------------------------------------------

.PHONY: all clean test debug windows windows-dist run help-doc

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(SDL_LIBS) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(NK_CFLAGS) -c $< -o $@

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

# ---------------------------------------------------------------------
# The manual lives in src/sv_help.c and is rendered two ways: the Help
# page draws it, and this writes it out. docs/HELP.md is generated —
# edit the source, not the Markdown.
# ---------------------------------------------------------------------

help-doc: $(TARGET)
	./$(TARGET) --help-doc > docs/HELP.md
	@echo "docs/HELP.md regenerated"


-include $(OBJS:.o=.d)

# ---------------------------------------------------------------------
# Tests — always sanitised. A test that only passes without ASan has not
# passed.
# ---------------------------------------------------------------------

TEST_CFLAGS = -std=c99 -O1 -g $(WARN) -I$(SRC_DIR) \
              -fsanitize=address,undefined -fno-omit-frame-pointer
ifeq ($(UNAME),Darwin)
    TEST_CFLAGS += -D_DARWIN_C_SOURCE
else
    TEST_CFLAGS += -D_DEFAULT_SOURCE
endif

TEST_SRCS = $(SRC_DIR)/sv_ocean.c $(SRC_DIR)/sv_geo.c $(SRC_DIR)/sv_proto.c \
            $(SRC_DIR)/sv_binfile.c $(SRC_DIR)/sv_profile.c \
            $(SRC_DIR)/sv_export.c $(SRC_DIR)/sv_vigo.c \
            $(SRC_DIR)/sv_config.c

test:
	@mkdir -p $(OBJ_DIR)
	@fail=0; \
	for t in $(TESTS); do \
	    $(CC) $(TEST_CFLAGS) $(TEST_DIR)/$$t.c $(TEST_SRCS) $(LDLIBS) \
	        -o $(OBJ_DIR)/$$t || exit 1; \
	    $(OBJ_DIR)/$$t || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo "TESTS FAILED"; exit 1; fi; \
	echo "all tests passed"

debug: CFLAGS += -g -O0 -fsanitize=address,undefined
debug: LDLIBS += -fsanitize=address,undefined
debug: clean $(TARGET)

run: $(TARGET)
	./$(TARGET) --sim

# ---------------------------------------------------------------------
# Windows cross-build (mingw-w64)
#
#   make windows        build svpview.exe
#   make windows-dist   exe + SDL2.dll + docs, zipped, in dist/
#
# Needs the mingw toolchain and the SDL2 mingw development SDK:
#
#   sudo pacman -S --needed mingw-w64-gcc          (Arch; apt: mingw-w64)
#   tools/win/get-sdl2.sh                          (downloads and unpacks)
#
# Arch has no mingw pkg-config and no mingw SDL2 package, so the SDK is
# pointed at by path rather than discovered. Override for another location:
#
#   make windows SDL2_MINGW=/path/to/SDL2-2.x.y/x86_64-w64-mingw32
#
# Objects go in their own directory. Sharing build/ with the native build
# links host .o files into the .exe, which fails in ways that look like the
# source is wrong.
# ---------------------------------------------------------------------

SDL2_VER   ?= 2.32.10
SDL2_MINGW ?= tools/win/SDL2-$(SDL2_VER)/x86_64-w64-mingw32

WIN_CC     = x86_64-w64-mingw32-gcc
WIN_RC     = x86_64-w64-mingw32-windres
WIN_OBJ    = $(OBJ_DIR)/win
WIN_TARGET = svpview.exe
WIN_RES    = $(WIN_OBJ)/svpview_res.o
WIN_DIST   = dist/svpview-$(SV_VERSION)-win64

# The version in the file's Properties tab is read out of the same header the
# About dialog reads, so the two cannot disagree. Leading zeros are stripped:
# the resource compiler reads 08 as a malformed octal constant.
SV_VERSION := $(shell sed -n 's/.*SVPVIEW_VERSION "\([^"]*\)".*/\1/p' \
                      $(SRC_DIR)/sv_version.h)
SV_VER_A   := $(shell echo $(SV_VERSION) | cut -d. -f1 | sed 's/^0*//')
SV_VER_B   := $(shell echo $(SV_VERSION) | cut -d. -f2 | sed 's/^0*//')
SV_VER_C   := $(shell echo $(SV_VERSION) | cut -d. -f3 | sed 's/^0*//')

WIN_OBJS = $(addprefix $(WIN_OBJ)/,\
             $(addsuffix .o,$(CORE) plat_win32 $(UI)))

# _USE_MATH_DEFINES: mingw's math.h hides M_PI in strict C99, which glibc
# does not, so sv_ocean stops compiling at the first line of gravity.
# __USE_MINGW_ANSI_STDIO: use mingw's own printf rather than the one in
# MSVCRT, which has no %zu and would print the literal text instead.
# Both SDL include paths: the program says <SDL2/SDL.h>, and the vendored
# nuklear_sdl_renderer.h says <SDL.h>, which is what sdl2-config's -I gives
# it on the native build.
WIN_CFLAGS = -std=c99 -O2 $(WARN) -MMD -MP -I$(SRC_DIR) -Ithird_party \
             -D_USE_MATH_DEFINES -D__USE_MINGW_ANSI_STDIO=1 \
             -I$(SDL2_MINGW)/include -I$(SDL2_MINGW)/include/SDL2

# -mwindows: no console window behind the application. --help and --help-doc
# reattach to the parent console themselves, so nothing is lost from a
# command prompt. -static-libgcc so the only DLL to ship is SDL2's.
WIN_LDFLAGS = -mwindows -static-libgcc -L$(SDL2_MINGW)/lib
WIN_LIBS    = -lmingw32 -lSDL2main -lSDL2 \
              -lws2_32 -liphlpapi -lsetupapi -luuid -lm

windows: $(WIN_TARGET)

$(WIN_TARGET): $(WIN_OBJS) $(WIN_RES)
	@test -f $(SDL2_MINGW)/lib/libSDL2.a || { \
	    echo "No SDL2 mingw SDK at $(SDL2_MINGW)"; \
	    echo "Run tools/win/get-sdl2.sh, or set SDL2_MINGW=<dir>"; \
	    exit 1; }
	$(WIN_CC) $(WIN_OBJS) $(WIN_RES) -o $@ $(WIN_LDFLAGS) $(WIN_LIBS)
	@echo "built $@ — ship it with $(SDL2_MINGW)/bin/SDL2.dll"

$(WIN_OBJ)/%.o: $(SRC_DIR)/%.c | $(WIN_OBJ)
	$(WIN_CC) $(WIN_CFLAGS) $(NK_CFLAGS) -c $< -o $@

$(WIN_RES): tools/win/svpview.rc tools/win/svpview.ico \
            tools/win/svpview.manifest $(SRC_DIR)/sv_version.h | $(WIN_OBJ)
	$(WIN_RC) -I tools/win -I $(SRC_DIR) \
	    -DSV_VER_A=$(SV_VER_A) -DSV_VER_B=$(SV_VER_B) -DSV_VER_C=$(SV_VER_C) \
	    -o $@ tools/win/svpview.rc

$(WIN_OBJ):
	@mkdir -p $(WIN_OBJ)

-include $(WIN_OBJS:.o=.d)

windows-dist: $(WIN_TARGET) help-doc
	@rm -rf $(WIN_DIST)
	@mkdir -p $(WIN_DIST)
	cp $(WIN_TARGET) $(WIN_DIST)/
	cp $(SDL2_MINGW)/bin/SDL2.dll $(WIN_DIST)/
	cp LICENSE $(WIN_DIST)/LICENSE.txt
	cp docs/HELP.md $(WIN_DIST)/
	cd dist && zip -qr $(notdir $(WIN_DIST)).zip $(notdir $(WIN_DIST))
	@echo "packaged dist/$(notdir $(WIN_DIST)).zip"

clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(WIN_TARGET) dist
