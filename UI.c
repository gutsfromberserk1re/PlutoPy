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
 * Right panel: tabs, accordions, link toggles, Crop Tools / Distort expanders
 * and scrolling all react to clicks. Everything else only gives hover / press feedback.
 */
#define CLAY_IMPLEMENTATION
#include "clay.h"
#include "raylib.h"

/* ------------------------------------------------------------------ */
/* Compatibility helpers: legacy CLAY(id, { config }) call style       */
/* ------------------------------------------------------------------ */
static uint32_t gClayAutoId = 0;

static Clay_ElementDeclaration ClayCompatDeclaration(
    Clay_ElementId id,
    Clay_ElementDeclaration declaration
) {
    declaration.id = id;
    return declaration;
}

#undef CLAY
#define CLAY(id, ...) \
    for ( \
        CLAY__ELEMENT_DEFINITION_LATCH = ( \
            Clay__OpenElement(), \
            Clay__ConfigureOpenElement( \
                ClayCompatDeclaration((id), (Clay_ElementDeclaration)__VA_ARGS__) \
            ), \
            0 \
        ); \
        CLAY__ELEMENT_DEFINITION_LATCH < 1; \
        CLAY__ELEMENT_DEFINITION_LATCH = 1, Clay__CloseElement() \
    )

#define CLAY_AUTO_ID(...) \
    CLAY(CLAY_IDI_LOCAL("PlutoAuto", gClayAutoId++), __VA_ARGS__)
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
#define C_BG        RGB(11,11,13)
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
/* right-panel palette (sampled from the reference) */
#define C_PGRAD     RGB(27,28,31)   /* sentinel: drawn as a vertical gradient 27,28,31 -> 21,22,24 */
#define C_RCARD     RGB(24,25,28)
#define C_RCARDB    RGB(35,36,40)
#define C_RFIELD    RGB(33,34,38)
#define RPANEL      .backgroundColor = C_PGRAD, .cornerRadius = CLAY_CORNER_RADIUS(16), \
                    .border = {.color = RGB(35,36,38), .width = CLAY_BORDER_OUTSIDE(1)}
#define RCARD       .backgroundColor = C_RCARD, .cornerRadius = CLAY_CORNER_RADIUS(14), \
                    .border = {.color = C_RCARDB, .width = CLAY_BORDER_OUTSIDE(1)}

/* ------------------------------------------------------------------ */
/* small layout shorthands                                             */
/* ------------------------------------------------------------------ */
#define GROW        CLAY_SIZING_GROW(0)
#define FIT         CLAY_SIZING_FIT(0)
#define FIX(n)      CLAY_SIZING_FIXED(n)
#define COLM        .layoutDirection = CLAY_TOP_TO_BOTTOM
#define AL_C        {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
#define AL_Y        {.y = CLAY_ALIGN_Y_CENTER}
#define HSPACE      CLAY_AUTO_ID({.layout = {.sizing = {.width = GROW}}}) {}
#define VSPACE      CLAY_AUTO_ID({.layout = {.sizing = {.height = GROW}}}) {}
#define GAPX(n)     CLAY_AUTO_ID({.layout = {.sizing = {.width = FIX(n), .height = FIX(1)}}}) {}
#define PANEL       .backgroundColor = C_PANEL, .cornerRadius = CLAY_CORNER_RADIUS(16), \
                    .border = {.color = C_PBORDER, .width = CLAY_BORDER_OUTSIDE(1)}
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

/* right panel state */
static bool  gLink[4] = {false, true, false, false};  /* position, scale, crop, distort */
static int   gOpenTool = -1;                          /* -1 none, 0 crop tools, 1 distort */
static float gScroll, gScrollT;                       /* inspector scroll (pixels down)   */
static bool  gFollow;                                 /* keep pinned to bottom while expanding */
static float gInspViewH;
static float gTabT = 1.f;                             /* tab-switch transition 0..1 */
static float gTabE = 1.f;                             /* eased */
static Rectangle gFadeR; static float gFadeA;         /* fade overlay while switching */
static bool  gSB; static Rectangle gSBR;              /* inspector scrollbar thumb */

/* playhead is drawn after everything else so rows never cover it */
static bool gPH; static Vector2 gPHPos; static float gPHLen;

/* ------------------------------------------------------------------ */
/* animation table                                                     */
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

/* fixed-duration linear progress 0..1 (always finishes exactly, no slow tail) */
static float AnimLin(uint32_t key, float target, float dur) {
    key ^= 0x5BD1E995u;
    for (int i = 0; i < 2048; i++) {
        if (gAnim[i].used && gAnim[i].id == key) {
            float step = gDt / dur, d = target - gAnim[i].v;
            if (fabsf(d) <= step) gAnim[i].v = target;
            else gAnim[i].v += (d > 0 ? step : -step);
            return gAnim[i].v;
        }
    }
    for (int i = 0; i < 2048; i++)
        if (!gAnim[i].used) { gAnim[i].used = true; gAnim[i].id = key; gAnim[i].v = target; return target; }
    return target;
}
static float EaseIO(float t) { if (t < 0) t = 0; if (t > 1) t = 1; return t * t * (3.f - 2.f * t); }

static Clay_Color Mix(Clay_Color a, Clay_Color b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
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
    b.press   = Anim(id.id ^ 0x9E3779B9u, down ? 1.f : 0.f, 60.f);
    b.clicked = over && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    return b;
}
static Clay_Color BtnBg(Clay_Color base, Clay_Color hov, Bs b) { return Mix(Mix(base, hov, b.hover), RGB(90, 95, 106), b.press * 0.6f); }

/* ------------------------------------------------------------------ */
/* text + icon helpers                                                 */
/* ------------------------------------------------------------------ */
static Clay_String S(const char *s) { return (Clay_String){.isStaticallyAllocated = false, .length = (int32_t)strlen(s), .chars = s}; }

static void Tx(const char *s, float size, Clay_Color c, int font) {
    CLAY_TEXT(S(s), CLAY_TEXT_CONFIG({.fontId = font, .fontSize = size, .textColor = c, .wrapMode = CLAY_TEXT_WRAP_NONE}));
}
static void IconV(int k, float sz, Clay_Color c, float v) {
    CLAY_AUTO_ID({.layout = {.sizing = {.width = FIX(sz), .height = FIX(sz)}},
                  .custom = {.customData = NC(k, c, c, c, v, 0, 0)}}) {}
}
static void Icon(int k, float sz, Clay_Color c) { IconV(k, sz, c, 1.f); }

static bool IconBtn(Clay_ElementId id, int icon, float box, float isz, Clay_Color base, bool sel, float rad) {
    Bs b = Interact(id);
    Clay_Color ic = Mix(Mix(base, C_TEXT, b.hover), C_TEXT, b.press);
    Clay_Color bg = sel ? Mix(C_SEL, RGB(90, 95, 106), b.press * 0.6f) : RGBA(255, 255, 255, 14 * b.hover + 56 * b.press);
    CLAY(id, {.layout = {.sizing = {FIX(box), FIX(box)}, .childAlignment = AL_C},
              .backgroundColor = bg, .cornerRadius = CLAY_CORNER_RADIUS(rad)}) {
        Icon(icon, isz, ic);
    }
    return b.clicked;
}

