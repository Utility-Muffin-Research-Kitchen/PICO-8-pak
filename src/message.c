/* Controller-dismissable setup and error screen in Leaf's standard dialog
   style: title, wrapped instructions and a fixed dismissal hint. It inherits
   Leaf's font, font size and colors, and needs nothing from the daemon, so it
   still works on older Leaf builds and when Leaf does not answer. */
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Logical canvas; SDL scales it to the output. Metrics match Catastrophe's
   dialog at 960 px: CAT_S(40) inset and screen_w - CAT_S(80) text width. */
#define CANVAS_W 960
#define CANVAS_H 720
#define INSET_X 37
#define TEXT_W 885
#define INSET_TOP 30
#define FOOTER_H 64
#define TITLE_GAP 18

static int font_bump(void) {
    const char *value = getenv("CAT_FONT_BUMP");
    char *end = NULL;
    long bump = value && *value ? strtol(value, &end, 10) : 2;
    return (value && *value && (*end || bump < 0 || bump > 5)) ? 2 : (int)bump;
}

static SDL_Color env_color(const char *name, Uint32 fallback) {
    const char *value = getenv(name);
    Uint32 rgb = fallback;
    if (value && strlen(value) == 7 && value[0] == '#' &&
        strspn(value + 1, "0123456789abcdefABCDEF") == 6) {
        rgb = (Uint32)strtoul(value + 1, NULL, 16);
    }
    return (SDL_Color){ (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255 };
}

static TTF_Font *open_font(const char *path, int size) {
    TTF_Font *font = TTF_OpenFont(path, size);
    if (font) TTF_SetFontStyle(font, TTF_STYLE_BOLD);
    return font;
}

static void copy_surface(SDL_Renderer *renderer, SDL_Surface *surface,
                         const SDL_Rect *src, int x, int y) {
    if (!surface || !src || src->w <= 0 || src->h <= 0) return;
    /* Only the visible part becomes a texture, so a long message never runs
       into the renderer's maximum texture height. */
    SDL_Surface *part = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32,
                                                       SDL_PIXELFORMAT_ARGB8888);
    if (!part) return;
    SDL_FillRect(part, NULL, 0);
    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
    SDL_Rect from = *src;
    SDL_BlitSurface(surface, &from, part, NULL);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, part);
    if (texture) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_Rect to = { x, y, src->w, src->h };
        SDL_RenderCopy(renderer, texture, NULL, &to);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(part);
}

typedef struct {
    SDL_Surface *title, *body, *hint, *scroll_hint;
    int body_y, viewport_h, max_scroll, line_h;
    SDL_Color background;
} screen;

static void layout(screen *s) {
    int title_h = s->title ? s->title->h : 0;
    int body_h = s->body ? s->body->h : 0;
    int offset = title_h + (s->body ? TITLE_GAP : 0);
    int available = CANVAS_H - FOOTER_H - INSET_TOP;
    int top = INSET_TOP;
    if (offset + body_h <= available) top += (available - offset - body_h) / 2;
    s->body_y = top + offset;
    s->viewport_h = CANVAS_H - FOOTER_H - s->body_y;
    if (s->viewport_h < 0) s->viewport_h = 0;
    /* A scrolling body shows whole lines only, so no row is cut in half
       against the footer. */
    if (body_h > s->viewport_h && s->viewport_h >= s->line_h) {
        s->viewport_h -= s->viewport_h % s->line_h;
    }
    s->max_scroll = body_h > s->viewport_h ? body_h - s->viewport_h : 0;
}

static void draw(SDL_Renderer *renderer, const screen *s, int scroll) {
    SDL_SetRenderDrawColor(renderer, s->background.r, s->background.g, s->background.b, 255);
    SDL_RenderClear(renderer);
    if (s->title) {
        SDL_Rect src = { 0, 0, s->title->w, s->title->h };
        copy_surface(renderer, s->title, &src, INSET_X,
                     s->body_y - s->title->h - (s->body ? TITLE_GAP : 0));
    }
    if (s->body) {
        int h = s->body->h - scroll;
        if (h > s->viewport_h) h = s->viewport_h;
        SDL_Rect src = { 0, scroll, s->body->w, h };
        copy_surface(renderer, s->body, &src, INSET_X, s->body_y);
    }
    /* The footer stays put and always says how to leave, even when Leaf's
       own button hints are turned off. */
    SDL_Surface *hint = s->max_scroll > 0 && s->scroll_hint ? s->scroll_hint : s->hint;
    if (hint) {
        SDL_Rect src = { 0, 0, hint->w, hint->h };
        copy_surface(renderer, hint, &src, INSET_X,
                     CANVAS_H - FOOTER_H + (FOOTER_H - hint->h) / 2);
    }
    SDL_RenderPresent(renderer);
}

