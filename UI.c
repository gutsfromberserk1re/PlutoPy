/*
 * Pluto - video editor UI (visual only, no backend)
 * Layout: Clay (https://github.com/nicbarker/clay, v0.14 API)
 * Renderer: raylib (custom renderer included below, no external clay renderer needed)
 *
 * Build (clay.h must sit next to this file):
 *   Linux:   gcc -O2 pluto_ui.c -o pluto -lraylib -lm -lGL -lpthread -ldl -lrt -lX11
 *   macOS:   clang -O2 pluto_ui.c -o pluto -lraylib -lm -framework IOKit -framework Cocoa -framework OpenGL
 *   Windows: gcc -O2 pluto_ui.c -o pluto.exe -lraylib -lopengl32 -lgdi32 -lwinmm
 *
 * Buttons only give hover / press feedback. The only things that change state
 * are the tab bars and the accordions, so you can see both of your screenshots.
 */
/* ClayCompatDeclaration: Clay v0.14 single-declaration CLAY() API port. */
#define CLAY_IMPLEMENTATION
#include "clay.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* palette                                                             */
/* ------------------------------------------------------------------ */
#define RGB(r,g,b)     ((Clay_Color){r,g,b,255})
#define RGBA(r,g,b,a)  ((Clay_Color){r,g,b,a})
#define C_BG        RGB(14,15,17)
#define C_PANEL     RGB(23,24,28)
#define C_PBORDER   RGB(36,38,43)
#define C_CARD      RGB(30,31,36)
#define C_CARDB     RGB(42,44,50)
#define C_FIELD     RGB(38,39,45)
#define C_SEG       RGB(16,17,20)
#define C_SEL       RGB(46,48,55)
#define C_TEXT      RGB(236,238,242)
#define C_DIM       RGB(190,194,202)
#define C_MUTED     RGB(138,142,152)
#define C_ACCENT    RGB(64,120,255)
#define C_ROW       RGB(19,20,24)

/* ------------------------------------------------------------------ */
/* small layout shorthands                                             */
/* ------------------------------------------------------------------ */
#define GROW        CLAY_SIZING_GROW(0)
#define FIT         CLAY_SIZING_FIT(0)
#define FIX(n)      CLAY_SIZING_FIXED(n)
#define COLM        .layoutDirection = CLAY_TOP_TO_BOTTOM
#define AL_C        {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
#define AL_Y        {.y = CLAY_ALIGN_Y_CENTER}
#define HSPACE      CLAY({.layout = {.sizing = {.width = GROW}}}) {}
#define VSPACE      CLAY({.layout = {.sizing = {.height = GROW}}}) {}
#define GAPX(n)     CLAY({.layout = {.sizing = {.width = FIX(n), .height = FIX(1)}}}) {}
#define PANEL       .backgroundColor = C_PANEL, .cornerRadius = CLAY_CORNER_RADIUS(16), \
                    .border = {.color = RGB(45,47,53), .width = CLAY_BORDER_OUTSIDE(1)}
#define CARD        .backgroundColor = C_CARD, .cornerRadius = CLAY_CORNER_RADIUS(14), \
                    .border = {.color = C_CARDB, .width = CLAY_BORDER_OUTSIDE(1)}

/* ------------------------------------------------------------------ */
/* custom-drawn element kinds                                          */
/* ------------------------------------------------------------------ */
enum {
    I_NONE, I_CROP, I_POS, I_SCALE, I_RESET, I_LINK, I_ROT, I_PLUS, I_CHEV_R, I_CHEV_L,
    I_CHEV_UP, I_CHEV_DN, I_SEARCH, I_LIBRARY, I_UNDO, I_REDO, I_EXPORT, I_NOTE, I_PALETTE,
    I_GRADING, I_SLIDERS, I_SUN, I_DROP, I_MOUNTAIN, I_DISTORT, I_SKIP_B, I_SKIP_F, I_PLAY,
    I_LOOP, I_GRID, I_POINTER, I_HAND, I_SCISSORS, I_TEXT, I_TRASH, I_KF_PREV, I_KF_NEXT,
    I_KF_DIAMOND, I_ZOOM_OUT, I_ZOOM_IN, I_MAGNET, I_FILM,
    K_THUMB = 100, K_KNOB, K_WHEEL, K_SWITCH, K_SLIDER, K_ROTRULER, K_RULER, K_PREVIEW,
    K_CLIP, K_SCROLLBAR
};

typedef struct { int kind; Clay_Color a, b, c; float v, f; int i; } Cust;
static Cust gCust[4096];
static int  gCustN;
static Cust *NC(int k, Clay_Color a, Clay_Color b, Clay_Color c, float v, float f, int i) {
    Cust *p = &gCust[gCustN++ & 4095];
    p->kind = k; p->a = a; p->b = b; p->c = c; p->v = v; p->f = f; p->i = i;
    return p;
}

/* ------------------------------------------------------------------ */
/* globals / state                                                     */
/* ------------------------------------------------------------------ */
static Font  gFonts[2];
static float gDt;
static bool  gHand;
static int   gLeftTab = 0, gRightTab = 0;
static bool  gAcc[5] = {true, false, false, false, false};

/* playhead is drawn after everything else so rows never cover it */
static bool gPH; static Vector2 gPHPos; static float gPHLen;
static bool gWindowDrag; static Vector2 gDragMouse, gDragWindow;

/* ------------------------------------------------------------------ */
/* animation table (smooth exponential easing, keyed by element id)    */
/* ------------------------------------------------------------------ */
typedef struct { uint32_t id; float v; bool used; } AnimSlot;
static AnimSlot gAnim[2048];

static float Anim(uint32_t key, float target, float speed) {
    for (int i = 0; i < 2048; i++) {
        if (gAnim[i].used && gAnim[i].id == key) {
            float d = target - gAnim[i].v;
            gAnim[i].v += d * (1.0f - expf(-speed * gDt));
            if (fabsf(target - gAnim[i].v) < 0.0008f) gAnim[i].v = target;
            return gAnim[i].v;
        }
    }
    for (int i = 0; i < 2048; i++)
        if (!gAnim[i].used) { gAnim[i].used = true; gAnim[i].id = key; gAnim[i].v = target; return target; }
    return target;
}