static bool TabBtn(Clay_ElementId id, const char *label, bool sel, float size, float rad, bool border) {
    Bs b = Interact(id);
    float s = Anim(id.id + 7, sel ? 1.f : 0.f, 45.f);
    float a = fmaxf(s * 255.f, b.hover * (1.f - s) * 60.f);
    Clay_Color fill = border ? RGB(42, 43, 49) : RGB(46, 48, 55);
    Clay_Color bg = Mix(RGBA(fill.r, fill.g, fill.b, a), RGBA(90, 95, 106, 255), b.press * 0.55f);
    Clay_Color off = border ? RGB(140, 142, 150) : C_MUTED;
    CLAY(id, {.layout = {.sizing = {GROW, GROW}, .childAlignment = AL_C},
              .backgroundColor = bg, .cornerRadius = CLAY_CORNER_RADIUS(rad),
              .border = {.color = RGBA(55, 56, 62, border ? s * 255.f : 0.f), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        Tx(label, size, Mix(off, C_TEXT, fmaxf(s, b.hover * 0.6f)), 0);
    }
    /* select on press, not release, so it feels instant */
    return Clay_PointerOver(id) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* link / chain toggle with blue glow when linked */
static bool LinkBtn(Clay_ElementId id, bool linked, float box, float isz) {
    Bs b = Interact(id);
    float la = Anim(id.id + 11u, linked ? 1.f : 0.f, 16.f);
    Clay_Color ic = Dark(Mix(Mix(C_MUTED, C_TEXT, b.hover), RGB(88, 142, 255), la), b.press * 0.2f);
    Clay_Color bg = Mix(RGBA(255, 255, 255, 14 * b.hover + 56 * b.press), RGBA(64, 120, 255, 46 + 12 * b.hover + 50 * b.press), la);
    CLAY(id, {.layout = {.sizing = {FIX(box), FIX(box)}, .childAlignment = AL_C},
              .backgroundColor = bg, .cornerRadius = CLAY_CORNER_RADIUS(box / 2)}) {
        Icon(I_LINK, isz, ic);
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
static void Capsule(Vector2 c, float angDeg, float len, float rad, float t, Color col) {
    float a = angDeg * DEG2RAD;
    Vector2 d = {cosf(a), sinf(a)}, n = {-d.y, d.x};
    Vector2 e0 = {c.x - d.x * len / 2, c.y - d.y * len / 2}, e1 = {c.x + d.x * len / 2, c.y + d.y * len / 2};
    DrawLineEx((Vector2){e0.x + n.x * rad, e0.y + n.y * rad}, (Vector2){e1.x + n.x * rad, e1.y + n.y * rad}, t, col);
    DrawLineEx((Vector2){e0.x - n.x * rad, e0.y - n.y * rad}, (Vector2){e1.x - n.x * rad, e1.y - n.y * rad}, t, col);
    Arc(e1, rad, t, angDeg - 90, angDeg + 90, col);
    Arc(e0, rad, t, angDeg + 90, angDeg + 270, col);
}
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
                    DrawCircleV(P(.7, .42), .06f * w, c); DrawCircleV(P(.38, .65), .06f * w, c);
                    Ring(P(.66, .72), .1f * w, t * .8f, c); break;
    case I_GRADING: Ring(P(.5, .5), .4f * w, t, c);
                    DrawCircleV(P(.5, .32), .08f * w, c); DrawCircleV(P(.33, .62), .08f * w, c); DrawCircleV(P(.67, .62), .08f * w, c); break;
    case I_SLIDERS: LN(.1, .3, .9, .3); LN(.1, .7, .9, .7);
                    DrawCircleV(P(.65, .3), .11f * w, c); DrawCircleV(P(.35, .7), .11f * w, c); break;
    case I_SUN:     Ring(P(.5, .5), .2f * w, t, c);
                    for (int i = 0; i < 8; i++) { float a = i * PI / 4; float s = sinf(a), co = cosf(a);
                        DrawLineEx((Vector2){x + (.5f + .32f * co) * w, y + (.5f + .32f * s) * h},
                                   (Vector2){x + (.5f + .45f * co) * w, y + (.5f + .45f * s) * h}, t, c); } break;
    case I_DROP:    Arc(P(.5, .64), .30f * w, t, 327.6f, 572.4f, c); LN(.5, .08, .247, .479); LN(.5, .08, .753, .479); break;
    case I_MOUNTAIN:LN(.08, .8, .4, .28); LN(.4, .28, .7, .8); LN(.08, .8, .92, .8); LN(.6, .55, .72, .4); LN(.72, .4, .92, .8); break;
    case I_CROP:    LN(.3, .1, .3, .7); LN(.3, .7, .9, .7); LN(.1, .3, .7, .3); LN(.7, .3, .7, .9); break;
    case I_POS:     LN(.5, .08, .5, .36); LN(.5, .64, .5, .92); LN(.08, .5, .36, .5); LN(.64, .5, .92, .5); DrawCircleV(P(.5, .5), .05f * w, c); break;
    case I_SCALE:   LN(.2, .8, .8, .2); LN(.55, .2, .8, .2); LN(.8, .2, .8, .45); LN(.2, .55, .2, .8); LN(.2, .8, .45, .8); break;
    case I_RESET:
    case I_ROT:     Arc(P(.5, .5), .34f * w, t, 210, 500, c); LN(.2, .33, .2, .1); LN(.2, .33, .42, .33); break;
    case I_LINK:    Capsule(P(.36, .64), -45, .20f * w, .15f * w, t, c);
                    Capsule(P(.64, .36), -45, .20f * w, .15f * w, t, c); break;
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
            DrawCircleSector(c, rad, i * 5.f, i * 5.f + 5.6f, 3, ColorFromHSV(fmodf(i * 5.f + 90.f, 360.f), .70f, .62f));
        /* darken + desaturate towards the centre (each ring has its own alpha) */
        for (float rr = rad; rr > 1.f; rr -= 1.f) {
            float t = 1.f - rr / rad;
            DrawRing(c, rr - 1.f, rr, 0, 360, 40, (Color){34, 32, 40, U8(225 * powf(t, 1.35f))});
        }
        Ring(c, rad, 2, (Color){38, 39, 45, 255});
        float dot = 9.f + 1.5f * cu->f;
        DrawCircleV(c, dot + 1.5f, (Color){14, 14, 18, 130});
        DrawCircleV(c, dot, (Color){226, 227, 231, 255});
    } break;

    case K_SWITCH: {
        float rad = r.height / 2;
        FillRR(r, rad, RC(Mix(RGB(44, 45, 51), C_ACCENT, cu->v)));
        StrokeRR(r, rad, 1, (Color){56, 58, 65, 255});
        float kx = r.x + rad + (r.width - 2 * rad) * cu->v;
        DrawCircleV((Vector2){kx, r.y + rad}, rad - 3.f + cu->f, (Color){226, 227, 231, 255});
    } break;

    case K_SLIDER: {
        bool big = cu->i == 1;
        float th = big ? 5.f : 4.f, kr = big ? 8.f : 7.f;
        float cy = r.y + r.height / 2;
        Rectangle tr = {r.x, cy - th / 2, r.width, th};
        FillRR(tr, th / 2, RC(cu->a));
        FillRR((Rectangle){r.x, cy - th / 2, kr + (r.width - 2 * kr) * cu->v, th}, th / 2, RC(cu->b));
        DrawCircleV((Vector2){r.x + kr + (r.width - 2 * kr) * cu->v, cy}, kr + cu->f, (Color){240, 240, 244, 255});
    } break;

    case K_ROTRULER: {
        Rectangle box = {r.x, r.y, r.width, 48};
        FillRR(box, 12, (Color){13, 14, 17, 255});
        StrokeRR(box, 12, 1, (Color){38, 40, 46, 255});
        float x0 = box.x + 24, x1 = box.x + box.width - 24, cy = box.y + 24;
        for (int i = 0; i <= 36; i++) {
            float px = x0 + (x1 - x0) * i / 36.f;
            if (i == 18)          DrawLineEx((Vector2){px, cy - 15}, (Vector2){px, cy + 15}, 3.f, (Color){235, 55, 65, 255});
            else if (i % 9 == 0)  DrawLineEx((Vector2){px, cy - 13}, (Vector2){px, cy + 13}, 1.8f, (Color){232, 234, 240, 255});
            else if (i % 3 == 0)  DrawLineEx((Vector2){px, cy - 8},  (Vector2){px, cy + 8},  1.2f, (Color){150, 153, 162, 220});
            else                  DrawLineEx((Vector2){px, cy - 5},  (Vector2){px, cy + 5},  1.f,  (Color){104, 107, 116, 200});
        }
        const char *lab[5] = {"-180\xC2\xB0", "-90\xC2\xB0", "0\xC2\xB0", "90\xC2\xB0", "180\xC2\xB0"};
        for (int i = 0; i < 5; i++) {
            float px = x0 + (x1 - x0) * (i * 9) / 36.f;
            TxtAt(0, lab[i], px - TxtW(0, lab[i], 14) / 2, box.y + 56, 14, RC(C_MUTED));
        }
        float cx = x0 + (x1 - x0) / 2;
        Tri((Vector2){cx, r.y + 78}, (Vector2){cx - 7, r.y + 90}, (Vector2){cx + 7, r.y + 90}, (Color){240, 240, 244, 255});
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
/* Clay -> raylib renderer (with nested scissor stack)                 */
/* ------------------------------------------------------------------ */
static Rectangle gScis[16];
static int gScisN;
static void ApplyScissor(void) {
    if (gScisN > 0) {
        Rectangle s = gScis[gScisN - 1];
        BeginScissorMode((int)s.x, (int)s.y, (int)s.width, (int)s.height);
    }
}

static void RenderUI(Clay_RenderCommandArray cmds) {
    gPH = false;
    gScisN = 0;
    for (int i = 0; i < cmds.length; i++) {
        Clay_RenderCommand *rc = Clay_RenderCommandArray_Get(&cmds, i);
        Clay_BoundingBox bb = rc->boundingBox;
        Rectangle r = {bb.x, bb.y, bb.width, bb.height};
        switch (rc->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
            Clay_RectangleRenderData *d = &rc->renderData.rectangle;
            Clay_Color bc = d->backgroundColor;
            if (bc.r == 27 && bc.g == 28 && bc.b == 31 && bc.a == 255)
                GradRR(r, d->cornerRadius.topLeft, (Color){29, 30, 33, 255}, (Color){22, 23, 26, 255});
            else FillRR(r, d->cornerRadius.topLeft, RC(bc));
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
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
            float x0 = roundf(r.x), y0 = roundf(r.y), x1 = roundf(r.x + r.width), y1 = roundf(r.y + r.height);
            if (gScisN > 0) {
                Rectangle p = gScis[gScisN - 1];
                x0 = fmaxf(x0, p.x); y0 = fmaxf(y0, p.y);
                x1 = fminf(x1, p.x + p.width); y1 = fminf(y1, p.y + p.height);
            }
            Rectangle n = {x0, y0, fmaxf(0.f, x1 - x0), fmaxf(0.f, y1 - y0)};
            if (gScisN < 16) gScis[gScisN++] = n;
            ApplyScissor();
        } break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
            if (gScisN > 0) gScisN--;
            EndScissorMode();
            ApplyScissor();
            break;
        case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
            Cust *cu = (Cust *)rc->renderData.custom.customData;
            if (cu) DrawCustom(cu, r);
        } break;
        default: break;
        }
    }
    if (gScisN > 0) { gScisN = 0; EndScissorMode(); }
    if (gPH) {
        Color red = {240, 62, 72, 255};
        DrawLineEx(gPHPos, (Vector2){gPHPos.x, gPHPos.y + gPHLen}, 1.6f, red);
        DrawRectangleRec((Rectangle){gPHPos.x - 5, gPHPos.y - 4, 10, 8}, red);
        Tri((Vector2){gPHPos.x - 5, gPHPos.y + 4}, (Vector2){gPHPos.x + 5, gPHPos.y + 4}, (Vector2){gPHPos.x, gPHPos.y + 10}, red);
    }
    if (gFadeA > 2.f && gFadeR.width > 0.f)
        GradRR(gFadeR, 16, (Color){29, 30, 33, U8(gFadeA)}, (Color){22, 23, 26, U8(gFadeA)});
    if (gSB) FillRR(gSBR, 2, (Color){92, 95, 104, 255});
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
    CLAY(CLAY_ID("TopBar"), {.layout = {.sizing = {GROW, FIX(48)}, .padding = {12, 12, 0, 0}, .childAlignment = AL_Y}}) {
        CLAY(CLAY_ID("LeftCluster"), {.layout = {.sizing = {FIX(316), FIT}, .childAlignment = AL_Y}}) {
            CLAY(CLAY_ID("Logo"), {.layout = {.sizing = {FIX(24), FIX(24)}}, .backgroundColor = RGB(20, 21, 25),
                                   .cornerRadius = CLAY_CORNER_RADIUS(6), .border = {.color = RGB(64, 66, 73), .width = CLAY_BORDER_OUTSIDE(1)}}) {}
            GAPX(14)
            Tx("Pluto", 15, C_TEXT, 1);
            GAPX(40)
            Tx("Untitled Project", 13, C_DIM, 0);
        }
        Clay_ElementId ex = CLAY_ID("Export");
        Bs b = Interact(ex);
        CLAY(ex, {.layout = {.sizing = {FIT, FIX(32)}, .padding = {12, 14, 0, 0}, .childGap = 8, .childAlignment = AL_Y},
                  .backgroundColor = BtnBg(RGB(27, 28, 33), RGB(38, 40, 46), b), .cornerRadius = CLAY_CORNER_RADIUS(9),
                  .border = {.color = Mix(RGB(54, 56, 62), RGB(86, 89, 98), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
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
    CLAY(sid, {.layout = {.sizing = {GROW, FIX(38)}, .padding = {12, 12, 0, 0}, .childGap = 8, .childAlignment = AL_Y},
               .backgroundColor = Mix(RGB(30, 31, 36), RGB(36, 37, 43), sb.hover), .cornerRadius = CLAY_CORNER_RADIUS(10)}) {
        Icon(I_SEARCH, 14, C_MUTED);
        Tx("Search media", 12, C_MUTED, 0);
    }
    for (int row = 0; row < 3; row++) {
        CLAY(CLAY_IDI("MRow", row), {.layout = {.sizing = {GROW, FIT}, .childGap = 10}}) {
            for (int col = 0; col < 2; col++) {
                int i = row * 2 + col;
                const MediaItem *m = &kMedia[i];
                CLAY(CLAY_IDI("Media", i), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6}}) {
                    Clay_ElementId tid = CLAY_IDI("Thumb", i);
                    Bs b = Interact(tid);
                    CLAY(tid, {.layout = {.sizing = {GROW, FIX(66)}, .padding = {5, 5, 5, 5}, .childAlignment = {.y = CLAY_ALIGN_Y_BOTTOM}},
                               .custom = {.customData = NC(K_THUMB, m->sky1, m->sky2, m->mtn, 0, b.hover, m->style)}}) {
                        CLAY_AUTO_ID({.layout = {.sizing = {FIT, FIT}, .padding = {5, 5, 1, 2}},
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
    CLAY(CLAY_IDI("KG", idx), {.layout = {COLM, .sizing = {FIT, FIT}, .childGap = 5, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
        Clay_ElementId kid = CLAY_IDI("Knob", idx);
        Bs b = Interact(kid);
        CLAY(kid, {.layout = {.sizing = {FIX(70), FIX(70)}}, .custom = {.customData = NC(K_KNOB, C_CARD, C_CARD, C_CARD, .5f, b.hover, 0)}}) {}
        Tx(label, 11, C_MUTED, 0);
        CLAY_AUTO_ID({.layout = {.sizing = {FIX(64), FIX(24)}, .childAlignment = AL_C},
                      .backgroundColor = RGB(28, 29, 34), .cornerRadius = CLAY_CORNER_RADIUS(6),
                      .border = {.color = RGB(40, 42, 48), .width = CLAY_BORDER_OUTSIDE(1)}}) {
            Tx(val, 12, C_TEXT, 0);
        }
    }
}

static void SwitchItem(int idx, const char *label) {
    Clay_ElementId id = CLAY_IDI("Switch", idx);
    Bs b = Interact(id);
    CLAY(id, {.layout = {.sizing = {FIT, FIT}, .childGap = 8, .childAlignment = AL_Y}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {FIX(40), FIX(24)}},
                      .custom = {.customData = NC(K_SWITCH, C_CARD, C_CARD, C_CARD, 0, b.hover * 1.f - b.press * 1.5f, 0)}}) {}
        Tx(label, 11, C_MUTED, 0);
    }
}

static void AudioView(void) {
    CLAY(CLAY_ID("AudioHead"), {.layout = {.sizing = {GROW, FIT}, .padding = {4, 0, 0, 0}, .childGap = 8, .childAlignment = AL_Y}}) {
        Icon(I_NOTE, 18, C_DIM);
        Tx("Audio", 15, C_TEXT, 1);
    }
    CLAY(CLAY_ID("AudioBody"), {.layout = {.sizing = {GROW, GROW}, .childGap = 6}}) {
        CLAY(CLAY_ID("AudioScroll"), {.layout = {COLM, .sizing = {GROW, GROW}, .childGap = 10, .padding = {2, 0, 0, 8}},
                                      .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()}}) {
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) { KnobGroup(0, "Volume", "100 %"); }
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .padding = {4, 4, 0, 0}}}) { KnobGroup(1, "Pan", "0"); HSPACE KnobGroup(2, "Bass", "0 dB"); }
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) { KnobGroup(3, "Treble", "0 dB"); }
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .padding = {6, 0, 8, 0}, .childGap = 18}}) { SwitchItem(0, "Fade In"); SwitchItem(1, "Fade Out"); }
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .padding = {2, 4, 0, 0}, .childAlignment = AL_Y}}) {
                Tx("Fade In Duration", 11, C_TEXT, 0);
                HSPACE
                CLAY_AUTO_ID({.layout = {.sizing = {FIX(64), FIX(28)}, .padding = {0, 10, 0, 0}, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER}, .childGap = 4},
                              .backgroundColor = RGB(30, 31, 36), .cornerRadius = CLAY_CORNER_RADIUS(6)}) {
                    Tx("1", 12, C_TEXT, 0); Tx("s", 10, C_MUTED, 0);
                }
            }
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(18)}, .padding = {2, 2, 0, 0}},
                          .custom = {.customData = NC(K_SLIDER, RGB(48, 50, 57), RGB(70, 73, 82), C_CARD, 0.05f, 0, 0)}}) {}
        }
        CLAY(CLAY_ID("AudioScrollbar"), {.layout = {.sizing = {FIX(4), GROW}}, .custom = {.customData = NC(K_SCROLLBAR, C_CARD, C_CARD, C_CARD, 0, 0, 0)}}) {}
    }
}

