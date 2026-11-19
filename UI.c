/*
 * Pluto - video editor UI (visual only, no backend)
 * Layout: Clay (https://github.com/nicbarker/clay, v0.14 API)
 * Renderer: raylib (custom renderer included below, no external clay renderer needed)
 *
 * Text rendering: every font size is rasterised at its exact pixel size (cached), so glyphs
 * are drawn 1:1 with no texture down-scaling -> crisp text.
 *
 * Font note: put Inter-Regular.ttf and Inter-SemiBold.ttf in ./resources/ for the exact
 * glyph shapes of the reference. Font sizes are normalised, so any font renders at the
 * same size.
 */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define Rectangle    Win32Rectangle
#define CloseWindow  Win32CloseWindow
#define ShowCursor   Win32ShowCursor
#define Arc          Win32Arc
#define DrawText     Win32DrawText
#define DrawTextEx   Win32DrawTextEx
#define DrawIcon     Win32DrawIcon
#define LoadImage    Win32LoadImage
#define PlaySound    Win32PlaySound
#include <windows.h>
#include <windowsx.h>
#undef Rectangle
#undef CloseWindow
#undef ShowCursor
#undef Arc
#undef DrawText
#undef DrawTextEx
#undef DrawIcon
#undef LoadImage
#undef PlaySound
#undef RGB
#endif

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
#include <stdarg.h>
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
#define C_TEXT      RGB(237,237,240)
#define C_DIM       RGB(190,194,202)
#define C_MUTED     RGB(138,142,152)
#define C_ACCENT    RGB(64,120,255)
#define C_ROW       RGB(19,20,24)
/* right-panel palette (sampled from the reference) */
#define C_ICON      RGB(140,142,150)   /* right-panel icons + secondary labels */
#define C_PGRAD     RGB(27,28,31)      /* sentinel: drawn as a vertical gradient 28,29,32 -> 21,22,24 */
#define C_RCARD     RGB(24,25,28)
#define C_RCARDB    RGB(38,39,41)
#define C_RFIELD    RGB(33,34,38)
#define C_VALBOX    RGB(33,34,38)
#define RPANEL_R(r) .backgroundColor = C_PGRAD, .cornerRadius = CLAY_CORNER_RADIUS(r), \
                    .border = {.color = RGB(40,41,43), .width = CLAY_BORDER_OUTSIDE(1)}
#define RPANEL      RPANEL_R(22)
#define RCARD       .backgroundColor = C_RCARD, .cornerRadius = CLAY_CORNER_RADIUS(18), \
                    .border = {.color = C_RCARDB, .width = CLAY_BORDER_OUTSIDE(1)}

/* ------------------------------------------------------------------ */
/* small layout shorthands                                             */
/* ------------------------------------------------------------------ */
#define GROW        CLAY_SIZING_GROW(0)
#define FIT         CLAY_SIZING_FIT(0)
#define FIX(n)      CLAY_SIZING_FIXED(((n) >= 8 ? (n) + 2.25f : (n)))
#define COLM        .layoutDirection = CLAY_TOP_TO_BOTTOM
#define AL_C        {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
#define AL_Y        {.y = CLAY_ALIGN_Y_CENTER}
#define HSPACE      CLAY_AUTO_ID({.layout = {.sizing = {.width = GROW}}}) {}
#define VSPACE      CLAY_AUTO_ID({.layout = {.sizing = {.height = GROW}}}) {}
#define GAPX(n)     CLAY_AUTO_ID({.layout = {.sizing = {.width = FIX(n), .height = FIX(1)}}}) {}
#define GAPY(n)     CLAY_AUTO_ID({.layout = {.sizing = {.width = FIX(1), .height = FIX(n)}}}) {}
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
    I_KF_DIAMOND, I_ZOOM_OUT, I_ZOOM_IN, I_MAGNET, I_FILM, I_BOWTIE, I_SPARKLE,
    I_MINIMIZE, I_MAXIMIZE, I_CLOSE,
    K_THUMB = 100, K_KNOB, K_WHEEL, K_SWITCH, K_SLIDER, K_ROTRULER, K_RULER, K_PREVIEW,
    K_CLIP, K_SCROLLBAR, K_PILL, K_FILTER, K_CHIP, K_GLOW
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
static float gFK[2] = {1.f, 1.f};   /* per-font size multiplier (normalises em size across fonts) */
static float gDt;
static bool  gHand, gCross;
static int   gLeftTab = 0, gRightTab = 0;
static int   gWindowAction; /* 0 none, 1 minimize, 2 maximize/restore, 3 close */
static bool  gWantQuit;
#ifdef _WIN32
static WNDPROC gPreviousWndProc;
#endif

/* right panel state */
static int   gOpenAcc = 0;                            /* single-open accordion, -1 none */
static bool  gLink[4] = {false, true, false, false};  /* position, scale, crop, distort */
static int   gOpenTool = -1;                          /* -1 none, 0 crop tools, 1 distort */
static float gScroll, gScrollT;                       /* inspector scroll (pixels down)   */
static bool  gFollow;                                 /* keep pinned to bottom while expanding */
static float gInspViewH;
static float gTabT = 1.f;                             /* tab-switch transition 0..1 */
static float gTabE = 1.f;                             /* eased */
static Rectangle gFadeR; static float gFadeA;         /* fade overlay while switching */
static bool  gSB; static Rectangle gSBR;              /* inspector scrollbar thumb */

/* left panel state */
static float gAudS, gAudT;                            /* audio panel scroll */
static bool  gSwOn[2];                                /* fade in / fade out */

/* values */
static float gNum[4] = {0, 0, 100, 100};              /* X, Y, W, H */
static float gRot, gRotT; static bool gRotDrag;       /* degrees -180..180 */
#define NSL 48
static float gSl[NSL], gSlT[NSL]; static bool gSlDrag[NSL]; /* slider 0..1 (0 = opacity, 40/41 = fades) */
static Vector2 gWh[3], gWhT[3]; static bool gWhDrag[3];     /* wheel puck, unit disk */
static int   gHsl = 0;                                /* selected HSL chip */
static bool  gTransOpen = true;                       /* effects: transitions card */
static int   gFilter = 0;                             /* effects: selected preset */
static uint32_t gDragId;                              /* active drag target (slider/ruler) */
static int   gDragWheel = -1;

/* rotation ruler geometry (inside the 322px wide ruler box) */
#define RULER_L 22.f
#define RULER_R 22.5f

/* playhead is drawn after everything else so rows never cover it */
static bool gPH; static Vector2 gPHPos; static float gPHLen;

/* formatted text lives in a ring so Clay can read it until render */
static char gFb[160][40]; static int gFbN;
static const char *Fmt(const char *f, ...) {
    char *b = gFb[gFbN++ % 160];
    va_list ap; va_start(ap, f); vsnprintf(b, 40, f, ap); va_end(ap);
    return b;
}

/* ------------------------------------------------------------------ */
/* crisp font cache: one atlas per (font, exact pixel size)            */
/* ------------------------------------------------------------------ */
static char gFontPath[2][260];
static bool gFontOK[2];
static int  gFcps[100], gFcpN;
typedef struct { int slot, px; Font f; } FontEnt;
static FontEnt gFC[96];
static int gFCN;

static int PxOf(int slot, float size) {
    int p = (int)roundf(size * gFK[slot]);
    return p < 6 ? 6 : p > 96 ? 96 : p;
}
static Font FontPx(int slot, int px) {
    if (!gFontOK[slot]) return GetFontDefault();
    for (int i = 0; i < gFCN; i++)
        if (gFC[i].slot == slot && gFC[i].px == px) return gFC[i].f;
    if (gFCN >= 96) return GetFontDefault();
    Font f = LoadFontEx(gFontPath[slot], px, gFcps, gFcpN);
    if (f.texture.id == 0) { gFontOK[slot] = false; return GetFontDefault(); }
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    gFC[gFCN].slot = slot; gFC[gFCN].px = px; gFC[gFCN].f = f;
    return gFC[gFCN++].f;
}

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

static float *AnimPtr(uint32_t key, float init) {
    for (int i = 0; i < 2048; i++)
        if (gAnim[i].used && gAnim[i].id == key) return &gAnim[i].v;
    for (int i = 0; i < 2048; i++)
        if (!gAnim[i].used) { gAnim[i].used = true; gAnim[i].id = key; gAnim[i].v = init; return &gAnim[i].v; }
    static float dummy; dummy = init; return &dummy;
}