static Clay_Color Mix(Clay_Color a, Clay_Color b, float t) {
    if (t < 0) t = 0; if (t > 1) t = 1;
    return (Clay_Color){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
static Clay_Color Dark(Clay_Color c, float t) { return Mix(c, (Clay_Color){0, 0, 0, c.a}, t); }

typedef struct { float hover, press; bool clicked; } Bs;
static Bs Interact(Clay_ElementId id) {
    bool over = Clay_PointerOver(id);
    bool down = over && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (over) gHand = true;
    Bs b;
    b.hover   = Anim(id.id, over ? 1.f : 0.f, 18.f);
    b.press   = Anim(id.id ^ 0x9E3779B9u, down ? 1.f : 0.f, 32.f);
    b.clicked = over && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    return b;
}
static Clay_Color BtnBg(Clay_Color base, Clay_Color hov, Bs b) { return Dark(Mix(base, hov, b.hover), b.press * 0.25f); }

/* ------------------------------------------------------------------ */
/* text + icon helpers                                                 */
/* ------------------------------------------------------------------ */
static Clay_String S(const char *s) { return (Clay_String){.isStaticallyAllocated = false, .length = (int32_t)strlen(s), .chars = s}; }

static void Tx(const char *s, float size, Clay_Color c, int font) {
    CLAY_TEXT(S(s), CLAY_TEXT_CONFIG({.fontId = font, .fontSize = size, .textColor = c, .wrapMode = CLAY_TEXT_WRAP_NONE}));
}
static void IconV(int k, float sz, Clay_Color c, float v) {
    CLAY({.layout = {.sizing = {.width = FIX(sz), .height = FIX(sz)}},
                  .custom = {.customData = NC(k, c, c, c, v, 0, 0)}}) {}
}
static void Icon(int k, float sz, Clay_Color c) { IconV(k, sz, c, 1.f); }

static bool IconBtn(Clay_ElementId id, int icon, float box, float isz, Clay_Color base, bool sel, float rad) {
    Bs b = Interact(id);
    Clay_Color ic = Dark(Mix(base, C_TEXT, b.hover), b.press * 0.2f);
    Clay_Color bg = sel ? Dark(C_SEL, b.press * 0.3f) : RGBA(255, 255, 255, 14 * b.hover + 12 * b.press);
    CLAY({ .id = (id), .layout = {.sizing = {FIX(box), FIX(box)}, .childAlignment = AL_C},
              .backgroundColor = bg, .cornerRadius = CLAY_CORNER_RADIUS(rad) }) {
        Icon(icon, isz, ic);
    }
    return b.clicked;
}

static bool TabBtn(Clay_ElementId id, const char *label, bool sel, float size, float rad) {
    Bs b = Interact(id);
    float s = Anim(id.id + 7, sel ? 1.f : 0.f, 16.f);
    float a = fmaxf(s * 255.f, b.hover * (1.f - s) * 90.f);
    Clay_Color bg = Dark(RGBA(46, 48, 55, a), b.press * 0.3f);
    CLAY({ .id = (id), .layout = {.sizing = {GROW, GROW}, .childAlignment = AL_C},
              .backgroundColor = bg, .cornerRadius = CLAY_CORNER_RADIUS(rad) }) {
        Tx(label, size, Mix(C_MUTED, C_TEXT, fmaxf(s, b.hover * 0.6f)), 0);
    }
    return b.clicked;
}

/* ------------------------------------------------------------------ */
/* raylib drawing helpers                                              */
/* ------------------------------------------------------------------ */
static unsigned char U8(float x) { return (unsigned char)(x < 0 ? 0 : x > 255 ? 255 : x + .5f); }
static Color RC(Clay_Color c) { return (Color){U8(c.r), U8(c.g), U8(c.b), U8(c.a)}; }
static Color LerpC(Color a, Color b, float t) {
    return (Color){U8(a.r + (b.r - a.r) * t), U8(a.g + (b.g - a.g) * t), U8(a.b + (b.b - a.b) * t), U8(a.a + (b.a - a.a) * t)};
}

static void FillRR(Rectangle r, float rad, Color c) {
    if (c.a == 0 || r.width <= 0 || r.height <= 0) return;
    float m = fminf(r.width, r.height);
    if (rad <= 0.5f) { DrawRectangleRec(r, c); return; }
    DrawRectangleRounded(r, fminf(1.f, rad * 2.f / m), 20, c);
}
static void StrokeRR(Rectangle r, float rad, float t, Color c) {
    if (c.a == 0 || r.width <= 0 || r.height <= 0) return;
    rad = fminf(rad, fminf(r.width, r.height) / 2.f);
    if (rad < 1.f) {
        DrawRectangleRec((Rectangle){r.x, r.y, r.width, t}, c);
        DrawRectangleRec((Rectangle){r.x, r.y + r.height - t, r.width, t}, c);
        DrawRectangleRec((Rectangle){r.x, r.y + t, t, r.height - 2 * t}, c);
        DrawRectangleRec((Rectangle){r.x + r.width - t, r.y + t, t, r.height - 2 * t}, c);
        return;
    }
    DrawRectangleRec((Rectangle){r.x + rad, r.y, r.width - 2 * rad, t}, c);
    DrawRectangleRec((Rectangle){r.x + rad, r.y + r.height - t, r.width - 2 * rad, t}, c);
    DrawRectangleRec((Rectangle){r.x, r.y + rad, t, r.height - 2 * rad}, c);
    DrawRectangleRec((Rectangle){r.x + r.width - t, r.y + rad, t, r.height - 2 * rad}, c);
    DrawRing((Vector2){r.x + rad, r.y + rad},                     rad - t, rad, 180, 270, 16, c);
    DrawRing((Vector2){r.x + r.width - rad, r.y + rad},           rad - t, rad, 270, 360, 16, c);
    DrawRing((Vector2){r.x + r.width - rad, r.y + r.height - rad}, rad - t, rad, 0, 90, 16, c);
    DrawRing((Vector2){r.x + rad, r.y + r.height - rad},          rad - t, rad, 90, 180, 16, c);
}
static void GradRR(Rectangle r, float rad, Color top, Color bot) {
    rad = fminf(rad, fminf(r.width, r.height) / 2.f);
    int h = (int)r.height;
    for (int i = 0; i < h; i++) {
        float y = i + 0.5f, inset = 0;
        if (y < rad)                 { float dy = rad - y;               inset = rad - sqrtf(fmaxf(0, rad * rad - dy * dy)); }
        else if (y > r.height - rad) { float dy = y - (r.height - rad);  inset = rad - sqrtf(fmaxf(0, rad * rad - dy * dy)); }
        DrawRectangle((int)(r.x + inset), (int)(r.y + i), (int)(r.width - 2 * inset), 1, LerpC(top, bot, (float)i / fmaxf(1, h - 1)));
    }
}
static void RadGrad(Vector2 c, float r, Color inner, Color outer) {
    for (float rr = r; rr > 0.5f; rr -= 1.f) DrawCircleV(c, rr, LerpC(inner, outer, rr / r));
}
static void Ring(Vector2 c, float r, float t, Color col) { DrawRing(c, fmaxf(0, r - t * .5f), r + t * .5f, 0, 360, 40, col); }
static void Arc(Vector2 c, float r, float t, float a0, float a1, Color col) { DrawRing(c, fmaxf(0, r - t * .5f), r + t * .5f, a0, a1, 32, col); }
static void Tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    float cr = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cr > 0) { Vector2 t = b; b = c; c = t; }
    DrawTriangle(a, b, c, col);
}
static void TxtAt(int font, const char *s, float x, float y, float size, Color c) {
    DrawTextEx(gFonts[font], s, (Vector2){roundf(x), roundf(y)}, size, 0, c);
}
static float TxtW(int font, const char *s, float size) { return MeasureTextEx(gFonts[font], s, size, 0).x; }