int main(int argc, char **argv) {
    if (argc != 2) return 1;
    fprintf(stderr, "PICO-8: %s\n", argv[1]);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) || TTF_Init()) return 1;
    const char *font_path = getenv("CAT_FONT_PATH");
    char resolved[4096];
    if (!font_path || !*font_path) font_path = "fonts/SpaceGrotesk/SpaceGrotesk-Regular.ttf";
    if (*font_path != '/') {
        const char *root = getenv("CAT_FONTS_DIR");
        if (!root || snprintf(resolved, sizeof(resolved), "%s/%s", root, font_path) >= (int)sizeof(resolved)) return 1;
        font_path = resolved;
    }
    int bump = font_bump();
    TTF_Font *title_font = open_font(font_path, 2 * (16 + bump));
    TTF_Font *body_font = open_font(font_path, 2 * (14 + bump));
    TTF_Font *hint_font = open_font(font_path, 2 * (12 + bump));
    if (!title_font || !body_font || !hint_font) return 1;
    SDL_Window *window = SDL_CreateWindow("PICO-8 setup", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, CANVAS_W, CANVAS_H, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, 0) : NULL;
    if (!renderer) return 1;
    SDL_RenderSetLogicalSize(renderer, CANVAS_W, CANVAS_H);

    SDL_Color text = env_color("CAT_COLOR_TEXT", 0xE8F1E3);
    SDL_Color hint = env_color("CAT_COLOR_HINT", 0x7E9579);
    screen s = { .background = env_color("CAT_COLOR_BACKGROUND", 0x0F160E) };
    s.title = TTF_RenderUTF8_Blended_Wrapped(title_font, "PICO-8 for Leaf", text, TEXT_W);
    /* The whole message, however long: no fixed buffer and no ellipsis. */
    s.body = argv[1][0] ? TTF_RenderUTF8_Blended_Wrapped(body_font, argv[1], text, TEXT_W) : NULL;
    s.hint = TTF_RenderUTF8_Blended(hint_font, "A / B: Return to Leaf", hint);
    s.scroll_hint = TTF_RenderUTF8_Blended(hint_font, "Up / Down: Scroll    A / B: Return to Leaf", hint);
    if (!s.title || !s.hint || (argv[1][0] && !s.body)) return 1;
    s.line_h = TTF_FontLineSkip(body_font);
    if (s.line_h < 1) s.line_h = 1;
    layout(&s);

    SDL_GameController *pads[4] = {0};
    int count = 0;
    for (int i = 0; i < SDL_NumJoysticks() && count < 4; i++) {
        if (SDL_IsGameController(i)) pads[count++] = SDL_GameControllerOpen(i);
    }
    int scroll = 0;
    draw(renderer, &s, scroll);
    SDL_Event event;
    while (SDL_WaitEvent(&event)) {
        if (event.type == SDL_QUIT) break;
        int step = 0;
        if (event.type == SDL_KEYDOWN) {
            SDL_Keycode key = event.key.keysym.sym;
            if (key == SDLK_ESCAPE || key == SDLK_RETURN) break;
            if (key == SDLK_UP) step = -1;
            if (key == SDLK_DOWN) step = 1;
        }
        if (event.type == SDL_CONTROLLERBUTTONDOWN) {
            Uint8 button = event.cbutton.button;
            if (button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_B) break;
            if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) step = -1;
            if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) step = 1;
        }
        if (step && s.max_scroll > 0) {
            int next = scroll + step * s.line_h;
            if (next < 0) next = 0;
            if (next > s.max_scroll) next = s.max_scroll;
            if (next != scroll) {
                scroll = next;
                draw(renderer, &s, scroll);
            }
        } else if (event.type == SDL_WINDOWEVENT) {
            draw(renderer, &s, scroll);
        }
    }
    for (int i = 0; i < count; i++) if (pads[i]) SDL_GameControllerClose(pads[i]);
    SDL_FreeSurface(s.title);
    SDL_FreeSurface(s.body);
    SDL_FreeSurface(s.hint);
    SDL_FreeSurface(s.scroll_hint);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_CloseFont(title_font);
    TTF_CloseFont(body_font);
    TTF_CloseFont(hint_font);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