/* damped spring: k = stiffness, d = damping (lower d = bouncier) */
static float Spring(uint32_t key, float target, float k, float d) {
    float *p = AnimPtr(key ^ 0x7F4A7C15u, target);
    float *v = AnimPtr(key ^ 0x2545F491u, 0.f);
    int n = (int)ceilf(gDt / 0.004f); if (n < 1) n = 1;
    float h = gDt / n;
    for (int i = 0; i < n; i++) { *v += (k * (target - *p) - d * *v) * h; *p += *v * h; }
    return *p;
}

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
    CLAY_TEXT(S(s), CLAY_TEXT_CONFIG({.fontId = font, .fontSize = size + 2.25f, .textColor = c, .wrapMode = CLAY_TEXT_WRAP_NONE}));
}
static void TxWrap(const char *s, float size, Clay_Color c, int font) {
    CLAY_TEXT(S(s), CLAY_TEXT_CONFIG({.fontId = font, .fontSize = size + 2.25f, .textColor = c, .wrapMode = CLAY_TEXT_WRAP_WORDS}));
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

static void WindowButton(const char *key, int icon) {
    Clay_ElementId id = CLAY_SIDI(S(key), 0);
    Bs b = Interact(id);
    Clay_Color bg = icon == I_CLOSE
        ? Mix(RGBA(0, 0, 0, 0), RGB(190, 48, 56), b.hover)
        : RGBA(255, 255, 255, 18.f * b.hover);
    CLAY(id, {.layout = {.sizing = {FIX(36), FIX(32)}, .childAlignment = AL_C},
              .backgroundColor = bg, .cornerRadius = CLAY_CORNER_RADIUS(5)}) {
        Icon(icon, 13, C_TEXT);
    }
    if (b.clicked) {
        gWindowAction = icon == I_MINIMIZE ? 1 : icon == I_MAXIMIZE ? 2 : 3;
    }
}

static bool TabBtn(Clay_ElementId id, const char *label, bool sel, float size, float rad, bool pill) {
    Bs b = Interact(id);
    float s = Anim(id.id + 7, sel ? 1.f : 0.f, 45.f);
    Clay_Color bg;
    uint16_t bw = 0;
    Clay_Color bc = RGBA(0, 0, 0, 0);
    if (pill) {
        /* the selected background is the sliding pill drawn by the segment itself;
           no press flash, no hover/press tint on the right-panel tabs */
        bg = RGBA(255, 255, 255, 0);
    } else {
        float a = fmaxf(s * 255.f, b.hover * (1.f - s) * 60.f);
        Clay_Color fill = RGB(44, 46, 52);
        bg = Mix(RGBA(fill.r, fill.g, fill.b, a), RGBA(90, 95, 106, 255), b.press * 0.55f);
        bw = 1;
        bc = RGBA(66, 68, 76, 255.f * s);
    }
    Clay_Color off = pill ? C_ICON : C_MUTED;
    CLAY(id, {.layout = {.sizing = {GROW, GROW}, .childAlignment = AL_C},
              .backgroundColor = bg, .cornerRadius = CLAY_CORNER_RADIUS(rad),
              .border = {.color = bc, .width = {.left = bw, .right = bw, .top = bw, .bottom = bw}}}) {
        Tx(label, size, Mix(off, C_TEXT, fmaxf(s, b.hover * 0.6f)), 0);
    }
    /* select on press, not release, so it feels instant */
    return Clay_PointerOver(id) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* link / chain toggle: bright chain when off, blue chain + soft blue glow when linked */
static bool LinkBtn(Clay_ElementId id, bool linked, float box, float isz) {
    Bs b = Interact(id);
    float la = Anim(id.id + 11u, linked ? 1.f : 0.f, 16.f);
    Clay_Color off = Mix(RGB(226, 227, 231), RGB(255, 255, 255), b.hover);
    Clay_Color ic = Dark(Mix(off, RGB(59, 130, 246), la), b.press * 0.2f);
    CLAY(id, {.layout = {.sizing = {FIX(box), FIX(box)}, .childAlignment = AL_C},
              .custom = {.customData = NC(K_GLOW, RGB(59, 130, 246), RGB(59, 130, 246), RGB(59, 130, 246), la,
                                          14.f * b.hover + 56.f * b.press, 0)}}) {
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
static void CubicStroke(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float thick, Color col) {
    Vector2 prev = p0;
    for (int i = 1; i <= 10; i++) {
        float t = i / 10.f, u = 1.f - t;
        Vector2 p = {
            u*u*u*p0.x + 3.f*u*u*t*p1.x + 3.f*u*t*t*p2.x + t*t*t*p3.x,
            u*u*u*p0.y + 3.f*u*u*t*p1.y + 3.f*u*t*t*p2.y + t*t*t*p3.y
        };
        DrawLineEx(prev, p, thick, col);
        prev = p;
    }
    DrawCircleV(p0, thick * .5f, col);
    DrawCircleV(p3, thick * .5f, col);
}
static void TxtAt(int font, const char *s, float x, float y, float size, Color c) {
    int px = PxOf(font, (float)(int)(size + 2.25f));
    DrawTextEx(FontPx(font, px), s, (Vector2){roundf(x), roundf(y)}, (float)px, 0, c);
}
static float TxtW(int font, const char *s, float size) {
    int px = PxOf(font, (float)(int)(size + 2.25f));
    return MeasureTextEx(FontPx(font, px), s, (float)px, 0).x;
}

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
    case I_SCALE:   LN(.12, .88, .88, .12); LN(.55, .12, .88, .12); LN(.88, .12, .88, .45); LN(.12, .55, .12, .88); LN(.12, .88, .45, .88); break;
    case I_RESET:
    case I_ROT:     Arc(P(.5, .5), .34f * w, t, 210, 500, c); LN(.2, .33, .2, .1); LN(.2, .33, .42, .33); break;
    case I_LINK: {
                    /* Two open, interlocking chain links, drawn with a clean rounded stroke. */
                    CubicStroke(P(.43, .47), P(.50, .37), P(.58, .27), P(.65, .22), t, c);
                    CubicStroke(P(.65, .22), P(.78, .12), P(.90, .23), P(.85, .39), t, c);
                    CubicStroke(P(.85, .39), P(.81, .48), P(.72, .56), P(.65, .63), t, c);
                    CubicStroke(P(.35, .37), P(.27, .45), P(.19, .53), P(.15, .62), t, c);
                    CubicStroke(P(.15, .62), P(.10, .79), P(.24, .90), P(.39, .80), t, c);
                    CubicStroke(P(.39, .80), P(.47, .73), P(.54, .64), P(.57, .56), t, c);
                } break;
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
    case I_BOWTIE:  LN(.1, .22, .1, .78); LN(.1, .22, .5, .5); LN(.1, .78, .5, .5);
                    LN(.9, .22, .9, .78); LN(.9, .22, .5, .5); LN(.9, .78, .5, .5); break;
    case I_SPARKLE: LN(.5, .08, .58, .42); LN(.58, .42, .92, .5); LN(.92, .5, .58, .58); LN(.58, .58, .5, .92);
                    LN(.5, .92, .42, .58); LN(.42, .58, .08, .5); LN(.08, .5, .42, .42); LN(.42, .42, .5, .08); break;
    case I_MINIMIZE: LN(.18, .5, .82, .5); break;
    case I_MAXIMIZE: StrokeRR((Rectangle){x + .2f * w, y + .2f * h, .6f * w, .6f * h}, 1.5f, t, c); break;
    case I_CLOSE: LN(.22, .22, .78, .78); LN(.78, .22, .22, .78); break;
    default: break;
    }
#undef P
#undef LN
}

/* ------------------------------------------------------------------ */
/* wheel helpers                                                       */
/* ------------------------------------------------------------------ */
static void WheelInfo(Vector2 p, float *hue, float *len) {
    *len = sqrtf(p.x * p.x + p.y * p.y);
    *hue = fmodf(atan2f(p.y, p.x) * RAD2DEG + 90.f + 360.f, 360.f);
}

/* media table (needed by the thumbnail renderer) */
typedef struct { const char *name, *dur; Clay_Color sky1, sky2, mtn; int style; const char *label; } MediaItem;
static const MediaItem kMedia[6] = {
    {"Coastal morning.mp4",  "0:12", {86, 172, 200, 255}, {150, 214, 224, 255}, {70, 82, 104, 255}, 0, "COASTAL MORNING"},
    {"City at dusk.mp4",     "0:09", {96, 48, 130, 255},  {186, 98, 160, 255},  {46, 30, 76, 255},  0, "CITY AT DUSK"},
    {"Forest trail.mp4",     "0:15", {48, 128, 108, 255}, {118, 188, 150, 255}, {26, 58, 62, 255},  0, "FOREST TRAIL"},
    {"Desert still.jpg",     "0:06", {126, 138, 154, 255}, {190, 196, 206, 255}, {76, 82, 100, 255}, 0, "DESERT STILL"},
    {"Night harbor.jpg",     "0:06", {38, 38, 88, 255},   {62, 52, 112, 255},   {28, 28, 54, 255},  0, "NIGHT HARBOR"},
    {"Ambient soundtrack.wav", "0:24", {150, 40, 150, 255}, {255, 134, 92, 255}, {0, 0, 0, 255},     1, NULL},
};

/* ------------------------------------------------------------------ */
/* custom elements                                                     */
/* ------------------------------------------------------------------ */
static void DrawCustom(Cust *cu, Rectangle r) {
    if (cu->kind < K_THUMB) { DrawIcon(cu->kind, r, RC(cu->a), cu->v); return; }
    switch (cu->kind) {
    case K_THUMB: {
        int style = cu->i & 15, idx = cu->i >> 4;
        GradRR(r, 10, RC(cu->a), RC(cu->b));
        if (style == 0) {
            DrawCircleV((Vector2){r.x + .72f * r.width, r.y + .30f * r.height}, r.height * .17f, (Color){250, 228, 150, 255});
            Color m = RC(cu->c);
            Tri((Vector2){r.x + .06f * r.width, r.y + r.height}, (Vector2){r.x + .30f * r.width, r.y + .40f * r.height}, (Vector2){r.x + .62f * r.width, r.y + r.height}, m);
            Tri((Vector2){r.x + .36f * r.width, r.y + r.height}, (Vector2){r.x + .64f * r.width, r.y + .48f * r.height}, (Vector2){r.x + .94f * r.width, r.y + r.height}, LerpC(m, WHITE, .10f));
            const char *lab = kMedia[idx].label;
            if (lab) TxtAt(1, lab, r.x + 7, r.y + r.height - 33, 5, (Color){240, 240, 244, 235});
        }
        FillRR(r, 10, (Color){255, 255, 255, U8(20 * cu->f)});
        StrokeRR(r, 10, 1, (Color){255, 255, 255, U8(20 + 60 * cu->f)});
        if (cu->f > .03f) {
            Rectangle pb = {r.x + r.width - 30, r.y + 6, 24, 24};
            FillRR(pb, 7, (Color){246, 246, 249, U8(255 * fminf(1.f, cu->f * 1.4f))});
            Color d = (Color){30, 31, 36, U8(255 * fminf(1.f, cu->f * 1.4f))};
            DrawLineEx((Vector2){pb.x + 7, pb.y + 12}, (Vector2){pb.x + 17, pb.y + 12}, 2.f, d);
            DrawLineEx((Vector2){pb.x + 12, pb.y + 7}, (Vector2){pb.x + 12, pb.y + 17}, 2.f, d);
        }
    } break;

    case K_KNOB: {
        Vector2 c = {r.x + r.width / 2, r.y + r.height / 2};
        float rad = fminf(r.width, r.height) / 2 - 1;
        DrawCircleV(c, rad, LerpC((Color){46, 48, 55, 255}, (Color){90, 94, 104, 255}, cu->f));
        RadGrad(c, rad - 1.5f, (Color){40, 41, 48, 255}, (Color){20, 21, 25, 255});
        float ang = (cu->v - .5f) * 4.7f;
        Vector2 d = {sinf(ang), -cosf(ang)};
        DrawLineEx((Vector2){c.x + d.x * rad * .74f, c.y + d.y * rad * .74f}, (Vector2){c.x + d.x * rad * .88f, c.y + d.y * rad * .88f}, 3.f, (Color){228, 229, 233, 255});
    } break;

    case K_WHEEL: {
        /* b.r / b.g = puck position (unit disk), c = puck colour, v = coloured? */
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
        Vector2 p = {c.x + cu->b.r * rad, c.y + cu->b.g * rad};
        float dot = 8.5f + 1.5f * cu->f;
        DrawCircleV(p, dot + 3.f, (Color){10, 10, 14, 120});
        if (cu->v > .5f) {
            DrawCircleV(p, dot + 1.8f, (Color){244, 245, 248, 255});
            DrawCircleV(p, dot - .5f, RC(cu->c));
        } else {
            DrawCircleV(p, dot + 1.f, (Color){226, 227, 231, 255});
        }
    } break;

    case K_SWITCH: {
        float rad = r.height / 2;
        FillRR(r, rad, RC(Mix(RGB(44, 45, 51), C_ACCENT, cu->v)));
        StrokeRR(r, rad, 1, (Color){56, 58, 65, 255});
        float kx = r.x + rad + (r.width - 2 * rad) * cu->v;
        DrawCircleV((Vector2){kx, r.y + rad}, rad - 3.f + cu->f, (Color){226, 227, 231, 255});
    } break;

    case K_SLIDER: {
        /* i bit0 = big, bit1 = gradient (a -> b), bit2 = rainbow */
        bool big = cu->i & 1, grad = cu->i & 2, rainbow = cu->i & 4;
        float th = big ? 5.f : 4.f, kr = big ? 7.5f : 7.f;
        float cy = r.y + r.height / 2;
        Rectangle tr = {r.x, cy - th / 2, r.width, th};
        if (rainbow) {
            Color cs[7] = {{240,60,60,255},{240,220,60,255},{60,220,90,255},{60,220,230,255},{70,100,250,255},{230,70,230,255},{240,60,60,255}};
            float sw = tr.width / 6.f;
            for (int i = 0; i < 6; i++)
                DrawRectangleGradientH((int)(tr.x + sw * i), (int)tr.y, (int)ceilf(sw), (int)th, cs[i], cs[i + 1]);
        } else if (grad) {
            DrawRectangleGradientH((int)tr.x, (int)tr.y, (int)tr.width, (int)th, RC(cu->a), RC(cu->b));
        } else {
            FillRR(tr, th / 2, RC(cu->a));
            FillRR((Rectangle){r.x, cy - th / 2, kr + (r.width - 2 * kr) * cu->v, th}, th / 2, RC(cu->b));
        }
        Vector2 kc = {r.x + kr + (r.width - 2 * kr) * cu->v, cy};
        DrawCircleV((Vector2){kc.x, kc.y + 1.f}, kr + cu->f + 1.f, (Color){0, 0, 0, 70});
        DrawCircleV(kc, kr + cu->f, (Color){244, 244, 246, 255});
    } break;

    case K_ROTRULER: {
        /* v = normalised marker position 0..1; box is 322 x 49, labels + pointer sit underneath */
        Rectangle box = {r.x, r.y, r.width, 49};
        GradRR(box, 14, (Color){11, 12, 14, 255}, (Color){15, 16, 18, 255});
        float x0 = box.x + RULER_L, x1 = box.x + box.width - RULER_R, cy = box.y + 24.5f;
        for (int i = 0; i <= 36; i++) {
            float px = x0 + (x1 - x0) * i / 36.f;
            if (i % 9 == 0)       DrawLineEx((Vector2){px, cy - 13.5f}, (Vector2){px, cy + 13.5f}, 1.7f, (Color){237, 237, 240, 255});
            else if (i % 3 == 0)  DrawLineEx((Vector2){px, cy - 7.5f},  (Vector2){px, cy + 7.5f},  1.7f, (Color){122, 123, 128, 255});
            else                  DrawLineEx((Vector2){px, cy - 5.f},   (Vector2){px, cy + 5.f},   1.5f, (Color){99, 100, 104, 255});
        }
        const char *lab[5] = {"-180\xC2\xB0", "-90\xC2\xB0", "0\xC2\xB0", "90\xC2\xB0", "180\xC2\xB0"};
        for (int i = 0; i < 5; i++) {
            float px = x0 + (x1 - x0) * (i * 9) / 36.f;
            TxtAt(0, lab[i], px - TxtW(0, lab[i], 17) / 2, box.y + 57, 17, RC(C_ICON));
        }
        float mx = x0 + (x1 - x0) * cu->v, my = cy + .5f;
        /* soft red glow: stacked translucent capsules */
        for (int k = 7; k >= 1; k--)
            FillRR((Rectangle){mx - 2 - k, my - 19 - k, 4 + 2 * k, 38 + 2 * k}, 2 + k, (Color){255, 59, 59, 7});
        FillRR((Rectangle){mx - 2, my - 19, 4, 38}, 2, (Color){255, 59, 59, 255});
        Tri((Vector2){mx, r.y + 79}, (Vector2){mx - 7, r.y + 89}, (Vector2){mx + 7, r.y + 89}, (Color){244, 244, 246, 255});
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

    case K_SCROLLBAR: {
        /* v = scroll position 0..1, f = visible fraction (>=1 means nothing to scroll) */
        if (cu->f >= .999f) break;
        float th = fmaxf(30.f, r.height * cu->f);
        float ty = r.y + (r.height - th) * cu->v;
        FillRR((Rectangle){r.x, ty, r.width, th}, r.width / 2, (Color){84, 87, 96, 255});
    } break;

    case K_PILL: {
        /* segmented control: dark track + sliding selection pill; v = fractional tab index.
           (Clay does not emit the background rectangle of custom elements, so the track is drawn here.) */
        FillRR(r, 14, (Color){12, 13, 15, 255});
        float tw = (r.width - 8.f - 12.f) / 4.f;
        Rectangle p = {r.x + 4.f + cu->v * (tw + 4.f), r.y + 4.f, tw, r.height - 8.f};
        FillRR(p, 11, (Color){42, 43, 49, 255});
        StrokeRR(p, 11, 1, (Color){55, 56, 62, 255});
    } break;

    case K_FILTER: {
        /* a,b gradient, c sun colour, f hover, i selected */
        GradRR(r, 10, RC(cu->a), RC(cu->b));
        DrawCircleV((Vector2){r.x + r.width * .72f, r.y + r.height * .26f}, r.width * .12f, RC(cu->c));
        FillRR(r, 10, (Color){255, 255, 255, U8(22 * cu->f)});
        if (cu->i) {
            StrokeRR((Rectangle){r.x - 1, r.y - 1, r.width + 2, r.height + 2}, 11, 2, (Color){240, 241, 245, 255});
        }
    } break;

    case K_CHIP: {
        Vector2 c = {r.x + r.width / 2, r.y + r.height / 2};
        float rad = r.width / 2;
        if (cu->i) {
            for (int i = 0; i < 36; i++)
                DrawCircleSector(c, rad, i * 10.f, i * 10.f + 11.f, 3, ColorFromHSV(fmodf(i * 10.f + 90.f, 360.f), .72f, .92f));
            DrawCircleV(c, rad * .42f, (Color){240, 240, 244, 30});
        } else {
            DrawCircleV(c, rad, RC(cu->a));
        }
    } break;

    case K_GLOW: {
        /* link toggle backdrop. v = linked amount 0..1 (soft blue glow), f = white hover/press alpha */
        Vector2 c = {r.x + r.width / 2, r.y + r.height / 2};
        if (cu->f > .5f) DrawCircleV(c, r.width / 2, (Color){255, 255, 255, U8(cu->f * (1.f - cu->v))});
        if (cu->v > .01f) {
            Color g = RC(cu->a);
            float a = 36.f * cu->v;
            DrawCircleV(c, 14.f, (Color){g.r, g.g, g.b, U8(a)});
            for (int i = 0; i < 10; i++)
                DrawRing(c, 14.f + i, 15.f + i, 0, 360, 48, (Color){g.r, g.g, g.b, U8(a * (1.f - (i + .5f) / 10.f))});
        }
    } break;
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
                GradRR(r, d->cornerRadius.topLeft, (Color){28, 29, 32, 255}, (Color){21, 22, 24, 255});
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
            int px = PxOf(d->fontId, (float)d->fontSize);
            DrawTextEx(FontPx(d->fontId, px), buf, (Vector2){roundf(r.x), roundf(r.y)}, (float)px, d->letterSpacing, RC(d->textColor));
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
        GradRR(gFadeR, 22, (Color){28, 29, 32, U8(gFadeA)}, (Color){21, 22, 24, U8(gFadeA)});
    if (gSB) FillRR(gSBR, 2, (Color){92, 95, 104, 255});
}

static Clay_Dimensions MeasureTxt(Clay_StringSlice t, Clay_TextElementConfig *cfg, void *u) {
    (void)u;
    char buf[512]; int n = t.length < 511 ? t.length : 511;
    memcpy(buf, t.chars, n); buf[n] = 0;
    int px = PxOf(cfg->fontId, (float)cfg->fontSize);
    Vector2 v = MeasureTextEx(FontPx(cfg->fontId, px), buf, (float)px, cfg->letterSpacing);
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
        HSPACE
        WindowButton("WindowMinimize", I_MINIMIZE);
        WindowButton("WindowMaximize", I_MAXIMIZE);
        WindowButton("WindowClose", I_CLOSE);
    }
}

/* ------------------------------------------------------------------ */
/* UI: library (left)                                                  */
/* ------------------------------------------------------------------ */
static void ValBox(const char *s, const char *unit, float w, float h, float fs, float ufs, float pad);
static void SliderLogic(Clay_ElementId id, int gi, float kr);

static void MediaView(void) {
    CLAY(CLAY_ID("MediaCol"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 9}}) {
        Clay_ElementId sid = CLAY_ID("Search");
        Bs sb = Interact(sid);
        CLAY(sid, {.layout = {.sizing = {GROW, FIX(34)}, .padding = {13, 12, 0, 0}, .childGap = 9, .childAlignment = AL_Y},
                   .backgroundColor = Mix(RGB(30, 31, 36), RGB(36, 37, 43), sb.hover), .cornerRadius = CLAY_CORNER_RADIUS(11)}) {
            Icon(I_SEARCH, 15, C_MUTED);
            Tx("Search media", 12, C_MUTED, 0);
        }
        for (int row = 0; row < 3; row++) {
            CLAY(CLAY_IDI("MRow", row), {.layout = {.sizing = {GROW, FIT}, .childGap = 9}}) {
                for (int col = 0; col < 2; col++) {
                    int i = row * 2 + col;
                    const MediaItem *m = &kMedia[i];
                    CLAY(CLAY_IDI("Media", i), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6}}) {
                        Clay_ElementId tid = CLAY_IDI("Thumb", i);
                        Bs b = Interact(tid);
                        CLAY(tid, {.layout = {.sizing = {GROW, FIX(77)}, .padding = {6, 6, 6, 6}, .childAlignment = {.y = CLAY_ALIGN_Y_BOTTOM}},
                                   .custom = {.customData = NC(K_THUMB, m->sky1, m->sky2, m->mtn, 0, b.hover, m->style | (i << 4))}}) {
                            CLAY_AUTO_ID({.layout = {.sizing = {FIT, FIT}, .padding = {5, 5, 1, 2}},
                                          .backgroundColor = RGBA(0, 0, 0, 150), .cornerRadius = CLAY_CORNER_RADIUS(5)}) {
                                Tx(m->dur, 9, C_TEXT, 1);
                            }
                        }
                        Tx(m->name, 9, C_MUTED, 0);
                    }
                }
            }
        }
    }
}

