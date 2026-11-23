/* ui.h — the small kit the simulations share: a window, a bitmap font,
 * sliders, buttons, tiles and plot helpers, on plain SDL2. Immediate mode:
 * each frame the program draws its controls and learns what changed.
 * Header-only on purpose, so every simulation stays one C file plus this.
 *
 * Part of econ-sims. Copyright (C) 2026 Mico Mrkaic.
 * GPL-3.0-or-later; see LICENSE. */
#ifndef ECON_UI_H
#define ECON_UI_H

#include <SDL.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "font8x8.h"

/* The canvas is fixed; the window may be resized and SDL scales it. */
#define UI_W 1180
#define UI_H 720
#define UI_PANEL_X 792              /* controls live to the right of this */
#define UI_PANEL_W (UI_W - UI_PANEL_X - 16)

typedef struct { Uint8 r, g, b; } UiColor;
/* Dark theme. The three series colours are a validated categorical set
 * (colour-blind separation and contrast checked against this surface). */
static const UiColor UI_BG = {11, 14, 20}, UI_SURFACE = {17, 21, 31}, UI_INK = {240, 239, 236},
    UI_INK2 = {195, 199, 208}, UI_MUTED = {138, 147, 163}, UI_GRID = {33, 40, 56}, UI_AXIS = {50, 59, 79},
    UI_S1 = {57, 135, 229}, UI_S2 = {217, 89, 38}, UI_S3 = {25, 158, 112}, UI_BAD = {208, 59, 59},
    UI_TRACK = {40, 48, 66};

/* Text sizes: glyphs are 8x8; BODY doubles the height (the VGA 8x16 look). */
enum { UI_SMALL = 0, UI_BODY = 1, UI_BIG = 2 };

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *font;
    int mx, my;                 /* mouse, in canvas coordinates */
    bool down, pressed;         /* button held; went down this frame */
    int active;                 /* id of the slider being dragged, 0 if none */
    int next_id;
    bool quit, paused, reset;   /* reset: R was pressed this frame */
    long frame;
    long max_frames;            /* --frames N: stop after N frames (0 = run) */
    const char *shot;           /* --shot FILE: save the last frame as BMP */
    void (*draw)(void);
} Ui;
static Ui ui;

/* ---- command line shared by every simulation ------------------------- */
/* Returns true if --test was given (the caller runs its model checks and
 * exits without opening a window). */
static inline bool ui_args(int argc, char **argv, const char *name, const char *what)
{
    bool test = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--test")) test = true;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) ui.max_frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) ui.shot = argv[++i];
        else {
            printf("%s — %s\n\n"
                   "  %s               open the window\n"
                   "  %s --test        run the model checks and exit (no window)\n"
                   "  %s --frames N    run N frames and exit\n"
                   "  %s --shot F.bmp  with --frames: save the last frame\n\n"
                   "In the window: drag the sliders; Space pauses, R restarts, Q quits.\n",
                   name, what, name, name, name, name);
            exit(strcmp(argv[i], "--help") && strcmp(argv[i], "-h") ? 2 : 0);
        }
    }
    return test;
}

/* ---- window ---------------------------------------------------------- */
static inline bool ui_open(const char *title)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return false; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    /* In the browser the canvas keeps its 1180 x 720 pixels and the page
     * scales it with CSS; a resizable SDL window would resize the canvas
     * to the page's box instead and letterbox the picture. */
#ifdef __EMSCRIPTEN__
    Uint32 flags = 0;
#else
    Uint32 flags = SDL_WINDOW_RESIZABLE;
