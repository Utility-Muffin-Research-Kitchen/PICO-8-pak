/* Small controller-dismissable setup screen. Uses Leaf's inherited font. */
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>

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
    TTF_Font *font = TTF_OpenFont(font_path, 28);
    if (!font) return 1;
    SDL_Window *window = SDL_CreateWindow("PICO-8 setup", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 960, 720, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, 0) : NULL;
    if (!renderer) return 1;
    SDL_RenderSetLogicalSize(renderer, 960, 720);
    char message[2048];
    snprintf(message, sizeof(message), "PICO-8 for Leaf\n\n%s\n\nPress A or B to return to Leaf.", argv[1]);
    SDL_Color color = {240, 240, 240, 255};
    SDL_Surface *surface = TTF_RenderUTF8_Blended_Wrapped(font, message, color, 820);
    SDL_Texture *texture = surface ? SDL_CreateTextureFromSurface(renderer, surface) : NULL;
    if (!texture) return 1;
    SDL_Rect area = {70, 100, surface->w, surface->h};
    SDL_FreeSurface(surface);
    SDL_GameController *pads[4] = {0};
    int count = 0;
    for (int i = 0; i < SDL_NumJoysticks() && count < 4; i++) {
        if (SDL_IsGameController(i)) pads[count++] = SDL_GameControllerOpen(i);
    }
    SDL_SetRenderDrawColor(renderer, 20, 26, 22, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, &area);
    SDL_RenderPresent(renderer);
    SDL_Event event;
    while (SDL_WaitEvent(&event)) {
        if (event.type == SDL_QUIT) break;
        if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_RETURN)) break;
        if (event.type == SDL_CONTROLLERBUTTONDOWN &&
            (event.cbutton.button == SDL_CONTROLLER_BUTTON_A || event.cbutton.button == SDL_CONTROLLER_BUTTON_B)) break;
    }
    for (int i = 0; i < count; i++) if (pads[i]) SDL_GameControllerClose(pads[i]);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