static void KnobGroup(int idx, const char *label, const char *val) {
    CLAY(CLAY_IDI("KG", idx), {.layout = {COLM, .sizing = {FIT, FIT}, .childGap = 5, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
        Clay_ElementId kid = CLAY_IDI("Knob", idx);
        Bs b = Interact(kid);
        CLAY(kid, {.layout = {.sizing = {FIX(69), FIX(69)}}, .custom = {.customData = NC(K_KNOB, C_CARD, C_CARD, C_CARD, .5f, b.hover, 0)}}) {}
        Tx(label, 9, C_MUTED, 0);
        CLAY_AUTO_ID({.layout = {.sizing = {FIX(62), FIX(23)}, .childAlignment = AL_C},
                      .backgroundColor = RGB(28, 29, 34), .cornerRadius = CLAY_CORNER_RADIUS(7),
                      .border = {.color = RGB(40, 42, 48), .width = CLAY_BORDER_OUTSIDE(1)}}) {
            Tx(val, 10, C_TEXT, 0);
        }
    }
}

static void SwitchItem(int idx, const char *label) {
    Clay_ElementId id = CLAY_IDI("Switch", idx);
    Bs b = Interact(id);
    if (b.clicked) gSwOn[idx] = !gSwOn[idx];
    float on = Anim(id.id + 5u, gSwOn[idx] ? 1.f : 0.f, 22.f);
    CLAY(id, {.layout = {.sizing = {FIT, FIT}, .childGap = 8, .childAlignment = AL_Y}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {FIX(38), FIX(22)}},
                      .custom = {.customData = NC(K_SWITCH, C_CARD, C_CARD, C_CARD, on, b.hover * 1.f - b.press * 1.5f, 0)}}) {}
        Tx(label, 10, C_MUTED, 0);
    }
}