#endif
    ui.win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, UI_W, UI_H, flags);
    if (!ui.win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
    ui.ren = SDL_CreateRenderer(ui.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ui.ren) ui.ren = SDL_CreateRenderer(ui.win, -1, SDL_RENDERER_SOFTWARE);
    if (!ui.ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return false; }
    SDL_RenderSetLogicalSize(ui.ren, UI_W, UI_H);
    /* the font as one texture: 128 glyphs in a 16x8 grid, white on clear */
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 128, 64, 32, SDL_PIXELFORMAT_RGBA32);
    if (!s) return false;
    SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 255, 255, 255, 0));
    for (int c = 0; c < 128; c++)
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < 8; col++)
                if (font8x8_basic[c][row] & (1 << col)) {
                    Uint32 *px = (Uint32 *)((Uint8 *)s->pixels + ((c / 16) * 8 + row) * s->pitch) + (c % 16) * 8 + col;
                    *px = SDL_MapRGBA(s->format, 255, 255, 255, 255);
                }
    ui.font = SDL_CreateTextureFromSurface(ui.ren, s);
    SDL_FreeSurface(s);
    if (!ui.font) return false;
    SDL_SetTextureBlendMode(ui.font, SDL_BLENDMODE_BLEND);
    return true;
}

static inline void ui_events(void)
{
    SDL_Event e;
    ui.pressed = false; ui.reset = false;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) ui.quit = true;
        else if (e.type == SDL_MOUSEMOTION) { ui.mx = e.motion.x; ui.my = e.motion.y; }
        else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            ui.mx = e.button.x; ui.my = e.button.y; ui.down = true; ui.pressed = true;
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) { ui.down = false; ui.active = 0; }
        else if (e.type == SDL_KEYDOWN) {
            SDL_Keycode k = e.key.keysym.sym;
            if (k == SDLK_SPACE) ui.paused = !ui.paused;
            else if (k == SDLK_r) ui.reset = true;
#ifndef __EMSCRIPTEN__
            else if (k == SDLK_q || k == SDLK_ESCAPE) ui.quit = true;
#endif
        }
    }
    ui.next_id = 0;
}

/* ---- drawing --------------------------------------------------------- */
static inline void ui_color(UiColor c) { SDL_SetRenderDrawColor(ui.ren, c.r, c.g, c.b, 255); }
static inline void ui_rect(int x, int y, int w, int h, UiColor c)
{
    SDL_Rect r = {x, y, w, h};
    ui_color(c); SDL_RenderFillRect(ui.ren, &r);
}
static inline void ui_frame_rect(int x, int y, int w, int h, UiColor c)
{
    SDL_Rect r = {x, y, w, h};
    ui_color(c); SDL_RenderDrawRect(ui.ren, &r);
}
static inline void ui_line(int x1, int y1, int x2, int y2, UiColor c) { ui_color(c); SDL_RenderDrawLine(ui.ren, x1, y1, x2, y2); }
/* a line two pixels thick: the data lines of the plots */
static inline void ui_line2(int x1, int y1, int x2, int y2, UiColor c)
{
    ui_color(c);
    SDL_RenderDrawLine(ui.ren, x1, y1, x2, y2);
    if (abs(x2 - x1) >= abs(y2 - y1)) SDL_RenderDrawLine(ui.ren, x1, y1 + 1, x2, y2 + 1);
    else SDL_RenderDrawLine(ui.ren, x1 + 1, y1, x2 + 1, y2);
}
static inline void ui_clear(void) { ui_color(UI_BG); SDL_RenderClear(ui.ren); }

static inline int ui_glyph_w(int size) { return size == UI_BIG ? 16 : 8; }
static inline int ui_glyph_h(int size) { return size == UI_SMALL ? 8 : size == UI_BODY ? 16 : 32; }
static inline int ui_text_w(const char *s, int size) { return (int)strlen(s) * ui_glyph_w(size); }
static inline void ui_text(int x, int y, int size, UiColor c, const char *s)
{
    int gw = ui_glyph_w(size), gh = ui_glyph_h(size);
    SDL_SetTextureColorMod(ui.font, c.r, c.g, c.b);
    for (; *s; s++, x += gw) {
        unsigned ch = (unsigned char)*s;
        if (ch >= 128 || ch == ' ') continue;
        SDL_Rect src = {(int)(ch % 16) * 8, (int)(ch / 16) * 8, 8, 8}, dst = {x, y, gw, gh};
        SDL_RenderCopy(ui.ren, ui.font, &src, &dst);
    }
}
static inline void ui_textf(int x, int y, int size, UiColor c, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    ui_text(x, y, size, c, buf);
}
static inline void ui_text_right(int xr, int y, int size, UiColor c, const char *s) { ui_text(xr - ui_text_w(s, size), y, size, c, s); }