/* ------------------------------------------------------------------ */
/* icons                                                               */
/* ------------------------------------------------------------------ */
static void DrawIcon(int k, Rectangle r, Color c, float v) {
    float x = r.x, y = r.y, w = r.width, h = r.height;
    float t = fmaxf(1.3f, w / 11.0f);
#define P(a, b) ((Vector2){x + (a) * w, y + (b) * h})
#define LN(a, b, c2, d) DrawLineEx(P(a, b), P(c2, d), t, c)
    switch (k) {
    case I_PLUS:    LN(.22, .5, .78, .5); if (v > 0.01f) LN(.5, .5 - .28 * v, .5, .5 + .28 * v); break;
    case I_CHEV_R:  LN(.38, .2, .66, .5); LN(.66, .5, .38, .8); break;
    case I_CHEV_L:  LN(.62, .2, .34, .5); LN(.34, .5, .62, .8); break;
    case I_CHEV_UP: LN(.25, .62, .5, .38); LN(.5, .38, .75, .62); break;
    case I_CHEV_DN: LN(.25, .38, .5, .62); LN(.5, .62, .75, .38); break;
    case I_SEARCH:  Ring(P(.44, .44), .28 * w, t, c); LN(.65, .65, .88, .88); break;
    case I_LIBRARY: StrokeRR((Rectangle){x + .1f * w, y + .15f * h, .8f * w, .7f * h}, .12f * w, t, c);
                    LN(.32, .15, .32, .85); LN(.68, .15, .68, .85); LN(.1, .5, .9, .5); break;
    case I_UNDO:    Arc(P(.5, .58), .3f * w, t, 215, 450, c); LN(.254, .408, .254, .2); LN(.254, .408, .47, .408); break;
    case I_REDO:    Arc(P(.5, .58), .3f * w, t, 90, 325, c);  LN(.746, .408, .746, .2); LN(.746, .408, .53, .408); break;
    case I_EXPORT:  LN(.5, .62, .5, .14); LN(.5, .14, .32, .32); LN(.5, .14, .68, .32);
                    LN(.2, .6, .2, .86); LN(.2, .86, .8, .86); LN(.8, .86, .8, .6); break;
    case I_NOTE:    DrawCircleV(P(.3, .76), .13f * w, c); DrawCircleV(P(.72, .66), .13f * w, c);
                    LN(.4, .76, .4, .2); LN(.82, .66, .82, .1); LN(.4, .2, .82, .1); break;
    case I_PALETTE: Ring(P(.5, .5), .4f * w, t, c);
                    DrawCircleV(P(.34, .4), .06f * w, c); DrawCircleV(P(.52, .3), .06f * w, c);
                    DrawCircleV(P(.7, .42), .06f * w, c); DrawCircleV(P(.38, .65), .06f * w, c); break;
    case I_GRADING: Ring(P(.5, .5), .4f * w, t, c);
                    DrawCircleV(P(.5, .32), .08f * w, c); DrawCircleV(P(.33, .62), .08f * w, c); DrawCircleV(P(.67, .62), .08f * w, c); break;
    case I_SLIDERS: LN(.1, .3, .9, .3); LN(.1, .7, .9, .7);
                    DrawCircleV(P(.35, .3), .11f * w, c); DrawCircleV(P(.65, .7), .11f * w, c); break;
    case I_SUN:     Ring(P(.5, .5), .2f * w, t, c);
                    for (int i = 0; i < 8; i++) { float a = i * PI / 4; float s = sinf(a), co = cosf(a);
                        DrawLineEx((Vector2){x + (.5f + .32f * co) * w, y + (.5f + .32f * s) * h},
                                   (Vector2){x + (.5f + .45f * co) * w, y + (.5f + .45f * s) * h}, t, c); } break;
    case I_DROP:    Arc(P(.5, .62), .26f * w, t, 0, 180, c); LN(.24, .62, .5, .1); LN(.76, .62, .5, .1); break;
    case I_MOUNTAIN:LN(.08, .8, .4, .28); LN(.4, .28, .7, .8); LN(.08, .8, .92, .8); LN(.6, .55, .72, .4); LN(.72, .4, .92, .8); break;
    case I_CROP:    LN(.3, .1, .3, .7); LN(.3, .7, .9, .7); LN(.1, .3, .7, .3); LN(.7, .3, .7, .9); break;
    case I_POS:     LN(.5, .08, .5, .36); LN(.5, .64, .5, .92); LN(.08, .5, .36, .5); LN(.64, .5, .92, .5); DrawCircleV(P(.5, .5), .05f * w, c); break;
    case I_SCALE:   LN(.2, .8, .8, .2); LN(.55, .2, .8, .2); LN(.8, .2, .8, .45); LN(.2, .55, .2, .8); LN(.2, .8, .45, .8); break;
    case I_RESET:
    case I_ROT:     Arc(P(.5, .5), .34f * w, t, 40, 330, c); LN(.8, .33, .8, .1); LN(.8, .33, .58, .33); break;
    case I_LINK:    Ring(P(.3, .7), .2f * w, t, c); Ring(P(.7, .3), .2f * w, t, c); LN(.42, .58, .58, .42); break;
    case I_DISTORT: StrokeRR((Rectangle){x + .1f * w, y + .15f * h, .8f * w, .7f * h}, .14f * w, t, c);
                    LN(.1, .5, .9, .5); LN(.5, .15, .5, .5); break;
    case I_SKIP_B:  LN(.28, .2, .28, .8); LN(.76, .2, .4, .5); LN(.4, .5, .76, .8); break;
    case I_SKIP_F:  LN(.72, .2, .72, .8); LN(.24, .2, .6, .5); LN(.6, .5, .24, .8); break;
    case I_PLAY:    Tri(P(.3, .16), P(.3, .84), P(.84, .5), c); break;
    case I_LOOP:    LN(.25, .35, .75, .35); LN(.75, .35, .75, .48); LN(.75, .65, .25, .65); LN(.25, .65, .25, .52);
                    LN(.66, .24, .78, .35); LN(.78, .35, .66, .46); LN(.34, .54, .22, .65); LN(.22, .65, .34, .76); break;
    case I_GRID:    StrokeRR((Rectangle){x + .14f * w, y + .14f * h, .72f * w, .72f * h}, .08f * w, t, c);
                    LN(.38, .14, .38, .86); LN(.62, .14, .62, .86); LN(.14, .38, .86, .38); LN(.14, .62, .86, .62); break;
    case I_POINTER: LN(.28, .1, .28, .8); LN(.28, .8, .45, .64); LN(.45, .64, .7, .64); LN(.7, .64, .28, .1); break;
    case I_HAND:    StrokeRR((Rectangle){x + .28f * w, y + .42f * h, .44f * w, .42f * h}, .1f * w, t, c);
                    LN(.34, .42, .34, .16); LN(.45, .42, .45, .1); LN(.56, .42, .56, .16); LN(.66, .42, .66, .26); break;
    case I_SCISSORS:Ring(P(.27, .74), .13f * w, t, c); Ring(P(.73, .74), .13f * w, t, c); LN(.33, .64, .8, .12); LN(.67, .64, .2, .12); break;
    case I_TEXT:    LN(.2, .2, .8, .2); LN(.5, .2, .5, .84); LN(.2, .2, .2, .3); LN(.8, .2, .8, .3); LN(.38, .84, .62, .84); break;
    case I_TRASH:   LN(.15, .25, .85, .25); LN(.4, .25, .4, .14); LN(.4, .14, .6, .14); LN(.6, .14, .6, .25);
                    LN(.25, .25, .3, .88); LN(.3, .88, .7, .88); LN(.7, .88, .75, .25); LN(.43, .4, .43, .75); LN(.57, .4, .57, .75); break;
    case I_KF_PREV: LN(.3, .2, .3, .8); LN(.72, .28, .46, .5); LN(.46, .5, .72, .72); break;
    case I_KF_NEXT: LN(.7, .2, .7, .8); LN(.28, .28, .54, .5); LN(.54, .5, .28, .72); break;
    case I_KF_DIAMOND: LN(.5, .1, .88, .5); LN(.88, .5, .5, .9); LN(.5, .9, .12, .5); LN(.12, .5, .5, .1);
                    LN(.5, .32, .68, .5); LN(.68, .5, .5, .68); LN(.5, .68, .32, .5); LN(.32, .5, .5, .32); break;
    case I_ZOOM_OUT:Ring(P(.44, .44), .3f * w, t, c); LN(.66, .66, .9, .9); LN(.3, .44, .58, .44); break;
    case I_ZOOM_IN: Ring(P(.44, .44), .3f * w, t, c); LN(.66, .66, .9, .9); LN(.3, .44, .58, .44); LN(.44, .3, .44, .58); break;
    case I_MAGNET:  Arc(P(.5, .5), .28f * w, t, 0, 180, c); LN(.22, .5, .22, .16); LN(.78, .5, .78, .16);
                    LN(.12, .2, .32, .2); LN(.68, .2, .88, .2); break;
    case I_FILM:    StrokeRR((Rectangle){x + .1f * w, y + .18f * h, .8f * w, .64f * h}, .1f * w, t, c);
                    LN(.34, .18, .34, .82); LN(.66, .18, .66, .82); break;
    default: break;
    }
#undef P
#undef LN
}