static void DurRow(int gi, const char *label) {
    CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}, .padding = {3, 0, 0, 0}, .childGap = 4}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = AL_Y}}) {
            Tx(label, 10, C_DIM, 0);
            HSPACE
            ValBox(Fmt("%d", 1 + (int)roundf(gSl[gi] * 9.f)), "s", 54, 26, 10, 8, 10);
        }
        Clay_ElementId sid = CLAY_IDI("Dur", gi);
        Bs bs = Interact(sid);
        SliderLogic(sid, gi, 7.f);
        CLAY(sid, {.layout = {.sizing = {GROW, FIX(18)}},
                   .custom = {.customData = NC(K_SLIDER, RGB(40, 42, 48), RGB(40, 42, 48), C_CARD, gSl[gi],
                                               (bs.hover + (gSlDrag[gi] ? 1.f : 0.f)) * 1.5f, 0)}}) {}
    }
}

static void AudioView(void) {
    /* scrolling (uses last frame's measured sizes) */
    Clay_BoundingBox vb = Clay_GetElementData(CLAY_ID("AudioScroll")).boundingBox;
    Clay_BoundingBox cb = Clay_GetElementData(CLAY_ID("AudioContent")).boundingBox;
    float maxS = fmaxf(0.f, cb.height - vb.height);
    float wh = GetMouseWheelMove();
    Vector2 mp = GetMousePosition();
    if (wh != 0.f && vb.width > 0.f && mp.x >= vb.x && mp.x <= vb.x + vb.width + 12 && mp.y >= vb.y && mp.y <= vb.y + vb.height)
        gAudT -= wh * 50.f;
    if (gAudT > maxS) gAudT = maxS;
    if (gAudT < 0.f) gAudT = 0.f;
    gAudS += (gAudT - gAudS) * (1.f - expf(-20.f * gDt));
    if (fabsf(gAudT - gAudS) < 0.3f) gAudS = gAudT;
    if (gAudS > maxS) gAudS = maxS;
    float sv = maxS > 1.f ? gAudS / maxS : 0.f;
    float sf = maxS > 1.f && cb.height > 1.f ? vb.height / cb.height : 1.f;

    CLAY(CLAY_ID("AudioHead"), {.layout = {.sizing = {GROW, FIT}, .padding = {4, 0, 6, 0}, .childGap = 10, .childAlignment = AL_Y}}) {
        Icon(I_NOTE, 20, C_DIM);
        Tx("Audio", 16, C_TEXT, 1);
    }
    CLAY(CLAY_ID("AudioBody"), {.layout = {.sizing = {GROW, GROW}, .childGap = 6}}) {
        CLAY(CLAY_ID("AudioScroll"), {.layout = {.sizing = {GROW, GROW}},
                                      .clip = {.vertical = true, .childOffset = {0, -gAudS}}}) {
            CLAY(CLAY_ID("AudioContent"), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 10, .padding = {0, 0, 0, 12}}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) { KnobGroup(0, "Volume", "100 %"); }
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .padding = {22, 22, 0, 0}}}) { KnobGroup(1, "Pan", "0"); HSPACE KnobGroup(2, "Bass", "0 dB"); }
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) { KnobGroup(3, "Treble", "0 dB"); }
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .padding = {0, 0, 14, 4}, .childGap = 40, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
                    SwitchItem(0, "Fade In"); SwitchItem(1, "Fade Out");
                }
                DurRow(40, "Fade In Duration");
                DurRow(41, "Fade Out Duration");
            }
        }
        CLAY(CLAY_ID("AudioBar"), {.layout = {.sizing = {CLAY_SIZING_FIXED(3), GROW}},
                                   .custom = {.customData = NC(K_SCROLLBAR, C_CARD, C_CARD, C_CARD, sv, sf, 0)}}) {}
    }
}