/* ---- controls -------------------------------------------------------- */
static inline bool ui_hit(int x, int y, int w, int h) { return ui.mx >= x && ui.mx < x + w && ui.my >= y && ui.my < y + h; }

/* A slider row, 46 pixels tall: label left, value right, track below.
 * Returns true in the frames where the value changes. */
static inline bool ui_slider(int x, int y, int w, const char *label, double *v, double lo, double hi, double step, const char *fmt)
{
    int id = ++ui.next_id, ty = y + 26;
    char val[32];
    snprintf(val, sizeof val, fmt, *v);
    ui_text(x, y, UI_BODY, UI_INK2, label);
    ui_text_right(x + w, y, UI_BODY, UI_INK, val);
    if (ui.pressed && ui_hit(x - 6, ty - 10, w + 12, 24)) ui.active = id;
    bool changed = false;
    if (ui.active == id) {
        double t = (double)(ui.mx - x) / w;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        double nv = lo + t * (hi - lo);
        if (step > 0) nv = lo + floor((nv - lo) / step + 0.5) * step;
        nv = nv < lo ? lo : nv > hi ? hi : nv;
        if (nv != *v) { *v = nv; changed = true; }
    }
    int kx = x + (int)((*v - lo) / (hi - lo) * w + 0.5);
    ui_rect(x, ty, w, 4, UI_TRACK);
    ui_rect(x, ty, kx - x, 4, UI_S1);
    ui_rect(kx - 5, ty - 6, 10, 16, ui.active == id ? UI_INK : UI_INK2);
    return changed;
}

/* A push button; true in the frame it is clicked. */
static inline bool ui_button(int x, int y, int w, const char *label)
{
    bool over = ui_hit(x, y, w, 28);
    ui_rect(x, y, w, 28, over ? UI_AXIS : UI_TRACK);
    ui_frame_rect(x, y, w, 28, UI_AXIS);
    ui_text(x + (w - ui_text_w(label, UI_BODY)) / 2, y + 6, UI_BODY, UI_INK, label);
    return over && ui.pressed;
}

/* A check box with a label; true in the frame it flips. */
static inline bool ui_toggle(int x, int y, const char *label, bool *v)
{
    bool over = ui_hit(x, y, 20 + 8 + ui_text_w(label, UI_BODY), 20);
    ui_frame_rect(x, y, 20, 20, over ? UI_INK : UI_MUTED);
    if (*v) ui_rect(x + 4, y + 4, 12, 12, UI_S1);
    ui_text(x + 28, y + 2, UI_BODY, UI_INK2, label);
    if (over && ui.pressed) { *v = !*v; return true; }
    return false;
}

/* A stat tile: label, a large value, an optional line under it. */
static inline void ui_tile(int x, int y, int w, const char *label, const char *value, const char *sub)
{
    ui_rect(x, y, w, 76, UI_SURFACE);
    ui_frame_rect(x, y, w, 76, UI_GRID);
    ui_text(x + 10, y + 8, UI_SMALL, UI_INK2, label);
    ui_text(x + 10, y + 22, UI_BIG, UI_INK, value);
    if (sub) ui_text(x + 10, y + 60, UI_SMALL, UI_MUTED, sub);
}

/* A legend key: a short stroke (or a box) of the colour, then the name. Returns the x after it. */
static inline int ui_key(int x, int y, UiColor c, bool box, const char *label)
{
    if (box) ui_rect(x, y + 3, 10, 10, c); else ui_rect(x, y + 7, 16, 2, c);
    ui_text(x + (box ? 16 : 22), y, UI_BODY, UI_INK2, label);
    return x + (box ? 16 : 22) + ui_text_w(label, UI_BODY) + 18;
}

/* ---- plots ----------------------------------------------------------- */
typedef struct { int x, y, w, h; double x0, x1, y0, y1; } UiPlot;