static void LeftPanel(void) {
    CLAY(CLAY_ID("Library"), {.layout = {COLM, .sizing = {FIX(242), GROW}, .padding = {12, 12, 14, 12}, .childGap = 14}, PANEL}) {
        CLAY(CLAY_ID("LibHead"), {.layout = {.sizing = {GROW, FIT}, .padding = {4, 0, 0, 0}, .childGap = 8, .childAlignment = AL_Y}}) {
            Icon(I_LIBRARY, 18, C_DIM);
            Tx("Library", 15, C_TEXT, 1);
            HSPACE
            Clay_ElementId imp = CLAY_ID("Import");
            Bs b = Interact(imp);
            CLAY(imp, {.layout = {.sizing = {FIT, FIX(28)}, .padding = {10, 12, 0, 0}, .childGap = 5, .childAlignment = AL_Y},
                       .backgroundColor = BtnBg(RGB(28, 29, 34), RGB(40, 42, 48), b), .cornerRadius = CLAY_CORNER_RADIUS(9)}) {
                Icon(I_PLUS, 11, C_DIM);
                Tx("Import", 12, C_TEXT, 0);
            }
        }
        CLAY(CLAY_ID("LibTabs"), {.layout = {.sizing = {GROW, FIX(40)}, .padding = CLAY_PADDING_ALL(4), .childGap = 4},
                                  .backgroundColor = C_SEG, .cornerRadius = CLAY_CORNER_RADIUS(12),
                                  .border = {.color = RGB(30, 32, 37), .width = CLAY_BORDER_OUTSIDE(1)}}) {
            if (TabBtn(CLAY_IDI("LibTab", 0), "Media", gLeftTab == 0, 12, 9, false)) gLeftTab = 0;
            if (TabBtn(CLAY_IDI("LibTab", 1), "Audio", gLeftTab == 1, 12, 9, false)) gLeftTab = 1;
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

    CLAY(CLAY_ID("Center"), {.layout = {COLM, .sizing = {GROW, GROW}}, PANEL}) {
        CLAY(CLAY_ID("PreviewArea"), {.layout = {.sizing = {GROW, GROW}, .padding = CLAY_PADDING_ALL(14), .childAlignment = AL_C},
                                      .clip = {.horizontal = true, .vertical = true}}) {
            CLAY(CLAY_ID("PreviewFrame"), {.layout = {.sizing = {FIX(pw), FIX(ph)}},
                                           .custom = {.customData = NC(K_PREVIEW, C_BG, C_BG, C_BG, 0, 0, 0)}}) {}
        }
        CLAY(CLAY_ID("Transport"), {.layout = {.sizing = {GROW, FIX(56)}, .padding = {14, 14, 0, 4}, .childAlignment = AL_Y}}) {
            CLAY(CLAY_ID("TimeL"), {.layout = {.sizing = {GROW, FIT}, .childGap = 7, .childAlignment = AL_Y}}) {
                Tx("00:00:00:00", 15, C_TEXT, 1);
                Tx("/", 14, C_MUTED, 0);
                Tx("00:00:09:00", 14, C_MUTED, 0);
            }
            CLAY(CLAY_ID("TransC"), {.layout = {.sizing = {FIT, FIT}, .childGap = 12, .childAlignment = AL_Y}}) {
                IconBtn(CLAY_ID("SkipB"), I_SKIP_B, 32, 16, C_DIM, false, 16);
                IconBtn(CLAY_ID("PrevF"), I_CHEV_L, 28, 14, C_DIM, false, 14);
                Clay_ElementId pid = CLAY_ID("Play");
                Bs b = Interact(pid);
                float sz = 44.f + 3.f * b.hover - 3.f * b.press;
                CLAY(pid, {.layout = {.sizing = {FIX(50), FIX(50)}, .childAlignment = AL_C}}) {
                    CLAY_AUTO_ID({.layout = {.sizing = {FIX(sz), FIX(sz)}, .childAlignment = AL_C},
                                  .backgroundColor = Dark(RGB(244, 244, 247), b.press * .12f), .cornerRadius = CLAY_CORNER_RADIUS(sz / 2)}) {
                        Icon(I_PLAY, 18, RGB(12, 12, 14));
                    }
                }
                IconBtn(CLAY_ID("NextF"), I_CHEV_R, 28, 14, C_DIM, false, 14);
                IconBtn(CLAY_ID("SkipF"), I_SKIP_F, 32, 16, C_DIM, false, 16);
            }
            CLAY(CLAY_ID("TransR"), {.layout = {.sizing = {GROW, FIT}, .childGap = 10, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER}}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {FIT, FIX(22)}, .padding = {8, 8, 0, 0}, .childAlignment = AL_C},
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
/* number field: value + unit + up/down chevrons */
static void NumField(int n, const char *val, const char *unit) {
    Clay_ElementId id = CLAY_IDI("NumF", n);
    Bs b = Interact(id);
    CLAY(id, {.layout = {.sizing = {GROW, FIX(36)}, .padding = {10, 6, 0, 0}, .childGap = 5, .childAlignment = AL_Y},
              .backgroundColor = Mix(Mix(C_RFIELD, RGB(46, 47, 54), b.hover), RGB(70, 73, 83), b.press * 0.8f), .cornerRadius = CLAY_CORNER_RADIUS(10),
              .border = {.color = Mix(RGB(44, 46, 52), RGB(76, 79, 88), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        HSPACE
        Tx(val, 20, C_TEXT, 0);
        Tx(unit, 15, C_MUTED, 0);
        CLAY_AUTO_ID({.layout = {COLM, .sizing = {FIT, FIT}, .padding = {2, 0, 0, 0}}}) {
            Icon(I_CHEV_UP, 12, C_MUTED);
            Icon(I_CHEV_DN, 12, C_MUTED);
        }
    }
}

/* small field used by crop / distort panels (T, L, R, B, TL, TR, BL, BR) */
static void MiniField(int n, const char *lab) {
    Clay_ElementId id = CLAY_IDI("MiniF", n);
    Bs b = Interact(id);
    CLAY(id, {.layout = {.sizing = {FIX(112), FIX(34)}, .padding = {10, 6, 0, 0}, .childGap = 4, .childAlignment = AL_Y},
              .backgroundColor = Mix(Mix(C_RFIELD, RGB(46, 47, 54), b.hover), RGB(70, 73, 83), b.press * 0.8f), .cornerRadius = CLAY_CORNER_RADIUS(9),
              .border = {.color = Mix(RGB(44, 46, 52), RGB(76, 79, 88), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        Tx(lab, 12, RGB(112, 124, 156), 1);
        HSPACE
        Tx("0", 18, C_TEXT, 0);
        Tx("px", 13, C_MUTED, 0);
        CLAY_AUTO_ID({.layout = {COLM, .sizing = {FIT, FIT}, .padding = {2, 0, 0, 0}}}) {
            Icon(I_CHEV_UP, 10, C_MUTED);
            Icon(I_CHEV_DN, 10, C_MUTED);
        }
    }
}

static void FieldCard(int n, const char *title, int icon, const char *l1, const char *l2, const char *val, const char *unit, int link) {
    CLAY(CLAY_IDI("FCard", n), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = CLAY_PADDING_ALL(12), .childGap = 8}, RCARD}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 8, .childAlignment = AL_Y}}) {
            Icon(icon, 19, C_DIM);
            Tx(title, 18, C_TEXT, 1);
            HSPACE
            IconBtn(CLAY_IDI("Reset", n), I_RESET, 24, 14, C_MUTED, false, 6);
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 6, .childAlignment = AL_Y}}) {
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(20), FIT}}}) { Tx(l1, 17, C_MUTED, 0); }
            NumField(n * 2, val, unit);
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
            if (LinkBtn(CLAY_IDI("Link", n), gLink[link], 28, 20)) gLink[link] = !gLink[link];
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 6, .childAlignment = AL_Y}}) {
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(20), FIT}}}) { Tx(l2, 17, C_MUTED, 0); }
            NumField(n * 2 + 1, val, unit);
        }
    }
}

