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
CORE = sv_ocean sv_proto sv_binfile sv_profile sv_export sv_vigo sv_config \
       sv_sim sv_app

ifeq ($(OS),Windows_NT)
    PLAT = plat_win32
else
    PLAT = plat_posix
endif

UI   = sv_theme sv_plot sv_ui sv_main

OBJS = $(addprefix $(OBJ_DIR)/,$(addsuffix .o,$(CORE) $(PLAT) $(UI)))

TARGET = svpview

TESTS = test_ocean test_proto test_binfile test_vigo test_profile

# ---------------------------------------------------------------------

.PHONY: all clean test debug windows run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(SDL_LIBS) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(NK_CFLAGS) -c $< -o $@

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

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

TEST_SRCS = $(SRC_DIR)/sv_ocean.c $(SRC_DIR)/sv_proto.c \
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
# Windows cross-build
# ---------------------------------------------------------------------

windows:
	$(MAKE) CC=x86_64-w64-mingw32-gcc OS=Windows_NT \
	        SDL_CFLAGS="$$(x86_64-w64-mingw32-pkg-config --cflags sdl2)" \
	        SDL_LIBS="$$(x86_64-w64-mingw32-pkg-config --libs sdl2)" \
	        TARGET=svpview.exe

clean:
	rm -rf $(OBJ_DIR) $(TARGET) svpview.exe