static inline double ui_nice_step(double span, int want)
{
    double raw = span / want, mag = pow(10.0, floor(log10(raw))), f = raw / mag;
    return (f <= 1 ? 1 : f <= 2 ? 2 : f <= 2.5 ? 2.5 : f <= 5 ? 5 : 10) * mag;
}
static inline int ui_px(const UiPlot *p, double x) { return p->x + (int)((x - p->x0) / (p->x1 - p->x0) * p->w + 0.5); }
static inline int ui_py(const UiPlot *p, double y) { return p->y + p->h - (int)((y - p->y0) / (p->y1 - p->y0) * p->h + 0.5); }

/* Begin a plot: the area (x, y, w, h) is the data rectangle; the title sits
 * above it, y tick labels to its left, x tick labels below. yfmt/xfmt are
 * printf formats for the tick values (xfmt NULL: no x ticks). */
static inline UiPlot ui_plot(int x, int y, int w, int h, const char *title, double x0, double x1, double y0, double y1,
                      const char *xfmt, const char *yfmt)
{
    UiPlot p = {x, y, w, h, x0, x1, y0, y1};
    char buf[32];
    if (!(p.y1 > p.y0)) p.y1 = p.y0 + 1;
    if (!(p.x1 > p.x0)) p.x1 = p.x0 + 1;
    if (title) ui_text(x, y - 22, UI_BODY, UI_INK, title);
    double st = ui_nice_step(p.y1 - p.y0, 4);
    for (double v = ceil(p.y0 / st - 1e-9) * st; v <= p.y1 + st * 1e-9; v += st) {
        if (fabs(v) < st * 1e-9) v = 0;
        ui_line(x, ui_py(&p, v), x + w, ui_py(&p, v), v == 0 ? UI_AXIS : UI_GRID);
        snprintf(buf, sizeof buf, yfmt, v);
        ui_text_right(x - 6, ui_py(&p, v) - 4, UI_SMALL, UI_MUTED, buf);
    }
    if (xfmt) {
        double sx = ui_nice_step(p.x1 - p.x0, 5);
        for (double v = ceil(p.x0 / sx - 1e-9) * sx; v <= p.x1 + sx * 1e-9; v += sx) {
            if (fabs(v) < sx * 1e-9) v = 0;
            snprintf(buf, sizeof buf, xfmt, v);
            ui_text(ui_px(&p, v) - ui_text_w(buf, UI_SMALL) / 2, y + h + 6, UI_SMALL, UI_MUTED, buf);
        }
    }
    return p;
}

/* A polyline through (xs[i], ys[i]); xs NULL means x = i. Points are clipped to the plot. */
static inline void ui_plot_line(const UiPlot *p, const double *xs, const double *ys, int n, UiColor c)
{
    SDL_Rect clip = {p->x, p->y - 1, p->w + 1, p->h + 3};
    SDL_RenderSetClipRect(ui.ren, &clip);
    for (int i = 1; i < n; i++)
        ui_line2(ui_px(p, xs ? xs[i - 1] : i - 1), ui_py(p, ys[i - 1]), ui_px(p, xs ? xs[i] : i), ui_py(p, ys[i]), c);
    SDL_RenderSetClipRect(ui.ren, NULL);
}

/* A history buffer for "value over time" plots: keeps the last cap points. */
#define UI_HIST_CAP 600
typedef struct { double v[UI_HIST_CAP]; int n; } UiHist;
static inline void ui_hist_push(UiHist *h, double v)
{
    if (h->n == UI_HIST_CAP) { memmove(h->v, h->v + 1, (UI_HIST_CAP - 1) * sizeof(double)); h->n--; }
    h->v[h->n++] = v;
}

/* Marks on a scrolling plot whose x axis is "periods ago" (0 at the right
 * edge): a dashed vertical line where something happened, with a label. */