static void LeftPanel(void) {
    CLAY(CLAY_ID("Library"), {.layout = {COLM, .sizing = {FIX(286), GROW}, .padding = {13, 13, 12, 12}, .childGap = 12}, PANEL}) {
        CLAY(CLAY_ID("LibHead"), {.layout = {.sizing = {GROW, FIT}, .padding = {4, 0, 0, 0}, .childGap = 10, .childAlignment = AL_Y}}) {
            Icon(I_LIBRARY, 20, C_DIM);
            Tx("Library", 16, C_TEXT, 1);
            HSPACE
            Clay_ElementId imp = CLAY_ID("Import");
            Bs b = Interact(imp);
            CLAY(imp, {.layout = {.sizing = {FIT, FIX(30)}, .padding = {12, 14, 0, 0}, .childGap = 6, .childAlignment = AL_Y},
                       .backgroundColor = BtnBg(RGB(28, 29, 34), RGB(40, 42, 48), b), .cornerRadius = CLAY_CORNER_RADIUS(10),
                       .border = {.color = Mix(RGB(38, 40, 46), RGB(66, 68, 76), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                Icon(I_PLUS, 12, C_DIM);
                Tx("Import", 12, C_TEXT, 0);
            }
        }
        CLAY(CLAY_ID("LibTabs"), {.layout = {.sizing = {GROW, FIX(34)}, .padding = CLAY_PADDING_ALL(4), .childGap = 4},
                                  .backgroundColor = C_SEG, .cornerRadius = CLAY_CORNER_RADIUS(13),
                                  .border = {.color = RGB(30, 32, 37), .width = CLAY_BORDER_OUTSIDE(1)}}) {
            if (TabBtn(CLAY_IDI("LibTab", 0), "Media", gLeftTab == 0, 12, 10, false)) gLeftTab = 0;
            if (TabBtn(CLAY_IDI("LibTab", 1), "Audio", gLeftTab == 1, 12, 10, false)) gLeftTab = 1;
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
/* UI: inspector (right) - interaction helpers                         */
/* ------------------------------------------------------------------ */
static float SlDefault(int gi) {
    if (gi == 0) return 1.f;                 /* opacity */
    if (gi == 40 || gi == 41) return 0.f;    /* fade durations */
    if (gi / 8 == 4 && gi % 8 == 0) return 0.f; /* sharpness starts at 0 */
    return .5f;
}
static void ResetAcc(int idx) {
    if (idx == 0) { for (int i = 0; i < 3; i++) gWhT[i] = (Vector2){0, 0}; }
    else { for (int s = 0; s < 8; s++) gSlT[idx * 8 + s] = SlDefault(idx * 8 + s); if (idx == 1) gHsl = 0; }
}
static void TickState(void) {
    float k = 1.f - expf(-18.f * gDt);
    for (int i = 0; i < NSL; i++) {
        if (gSlDrag[i]) continue;
        gSl[i] += (gSlT[i] - gSl[i]) * k;
        if (fabsf(gSlT[i] - gSl[i]) < 0.0006f) gSl[i] = gSlT[i];
    }
    if (!gRotDrag) {
        gRot += (gRotT - gRot) * k;
        if (fabsf(gRotT - gRot) < 0.02f) gRot = gRotT;
    }
    for (int i = 0; i < 3; i++) {
        if (gWhDrag[i]) continue;
        gWh[i].x += (gWhT[i].x - gWh[i].x) * k; gWh[i].y += (gWhT[i].y - gWh[i].y) * k;
        if (fabsf(gWhT[i].x - gWh[i].x) < 0.002f && fabsf(gWhT[i].y - gWh[i].y) < 0.002f) gWh[i] = gWhT[i];
    }
}

/* drag a horizontal slider (value 0..1 in gSl[gi]) */
static void SliderLogic(Clay_ElementId id, int gi, float kr) {
    if (Clay_PointerOver(id) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gDragId = id.id;
    gSlDrag[gi] = false;
    if (gDragId == id.id) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Clay_BoundingBox bb = Clay_GetElementData(id).boundingBox;
            if (bb.width > 2 * kr) {
                float t = (GetMousePosition().x - bb.x - kr) / (bb.width - 2 * kr);
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                gSl[gi] = gSlT[gi] = t;
            }
            gSlDrag[gi] = true;
        } else gDragId = 0;
    }
}

static void WheelLogic(int i, Clay_ElementId id) {
    Clay_BoundingBox bb = Clay_GetElementData(id).boundingBox;
    Vector2 m = GetMousePosition();
    float rad = fminf(bb.width, bb.height) / 2.f;
    Vector2 c = {bb.x + bb.width / 2.f, bb.y + bb.height / 2.f};
    float dx = m.x - c.x, dy = m.y - c.y;
    if (rad > 1.f && Clay_PointerOver(id) && sqrtf(dx * dx + dy * dy) <= rad + 6.f) {
        gCross = true;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gDragWheel = i;
    }
    gWhDrag[i] = false;
    if (gDragWheel == i) {
        gCross = true;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && rad > 1.f) {
            float px = dx / (rad - 1.f), py = dy / (rad - 1.f), l = sqrtf(px * px + py * py);
            if (l > 1.f) { px /= l; py /= l; }
            gWh[i] = gWhT[i] = (Vector2){px, py};
            gWhDrag[i] = true;
        } else gDragWheel = -1;
    }
}

/* stepper chevron (8 x 5 px ink) */
static bool Arrow(Clay_ElementId id, int icon) {
    Bs b = Interact(id);
    CLAY(id, {.layout = {.sizing = {FIX(16), FIX(16)}, .childAlignment = AL_C}}) {
        Icon(icon, 16, Mix(C_ICON, C_TEXT, b.hover));
    }
    return b.clicked;
}

/* read-only value pill: value (fs) + optional unit (ufs), right aligned with `pad` px inset */
static void ValBox(const char *s, const char *unit, float w, float h, float fs, float ufs, float pad) {
    CLAY_AUTO_ID({.layout = {.sizing = {FIX(w), FIX(h)}, .padding = {0, pad, 0, 0}, .childGap = 5,
                             .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER}},
                  .backgroundColor = C_VALBOX, .cornerRadius = CLAY_CORNER_RADIUS(12)}) {
        Tx(s, fs, C_TEXT, 0);
        if (unit) Tx(unit, ufs, C_ICON, 0);
    }
}

/* number field (112 x 39): value + unit + up/down steppers */
static void NumField(int n, float *v, const char *unit) {
    Clay_ElementId id = CLAY_IDI("NumF", n);
    Bs b = Interact(id);
    CLAY(id, {.layout = {.sizing = {FIX(112), FIX(39)}, .padding = {10, 5, 0, 0}, .childGap = 6, .childAlignment = AL_Y},
              .backgroundColor = Mix(Mix(C_RFIELD, RGB(46, 47, 54), b.hover), RGB(70, 73, 83), b.press * 0.8f), .cornerRadius = CLAY_CORNER_RADIUS(11),
              .border = {.color = Mix(C_RFIELD, RGB(76, 79, 88), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        HSPACE
        Tx(Fmt("%d", (int)roundf(*v)), 22, C_TEXT, 0);
        Tx(unit, 17, C_ICON, 0);
        GAPX(3)
        CLAY_AUTO_ID({.layout = {COLM, .sizing = {FIT, FIT}}}) {
            if (Arrow(CLAY_IDI("NumU", n), I_CHEV_UP)) *v += 1.f;
            if (Arrow(CLAY_IDI("NumD", n), I_CHEV_DN)) *v -= 1.f;
        }
    }
}

/* small field used by crop / distort panels (T, L, R, B, TL, TR, BL, BR) */
static void MiniField(int n, const char *lab) {
    Clay_ElementId id = CLAY_IDI("MiniF", n);
    Bs b = Interact(id);
    CLAY(id, {.layout = {.sizing = {FIX(112), FIX(34)}, .padding = {10, 6, 0, 0}, .childGap = 4, .childAlignment = AL_Y},
              .backgroundColor = Mix(Mix(C_RFIELD, RGB(46, 47, 54), b.hover), RGB(70, 73, 83), b.press * 0.8f), .cornerRadius = CLAY_CORNER_RADIUS(10),
              .border = {.color = Mix(C_RFIELD, RGB(76, 79, 88), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        Tx(lab, 12, RGB(112, 124, 156), 1);
        HSPACE
        Tx("0", 18, C_TEXT, 0);
        Tx("px", 13, C_ICON, 0);
        CLAY_AUTO_ID({.layout = {COLM, .sizing = {FIT, FIT}, .padding = {2, 0, 0, 0}}}) {
            Icon(I_CHEV_UP, 10, C_ICON);
            Icon(I_CHEV_DN, 10, C_ICON);
        }
    }
}

/* Position / Scale card: exactly 169 px tall
   12 pad | 24 header | 6 | 39 field | 34 link row | 39 field | 15 pad */
static void FieldCard(int n, const char *title, int icon, const char *l1, const char *l2, float *a, float *b, float def, const char *unit, int link) {
    CLAY(CLAY_IDI("FCard", n), {.layout = {COLM, .sizing = {GROW, FIX(169)}, .padding = {13, 13, 12, 0}}, RCARD}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(24)}, .padding = {3, 0, 0, 0}, .childGap = 10, .childAlignment = AL_Y}}) {
            Icon(icon, 20, C_ICON);
            Tx(title, 19, C_TEXT, 1);
            HSPACE
            if (IconBtn(CLAY_IDI("Reset", n), I_RESET, 24, 13, RGB(104, 105, 112), false, 6)) { *a = def; *b = def; }
        }
        GAPY(6)
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(39)}, .childGap = 7, .childAlignment = AL_Y}}) {
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(22), FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) { Tx(l1, 19, C_ICON, 0); }
            NumField(n * 2, a, unit);
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(34)}, .childAlignment = AL_C}}) {
            if (LinkBtn(CLAY_IDI("Link", n), gLink[link], 28, 24)) gLink[link] = !gLink[link];
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(39)}, .childGap = 7, .childAlignment = AL_Y}}) {
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(22), FIT}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) { Tx(l2, 19, C_ICON, 0); }
            NumField(n * 2 + 1, b, unit);
        }
    }
}