/* Crop Tools / Distort toggle button */
static bool NavButton(int n, int icon, const char *label, float open) {
    Clay_ElementId id = CLAY_IDI("Nav", n);
    Bs b = Interact(id);
    Clay_Color base = Mix(C_RCARD, RGB(42, 44, 51), open);
    Clay_Color hov  = Mix(RGB(38, 40, 46), RGB(48, 50, 58), open);
    CLAY(id, {.layout = {.sizing = {GROW, FIX(54)}, .padding = {14, 12, 0, 0}, .childGap = 10, .childAlignment = AL_Y},
              .backgroundColor = BtnBg(base, hov, b), .cornerRadius = CLAY_CORNER_RADIUS(14),
              .border = {.color = Mix(C_RCARDB, RGB(80, 83, 92), fmaxf(b.hover, open * 0.8f)), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        Icon(icon, 22, Mix(C_DIM, C_TEXT, open));
        Tx(label, 18, C_TEXT, 0);
        HSPACE
        Icon(open > 0.5f ? I_CHEV_DN : I_CHEV_R, 14, Mix(C_MUTED, C_TEXT, open));
    }
    return b.clicked;
}

#define CROP_H 146.f
#define DIST_H 142.f

static void CropBody(void) {
    CLAY(CLAY_ID("CropBody"), {.layout = {COLM, .sizing = {GROW, FIX(CROP_H)}, .padding = CLAY_PADDING_ALL(12), .childGap = 10,
                                          .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}, RCARD}) {
        MiniField(0, "T");
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = AL_Y}}) {
            MiniField(1, "L");
            HSPACE
            if (LinkBtn(CLAY_ID("LinkCrop"), gLink[2], 30, 20)) gLink[2] = !gLink[2];
            HSPACE
            MiniField(2, "R");
        }
        MiniField(3, "B");
    }
}