/* ------------------------------------------------------------------ */
/* custom elements                                                     */
/* ------------------------------------------------------------------ */
static void DrawCustom(Cust *cu, Rectangle r) {
    if (cu->kind < K_THUMB) { DrawIcon(cu->kind, r, RC(cu->a), cu->v); return; }
    switch (cu->kind) {
    case K_THUMB: {
        GradRR(r, 8, RC(cu->a), RC(cu->b));
        if (cu->i == 0) {
            DrawCircleV((Vector2){r.x + .76f * r.width, r.y + .3f * r.height}, r.height * .16f, (Color){250, 228, 150, 255});
            Color m = RC(cu->c);
            Tri((Vector2){r.x + .1f * r.width,  r.y + .92f * r.height}, (Vector2){r.x + .32f * r.width, r.y + .42f * r.height}, (Vector2){r.x + .6f * r.width,  r.y + .92f * r.height}, m);
            Tri((Vector2){r.x + .38f * r.width, r.y + .92f * r.height}, (Vector2){r.x + .64f * r.width, r.y + .5f * r.height},  (Vector2){r.x + .92f * r.width, r.y + .92f * r.height}, LerpC(m, WHITE, .12f));
        }
        FillRR(r, 8, (Color){255, 255, 255, U8(26 * cu->f)});
        StrokeRR(r, 8, 1, (Color){255, 255, 255, U8(24 + 70 * cu->f)});
    } break;

    case K_KNOB: {
        Vector2 c = {r.x + r.width / 2, r.y + r.height / 2};
        float rad = fminf(r.width, r.height) / 2 - 1;
        DrawCircleV(c, rad, LerpC((Color){46, 48, 55, 255}, (Color){90, 94, 104, 255}, cu->f));
        RadGrad(c, rad - 1.5f, (Color){40, 41, 48, 255}, (Color){20, 21, 25, 255});
        float ang = (cu->v - .5f) * 4.7f;
        Vector2 d = {sinf(ang), -cosf(ang)};
        DrawLineEx((Vector2){c.x + d.x * rad * .72f, c.y + d.y * rad * .72f}, (Vector2){c.x + d.x * rad * .9f, c.y + d.y * rad * .9f}, 3.f, (Color){228, 229, 233, 255});
    } break;

    case K_WHEEL: {
        Vector2 c = {r.x + r.width / 2, r.y + r.height / 2};
        float rad = fminf(r.width, r.height) / 2 - 1;
        for (int i = 0; i < 72; i++)
            DrawCircleSector(c, rad, i * 5.f, i * 5.f + 5.6f, 3, ColorFromHSV(fmodf(i * 5.f + 250.f, 360.f), .72f, .74f));
        for (float rr = rad; rr > 1; rr -= 1.f) {
            float t = 1.f - rr / rad;
            DrawCircleV(c, rr, (Color){60, 60, 66, U8(26 * t * t * t * 2.2f)});
        }
        Ring(c, rad, 2, (Color){52, 54, 60, 255});
        float dot = 9.f + 1.5f * cu->f;
        DrawCircleV(c, dot + 1.5f, (Color){20, 20, 24, 120});
        DrawCircleV(c, dot, (Color){228, 228, 232, 255});
    } break;

    case K_SWITCH: {
        float rad = r.height / 2;
        FillRR(r, rad, RC(Mix(RGB(44, 45, 51), C_ACCENT, cu->v)));
        StrokeRR(r, rad, 1, (Color){56, 58, 65, 255});
        float kx = r.x + rad + (r.width - 2 * rad) * cu->v;
        DrawCircleV((Vector2){kx, r.y + rad}, rad - 3.f + cu->f, (Color){226, 227, 231, 255});
    } break;

    case K_SLIDER: {
        float cy = r.y + r.height / 2;
        Rectangle tr = {r.x, cy - 2, r.width, 4};
        FillRR(tr, 2, RC(cu->a));
        FillRR((Rectangle){r.x, cy - 2, 7 + (r.width - 14) * cu->v, 4}, 2, RC(cu->b));
        DrawCircleV((Vector2){r.x + 7 + (r.width - 14) * cu->v, cy}, 7.f + cu->f, (Color){240, 240, 244, 255});
    } break;

    case K_ROTRULER: {
        Rectangle box = {r.x, r.y, r.width, 38};
        FillRR(box, 10, (Color){18, 19, 23, 255});
        StrokeRR(box, 10, 1, (Color){40, 42, 48, 255});
        float x0 = box.x + 22, x1 = box.x + box.width - 22, cy = box.y + 19;
        for (int i = 0; i <= 36; i++) {
            float px = x0 + (x1 - x0) * i / 36.f;
            float len = (i % 9 == 0) ? 20 : (i % 3 == 0) ? 14 : 9;
            if (i == 18) DrawLineEx((Vector2){px, cy - 12}, (Vector2){px, cy + 12}, 2.f, (Color){235, 55, 65, 255});
            else DrawLineEx((Vector2){px, cy - len / 2}, (Vector2){px, cy + len / 2}, 1.f, (Color){150, 153, 162, 200});
        }
        const char *lab[5] = {"-180\xC2\xB0", "-90\xC2\xB0", "0\xC2\xB0", "90\xC2\xB0", "180\xC2\xB0"};
        for (int i = 0; i < 5; i++) {
            float px = x0 + (x1 - x0) * (i * 9) / 36.f;
            TxtAt(0, lab[i], px - TxtW(0, lab[i], 11) / 2, box.y + 46, 11, RC(C_MUTED));
        }
        float cx = x0 + (x1 - x0) / 2;
        Tri((Vector2){cx, r.y + 68}, (Vector2){cx - 6, r.y + 78}, (Vector2){cx + 6, r.y + 78}, (Color){240, 240, 244, 255});
    } break;

    case K_RULER: {
        const float pps = 21.5f;
        float bottom = r.y + r.height;
        for (int s = 0;; s++) {
            float px = r.x + s * pps;
            if (px > r.x + r.width) break;
            bool major = (s % 5 == 0);
            DrawLineEx((Vector2){px, bottom - (major ? 10 : 5)}, (Vector2){px, bottom}, 1.f, (Color){72, 75, 84, 255});
            if (major) { char b[16]; snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60); TxtAt(0, b, px + 3, r.y + 3, 10, RC(C_MUTED)); }
        }
        gPH = true; gPHPos = (Vector2){r.x, r.y + 4}; gPHLen = r.height - 4 + cu->f;
    } break;

    case K_PREVIEW: {
        DrawRectangleRec(r, BLACK);
        DrawRectangleLinesEx(r, 1.5f, WHITE);
        Vector2 q[4] = {{r.x, r.y}, {r.x + r.width, r.y}, {r.x, r.y + r.height}, {r.x + r.width, r.y + r.height}};
        for (int i = 0; i < 4; i++) { DrawCircleV(q[i], 5.5f, WHITE); }
    } break;

    case K_CLIP: {
        GradRR(r, 6, RC(cu->a), RC(cu->b));
        float cy = r.y + r.height - 9;
        for (float px = r.x + 6; px < r.x + r.width - 6; px += 3.f) {
            float hh = (.2f + .8f * fabsf(sinf(px * .53f) * cosf(px * .17f))) * 11.f;
            DrawLineEx((Vector2){px, cy - hh / 2}, (Vector2){px, cy + hh / 2}, 1.4f, (Color){226, 205, 240, 170});
        }
        FillRR(r, 6, (Color){255, 255, 255, U8(18 * cu->f)});
        StrokeRR(r, 6, 2, (Color){245, 245, 250, 255});
    } break;

    case K_SCROLLBAR:
        FillRR((Rectangle){r.x, r.y, r.width, r.height * .9f}, r.width / 2, (Color){58, 60, 67, 255});
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Clay -> raylib renderer                                             */
/* ------------------------------------------------------------------ */
static void RenderUI(Clay_RenderCommandArray cmds) {
    gPH = false;
    for (int i = 0; i < cmds.length; i++) {
        Clay_RenderCommand *rc = Clay_RenderCommandArray_Get(&cmds, i);
        Clay_BoundingBox bb = rc->boundingBox;
        Rectangle r = {bb.x, bb.y, bb.width, bb.height};
        switch (rc->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
            Clay_RectangleRenderData *d = &rc->renderData.rectangle;
            FillRR(r, d->cornerRadius.topLeft, RC(d->backgroundColor));
        } break;
        case CLAY_RENDER_COMMAND_TYPE_BORDER: {
            Clay_BorderRenderData *d = &rc->renderData.border;
            Color c = RC(d->color);
            float rad = d->cornerRadius.topLeft;
            if (d->width.left && d->width.left == d->width.right && d->width.left == d->width.top && d->width.left == d->width.bottom) {
                StrokeRR(r, rad, d->width.left, c);
            } else {
                if (d->width.top)    DrawRectangleRec((Rectangle){r.x, r.y, r.width, d->width.top}, c);
                if (d->width.bottom) DrawRectangleRec((Rectangle){r.x, r.y + r.height - d->width.bottom, r.width, d->width.bottom}, c);
                if (d->width.left)   DrawRectangleRec((Rectangle){r.x, r.y, d->width.left, r.height}, c);
                if (d->width.right)  DrawRectangleRec((Rectangle){r.x + r.width - d->width.right, r.y, d->width.right, r.height}, c);
            }
        } break;
        case CLAY_RENDER_COMMAND_TYPE_TEXT: {
            Clay_TextRenderData *d = &rc->renderData.text;
            char buf[512]; int n = d->stringContents.length < 511 ? d->stringContents.length : 511;
            memcpy(buf, d->stringContents.chars, n); buf[n] = 0;
            DrawTextEx(gFonts[d->fontId], buf, (Vector2){roundf(r.x), roundf(r.y)}, d->fontSize, d->letterSpacing, RC(d->textColor));
        } break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
            BeginScissorMode((int)r.x, (int)r.y, (int)ceilf(r.width), (int)ceilf(r.height)); break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
            EndScissorMode(); break;
        case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
            Cust *cu = (Cust *)rc->renderData.custom.customData;
            if (cu) DrawCustom(cu, r);
        } break;
        default: break;
        }
    }
    if (gPH) {
        Color red = {240, 62, 72, 255};
        DrawLineEx(gPHPos, (Vector2){gPHPos.x, gPHPos.y + gPHLen}, 1.6f, red);
        DrawRectangleRec((Rectangle){gPHPos.x - 5, gPHPos.y - 4, 10, 8}, red);
        Tri((Vector2){gPHPos.x - 5, gPHPos.y + 4}, (Vector2){gPHPos.x + 5, gPHPos.y + 4}, (Vector2){gPHPos.x, gPHPos.y + 10}, red);
    }
}

static Clay_Dimensions MeasureTxt(Clay_StringSlice t, Clay_TextElementConfig *cfg, void *u) {
    (void)u;
    char buf[512]; int n = t.length < 511 ? t.length : 511;
    memcpy(buf, t.chars, n); buf[n] = 0;
    Vector2 v = MeasureTextEx(gFonts[cfg->fontId], buf, cfg->fontSize, cfg->letterSpacing);
    return (Clay_Dimensions){v.x, v.y};
}
static void OnClayError(Clay_ErrorData e) { printf("Clay error: %.*s\n", e.errorText.length, e.errorText.chars); }

/* ------------------------------------------------------------------ */
/* UI: top bar                                                         */
/* ------------------------------------------------------------------ */
static void TopBar(void) {
    CLAY({ .id = (CLAY_ID("TopBar")), .layout = {.sizing = {GROW, FIX(46)}, .padding = {8, 10, 0, 0}, .childAlignment = AL_Y} }) {
        CLAY({ .id = (CLAY_ID("LeftCluster")), .layout = {.sizing = {FIX(284), FIT}, .childAlignment = AL_Y} }) {
            CLAY({ .id = (CLAY_ID("Logo")), .layout = {.sizing = {FIX(18), FIX(24)}, .childAlignment = AL_C} }) {
                CLAY({.layout = {COLM, .sizing = {FIX(14), FIX(10)}, .childGap = 3}}) {
                    CLAY({.layout = {.sizing = {GROW, FIX(1)}}, .backgroundColor = C_DIM}) {}
                    CLAY({.layout = {.sizing = {GROW, FIX(1)}}, .backgroundColor = C_DIM}) {}
                    CLAY({.layout = {.sizing = {GROW, FIX(1)}}, .backgroundColor = C_DIM}) {}
                }
            }
            GAPX(4)
            CLAY({.layout = {.sizing = {FIX(1), FIX(24)}}, .backgroundColor = RGB(52, 54, 60)}) {}
            GAPX(10)
            Tx("Pluto", 15, C_TEXT, 1);
            GAPX(28)
            Tx("Untitled Project", 13, C_DIM, 0);
        }
        Clay_ElementId ex = CLAY_ID("Export");
        Bs b = Interact(ex);
        CLAY({ .id = (ex), .layout = {.sizing = {FIT, FIX(32)}, .padding = {12, 14, 0, 0}, .childGap = 8, .childAlignment = AL_Y},
                  .backgroundColor = BtnBg(RGB(27, 28, 33), RGB(38, 40, 46), b), .cornerRadius = CLAY_CORNER_RADIUS(9),
                  .border = {.color = Mix(RGB(54, 56, 62), RGB(86, 89, 98), b.hover), .width = CLAY_BORDER_OUTSIDE(1)} }) {
            Icon(I_EXPORT, 14, C_TEXT);
            Tx("Export", 13, C_TEXT, 1);
        }
        GAPX(18)
        IconBtn(CLAY_ID("Undo"), I_UNDO, 28, 15, C_DIM, false, 8);
        GAPX(8)
        IconBtn(CLAY_ID("Redo"), I_REDO, 28, 15, RGB(76, 79, 88), false, 8);
    }
}