/* Crop Tools / Distort toggle button (66 px tall before FIX scaling) */
static bool NavButton(int n, int icon, const char *label, float open) {
    Clay_ElementId id = CLAY_IDI("Nav", n);
    Bs b = Interact(id);
    Clay_Color base = Mix(C_RCARD, RGB(42, 44, 51), open);
    Clay_Color hov  = Mix(RGB(38, 40, 46), RGB(48, 50, 58), open);
    CLAY(id, {.layout = {.sizing = {GROW, FIX(64)}, .padding = {16, 18, 0, 0}, .childGap = 12, .childAlignment = AL_Y},
              .backgroundColor = BtnBg(base, hov, b), .cornerRadius = CLAY_CORNER_RADIUS(20),
              .border = {.color = Mix(C_RCARDB, RGB(80, 83, 92), fmaxf(b.hover, open * 0.8f)), .width = CLAY_BORDER_OUTSIDE(1)}}) {
        Icon(icon, 29, Mix(C_ICON, C_TEXT, open));
        Tx(label, 19, C_TEXT, 0);
        HSPACE
        Icon(open > 0.5f ? I_CHEV_DN : I_CHEV_R, 20, Mix(C_ICON, C_TEXT, open));
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

static const char *RotStr(float r) { return fabsf(r) < 0.05f ? "0" : Fmt("%.1f", r); }

/* Transform tab. A flexible spacer positions the enlarged 66px nav buttons
   above a deliberate 26px bottom gap, matching the reference panel rhythm. */
static void TransformView(void) {
    CLAY(CLAY_ID("TfCol"), {.layout = {COLM, .sizing = {GROW, GROW}}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 10}}) {
            FieldCard(0, "Position", I_POS, "X", "Y", &gNum[0], &gNum[1], 0.f, "px", 0);
            FieldCard(1, "Scale", I_SCALE, "W", "H", &gNum[2], &gNum[3], 100.f, "%", 1);
        }
        GAPY(10)
        CLAY(CLAY_ID("RotCard"), {.layout = {COLM, .sizing = {GROW, FIX(160)}, .padding = {13, 13, 13, 0}, .childGap = 6}, RCARD}) {
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(35)}, .childGap = 8, .childAlignment = AL_Y}}) {
                if (IconBtn(CLAY_ID("RotReset"), I_ROT, 34, 24, C_TEXT, false, 17)) gRotT = 0.f;
                Tx("Rotation", 19, C_TEXT, 0);
                HSPACE
                ValBox(RotStr(gRot), "\xC2\xB0", 128, 35, 22, 18, 11);
            }
            Clay_ElementId rid = CLAY_ID("RotRuler");
            Bs b = Interact(rid);
            if (Clay_PointerOver(rid) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gDragId = rid.id;
            gRotDrag = false;
            if (gDragId == rid.id) {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    Clay_BoundingBox bb = Clay_GetElementData(rid).boundingBox;
                    float span = bb.width - RULER_L - RULER_R;
                    if (span > 8) {
                        float t = (GetMousePosition().x - (bb.x + RULER_L)) / span;
                        t = t < 0 ? 0 : t > 1 ? 1 : t;
                        gRot = gRotT = -180.f + 360.f * t;
                    }
                    gRotDrag = true;
                } else gDragId = 0;
            }
            CLAY(rid, {.layout = {.sizing = {GROW, FIX(93)}}, .custom = {.customData = NC(K_ROTRULER, C_CARD, C_CARD, C_CARD, (gRot + 180.f) / 360.f, b.hover, 0)}}) {}
        }
        GAPY(10)
        CLAY_AUTO_ID({.layout = {COLM, .sizing = {GROW, FIT}}}) {
            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(27)}, .childAlignment = AL_Y}}) {
                Tx("Opacity", 18, C_TEXT, 0);
                HSPACE
                ValBox(Fmt("%d", (int)roundf(gSl[0] * 100.f)), "%", 72, 27, 16, 13, 8);
            }
            Clay_ElementId sid = CLAY_ID("OpacitySlider");
            Bs b = Interact(sid);
            SliderLogic(sid, 0, 7.5f);
            CLAY(sid, {.layout = {.sizing = {GROW, FIX(26)}},
                       .custom = {.customData = NC(K_SLIDER, RGB(43, 44, 49), RGB(43, 44, 49), C_CARD, gSl[0], (b.hover + (gSlDrag[0] ? 1.f : 0.f)) * 1.5f, 1)}}) {}
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, CLAY_SIZING_GROW(10)}}}) {}   /* min 10px, grows when the window is taller */
        NavSection();
        GAPY(26)
    }
}

/* ---------------- colors tab ---------------- */
static void WheelItem(int i, const char *label) {
    CLAY(CLAY_IDI("WheelCol", i), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6, .childAlignment = {.x = CLAY_ALIGN_X_CENTER}}}) {
        Clay_ElementId wid = CLAY_IDI("Wheel", i);
        Bs b = Interact(wid);
        WheelLogic(i, wid);
        float hue, len; WheelInfo(gWh[i], &hue, &len);
        bool col = len > 0.02f;
        Color dc = ColorFromHSV(hue, fminf(1.f, .3f + .7f * len), 1.f);
        CLAY(wid, {.layout = {.sizing = {FIX(100), FIX(100)}},
                   .custom = {.customData = NC(K_WHEEL, C_CARD, (Clay_Color){gWh[i].x, gWh[i].y, 0, 0}, (Clay_Color){dc.r, dc.g, dc.b, 255},
                                               col ? 1.f : 0.f, fmaxf(b.hover * .6f, gWhDrag[i] ? 1.f : 0.f), 0)}}) {}
        Tx(label, 16, C_TEXT, 1);
        if (col) Tx(Fmt("%d\xC2\xB0 \xC2\xB7 %d", (int)roundf(hue) % 360, (int)roundf(fminf(1.f, len) * 100.f)), 13, RGB(140, 142, 150), 0);
        else     Tx("Neutral", 13, RGB(140, 142, 150), 0);
    }
}

static void AccHeader(int idx, const char *title, int icon, float openAnim, bool modified) {
    Clay_ElementId id = CLAY_IDI("AccHdr", idx);
    Bs b = Interact(id);
    if (b.clicked) {
        if (gOpenAcc == idx) { gOpenAcc = -1; gFollow = false; }
        else { gOpenAcc = idx; gFollow = (idx >= 3); }
    }

    /* press-squash: shrinks while held, springs back on release */
    bool down = Clay_PointerOver(id) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    float sq = Spring(id.id, down ? 1.f : 0.f, 700.f, 26.f);
    sq = sq < 0.f ? 0.f : sq > 1.2f ? 1.2f : sq;
    int px = (int)roundf(sq * 4.f), py = (int)roundf(sq * 1.5f);

    CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(52)}, .padding = {px, px, py, py}}}) {
        CLAY(id, {.layout = {.sizing = {GROW, GROW}, .padding = {16, 14, 0, 0}, .childGap = 12, .childAlignment = AL_Y},
                  /* hover only, no press flash */
                  .backgroundColor = Mix(Mix(C_RCARD, RGB(33, 34, 38), openAnim), Mix(RGB(36, 37, 43), RGB(40, 41, 47), openAnim), b.hover),
                  .cornerRadius = CLAY_CORNER_RADIUS(18),
                  .border = {.color = Mix(Mix(C_RCARDB, RGB(62, 64, 72), openAnim), RGB(84, 87, 96), b.hover * 0.7f),
                             .width = CLAY_BORDER_OUTSIDE(1)}}) {
            Icon(icon, 22, C_ICON);
            Tx(title, 18, C_TEXT, 0);
            HSPACE
            if (modified) {
                CLAY_AUTO_ID({.layout = {.sizing = {FIX(6), FIX(6)}}, .backgroundColor = C_TEXT, .cornerRadius = CLAY_CORNER_RADIUS(3)}) {}
                GAPX(2)
            }
            CLAY_AUTO_ID({.layout = {.sizing = {FIX(26), FIX(26)}, .childAlignment = AL_C}, .cornerRadius = CLAY_CORNER_RADIUS(13),
                          .border = {.color = Mix(RGB(74, 77, 85), RGB(130, 134, 144), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
                IconV(I_PLUS, 13, C_DIM, 1.f - openAnim);
            }
        }
    }
}

static const char *kSliderNames[5][8] = {
    {0}, {"Hue", "Saturation", "Lightness"},
    {"Exposure", "Brightness", "Contrast", "Highlights", "Shadows", "Whites", "Blacks"},
    {"Temperature", "Tint", "Saturation"}, {"Sharpness", "Clarity", "Vignette"}
};
/* last measured body heights (fallbacks until first measure) */
static float gAccH[5] = {250.f, 270.f, 410.f, 190.f, 190.f};

static void SliderSpec(int idx, int s, int *mode, Clay_Color *a, Clay_Color *b) {
    *mode = 0; *a = RGB(43, 44, 49); *b = *a;
    if (idx == 1) {
        if (s == 0) *mode = 4;
        else if (s == 1) { *mode = 2; *a = RGB(96, 96, 102); *b = RGB(236, 64, 80); }
        else             { *mode = 2; *a = RGB(8, 8, 10);    *b = RGB(240, 240, 244); }
    } else if (idx == 3) {
        *mode = 2;
        if (s == 0)      { *a = RGB(70, 130, 230); *b = RGB(240, 170, 70); }
        else if (s == 1) { *a = RGB(70, 200, 120); *b = RGB(220, 70, 190); }
        else             { *a = RGB(110, 110, 116); *b = RGB(236, 64, 80); }
    }
}

static void SliderRow(int idx, int s) {
    int gi = idx * 8 + s, mode; Clay_Color a, b;
    SliderSpec(idx, s, &mode, &a, &b);
    bool uni = (idx == 4 && s == 0);
    int val = uni ? (int)roundf(gSl[gi] * 100.f) : (int)roundf((gSl[gi] - .5f) * 200.f);
    CLAY(CLAY_IDI("SRow", gi), {.layout = {COLM, .sizing = {GROW, FIT}, .childGap = 6}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = AL_Y}}) {
            Tx(kSliderNames[idx][s], 14, C_DIM, 0);
            HSPACE
            ValBox(Fmt("%d", val), NULL, 62, 26, 14, 0, 10);
        }
        Clay_ElementId sid = CLAY_IDI("Slid", gi);
        Bs bs = Interact(sid);
        SliderLogic(sid, gi, 7.5f);
        CLAY(sid, {.layout = {.sizing = {GROW, FIX(18)}},
                   .custom = {.customData = NC(K_SLIDER, a, b, C_CARD, gSl[gi], (bs.hover + (gSlDrag[gi] ? 1.f : 0.f)) * 1.5f, 1 | mode)}}) {}
    }
}