static void DistortBody(void) {
    CLAY(CLAY_ID("DistBody"), {.layout = {COLM, .sizing = {GROW, FIX(DIST_H)}, .padding = CLAY_PADDING_ALL(12), .childGap = 10}, RCARD}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}}}) { MiniField(4, "TL"); HSPACE MiniField(5, "TR"); }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
            if (LinkBtn(CLAY_ID("LinkDist"), gLink[3], 30, 20)) gLink[3] = !gLink[3];
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}}}) { MiniField(6, "BL"); HSPACE MiniField(7, "BR"); }
    }
}

static void NavSection(void) {
    float o0 = EaseIO(AnimLin(CLAY_ID("ExpCrop").id, gOpenTool == 0 ? 1.f : 0.f, 0.2f));
    float o1 = EaseIO(AnimLin(CLAY_ID("ExpDist").id, gOpenTool == 1 ? 1.f : 0.f, 0.2f));
    float h0 = roundf(o0 * (CROP_H + 10.f));
    float h1 = roundf(o1 * (DIST_H + 10.f));
    CLAY(CLAY_ID("NavCol"), {.layout = {COLM, .sizing = {GROW, FIT}}}) {
        CLAY(CLAY_ID("NavRow"), {.layout = {.sizing = {GROW, FIT}, .childGap = 10}}) {
            if (NavButton(0, I_CROP, "Crop Tools", o0)) { gOpenTool = (gOpenTool == 0) ? -1 : 0; gFollow = (gOpenTool == 0); }
            if (NavButton(1, I_DISTORT, "Distort", o1)) { gOpenTool = (gOpenTool == 1) ? -1 : 1; gFollow = (gOpenTool == 1); }
        }
        if (h0 >= 1.f) {
            CLAY(CLAY_ID("CropWrap"), {.layout = {COLM, .sizing = {GROW, FIX(h0)}}, .clip = {.vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                CropBody();
            }
        }
        if (h1 >= 1.f) {
            CLAY(CLAY_ID("DistWrap"), {.layout = {COLM, .sizing = {GROW, FIX(h1)}}, .clip = {.vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                DistortBody();
            }
        }
    }
}

static void TransformView(void) {
    CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 10}}) {
        FieldCard(0, "Position", I_POS, "X", "Y", "0", "px", 0);
        FieldCard(1, "Scale", I_SCALE, "W", "H", "100", "%", 1);
    }
    CLAY(CLAY_ID("RotCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = CLAY_PADDING_ALL(12), .childGap = 6}, RCARD}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 10, .childAlignment = AL_Y}}) {
            IconBtn(CLAY_ID("RotReset"), I_ROT, 34, 24, C_DIM, false, 17);
            Tx("Rotation", 18, C_TEXT, 0);
            HSPACE
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(128), FIX(36)}}}) {
                NumField(10, "0", "\xC2\xB0");
            }
        }
        Clay_ElementId rid = CLAY_ID("RotRuler");
        Bs b = Interact(rid);
        CLAY(rid, {.layout = {.sizing = {GROW, FIX(92)}}, .custom = {.customData = NC(K_ROTRULER, C_CARD, C_CARD, C_CARD, 0, b.hover, 0)}}) {}
    }
    CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .padding = {2, 2, 2, 0}, .childGap = 8}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = AL_Y}}) {
            Tx("Opacity", 18, C_TEXT, 0);
            HSPACE
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(80), FIX(28)}, .padding = {0, 10, 0, 0}, .childGap = 4, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER}},
                          .backgroundColor = RGB(38, 39, 45), .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
                Tx("100", 16, C_TEXT, 0); Tx("%", 13, C_MUTED, 0);
            }
        }
        Clay_ElementId sid = CLAY_ID("OpacitySlider");
        Bs b = Interact(sid);
        CLAY(sid, {.layout = {.sizing = {GROW, FIX(18)}},
                   .custom = {.customData = NC(K_SLIDER, RGB(52, 54, 61), RGB(52, 54, 61), C_CARD, 1.f, b.hover * 1.5f, 1)}}) {}
    }
    VSPACE
    NavSection();
}