#define UI_MARKS_CAP 24
typedef struct { int age[UI_MARKS_CAP]; const char *label[UI_MARKS_CAP]; int n; } UiMarks;
static inline void ui_marks_add(UiMarks *m, const char *label)
{
    if (m->n == UI_MARKS_CAP) { memmove(m->age, m->age + 1, sizeof m->age - sizeof m->age[0]); memmove(m->label, m->label + 1, sizeof m->label - sizeof m->label[0]); m->n--; }
    m->age[m->n] = 0; m->label[m->n] = label; m->n++;
}
/* One period passes; marks older than `keep` periods are dropped. */
static inline void ui_marks_tick(UiMarks *m, int keep)
{
    int k = 0;
    for (int i = 0; i < m->n; i++) if (m->age[i] + 1 < keep) { m->age[k] = m->age[i] + 1; m->label[k] = m->label[i]; k++; }
    m->n = k;
}
static inline void ui_marks_draw(const UiMarks *m, const UiPlot *p, bool labels)
{
    for (int i = 0; i < m->n; i++) {
        int x = ui_px(p, -m->age[i]);
        if (x < p->x || x > p->x + p->w) continue;
        for (int y = p->y; y < p->y + p->h; y += 6) ui_line(x, y, x, y + 2 < p->y + p->h ? y + 2 : p->y + p->h, UI_MUTED);
        if (!labels) continue;
        int w = ui_text_w(m->label[i], UI_SMALL);
        ui_text(x + 5 + w <= p->x + p->w ? x + 5 : x - 5 - w, p->y + 3, UI_SMALL, UI_INK2, m->label[i]);
    }
}

/* ---- frame loop, screenshots ----------------------------------------- */
static inline void ui_save_bmp(const char *path)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, UI_W, UI_H, 24, SDL_PIXELFORMAT_BGR24);
    if (!s) return;
    SDL_RenderSetLogicalSize(ui.ren, 0, 0);
    SDL_Rect all = {0, 0, UI_W, UI_H};
    if (SDL_RenderReadPixels(ui.ren, &all, SDL_PIXELFORMAT_BGR24, s->pixels, s->pitch) == 0) SDL_SaveBMP(s, path);
    else fprintf(stderr, "screenshot: %s\n", SDL_GetError());
    SDL_RenderSetLogicalSize(ui.ren, UI_W, UI_H);
    SDL_FreeSurface(s);
}

static inline void ui_one_frame(void)
{
    ui_events();
    ui_clear();
    ui.draw();
    ui.frame++;
    if (ui.max_frames && ui.frame >= ui.max_frames) {
        if (ui.shot) ui_save_bmp(ui.shot);
        ui.quit = true;
    }
    SDL_RenderPresent(ui.ren);
#ifdef __EMSCRIPTEN__
    if (ui.quit) emscripten_cancel_main_loop();
#endif
}

/* Run draw() once per frame until the window closes. */
static inline void ui_run(void (*draw)(void))
{
    ui.draw = draw;
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(ui_one_frame, 0, 1);
#else
    while (!ui.quit) {
        Uint32 t0 = SDL_GetTicks();
        ui_one_frame();
        Uint32 dt = SDL_GetTicks() - t0;
        if (!ui.max_frames && dt < 16) SDL_Delay(16 - dt);     /* cap near 60 fps if vsync is off */
    }
    SDL_DestroyTexture(ui.font);
    SDL_DestroyRenderer(ui.ren);
    SDL_DestroyWindow(ui.win);
    SDL_Quit();
#endif
}

/* UI_MAIN(app): the program's main, calling app(argc, argv). In the browser
 * main takes no arguments: a page has no command line, and some distribution
 * builds of Emscripten (Ubuntu 24's 3.1.6 with LLVM 15) silently drop a main
 * that takes them — the compiler names it __main_argc_argv, which that older
 * runtime does not look for, and the page comes up empty. */
#ifdef __EMSCRIPTEN__
#define UI_MAIN(app) int main(void) { return app(0, NULL); }
#else
#define UI_MAIN(app) int main(int argc, char **argv) { return app(argc, argv); }
#endif

/* The standard page furniture: title, one line of explanation, the key hints. */
static inline void ui_header(const char *title, const char *line)
{
    ui_text(16, 12, UI_BIG, UI_INK, title);
    ui_text(16, 50, UI_BODY, UI_MUTED, line);
    ui_text(16, UI_H - 16, UI_SMALL, UI_MUTED,
#ifdef __EMSCRIPTEN__
            "Space: pause   R: restart"
#else
            "Space: pause   R: restart   Q: quit"
#endif
            );
    if (ui.paused) ui_text_right(UI_PANEL_X - 16, 12, UI_BODY, UI_S2, "PAUSED");
}

#endif