static void Chip(int i) {
    static const Clay_Color cc[9] = {{0,0,0,255}, {239,68,68,255}, {249,115,22,255}, {234,179,8,255}, {34,197,94,255},
                                     {6,182,212,255}, {59,130,246,255}, {139,92,246,255}, {236,72,153,255}};
    Clay_ElementId id = CLAY_IDI("Chip", i);
    Bs b = Interact(id);
    float sel = Anim(id.id + 3u, gHsl == i ? 1.f : 0.f, 30.f);
    if (b.clicked) gHsl = i;
    CLAY(id, {.layout = {.sizing = {FIX(30), FIX(30)}, .childAlignment = AL_C}, .cornerRadius = CLAY_CORNER_RADIUS(15),
              .border = {.color = RGBA(240, 242, 246, 255.f * sel + 70.f * b.hover * (1.f - sel)), .width = CLAY_BORDER_OUTSIDE(2)}}) {
        float sz = 22.f + 2.f * b.hover - 2.f * b.press;
        CLAY_AUTO_ID({.layout = {.sizing = {FIX(sz), FIX(sz)}}, .custom = {.customData = NC(K_CHIP, cc[i], cc[i], cc[i], 0, 0, i == 0)}}) {}
    }
}

static void Accordion(int idx, const char *title, int icon) {
    Clay_ElementId hid = CLAY_IDI("AccHdr", idx);
    float o = EaseIO(AnimLin(hid.id, gOpenAcc == idx ? 1.f : 0.f, 0.2f));
    float mh = Clay_GetElementData(CLAY_IDI("AccBody", idx)).boundingBox.height;
    if (mh > 1.f) gAccH[idx] = mh;
    float wrapH = roundf(o * (gAccH[idx] + 10.f));
    int n = 0;
    if (idx > 0) while (n < 7 && kSliderNames[idx][n]) n++;
    bool modified = false;
    if (idx == 0) for (int i = 0; i < 3; i++) if (fabsf(gWhT[i].x) + fabsf(gWhT[i].y) > 0.02f) modified = true;
    CLAY(CLAY_IDI("AccCol", idx), {.layout = {COLM, .sizing = {GROW, FIT}}}) {
        AccHeader(idx, title, icon, o, modified);
        if (wrapH >= 1.f) {
            CLAY(CLAY_IDI("AccWrap", idx), {.layout = {COLM, .sizing = {GROW, FIX(wrapH)}}, .clip = {.vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(10)}}}) {}
                CLAY(CLAY_IDI("AccBody", idx), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = CLAY_PADDING_ALL(12), .childGap = 12}, RCARD}) {
                    CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT}}}) {
                        Clay_ElementId rid = CLAY_IDI("AccReset", idx);
                        Bs b = Interact(rid);
                        Clay_Color rc = Mix(RGB(140, 142, 150), C_TEXT, b.hover);
                        CLAY(rid, {.layout = {.sizing = {FIT, FIT}, .childGap = 5, .childAlignment = AL_Y}}) {
                            Icon(I_RESET, 14, rc);
                            Tx("Reset", 14, rc, 0);
                        }
                        if (b.clicked) ResetAcc(idx);
                    }
                    if (idx == 0) {
                        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 10}}) {
                            WheelItem(0, "Shadows"); WheelItem(1, "Tint"); WheelItem(2, "Highlights");
                        }
                    } else {
                        if (idx == 1) {
                            CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 6}}) {
                                for (int c = 0; c < 9; c++) Chip(c);
                            }
                        }
                        for (int s = 0; s < n; s++) SliderRow(idx, s);
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

/* ---------------- effects tab ---------------- */
typedef struct { Clay_Color a, b, sun; } FilterP;
static const FilterP kFilters[8] = {
    {{236,110,96,255},  {84,66,208,255},  {250,226,140,255}},
    {{255,150,40,255},  {70,88,232,255},  {255,222,120,255}},
    {{158,158,164,255}, {38,38,44,255},   {240,240,244,255}},
    {{232,160,136,255}, {120,98,192,255}, {250,226,150,255}},
    {{60,176,204,255},  {38,48,124,255},  {250,236,170,255}},
    {{128,196,112,255}, {28,86,80,255},   {250,236,160,255}},
    {{204,80,164,255},  {58,30,112,255},  {252,214,150,255}},
    {{242,204,100,255}, {156,66,52,255},  {255,244,200,255}},
};

static void FilterThumb(int i) {
    Clay_ElementId id = CLAY_IDI("Flt", i);
    Bs b = Interact(id);
    if (b.clicked) gFilter = i;
    CLAY(id, {.layout = {.sizing = {GROW, FIX(84)}},
              .custom = {.customData = NC(K_FILTER, kFilters[i].a, kFilters[i].b, kFilters[i].sun, 0, b.hover, gFilter == i)}}) {}
}

#define TRANS_DESC_H 46.f
static void EffectsView(void) {
    float o = EaseIO(AnimLin(CLAY_ID("TransAnim").id, gTransOpen ? 1.f : 0.f, 0.2f));
    float dh = roundf(o * TRANS_DESC_H);
    CLAY(CLAY_ID("TransCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = {14, 14, 14, 14}}, RCARD}) {
        Clay_ElementId hid = CLAY_ID("TransHdr");
        Bs b = Interact(hid);
        if (b.clicked) gTransOpen = !gTransOpen;
        CLAY(hid, {.layout = {.sizing = {GROW, FIT}, .childGap = 10, .childAlignment = AL_Y}}) {
            Icon(I_BOWTIE, 20, C_ICON);
            Tx("Transitions", 16, C_TEXT, 1);
            HSPACE
            Icon(gTransOpen ? I_CHEV_DN : I_CHEV_R, 14, Mix(C_MUTED, C_TEXT, b.hover));
        }
        if (dh >= 1.f) {
            CLAY(CLAY_ID("TransDesc"), {.layout = {COLM, .sizing = {GROW, FIX(dh)}}, .clip = {.vertical = true}}) {
                CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIX(8)}}}) {}
                TxWrap("Place another clip right after this one to add a transition between them.", 14, C_MUTED, 0);
            }
        }
    }
    CLAY(CLAY_ID("FiltersCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = {14, 14, 14, 14}, .childGap = 10}, RCARD}) {
        Clay_ElementId fid = CLAY_ID("FiltersHdr");
        Bs b = Interact(fid);
        CLAY(fid, {.layout = {.sizing = {GROW, FIT}, .childGap = 10, .childAlignment = AL_Y}}) {
            Icon(I_SPARKLE, 20, C_ICON);
            Tx("Filters", 16, C_TEXT, 1);
            HSPACE
            Icon(I_CHEV_R, 14, Mix(C_MUTED, C_TEXT, b.hover));
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childAlignment = AL_Y}}) {
            Tx("Filter presets", 13, C_MUTED, 0);
            HSPACE
            Clay_ElementId rid = CLAY_ID("FiltersReset");
            Bs rb = Interact(rid);
            Clay_Color rc = Mix(RGB(140, 142, 150), C_TEXT, rb.hover);
            CLAY(rid, {.layout = {.sizing = {FIT, FIT}, .childGap = 5, .childAlignment = AL_Y}}) {
                Icon(I_RESET, 14, rc);
                Tx("Reset", 14, rc, 0);
            }
            if (rb.clicked) gFilter = 0;
        }
        for (int row = 0; row < 2; row++) {
            CLAY(CLAY_IDI("FltRow", row), {.layout = {.sizing = {GROW, FIT}, .childGap = 8}}) {
                for (int c = 0; c < 4; c++) FilterThumb(row * 4 + c);
            }
        }
    }
}