static void WheelItem(int i, const char *label) {
    CLAY(CLAY_IDI("WheelCol", i), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
        Clay_ElementId wid = CLAY_IDI("Wheel", i);
        Bs b = Interact(wid);
        CLAY(wid, {.layout = {.sizing = {FIX(100), FIX(100)}}, .custom = {.customData = NC(K_WHEEL, C_CARD, C_CARD, C_CARD, 0, b.hover, 0)}}) {}
        Tx(label, 16, C_TEXT, 1);
        Tx("Neutral", 13, RGB(140, 142, 150), 0);
    }
}

static void AccHeader(int idx, const char *title, int icon, float openAnim) {
    Clay_ElementId id = CLAY_IDI("AccHdr", idx);
    Bs b = Interact(id);
    if (b.clicked) gAcc[idx] = !gAcc[idx];
    CLAY(id, {.layout = {.sizing = {GROW, FIX(52)}, .padding = {16, 14, 0, 0}, .childGap = 12, .childAlignment = AL_Y},
              .backgroundColor = BtnBg(Mix(C_RCARD, RGB(33, 34, 38), openAnim), Mix(RGB(36, 37, 43), RGB(40, 41, 47), openAnim), b), .cornerRadius = CLAY_CORNER_RADIUS(14),
              .border = {.color = Mix(Mix(C_RCARDB, RGB(62, 64, 72), openAnim), RGB(84, 87, 96), fmaxf(b.hover * 0.7f, b.press)), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        Icon(icon, 22, C_DIM);
        Tx(title, 18, C_TEXT, 0);
        HSPACE
        CLAY_AUTO_ID({.layout = {.sizing = {FIX(26), FIX(26)}, .childAlignment = AL_C}, .cornerRadius = CLAY_CORNER_RADIUS(13),
                      .border = {.color = Mix(RGB(74, 77, 85), RGB(130, 134, 144), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
            IconV(I_PLUS, 13, C_DIM, 1.f - openAnim);
        }
    }
}

static const char *kSliderNames[5][4] = {
    {0, 0, 0, 0}, {"Hue", "Saturation", "Luminance", 0}, {"Exposure", "Contrast", "Highlights", "Shadows"},
    {"Temperature", "Tint", "Saturation", 0}, {"Sharpness", "Clarity", "Grain", 0}
};
/* last measured body heights (fallbacks until first measure) */
static float gAccH[5] = {196.f, 156.f, 204.f, 156.f, 156.f};

static void Accordion(int idx, const char *title, int icon) {
    Clay_ElementId hid = CLAY_IDI("AccHdr", idx);
    float o = EaseIO(AnimLin(hid.id, gAcc[idx] ? 1.f : 0.f, 0.2f));
    float mh = Clay_GetElementData(CLAY_IDI("AccBody", idx)).boundingBox.height;
    if (mh > 1.f) gAccH[idx] = mh;
    float wrapH = roundf(o * (gAccH[idx] + 10.f));
    int n = 0;
    if (idx > 0) while (n < 4 && kSliderNames[idx][n]) n++;
    CLAY(CLAY_IDI("AccCol", idx), {.layout = {COLM, .sizing = {GROW, FIT}}}) {
        AccHeader(idx, title, icon, o);
        if (wrapH >= 1.f) {
            CLAY(CLAY_IDI("AccWrap", idx), {.layout = {COLM, .sizing = {GROW, FIX(wrapH)}}, .clip = {.vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                CLAY(CLAY_IDI("AccBody", idx), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = CLAY_PADDING_ALL(12), .childGap = 12}, RCARD}) {
                    if (idx == 0) {
                        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT}}}) {
                            Clay_ElementId rid = CLAY_ID("ResetGrade");
                            Bs b = Interact(rid);
                            CLAY(rid, {.layout = {.sizing = {FIT, FIT}, .childGap = 5, .childAlignment = AL_Y}}) {
                                Icon(I_RESET, 14, Mix(RGB(140, 142, 150), C_TEXT, b.hover));
                                Tx("Reset", 14, Mix(RGB(140, 142, 150), C_TEXT, b.hover), 0);
                            }
                        }
                        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 10}}) {
                            WheelItem(0, "Shadows"); WheelItem(1, "Tint"); WheelItem(2, "Highlights");
                        }
                    } else {
                        for (int s = 0; s < n; s++) {
                            CLAY(CLAY_IDI("SRow", idx * 8 + s), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6}}) {
                                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}}}) { Tx(kSliderNames[idx][s], 14, C_DIM, 0); HSPACE Tx("0", 14, C_MUTED, 0); }
                                Clay_ElementId sid = CLAY_IDI("Slid", idx * 8 + s);
                                Bs b = Interact(sid);
                                CLAY(sid, {.layout = {.sizing = {GROW, FIX(18)}},
                                           .custom = {.customData = NC(K_SLIDER, RGB(52, 54, 61), RGB(60, 62, 70), C_CARD, .5f, b.hover * 1.5f, 1)}}) {}
                            }
                        }
                    }
                }
            }
        }
    }
}