/* ------------------------------------------------------------------ */
/* UI: library (left)                                                  */
/* ------------------------------------------------------------------ */
typedef struct { const char *name, *dur; Clay_Color sky1, sky2, mtn; int style; } MediaItem;
static const MediaItem kMedia[6] = {
    {"Coastal morning.m...", "0:12", {86, 172, 200, 255}, {150, 214, 224, 255}, {88, 98, 116, 255}, 0},
    {"City at dusk.mp4",     "0:09", {106, 52, 138, 255}, {188, 98, 160, 255}, {58, 36, 88, 255}, 0},
    {"Forest trail.mp4",     "0:15", {48, 128, 108, 255}, {118, 188, 150, 255}, {32, 68, 70, 255}, 0},
    {"Desert still.jpg",     "0:06", {126, 138, 154, 255}, {190, 196, 206, 255}, {92, 98, 114, 255}, 0},
    {"Night harbor.jpg",     "0:06", {38, 38, 88, 255}, {62, 52, 112, 255}, {34, 34, 60, 255}, 0},
    {"Ambient soundtrac...", "0:24", {150, 40, 150, 255}, {255, 134, 92, 255}, {0, 0, 0, 255}, 1},
};

static void MediaView(void) {
    Clay_ElementId sid = CLAY_ID("Search");
    Bs sb = Interact(sid);
    CLAY({ .id = (sid), .layout = {.sizing = {GROW, FIX(34)}, .padding = {12, 12, 0, 0}, .childGap = 8, .childAlignment = AL_Y},
               .backgroundColor = Mix(RGB(30, 31, 36), RGB(36, 37, 43), sb.hover), .cornerRadius = CLAY_CORNER_RADIUS(10) }) {
        Icon(I_SEARCH, 14, C_MUTED);
        Tx("Search media", 12, C_MUTED, 0);
    }
    for (int row = 0; row < 3; row++) {
        CLAY({ .id = (CLAY_IDI("MRow", row)), .layout = {.sizing = {GROW, FIT}, .childGap = 10} }) {
            for (int col = 0; col < 2; col++) {
                int i = row * 2 + col;
                const MediaItem *m = &kMedia[i];
                CLAY({ .id = (CLAY_IDI("Media", i)), .layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6} }) {
                    Clay_ElementId tid = CLAY_IDI("Thumb", i);
                    Bs b = Interact(tid);
                    CLAY({ .id = (tid), .layout = {.sizing = {GROW, FIX(58)}, .padding = {5, 5, 5, 5}, .childAlignment = {.y = CLAY_ALIGN_Y_BOTTOM}},
                               .custom = {.customData = NC(K_THUMB, m->sky1, m->sky2, m->mtn, 0, b.hover, m->style)} }) {
                        CLAY({.layout = {.sizing = {FIT, FIT}, .padding = {5, 5, 1, 2}},
                                      .backgroundColor = RGBA(0, 0, 0, 150), .cornerRadius = CLAY_CORNER_RADIUS(4)}) {
                            Tx(m->dur, 10, C_TEXT, 1);
                        }
                    }
                    Tx(m->name, 11, C_MUTED, 0);
                }
            }
        }
    }
}

static void KnobGroup(int idx, const char *label, const char *val) {
    CLAY({ .id = (CLAY_IDI("KG", idx)), .layout = {COLM, .sizing = {FIT, FIT}, .childGap = 5, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}} }) {
        Clay_ElementId kid = CLAY_IDI("Knob", idx);
        Bs b = Interact(kid);
        CLAY({ .id = (kid), .layout = {.sizing = {FIX(70), FIX(70)}}, .custom = {.customData = NC(K_KNOB, C_CARD, C_CARD, C_CARD, .5f, b.hover, 0)} }) {}
        Tx(label, 11, C_MUTED, 0);
        CLAY({.layout = {.sizing = {FIX(64), FIX(24)}, .childAlignment = AL_C},
                      .backgroundColor = RGB(28, 29, 34), .cornerRadius = CLAY_CORNER_RADIUS(6),
                      .border = {.color = RGB(40, 42, 48), .width = CLAY_BORDER_OUTSIDE(1)}}) {
            Tx(val, 12, C_TEXT, 0);
        }
    }
}

static void SwitchItem(int idx, const char *label) {
    Clay_ElementId id = CLAY_IDI("Switch", idx);
    Bs b = Interact(id);
    CLAY({ .id = (id), .layout = {.sizing = {FIT, FIT}, .childGap = 8, .childAlignment = AL_Y} }) {
        CLAY({.layout = {.sizing = {FIX(40), FIX(24)}},
                      .custom = {.customData = NC(K_SWITCH, C_CARD, C_CARD, C_CARD, 0, b.hover * 1.f - b.press * 1.5f, 0)}}) {}
        Tx(label, 11, C_MUTED, 0);
    }
}

static void AudioView(void) {
    CLAY({ .id = (CLAY_ID("AudioHead")), .layout = {.sizing = {GROW, FIT}, .padding = {4, 0, 0, 0}, .childGap = 8, .childAlignment = AL_Y} }) {
        Icon(I_NOTE, 18, C_DIM);
        Tx("Audio", 15, C_TEXT, 1);
    }
    CLAY({ .id = (CLAY_ID("AudioBody")), .layout = {.sizing = {GROW, GROW}, .childGap = 6} }) {
        CLAY({ .id = (CLAY_ID("AudioScroll")), .layout = {COLM, .sizing = {GROW, GROW}, .childGap = 10, .padding = {2, 0, 0, 8}},
                                      .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()} }) {
            CLAY({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) { KnobGroup(0, "Volume", "100 %"); }
            CLAY({.layout = {.sizing = {GROW, FIT}, .padding = {4, 4, 0, 0}}}) { KnobGroup(1, "Pan", "0"); HSPACE KnobGroup(2, "Bass", "0 dB"); }
            CLAY({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) { KnobGroup(3, "Treble", "0 dB"); }
            CLAY({.layout = {.sizing = {GROW, FIT}, .padding = {6, 0, 8, 0}, .childGap = 18}}) { SwitchItem(0, "Fade In"); SwitchItem(1, "Fade Out"); }
            CLAY({.layout = {.sizing = {GROW, FIT}, .padding = {2, 4, 0, 0}, .childAlignment = AL_Y}}) {
                Tx("Fade In Duration", 11, C_TEXT, 0);
                HSPACE
                CLAY({.layout = {.sizing = {FIX(64), FIX(28)}, .padding = {0, 10, 0, 0}, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER}, .childGap = 4},
                              .backgroundColor = RGB(30, 31, 36), .cornerRadius = CLAY_CORNER_RADIUS(6)}) {
                    Tx("1", 12, C_TEXT, 0); Tx("s", 10, C_MUTED, 0);
                }
            }
            CLAY({.layout = {.sizing = {GROW, FIX(18)}, .padding = {2, 2, 0, 0}},
                          .custom = {.customData = NC(K_SLIDER, RGB(48, 50, 57), RGB(70, 73, 82), C_CARD, 0.05f, 0, 0)}}) {}
        }
        CLAY({ .id = (CLAY_ID("AudioScrollbar")), .layout = {.sizing = {FIX(4), GROW}}, .custom = {.customData = NC(K_SCROLLBAR, C_CARD, C_CARD, C_CARD, 0, 0, 0)} }) {}
    }
}

static void LeftPanel(void) {
    CLAY({ .id = (CLAY_ID("Library")), .layout = {COLM, .sizing = {FIX(220), GROW}, .padding = {12, 12, 14, 12}, .childGap = 14}, PANEL }) {
        CLAY({ .id = (CLAY_ID("LibHead")), .layout = {.sizing = {GROW, FIT}, .padding = {4, 0, 0, 0}, .childGap = 8, .childAlignment = AL_Y} }) {
            Icon(I_LIBRARY, 18, C_DIM);
            Tx("Library", 15, C_TEXT, 1);
            HSPACE
            Clay_ElementId imp = CLAY_ID("Import");
            Bs b = Interact(imp);
            CLAY({ .id = (imp), .layout = {.sizing = {FIT, FIX(28)}, .padding = {10, 12, 0, 0}, .childGap = 5, .childAlignment = AL_Y},
                       .backgroundColor = BtnBg(RGB(28, 29, 34), RGB(40, 42, 48), b), .cornerRadius = CLAY_CORNER_RADIUS(9) }) {
                Icon(I_PLUS, 11, C_DIM);
                Tx("Import", 12, C_TEXT, 0);
            }
        }
        CLAY({ .id = (CLAY_ID("LibTabs")), .layout = {.sizing = {GROW, FIX(36)}, .padding = CLAY_PADDING_ALL(4), .childGap = 4},
                                  .backgroundColor = C_SEG, .cornerRadius = CLAY_CORNER_RADIUS(12),
                                  .border = {.color = RGB(30, 32, 37), .width = CLAY_BORDER_OUTSIDE(1)} }) {
            if (TabBtn(CLAY_IDI("LibTab", 0), "Media", gLeftTab == 0, 12, 9)) gLeftTab = 0;
            if (TabBtn(CLAY_IDI("LibTab", 1), "Audio", gLeftTab == 1, 12, 9)) gLeftTab = 1;
        }
        if (gLeftTab == 0) MediaView(); else AudioView();
    }
}