/* ---------------- text tab ---------------- */
static void TextView(void) {
    CLAY(CLAY_ID("SubCard"), {.layout = {COLM, .sizing = {GROW, FIT}, .padding = CLAY_PADDING_ALL(12), .childGap = 8}, RCARD}) {
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .childGap = 10, .childAlignment = AL_Y}}) {
            Icon(I_FILM, 20, C_ICON);
            Tx("Subtitles", 16, C_TEXT, 1);
        }
        CLAY_AUTO_ID({.layout = {.sizing = {GROW, FIT}, .padding = {2, 0, 2, 0}}}) { Tx("Caption", 13, C_MUTED, 0); }
        Clay_ElementId id = CLAY_ID("SubField");
        Bs b = Interact(id);
        CLAY(id, {.layout = {.sizing = {GROW, FIX(40)}, .padding = {12, 12, 0, 0}, .childAlignment = AL_Y},
                  .backgroundColor = Mix(RGB(28, 29, 33), RGB(36, 37, 43), b.hover), .cornerRadius = CLAY_CORNER_RADIUS(10),
                  .border = {.color = Mix(RGB(28, 29, 33), RGB(60, 62, 70), b.hover), .width = CLAY_BORDER_OUTSIDE(1)}}) {
            Tx("Type a subtitle...", 14, RGB(112, 115, 124), 0);
        }
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
    float pill = Anim(CLAY_ID("PillAnim").id, (float)gRightTab, 20.f);
    bool fitBody = (gRightTab == 3);
    CLAY(CLAY_ID("Right"), {.layout = {COLM, .sizing = {FIX(374), GROW}, .childGap = 10}}) {
        /* tab bar: 54 tall = 6 pad + 42 track (4 pad + 34 pill + 4 pad) + 6 pad */
        CLAY(CLAY_ID("InspTabs"), {.layout = {.sizing = {GROW, FIX(54)}, .padding = CLAY_PADDING_ALL(6)}, RPANEL_R(18)}) {
            CLAY(CLAY_ID("InspSeg"), {.layout = {.sizing = {GROW, GROW}, .padding = CLAY_PADDING_ALL(4), .childGap = 4},
                                      .cornerRadius = CLAY_CORNER_RADIUS(14),
                                      .custom = {.customData = NC(K_PILL, C_CARD, C_CARD, C_CARD, pill, 0, 0)}}) {
                for (int i = 0; i < 4; i++)
                    if (TabBtn(CLAY_IDI("InspTab", i), tabs[i], gRightTab == i, 16, 11, true)) {
                        if (gRightTab != i) { gRightTab = i; gScroll = gScrollT = 0.f; gFollow = false; gTabT = 0.f; gTabE = 0.f; }
                    }
            }
        }
        CLAY(CLAY_ID("InspBody"), {.layout = {.sizing = {GROW, fitBody ? FIT : GROW}}, RPANEL,
                                   .clip = {.vertical = true, .childOffset = {0, -gScroll + (1.f - gTabE) * 18.f}}}) {
            CLAY(CLAY_ID("InspContent"), {.layout = {COLM, .sizing = {GROW, fitBody ? FIT : CLAY_SIZING_FIT(gInspViewH)}, .padding = {13, 13, 15, 13}, .childGap = 10}}) {
                if (gRightTab != 2) {
                    CLAY(CLAY_ID("InspTitle"), {.layout = {.sizing = {GROW, FIT}, .padding = {5, 0, 0, 5}, .childGap = 10, .childAlignment = AL_Y}}) {
                        Icon(icons[gRightTab], 29, C_ICON);
                        Tx(tabs[gRightTab], 24, C_TEXT, 1);
                    }
                }
                if (gRightTab == 0) TransformView();
                else if (gRightTab == 1) ColorsView();
                else if (gRightTab == 2) EffectsView();
                else TextView();
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
    gFbN = 0;
    TickState();
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

/* unitsPerEm / (hhea ascender - descender) of a TTF.
   raylib/stb size a font so that (ascender - descender) == fontSize, so the real em height
   is fontSize * this ratio; Segoe UI gives 0.752, Inter 0.826, DejaVu 0.86 ... */
static float FontEmRatio(const char *path) {
    int n = 0;
    unsigned char *d = LoadFileData(path, &n);
    float k = 0.f;
    if (d && n > 64) {
#define BE16(p) ((unsigned)(((p)[0] << 8) | (p)[1]))
        int nt = (int)BE16(d + 4);
        unsigned upm = 0; int asc = 0, desc = 0;
        for (int i = 0; i < nt && 12 + 16 * (i + 1) <= n; i++) {
            unsigned char *rec = d + 12 + 16 * i;
            unsigned off = ((unsigned)rec[8] << 24) | ((unsigned)rec[9] << 16) | ((unsigned)rec[10] << 8) | (unsigned)rec[11];
            if ((long long)off + 20 > n) continue;
            if (memcmp(rec, "head", 4) == 0) upm = BE16(d + off + 18);
            else if (memcmp(rec, "hhea", 4) == 0) { asc = (short)BE16(d + off + 4); desc = (short)BE16(d + off + 6); }
        }
#undef BE16
        if (upm && asc - desc > 0) k = (float)upm / (float)(asc - desc);
    }
    if (d) UnloadFileData(d);
    return k;
}

/* pick the first existing font file; atlases are rasterised lazily per exact pixel size */
static bool FindFont(const char **paths, int n, int slot) {
    for (int i = 0; i < n; i++) {
        if (FileExists(paths[i])) {
            /* every size in this file was tuned on Segoe UI (ratio 0.752); keep the same em for any other font */
            float er = FontEmRatio(paths[i]);
            gFK[slot] = (er > 0.3f && er < 1.3f) ? 0.752f / er : 1.f;
            strncpy(gFontPath[slot], paths[i], sizeof(gFontPath[slot]) - 1);
            gFontPath[slot][sizeof(gFontPath[slot]) - 1] = 0;
            gFontOK[slot] = true;
            return true;
        }
    }
    gFK[slot] = 1.f;
    return false;
}

#ifdef _WIN32
static LRESULT CALLBACK PlutoWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST && gPreviousWndProc) {
        LRESULT base = CallWindowProc(gPreviousWndProc, hwnd, msg, wParam, lParam);
        if (base != HTCLIENT) return base;

        POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &p);
        RECT r; GetClientRect(hwnd, &r);

        if (!IsZoomed(hwnd)) {
            const int edge = 7;
            bool left = p.x < edge, right = p.x >= r.right - edge;
            bool top = p.y < edge, bottom = p.y >= r.bottom - edge;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }

        /* The empty middle of the custom top bar behaves like a normal title bar. */
        if (p.y >= 7 && p.y < 50 && p.x >= 450 && p.x < r.right - 132) return HTCAPTION;
        return HTCLIENT;
    }
    return gPreviousWndProc ? CallWindowProc(gPreviousWndProc, hwnd, msg, wParam, lParam)
                            : DefWindowProc(hwnd, msg, wParam, lParam);
}
#endif

int main(void) {
#ifdef _WIN32
    /* Prevent Windows bitmap-scaling the UI on scaled displays. */
    SetProcessDPIAware();
#endif
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_UNDECORATED | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1600, 967, "Pluto");   /* borderless custom title bar; 967 px tall => Transform fits */
    SetWindowMinSize(1180, 720);
#ifdef _WIN32
    HWND hwnd = (HWND)GetWindowHandle();
    if (hwnd) {
        gPreviousWndProc = (WNDPROC)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
        if (gPreviousWndProc) SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)PlutoWindowProc);
    }
#endif

    const char *reg[] = {"resources/Inter-Regular.ttf", "Inter-Regular.ttf", "C:/Windows/Fonts/segoeui.ttf",
                         "/System/Library/Fonts/Supplemental/Arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                         "/usr/share/fonts/TTF/DejaVuSans.ttf"};
    const char *bold[] = {"resources/Inter-SemiBold.ttf", "Inter-SemiBold.ttf", "C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/segoeuib.ttf",
                          "/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                          "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"};
    gFcpN = 0;
    for (int c = 32; c < 127; c++) gFcps[gFcpN++] = c;
    gFcps[gFcpN++] = 0xB0; gFcps[gFcpN++] = 0xD7; gFcps[gFcpN++] = 0xB7; gFcps[gFcpN++] = 0x2026;
    bool okR = FindFont(reg, 6, 0);
    bool okB = FindFont(bold, 7, 1);
    if (!okB && okR) {
        strcpy(gFontPath[1], gFontPath[0]);
        gFontOK[1] = true;
        gFK[1] = gFK[0];
    }

    for (int i = 0; i < NSL; i++) gSl[i] = gSlT[i] = SlDefault(i);

    uint64_t mem = Clay_MinMemorySize();
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(mem, malloc(mem));
    Clay_Initialize(arena, (Clay_Dimensions){(float)GetScreenWidth(), (float)GetScreenHeight()}, (Clay_ErrorHandler){OnClayError, 0});
    Clay_SetMeasureTextFunction(MeasureTxt, NULL);

    while (!WindowShouldClose() && !gWantQuit) {
        gDt = GetFrameTime(); if (gDt > 0.05f) gDt = 0.05f;
        Clay_SetLayoutDimensions((Clay_Dimensions){(float)GetScreenWidth(), (float)GetScreenHeight()});
        Vector2 m = GetMousePosition();
        Clay_SetPointerState((Clay_Vector2){m.x, m.y}, IsMouseButtonDown(MOUSE_BUTTON_LEFT));
        Clay_UpdateScrollContainers(false, (Clay_Vector2){0, GetMouseWheelMove() * 28.f}, gDt);

        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { gDragId = 0; gDragWheel = -1; }
        gCustN = 0; gHand = false; gCross = false;
        Clay_RenderCommandArray cmds = BuildUI();

        if (gWindowAction == 1) MinimizeWindow();
        else if (gWindowAction == 2) {
            if (IsWindowState(FLAG_WINDOW_MAXIMIZED)) RestoreWindow();
            else MaximizeWindow();
        }
        else if (gWindowAction == 3) gWantQuit = true;
        gWindowAction = 0;
        if (gWantQuit) break;

        BeginDrawing();
        ClearBackground((Color){14, 15, 17, 255});
        RenderUI(cmds);
        EndDrawing();
        SetMouseCursor(gCross ? MOUSE_CURSOR_CROSSHAIR : gHand ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
    }
    CloseWindow();
    return 0;
}