static void ColorsView(void) {
    CLAY(CLAY_ID("AccList"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 10}}) {
        Accordion(0, "Color Grading", I_GRADING);
        Accordion(1, "HSL", I_SLIDERS);
        Accordion(2, "Light", I_SUN);
        Accordion(3, "Color", I_DROP);
        Accordion(4, "Detail", I_MOUNTAIN);
    }
}

/* scrolling for the inspector body (wheel + smooth follow + scrollbar thumb) */
static void UpdateInspScroll(void) {
    Clay_BoundingBox bb = Clay_GetElementData(CLAY_ID("InspBody")).boundingBox;
    Clay_BoundingBox cb = Clay_GetElementData(CLAY_ID("InspContent")).boundingBox;
    gInspViewH = bb.height;
    gTabT = fminf(1.f, gTabT + gDt / 0.24f);
    gTabE = gTabT * gTabT * (3.f - 2.f * gTabT);
    gFadeR = (Rectangle){bb.x, bb.y, bb.width, bb.height};
    gFadeA = (1.f - gTabE) * 255.f;
    float maxS = fmaxf(0.f, cb.height - bb.height);
    float wh = GetMouseWheelMove();
    Vector2 m = GetMousePosition();
    if (wh != 0.f && m.x >= bb.x && m.x <= bb.x + bb.width && m.y >= bb.y && m.y <= bb.y + bb.height) {
        gScrollT -= wh * 60.f;
        gFollow = false;
    }
    if (gFollow) gScrollT = maxS;
    if (gScrollT > maxS) gScrollT = maxS;
    if (gScrollT < 0.f) gScrollT = 0.f;
    gScroll += (gScrollT - gScroll) * (1.f - expf(-20.f * gDt));
    if (fabsf(gScrollT - gScroll) < 0.3f) gScroll = gScrollT;
    if (gScroll > maxS) gScroll = maxS;
    gSB = (maxS > 1.f && bb.width > 0.f);
    if (gSB) {
        float top = bb.y + 10.f, track = bb.height - 20.f;
        float thumb = fmaxf(36.f, track * bb.height / cb.height);
        float frac = gScroll / maxS;
        gSBR = (Rectangle){bb.x + bb.width - 9.f, top + (track - thumb) * frac, 4.f, thumb};
    }
}