/* ------------------------------------------------------------------ */
/* UI: preview (center)                                                */
/* ------------------------------------------------------------------ */
static void CenterPanel(void) {
    Clay_BoundingBox pb = Clay_GetElementData(CLAY_ID("PreviewArea")).boundingBox;
    float aw = pb.width - 28, ah = pb.height - 28;
    if (aw < 60 || ah < 60) { aw = 640; ah = 360; }
    float pw, ph;
    if (aw / ah > 16.f / 9.f) { ph = ah; pw = ah * 16.f / 9.f; } else { pw = aw; ph = aw * 9.f / 16.f; }

    CLAY({ .id = (CLAY_ID("Center")), .layout = {COLM, .sizing = {GROW, GROW}}, PANEL }) {
        CLAY({ .id = (CLAY_ID("PreviewArea")), .layout = {.sizing = {GROW, GROW}, .padding = CLAY_PADDING_ALL(14), .childAlignment = AL_C},
                                      .clip = {.horizontal = true, .vertical = true} }) {
            CLAY({ .id = (CLAY_ID("PreviewFrame")), .layout = {.sizing = {FIX(pw), FIX(ph)}},
                                           .custom = {.customData = NC(K_PREVIEW, C_BG, C_BG, C_BG, 0, 0, 0)} }) {}
        }
        CLAY({ .id = (CLAY_ID("Transport")), .layout = {.sizing = {GROW, FIX(56)}, .padding = {14, 14, 0, 4}, .childAlignment = AL_Y} }) {
            CLAY({ .id = (CLAY_ID("TimeL")), .layout = {.sizing = {GROW, FIT}, .childGap = 7, .childAlignment = AL_Y} }) {
                Tx("00:00:00:00", 15, C_TEXT, 1);
                Tx("/", 14, C_MUTED, 0);
                Tx("00:00:09:00", 14, C_MUTED, 0);
            }
            CLAY({ .id = (CLAY_ID("TransC")), .layout = {.sizing = {FIT, FIT}, .childGap = 12, .childAlignment = AL_Y} }) {
                IconBtn(CLAY_ID("SkipB"), I_SKIP_B, 32, 16, C_DIM, false, 16);
                IconBtn(CLAY_ID("PrevF"), I_CHEV_L, 28, 14, C_DIM, false, 14);
                Clay_ElementId pid = CLAY_ID("Play");
                Bs b = Interact(pid);
                float sz = 44.f + 3.f * b.hover - 3.f * b.press;
                CLAY({ .id = (pid), .layout = {.sizing = {FIX(50), FIX(50)}, .childAlignment = AL_C} }) {
                    CLAY({.layout = {.sizing = {FIX(sz), FIX(sz)}, .childAlignment = AL_C},
                                  .backgroundColor = Dark(RGB(244, 244, 247), b.press * .12f), .cornerRadius = CLAY_CORNER_RADIUS(sz / 2)}) {
                        Icon(I_PLAY, 18, RGB(12, 12, 14));
                    }
                }
                IconBtn(CLAY_ID("NextF"), I_CHEV_R, 28, 14, C_DIM, false, 14);
                IconBtn(CLAY_ID("SkipF"), I_SKIP_F, 32, 16, C_DIM, false, 16);
            }
            CLAY({ .id = (CLAY_ID("TransR")), .layout = {.sizing = {GROW, FIT}, .childGap = 10, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER}} }) {
                CLAY({.layout = {.sizing = {FIT, FIX(22)}, .padding = {8, 8, 0, 0}, .childAlignment = AL_C},
                              .backgroundColor = RGB(30, 31, 36), .cornerRadius = CLAY_CORNER_RADIUS(6)}) {
                    Tx("1920\xC3\x97" "1080 \xC2\xB7 30 fps", 10, C_MUTED, 0);
                }
                IconBtn(CLAY_ID("Loop"), I_LOOP, 30, 17, C_DIM, false, 8);
                IconBtn(CLAY_ID("GridBtn"), I_GRID, 30, 17, C_DIM, false, 8);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* UI: inspector (right)                                               */
/* ------------------------------------------------------------------ */
static void NumField(int n, const char *val, const char *unit) {
    Clay_ElementId id = CLAY_IDI("NumF", n);
    Bs b = Interact(id);
    CLAY({ .id = (id), .layout = {.sizing = {GROW, FIX(36)}, .padding = {10, 6, 0, 0}, .childGap = 4, .childAlignment = AL_Y},
              .backgroundColor = Mix(C_FIELD, RGB(46, 47, 54), b.hover), .cornerRadius = CLAY_CORNER_RADIUS(10),
              .border = {.color = Mix(RGB(44, 46, 52), RGB(76, 79, 88), b.hover), .width = CLAY_BORDER_OUTSIDE(1)} }) {
        HSPACE
        Tx(val, 14, C_TEXT, 0);
        Tx(unit, 11, C_MUTED, 0);
        CLAY({.layout = {COLM, .sizing = {FIT, FIT}, .padding = {2, 0, 0, 0}}}) {
            Icon(I_CHEV_UP, 10, C_MUTED);
            Icon(I_CHEV_DN, 10, C_MUTED);
        }
    }
}

static void FieldCard(int n, const char *title, int icon, const char *l1, const char *l2, const char *val, const char *unit, bool linked) {
    CLAY({ .id = (CLAY_IDI("FCard", n)), .layout = {COLM, .sizing = {GROW, FIT}, .padding = CLAY_PADDING_ALL(10), .childGap = 7}, CARD }) {
        CLAY({.layout = {.sizing = {GROW, FIT}, .childGap = 8, .childAlignment = AL_Y}}) {
            Icon(icon, 15, C_DIM);
            Tx(title, 13, C_TEXT, 1);
            HSPACE
            IconBtn(CLAY_IDI("Reset", n), I_RESET, 22, 12, C_MUTED, false, 6);
        }
        CLAY({.layout = {.sizing = {GROW, FIT}, .childGap = 6, .childAlignment = AL_Y}}) {
            CLAY({.layout = {.sizing = {FIX(18), FIT}}}) { Tx(l1, 13, C_MUTED, 0); }
            NumField(n * 2, val, unit);
        }
        CLAY({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
            IconBtn(CLAY_IDI("Link", n), I_LINK, 26, 16, linked ? C_ACCENT : C_MUTED, false, 8);
        }
        CLAY({.layout = {.sizing = {GROW, FIT}, .childGap = 6, .childAlignment = AL_Y}}) {
            CLAY({.layout = {.sizing = {FIX(18), FIT}}}) { Tx(l2, 13, C_MUTED, 0); }
            NumField(n * 2 + 1, val, unit);
        }
    }
}

static void NavButton(int n, int icon, const char *label) {
    Clay_ElementId id = CLAY_IDI("Nav", n);
    Bs b = Interact(id);
    CLAY({ .id = (id), .layout = {.sizing = {GROW, FIX(48)}, .padding = {14, 12, 0, 0}, .childGap = 10, .childAlignment = AL_Y},
              .backgroundColor = BtnBg(C_CARD, RGB(38, 40, 46), b), .cornerRadius = CLAY_CORNER_RADIUS(14),
              .border = {.color = Mix(C_CARDB, RGB(76, 79, 88), b.hover), .width = CLAY_BORDER_OUTSIDE(1)} }) {
        Icon(icon, 18, C_DIM);
        Tx(label, 13, C_TEXT, 0);
        HSPACE
        Icon(I_CHEV_R, 12, C_MUTED);
    }
}

static void TransformView(void) {
    CLAY({.layout = {.sizing = {GROW, FIT}, .childGap = 10}}) {
        FieldCard(0, "Position", I_POS, "X", "Y", "0", "px", false);
        FieldCard(1, "Scale", I_SCALE, "W", "H", "100", "%", true);
    }
    CLAY({ .id = (CLAY_ID("RotCard")), .layout = {COLM, .sizing = {GROW, FIT}, .padding = CLAY_PADDING_ALL(10), .childGap = 7}, CARD }) {
        CLAY({.layout = {.sizing = {GROW, FIT}, .childGap = 10, .childAlignment = AL_Y}}) {
            Icon(I_ROT, 20, C_DIM);
            Tx("Rotation", 13, C_TEXT, 0);
            HSPACE
            CLAY({.layout = {.sizing = {FIX(116), FIX(36)}}}) {
                NumField(10, "0", "\xC2\xB0");
            }
        }
        Clay_ElementId rid = CLAY_ID("RotRuler");
        Bs b = Interact(rid);
        CLAY({ .id = (rid), .layout = {.sizing = {GROW, FIX(84)}}, .custom = {.customData = NC(K_ROTRULER, C_CARD, C_CARD, C_CARD, 0, b.hover, 0)} }) {}
    }
    CLAY({.layout = {COLM, .sizing = {GROW, FIT}, .padding = {2, 2, 4, 0}, .childGap = 10}}) {
        CLAY({.layout = {.sizing = {GROW, FIT}, .childAlignment = AL_Y}}) {
            Tx("Opacity", 13, C_TEXT, 0);
            HSPACE
            CLAY({.layout = {.sizing = {FIX(76), FIX(26)}, .padding = {0, 8, 0, 0}, .childGap = 4, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER}},
                          .backgroundColor = RGB(38, 39, 45), .cornerRadius = CLAY_CORNER_RADIUS(6)}) {
                Tx("100", 12, C_TEXT, 0); Tx("%", 10, C_MUTED, 0);
            }
        }
        Clay_ElementId sid = CLAY_ID("OpacitySlider");
        Bs b = Interact(sid);
        CLAY({ .id = (sid), .layout = {.sizing = {GROW, FIX(18)}},
                   .custom = {.customData = NC(K_SLIDER, RGB(48, 50, 57), RGB(70, 73, 82), C_CARD, 1.f, b.hover * 1.5f, 0)} }) {}
    }
    VSPACE
    CLAY({.layout = {.sizing = {GROW, FIT}, .childGap = 10}}) {
        NavButton(0, I_CROP, "Crop Tools");
        NavButton(1, I_DISTORT, "Distort");
    }
}

static void WheelItem(int i, const char *label) {
    CLAY({ .id = (CLAY_IDI("WheelCol", i)), .layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}} }) {
        Clay_ElementId wid = CLAY_IDI("Wheel", i);
        Bs b = Interact(wid);
        CLAY({ .id = (wid), .layout = {.sizing = {FIX(100), FIX(100)}}, .custom = {.customData = NC(K_WHEEL, C_CARD, C_CARD, C_CARD, 0, b.hover, 0)} }) {}
        Tx(label, 12, C_TEXT, 1);
        Tx("Neutral", 10, RGB(80, 140, 210), 0);
    }
}

static void AccHeader(int idx, const char *title, int icon, float openAnim) {
    Clay_ElementId id = CLAY_IDI("AccHdr", idx);
    Bs b = Interact(id);
    if (b.clicked) gAcc[idx] = !gAcc[idx];
    CLAY({ .id = (id), .layout = {.sizing = {GROW, FIX(52)}, .padding = {16, 14, 0, 0}, .childGap = 12, .childAlignment = AL_Y},
              .backgroundColor = BtnBg(C_CARD, RGB(36, 37, 43), b), .cornerRadius = CLAY_CORNER_RADIUS(14),
              .border = {.color = Mix(C_CARDB, RGB(76, 79, 88), b.hover), .width = CLAY_BORDER_OUTSIDE(1)} }) {
        Icon(icon, 20, C_DIM);
        Tx(title, 14, C_TEXT, 0);
        HSPACE
        CLAY({.layout = {.sizing = {FIX(24), FIX(24)}, .childAlignment = AL_C}, .cornerRadius = CLAY_CORNER_RADIUS(12),
                      .border = {.color = Mix(RGB(74, 77, 85), RGB(130, 134, 144), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
            IconV(I_PLUS, 12, C_DIM, 1.f - openAnim);
        }
    }
}

static const char *kSliderNames[5][4] = {
    {0, 0, 0, 0}, {"Hue", "Saturation", "Luminance", 0}, {"Exposure", "Contrast", "Highlights", "Shadows"},
    {"Temperature", "Tint", "Saturation", 0}, {"Sharpness", "Clarity", "Grain", 0}
};
static float SliderCardH(int n) { return 28.f + n * 32.f + (n - 1) * 12.f; }

static void Accordion(int idx, const char *title, int icon) {
    Clay_ElementId hid = CLAY_IDI("AccHdr", idx);
    float o = Anim(hid.id + 3, gAcc[idx] ? 1.f : 0.f, 10.f);
    int n = 0;
    if (idx > 0) while (n < 4 && kSliderNames[idx][n]) n++;
    float cardH = (idx == 0) ? 184.f : SliderCardH(n);
    CLAY({ .id = (CLAY_IDI("AccCol", idx)), .layout = {COLM, .sizing = {GROW, FIT}} }) {
        AccHeader(idx, title, icon, o);
        CLAY({ .id = (CLAY_IDI("AccWrap", idx)), .layout = {COLM, .sizing = {GROW, FIX(o * (cardH + 10.f))}}, .clip = {.vertical = true} }) {
            CLAY({.layout = {.sizing = {GROW, FIX(10)}}}) {}
            CLAY({ .id = (CLAY_IDI("AccBody", idx)), .layout = {COLM, .sizing = {GROW, FIX(cardH)}, .padding = CLAY_PADDING_ALL(12), .childGap = 12}, CARD }) {
                if (idx == 0) {
                    CLAY({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT}}}) {
                        Clay_ElementId rid = CLAY_ID("ResetGrade");
                        Bs b = Interact(rid);
                        CLAY({ .id = (rid), .layout = {.sizing = {FIT, FIT}, .childGap = 5, .childAlignment = AL_Y} }) {
                            Icon(I_RESET, 12, Mix(RGB(120, 128, 160), C_TEXT, b.hover));
                            Tx("Reset", 11, Mix(RGB(120, 128, 160), C_TEXT, b.hover), 0);
                        }
                    }
                    CLAY({.layout = {.sizing = {GROW, FIT}, .childGap = 10}}) {
                        WheelItem(0, "Shadows"); WheelItem(1, "Tint"); WheelItem(2, "Highlights");
                    }
                } else {
                    for (int s = 0; s < n; s++) {
                        CLAY({ .id = (CLAY_IDI("SRow", idx * 8 + s)), .layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6} }) {
                            CLAY({.layout = {.sizing = {GROW, FIT}}}) { Tx(kSliderNames[idx][s], 11, C_DIM, 0); HSPACE Tx("0", 11, C_MUTED, 0); }
                            Clay_ElementId sid = CLAY_IDI("Slid", idx * 8 + s);
                            Bs b = Interact(sid);
                            CLAY({ .id = (sid), .layout = {.sizing = {GROW, FIX(16)}},
                                       .custom = {.customData = NC(K_SLIDER, RGB(48, 50, 57), RGB(70, 73, 82), C_CARD, .5f, b.hover * 1.5f, 0)} }) {}
                        }
                    }
                }
            }
        }
    }
}

static void ColorsView(void) {
    CLAY({ .id = (CLAY_ID("AccList")), .layout = {COLM, .sizing = {GROW, GROW}, .childGap = 10} }) {
        Accordion(0, "Color Grading", I_GRADING);
        Accordion(1, "HSL", I_SLIDERS);
        Accordion(2, "Light", I_SUN);
        Accordion(3, "Color", I_DROP);
        Accordion(4, "Detail", I_MOUNTAIN);
    }
}

static void RightPanel(void) {
    static const char *tabs[4] = {"Transform", "Colors", "Effects", "Text"};
    static const char *titles[4] = {"Transform", "Colors", "Effects", "Text"};
    static const int icons[4] = {I_CROP, I_PALETTE, I_SUN, I_TEXT};
    CLAY({ .id = (CLAY_ID("Right")), .layout = {COLM, .sizing = {FIX(340), GROW}, .childGap = 10} }) {
        CLAY({ .id = (CLAY_ID("InspTabs")), .layout = {.sizing = {GROW, FIX(48)}, .padding = CLAY_PADDING_ALL(5), .childGap = 4}, .backgroundColor = C_PANEL, .cornerRadius = CLAY_CORNER_RADIUS(16), .border = {.color = RGB(43,45,51), .width = CLAY_BORDER_OUTSIDE(1)} }) {
            for (int i = 0; i < 4; i++)
                if (TabBtn(CLAY_IDI("InspTab", i), tabs[i], gRightTab == i, 12, 11)) gRightTab = i;
        }
        CLAY({ .id = (CLAY_ID("InspBody")), .layout = {COLM, .sizing = {GROW, GROW}, .padding = CLAY_PADDING_ALL(12), .childGap = 10}, PANEL }) {
            CLAY({ .id = (CLAY_ID("InspTitle")), .layout = {.sizing = {GROW, FIT}, .padding = {4, 0, 2, 4}, .childGap = 12, .childAlignment = AL_Y} }) {
                Icon(icons[gRightTab], 22, C_DIM);
                Tx(titles[gRightTab], 18, C_TEXT, 1);
            }
            if (gRightTab == 0) TransformView();
            else if (gRightTab == 1) ColorsView();
        }
    }
}

/* ------------------------------------------------------------------ */
/* UI: timeline                                                        */
/* ------------------------------------------------------------------ */
static void Timeline(void) {
    float bodyH = Clay_GetElementData(CLAY_ID("TimeBody")).boundingBox.height;
    float extra = fmaxf(0.f, bodyH - 26.f);

    CLAY({ .id = (CLAY_ID("Timeline")), .layout = {COLM, .sizing = {GROW, FIX(260)}}, PANEL }) {
        CLAY({ .id = (CLAY_ID("TlBar")), .layout = {.sizing = {GROW, FIX(48)}, .padding = {14, 12, 0, 0}, .childGap = 4, .childAlignment = AL_Y} }) {
            IconBtn(CLAY_ID("ToolPtr"),  I_POINTER,  36, 18, C_TEXT, true, 9);
            IconBtn(CLAY_ID("ToolHand"), I_HAND,     36, 18, C_DIM, false, 9);
            IconBtn(CLAY_ID("ToolCut"),  I_SCISSORS, 36, 18, C_DIM, false, 9);
            IconBtn(CLAY_ID("ToolText"), I_TEXT,     36, 18, C_DIM, false, 9);
            GAPX(8)
            CLAY({.layout = {.sizing = {FIX(1), FIX(20)}}, .backgroundColor = RGB(46, 48, 54)}) {}
            GAPX(8)
            IconBtn(CLAY_ID("ToolDel"),  I_TRASH,    36, 18, C_DIM, false, 9);
            HSPACE
            CLAY({ .id = (CLAY_ID("KfGroup")), .layout = {.sizing = {FIT, FIX(32)}, .padding = {2, 2, 0, 0}, .childAlignment = AL_Y},
                                      .backgroundColor = RGB(21, 22, 26), .cornerRadius = CLAY_CORNER_RADIUS(10),
                                      .border = {.color = RGB(40, 42, 48), .width = CLAY_BORDER_OUTSIDE(1)} }) {
                IconBtn(CLAY_ID("KfPrev"), I_KF_PREV, 28, 14, RGB(90, 93, 102), false, 8);
                IconBtn(CLAY_ID("KfAdd"),  I_KF_DIAMOND, 28, 14, C_DIM, false, 8);
                IconBtn(CLAY_ID("KfNext"), I_KF_NEXT, 28, 14, RGB(90, 93, 102), false, 8);
            }
            GAPX(14)
            IconBtn(CLAY_ID("ZoomOut"), I_ZOOM_OUT, 28, 16, C_DIM, false, 8);
            Clay_ElementId zid = CLAY_ID("ZoomSlider");
            Bs zb = Interact(zid);
            CLAY({ .id = (zid), .layout = {.sizing = {FIX(125), FIX(20)}}, .custom = {.customData = NC(K_SLIDER, RGB(48, 50, 57), RGB(48, 50, 57), C_CARD, .24f, zb.hover * 1.5f, 0)} }) {}
            IconBtn(CLAY_ID("ZoomIn"), I_ZOOM_IN, 28, 16, C_DIM, false, 8);
            GAPX(12)
            Clay_ElementId mid = CLAY_ID("Snap");
            Bs mb = Interact(mid);
            CLAY({ .id = (mid), .layout = {.sizing = {FIX(36), FIX(36)}, .childAlignment = AL_C},
                       .backgroundColor = BtnBg(RGB(30, 44, 88), RGB(38, 56, 112), mb), .cornerRadius = CLAY_CORNER_RADIUS(10),
                       .border = {.color = RGB(48, 70, 140), .width = CLAY_BORDER_OUTSIDE(1)} }) {
                Icon(I_MAGNET, 18, RGB(96, 140, 255));
            }
        }
        CLAY({ .id = (CLAY_ID("TimeBody")), .layout = {COLM, .sizing = {GROW, GROW}} }) {
            CLAY({ .id = (CLAY_ID("RulerRow")), .layout = {.sizing = {GROW, FIX(26)}} }) {
                GAPX(60)
                CLAY({ .id = (CLAY_ID("Ruler")), .layout = {.sizing = {GROW, GROW}}, .custom = {.customData = NC(K_RULER, C_CARD, C_CARD, C_CARD, 0, extra, 0)} }) {}
            }
            CLAY({ .id = (CLAY_ID("TrackV1")), .layout = {.sizing = {GROW, FIX(44)}, .childAlignment = AL_Y}, .backgroundColor = C_ROW,
                                      .border = {.color = RGB(32, 34, 39), .width = {.top = 1, .bottom = 1}} }) {
                CLAY({.layout = {.sizing = {FIX(60), FIT}, .padding = {16, 0, 0, 0}, .childGap = 5, .childAlignment = AL_Y}}) {
                    Icon(I_FILM, 12, C_MUTED); Tx("V1", 11, C_MUTED, 0);
                }
                Clay_ElementId cid = CLAY_ID("Clip");
                Bs cb = Interact(cid);
                CLAY({ .id = (cid), .layout = {.sizing = {FIX(175), FIX(38)}, .padding = {10, 6, 5, 0}, .childGap = 5},
                           .custom = {.customData = NC(K_CLIP, RGB(112, 52, 140), RGB(78, 36, 108), C_CARD, 0, cb.hover, 0)} }) {
                    Icon(I_FILM, 11, C_TEXT);
                    Tx("City at dusk.mp4", 11, C_TEXT, 1);
                }
            }
            CLAY({ .id = (CLAY_ID("TrackA1")), .layout = {.sizing = {GROW, FIX(44)}, .childAlignment = AL_Y}, .backgroundColor = RGB(21, 22, 26),
                                      .border = {.color = RGB(32, 34, 39), .width = {.bottom = 1}} }) {
                CLAY({.layout = {.sizing = {FIX(60), FIT}, .padding = {16, 0, 0, 0}, .childGap = 5, .childAlignment = AL_Y}}) {
                    Icon(I_NOTE, 12, C_MUTED); Tx("A1", 11, C_MUTED, 0);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* frame                                                               */
/* ------------------------------------------------------------------ */
static Clay_RenderCommandArray BuildUI(void) {
    Clay_BeginLayout();
    CLAY({ .id = (CLAY_ID("Root")), .layout = {COLM, .sizing = {GROW, GROW}, .padding = {0, 8, 4, 4}, .childGap = 8}, .backgroundColor = C_BG }) {
        TopBar();
        CLAY({ .id = (CLAY_ID("Main")), .layout = {.sizing = {GROW, GROW}, .childGap = 10} }) {
            LeftPanel();
            CenterPanel();
            RightPanel();
        }
        Timeline();
    }
    return Clay_EndLayout();
}

static Font TryFont(const char **paths, int n) {
    int cps[100], k = 0;
    for (int c = 32; c < 127; c++) cps[k++] = c;
    cps[k++] = 0xB0; cps[k++] = 0xD7; cps[k++] = 0xB7; cps[k++] = 0x2026;
    for (int i = 0; i < n; i++) {
        if (FileExists(paths[i])) {
            Font f = LoadFontEx(paths[i], 48, cps, k);
            if (f.texture.id > 0) { SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR); return f; }
        }
    }
    return GetFontDefault();
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1676, 895, "Pluto");
    SetWindowMinSize(1180, 720);

    const char *reg[] = {"resources/Inter-Regular.ttf", "Inter-Regular.ttf", "C:/Windows/Fonts/segoeui.ttf",
                         "/System/Library/Fonts/Supplemental/Arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                         "/usr/share/fonts/TTF/DejaVuSans.ttf"};
    const char *bold[] = {"resources/Inter-SemiBold.ttf", "Inter-SemiBold.ttf", "C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/segoeuib.ttf",
                          "/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                          "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"};
    gFonts[0] = TryFont(reg, 6);
    gFonts[1] = TryFont(bold, 7);
    if (gFonts[1].texture.id == GetFontDefault().texture.id) gFonts[1] = gFonts[0];

    uint64_t mem = Clay_MinMemorySize();
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(mem, malloc(mem));
    Clay_Initialize(arena, (Clay_Dimensions){(float)GetScreenWidth(), (float)GetScreenHeight()}, (Clay_ErrorHandler){OnClayError, 0});
    Clay_SetMeasureTextFunction(MeasureTxt, NULL);

    while (!WindowShouldClose()) {
        gDt = GetFrameTime(); if (gDt > 0.05f) gDt = 0.05f;
        Clay_SetLayoutDimensions((Clay_Dimensions){(float)GetScreenWidth(), (float)GetScreenHeight()});
        Vector2 m = GetMousePosition();
        Clay_SetPointerState((Clay_Vector2){m.x, m.y}, IsMouseButtonDown(MOUSE_BUTTON_LEFT));
        Clay_UpdateScrollContainers(false, (Clay_Vector2){0, GetMouseWheelMove() * 28.f}, gDt);

        gCustN = 0; gHand = false;
        Clay_RenderCommandArray cmds = BuildUI();

        // Custom frameless title bar: drag the top strip to move the window.
        Vector2 mouseNow = GetMousePosition();
        Vector2 mouseDelta = GetMouseDelta();
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouseNow.y >= 2 && mouseNow.y <= 42 && mouseNow.x < 260) {
            gWindowDrag = true;
            gDragMouse = mouseNow;
            gDragWindow = GetWindowPosition();
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) gWindowDrag = false;
        if (gWindowDrag && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Vector2 wp = GetWindowPosition();
            SetWindowPosition((int)(wp.x + mouseDelta.x), (int)(wp.y + mouseDelta.y));
        }

        BeginDrawing();
        ClearBackground((Color){14, 15, 17, 255});
        RenderUI(cmds);
        EndDrawing();
        SetMouseCursor(gHand ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
    }
    CloseWindow();
    return 0;
}
