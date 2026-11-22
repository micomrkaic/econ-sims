# econ-sims — economics you can watch: four simulations with sliders.
# Native programs need SDL2; the browser builds need Emscripten.
#
#   make            build the four programs into build/
#   make test       run every model's checks, then a headless frame of each
#   make web        build the browser versions into docs/ (needs emcc)
#   make shots      save a screenshot of each simulation into build/ (BMP)
#   make clean
#
# Copyright (C) 2026 Mico Mrkaic. GPL-3.0-or-later; see LICENSE.

SIMS     = schelling wealth bankrun orderbook
CC      ?= cc
CFLAGS  ?= -std=c17 -O2 -Wall -Wextra -Wpedantic
LDLIBS  ?= -lm
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)
KIT      = src/ui.h src/rng.h src/font8x8.h
BINS     = $(SIMS:%=build/%)

.PHONY: all test web shots clean
all: $(BINS)

build/%: src/%.c $(KIT)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS) $(LDLIBS)

# The model checks need no display. The frame run does, so it uses SDL's
# dummy video driver: it proves the drawing code runs, on any machine.
test: $(BINS)
	@for s in $(SIMS); do ./build/$$s --test || exit 1; done
	@for s in $(SIMS); do SDL_VIDEODRIVER=dummy ./build/$$s --frames 30 || exit 1; done
	@echo "frames: each simulation drew 30 frames headless"

shots: $(BINS)
	@mkdir -p build
	@for s in $(SIMS); do SDL_VIDEODRIVER=dummy ./build/$$s --frames 400 --shot build/$$s.bmp || exit 1; done
	@echo "wrote build/*.bmp (convert to PNG for docs/img)"

# Browser builds: one page per simulation in docs/, from web/shell.html.
# A distribution's Emscripten (Debian's, under /usr/share) keeps a frozen cache
# a user cannot write to, and SDL2 has to be built into the cache on first
# use; then the cache is copied once to ~/.cache/emscripten and used from
# there (unless EM_CACHE is set already).
EMCC    ?= emcc
EMFLAGS  = -std=gnu17 -O2 -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 -sMINIFY_HTML=0 -sENVIRONMENT=web
web: $(SIMS:%=src/%.c) $(KIT) web/shell.html
	@mkdir -p docs
	@c=$$(em-config CACHE 2>/dev/null); f=$$(em-config FROZEN_CACHE 2>/dev/null); \
	if [ -z "$$EM_CACHE" ] && [ -n "$$c" ] && { [ ! -w "$$c" ] || [ "$$f" = True ]; }; then \
	  EM_CACHE="$$HOME/.cache/emscripten"; EM_FROZEN_CACHE=; export EM_CACHE EM_FROZEN_CACHE; \
	  if [ ! -d "$$EM_CACHE" ]; then mkdir -p "$$EM_CACHE" && cp -R "$$c"/. "$$EM_CACHE"/ && rm -f "$$EM_CACHE"/cache.lock; fi; \
	  echo "Emscripten's cache $$c is read-only (a distribution's): using $$EM_CACHE"; fi; \
	for s in $(SIMS); do \
	  echo "$(EMCC) $(EMFLAGS) --shell-file web/shell.html -o docs/$$s.html src/$$s.c"; \
	  $(EMCC) $(EMFLAGS) --shell-file web/shell.html -o docs/$$s.html src/$$s.c || exit 1; \
	done

clean:
	rm -rf build