static void RightPanel(void) {
    static const char *tabs[4] = {"Transform", "Colors", "Effects", "Text"};
    static const int icons[4] = {I_CROP, I_PALETTE, I_SUN, I_TEXT};
    CLAY(CLAY_ID("Right"), {.layout = {COLM, .sizing = {FIX(374), GROW}, .childGap = 10}}) {
        /* outer panel border + inner segmented container with its own border */
        CLAY(CLAY_ID("InspTabs"), {.layout = {.sizing = {GROW, FIX(56)}, .padding = CLAY_PADDING_ALL(5)}, RPANEL}) {
            CLAY(CLAY_ID("InspSeg"), {.layout = {.sizing = {GROW, GROW}, .padding = CLAY_PADDING_ALL(4), .childGap = 4},
                                      .backgroundColor = RGB(12, 13, 15), .cornerRadius = CLAY_CORNER_RADIUS(12),
                                      .border = {.color = RGB(24, 25, 28), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                for (int i = 0; i < 4; i++)
                    if (TabBtn(CLAY_IDI("InspTab", i), tabs[i], gRightTab == i, 16, 9, true)) {
                        if (gRightTab != i) { gRightTab = i; gScroll = gScrollT = 0.f; gFollow = false; gTabT = 0.f; gTabE = 0.f; }
                    }
            }
        }
        CLAY(CLAY_ID("InspBody"), {.layout = {.sizing = {GROW, GROW}}, RPANEL,
                                   .clip = {.vertical = true, .childOffset = {0, -gScroll + (1.f - gTabE) * 18.f}}}) {
            CLAY(CLAY_ID("InspContent"), {.layout = {COLM, .sizing = {GROW, CLAY_SIZING_FIT(gInspViewH)}, .padding = CLAY_PADDING_ALL(14), .childGap = 10}}) {
                CLAY(CLAY_ID("InspTitle"), {.layout = {.sizing = {GROW, FIT}, .padding = {4, 0, 2, 4}, .childGap = 12, .childAlignment = AL_Y}}) {
                    Icon(icons[gRightTab], 24, C_DIM);
                    Tx(tabs[gRightTab], 24, C_TEXT, 1);
                }
                if (gRightTab == 0) TransformView();
                else if (gRightTab == 1) ColorsView();
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* UI: timeline                                                        */
/* ------------------------------------------------------------------ */
static void Timeline(void) {
    float bodyH = Clay_GetElementData(CLAY_ID("TimeBody")).boundingBox.height;
    float extra = fmaxf(0.f, bodyH - 26.f);

    CLAY(CLAY_ID("Timeline"), {.layout = {COLM, .sizing = {GROW, FIX(284)}}, PANEL}) {
        CLAY(CLAY_ID("TlBar"), {.layout = {.sizing = {GROW, FIX(52)}, .padding = {14, 12, 0, 0}, .childGap = 4, .childAlignment = AL_Y}}) {
            IconBtn(CLAY_ID("ToolPtr"),  I_POINTER,  36, 18, C_TEXT, true, 9);
            IconBtn(CLAY_ID("ToolHand"), I_HAND,     36, 18, C_DIM, false, 9);
            IconBtn(CLAY_ID("ToolCut"),  I_SCISSORS, 36, 18, C_DIM, false, 9);
            IconBtn(CLAY_ID("ToolText"), I_TEXT,     36, 18, C_DIM, false, 9);
            GAPX(8)
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(1), FIX(20)}}, .backgroundColor = RGB(46, 48, 54)}) {}
            GAPX(8)
            IconBtn(CLAY_ID("ToolDel"),  I_TRASH,    36, 18, C_DIM, false, 9);
            HSPACE
            CLAY(CLAY_ID("KfGroup"), {.layout = {.sizing = {FIT, FIX(32)}, .padding = {2, 2, 0, 0}, .childAlignment = AL_Y},
                                      .backgroundColor = RGB(21, 22, 26), .cornerRadius = CLAY_CORNER_RADIUS(10),
                                      .border = {.color = RGB(40, 42, 48), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                IconBtn(CLAY_ID("KfPrev"), I_KF_PREV, 28, 14, RGB(90, 93, 102), false, 8);
                IconBtn(CLAY_ID("KfAdd"),  I_KF_DIAMOND, 28, 14, C_DIM, false, 8);
                IconBtn(CLAY_ID("KfNext"), I_KF_NEXT, 28, 14, RGB(90, 93, 102), false, 8);
            }
            GAPX(14)
            IconBtn(CLAY_ID("ZoomOut"), I_ZOOM_OUT, 28, 16, C_DIM, false, 8);
            Clay_ElementId zid = CLAY_ID("ZoomSlider");
            Bs zb = Interact(zid);
            CLAY(zid, {.layout = {.sizing = {FIX(125), FIX(20)}}, .custom = {.customData = NC(K_SLIDER, RGB(48, 50, 57), RGB(48, 50, 57), C_CARD, .24f, zb.hover * 1.5f, 0)}}) {}
            IconBtn(CLAY_ID("ZoomIn"), I_ZOOM_IN, 28, 16, C_DIM, false, 8);
            GAPX(12)
            Clay_ElementId mid = CLAY_ID("Snap");
            Bs mb = Interact(mid);
            CLAY(mid, {.layout = {.sizing = {FIX(36), FIX(36)}, .childAlignment = AL_C},
                       .backgroundColor = BtnBg(RGB(30, 44, 88), RGB(38, 56, 112), mb), .cornerRadius = CLAY_CORNER_RADIUS(10),
                       .border = {.color = RGB(48, 70, 140), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                Icon(I_MAGNET, 18, RGB(96, 140, 255));
            }
        }
        CLAY(CLAY_ID("TimeBody"), {.layout = {COLM, .sizing = {GROW, GROW}}}) {
            CLAY(CLAY_ID("RulerRow"), {.layout = {.sizing = {GROW, FIX(26)}}}) {
                GAPX(60)
                CLAY(CLAY_ID("Ruler"), {.layout = {.sizing = {GROW, GROW}}, .custom = {.customData = NC(K_RULER, C_CARD, C_CARD, C_CARD, 0, extra, 0)}}) {}
            }
            CLAY(CLAY_ID("TrackV1"), {.layout = {.sizing = {GROW, FIX(47)}, .childAlignment = AL_Y}, .backgroundColor = C_ROW,
                                      .border = {.color = RGB(32, 34, 39), .width = {.top = 1, .bottom = 1}}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {FIX(60), FIT}, .padding = {16, 0, 0, 0}, .childGap = 5, .childAlignment = AL_Y}}) {
                    Icon(I_FILM, 12, C_MUTED); Tx("V1", 11, C_MUTED, 0);
                }
                Clay_ElementId cid = CLAY_ID("Clip");
                Bs cb = Interact(cid);
                CLAY(cid, {.layout = {.sizing = {FIX(193), FIX(42)}, .padding = {10, 6, 5, 0}, .childGap = 5},
                           .custom = {.customData = NC(K_CLIP, RGB(112, 52, 140), RGB(78, 36, 108), C_CARD, 0, cb.hover, 0)}}) {
                    Icon(I_FILM, 11, C_TEXT);
                    Tx("City at dusk.mp4", 11, C_TEXT, 1);
                }
            }
            CLAY(CLAY_ID("TrackA1"), {.layout = {.sizing = {GROW, FIX(47)}, .childAlignment = AL_Y}, .backgroundColor = RGB(21, 22, 26),
                                      .border = {.color = RGB(32, 34, 39), .width = {.bottom = 1}}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {FIX(60), FIT}, .padding = {16, 0, 0, 0}, .childGap = 5, .childAlignment = AL_Y}}) {
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
    gClayAutoId = 0;
    UpdateInspScroll();
    Clay_BeginLayout();
    CLAY(CLAY_ID("Root"), {.layout = {COLM, .sizing = {GROW, GROW}, .padding = {8, 8, 0, 12}, .childGap = 10}, .backgroundColor = C_BG}) {
        TopBar();
        CLAY(CLAY_ID("Main"), {.layout = {.sizing = {GROW, GROW}, .childGap = 10}}) {
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
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1600, 900, "Pluto");
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

        BeginDrawing();
        ClearBackground((Color){14, 15, 17, 255});
        RenderUI(cmds);
        EndDrawing();
        SetMouseCursor(gHand ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
    }
    CloseWindow();
    return 0;
}
