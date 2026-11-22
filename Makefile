# econ-sims — economics you can watch: simulations with sliders.
# Native programs need SDL2; the browser builds need Emscripten.
#
#   make            build the programs into build/
#   make test       run every model's checks, then a headless frame of each
#   make web        build the browser versions into docs/ (needs emcc)
#   make shots      save a screenshot of each simulation into build/ (BMP)
#   make clean
#
# The macro simulations (MACRO below) carry the Cozy interpreter, vendored in
# third_party/cozy, and its dsge package; see src/macro.h.
#
# Copyright (C) 2026 Mico Mrkaic. GPL-3.0-or-later; see LICENSE.

SIMS     = schelling wealth bankrun orderbook
MACRO    = nk cycles irf
ALL      = $(SIMS) $(MACRO)
CC      ?= cc
CFLAGS  ?= -std=c17 -O2 -Wall -Wextra -Wpedantic
LDLIBS  ?= -lm
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)
KIT      = src/ui.h src/rng.h src/font8x8.h
BINS     = $(ALL:%=build/%)

# Cozy is C23 and is compiled as it comes, with its own flags.
COZY        = third_party/cozy
COZY_SRCS   = lexer arena ast parser value eval chunk compile vm sparse linalg_tier0 embed
COZY_CFLAGS ?= -std=c2x -O2
COZY_O      = $(COZY_SRCS:%=build/cozy/%.o)

.PHONY: all test web shots clean
all: $(BINS)

build/%: src/%.c $(KIT)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS) $(LDLIBS)

build/cozy/%.o: $(COZY)/%.c $(wildcard $(COZY)/*.h $(COZY)/*.inc)
	@mkdir -p build/cozy
	$(CC) $(COZY_CFLAGS) -c $< -o $@
build/libcozy.a: $(COZY_O)
	rm -f $@ && ar rcs $@ $^

# The dsge package rides inside each macro simulation as text.
build/txt2c: tools/txt2c.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $<
build/dsge_cz.h: $(COZY)/packages/dsge.cz build/txt2c
	./build/txt2c DSGE_CZ < $< > $@

$(MACRO:%=build/%): build/%: src/%.c $(KIT) src/macro.h src/models.h build/dsge_cz.h build/libcozy.a
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -Ibuild -I$(COZY) -o $@ $< build/libcozy.a $(SDL_LIBS) $(LDLIBS)

# The model checks need no display. The frame run does, so it uses SDL's
# dummy video driver: it proves the drawing code runs, on any machine.
test: $(BINS)
	@for s in $(ALL); do ./build/$$s --test || exit 1; done
	@for s in $(ALL); do SDL_VIDEODRIVER=dummy ./build/$$s --frames 30 || exit 1; done
	@echo "frames: each simulation drew 30 frames headless"

shots: $(BINS)
	@mkdir -p build
	@for s in $(ALL); do SDL_VIDEODRIVER=dummy ./build/$$s --frames 400 --shot build/$$s.bmp || exit 1; done
	@echo "wrote build/*.bmp (convert to PNG for docs/img)"

# Browser builds: one page per simulation in docs/, from web/shell.html.
# A distribution's Emscripten (Debian's, under /usr/share) keeps a frozen cache
# a user cannot write to, and SDL2 has to be built into the cache on first
# use; then the cache is copied once to ~/.cache/emscripten and used from
# there (unless EM_CACHE is set already).
EMCC    ?= emcc
EMFLAGS  = -std=gnu17 -O2 -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 -sMINIFY_HTML=0 -sENVIRONMENT=web
# The macro simulations link Cozy, compiled to WebAssembly here. Cozy
# reports errors with longjmp and recurses, hence the two settings. An
# Emscripten whose clang predates C23's keywords gets them as macros.
COZY_EMFLAGS  = -std=gnu2x -O2 -sSUPPORT_LONGJMP=1
MACRO_EMFLAGS = $(EMFLAGS) -sSUPPORT_LONGJMP=1 -sTOTAL_STACK=2097152 -Ibuild -I$(COZY)
C23_SHIMS     = -Dnullptr=NULL -Dalignof=_Alignof -Dtypeof=__typeof__ -Dstatic_assert=_Static_assert
web: $(ALL:%=src/%.c) $(KIT) src/macro.h src/models.h build/dsge_cz.h web/shell.html
	@mkdir -p docs build/web
	@c=$$(em-config CACHE 2>/dev/null); f=$$(em-config FROZEN_CACHE 2>/dev/null); \
	if [ -z "$$EM_CACHE" ] && [ -n "$$c" ] && { [ ! -w "$$c" ] || [ "$$f" = True ]; }; then \
	  EM_CACHE="$$HOME/.cache/emscripten"; EM_FROZEN_CACHE=; export EM_CACHE EM_FROZEN_CACHE; \
	  if [ ! -d "$$EM_CACHE" ]; then mkdir -p "$$EM_CACHE" && cp -R "$$c"/. "$$EM_CACHE"/ && rm -f "$$EM_CACHE"/cache.lock; fi; \
	  echo "Emscripten's cache $$c is read-only (a distribution's): using $$EM_CACHE"; fi; \
	for s in $(SIMS); do \
	  echo "$(EMCC) $(EMFLAGS) --shell-file web/shell.html -o docs/$$s.html src/$$s.c"; \
	  $(EMCC) $(EMFLAGS) --shell-file web/shell.html -o docs/$$s.html src/$$s.c || exit 1; \
	done; \
	shims=; printf 'void *p = nullptr;\n' > build/web/c23.c; \
	$(EMCC) -std=gnu2x -fsyntax-only build/web/c23.c 2>/dev/null || shims="$(C23_SHIMS)"; \
	objs=; for f in $(COZY_SRCS); do \
	  echo "$(EMCC) $(COZY_EMFLAGS) $$shims -c $(COZY)/$$f.c"; \
	  $(EMCC) $(COZY_EMFLAGS) $$shims -c $(COZY)/$$f.c -o build/web/$$f.o || exit 1; objs="$$objs build/web/$$f.o"; \
	done; \
	for s in $(MACRO); do \
	  echo "$(EMCC) $(MACRO_EMFLAGS) --shell-file web/shell.html -o docs/$$s.html src/$$s.c (with Cozy)"; \
	  $(EMCC) $(MACRO_EMFLAGS) --shell-file web/shell.html -o docs/$$s.html src/$$s.c $$objs || exit 1; \
	done

clean:
	rm -rf build
