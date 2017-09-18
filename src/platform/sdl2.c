#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

#include "../global.h"
#include "../gameboy.h"
#include "platform.h"

#define max(a, b) ((a) > (b) ? (a) : (b))

static SDL_Window *window = NULL;
static SDL_Palette *palette;
static SDL_Surface *winSurface;
static SDL_Surface *frameBufferSurface;
static unsigned long int ticks = 0;

void platform_fatal_error(char *fmt, ...)
{
    va_list args;
    char buffer[1000];
    
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    fputs(buffer, stderr);
    fputc('\n', stderr);
    fflush(stderr);
    //SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", buffer, window);
    va_end(args);
    exit(1);
}

//------------------------------------------------------------------------------
// GFX
//------------------------------------------------------------------------------

#define NUM_TILES 256
//#define TILE_ATLAS_WIDTH (8 * NUM_TILES)
//#define TILE_ATLAS_HEIGHT (8 * 2)

static SDL_Surface *bgTilePatternSurface = NULL;
static int bgTilePattern;
static int bgTileMap;

uint8_t *platform_get_framebuffer(void)
{
    SDL_LockSurface(frameBufferSurface);
    return frameBufferSurface->pixels;
}

void platform_draw_done(void)
{
    SDL_Rect dstRect = {0, 0, GB_DISPLAY_WIDTH, GB_DISPLAY_HEIGHT};
    
    SDL_UnlockSurface(frameBufferSurface);
    SDL_BlitSurface(frameBufferSurface, NULL, winSurface, &dstRect);
    SDL_UpdateWindowSurface(window);
}

static void render(void)
{
    struct OamEntry *sprite = (struct OamEntry *)oam;
    unsigned int x, y;
    void *tileMap;
    
    if (REG_LCDC & (1 << 3))
        tileMap = vram + 0x1C00;
    else
        tileMap = vram + 0x1800;

    if (REG_LCDC & (1 << 4))
    {
        uint8_t *pTileNum = tileMap;
        
        for (y = 0; y < 32; y++)
        {
            for (x = 0; x < 32; x++)
            {
                SDL_Rect srcRect = {*pTileNum * 8, 0, 8, 8};
                SDL_Rect dstRect = {x * 8, y * 8, 8, 8};
                
                //dstRect.x += REG_SCX;
                SDL_BlitSurface(bgTilePatternSurface, &srcRect, winSurface, &dstRect);
                pTileNum++;
            }
        }
    }
    else
    {
        int8_t *pTileNum = tileMap;
        
        for (y = 0; y < 32; y++)
        {
            for (x = 0; x < 32; x++)
            {
                SDL_Rect srcRect = {(128 + *pTileNum) * 8, 8, 8, 8};
                SDL_Rect dstRect = {x * 8, y * 8, 8, 8};
                
                //dstRect.x += REG_SCX;
                SDL_BlitSurface(bgTilePatternSurface, &srcRect, winSurface, &dstRect);
                pTileNum++;
            }
        }
    }
    
    if (REG_LCDC & (1 << 5))
    {
        if (REG_LCDC & (1 << 6))
            tileMap = vram + 0x1C00;
        else
            tileMap = vram + 0x1800;
        
        if (REG_LCDC & (1 << 4))
        {
            uint8_t *pTileNum = tileMap;
            
            for (y = 0; y < 32; y++)
            {
                for (x = 0; x < 32; x++)
                {
                    SDL_Rect srcRect = {*pTileNum * 8, 0, 8, 8};
                    SDL_Rect dstRect = {x * 8, y * 8, 8, 8};
                    
                    dstRect.x += REG_WX - 7;
                    dstRect.y += REG_WY;
                    //dstRect.x += REG_SCX;
                    SDL_BlitSurface(bgTilePatternSurface, &srcRect, winSurface, &dstRect);
                    pTileNum++;
                }
            }
        }
        else
        {
            int8_t *pTileNum = tileMap;
            
            for (y = 0; y < 32; y++)
            {
                for (x = 0; x < 32; x++)
                {
                    SDL_Rect srcRect = {(128 + *pTileNum) * 8, 8, 8, 8};
                    SDL_Rect dstRect = {x * 8, y * 8, 8, 8};
                    
                    //dstRect.x += REG_SCX;
                    dstRect.x += REG_WX -7;
                    dstRect.y += REG_WY;
                    SDL_BlitSurface(bgTilePatternSurface, &srcRect, winSurface, &dstRect);
                    pTileNum++;
                }
            }
        }
    }
    
    // Draw Sprites
    for (unsigned int i = 0; i < 40; i++)
    {
        if (sprite->x != 0 || sprite->y != 0)
        {
            SDL_Rect srcRect = {sprite->tileNum * 8, 0, 8, 8};
            SDL_Rect dstRect = {sprite->x - 8, sprite->y - 16, 8, 8};
            
            /*
            if (sprite->flags & (1 << 5))
            {
                srcRect.x += 8;
                srcRect.w = -8;
            }
            */
            SDL_BlitSurface(bgTilePatternSurface, &srcRect, winSurface, &dstRect);
        }
        sprite++;
    }
    SDL_UpdateWindowSurface(window);
}

static void render_debug(void)
{
    int x, y;
    int tileNum;
    
    tileNum = 0;        
    for (y = 0; ; y++)
    {
        for (x = 0; x < 16; x++)
        {
            SDL_Rect srcRect = {tileNum * 8, 0, 8, 8};
            SDL_Rect dstRect = {x * 8, y * 8, 8, 8};
            SDL_BlitSurface(bgTilePatternSurface, &srcRect, winSurface, &dstRect);
            tileNum++;
            if (tileNum == NUM_TILES)
                goto done_1;
        }
    }
  done_1:
    
    tileNum = 0;
    for (y = 0; ; y++)
    {
        for (x = 0; x < 16; x++)
        {
            SDL_Rect srcRect = {tileNum * 8, 8, 8, 8};
            SDL_Rect dstRect = {x * 8, (y + 64) * 8, 8, 8};
            SDL_BlitSurface(bgTilePatternSurface, &srcRect, winSurface, &dstRect);
            tileNum++;
            if (tileNum == NUM_TILES)
                goto done_2;
        }
    }
  done_2:
    SDL_UpdateWindowSurface(window);
}

static void render_debug2(void)
{
    SDL_Rect dstRect = {0, 0, NUM_TILES * 8, 16};
    
    SDL_BlitSurface(bgTilePatternSurface, NULL, winSurface, &dstRect);
    SDL_UpdateWindowSurface(window);
}

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------

int main(int argc, char **argv)
{
    if (argc < 2)
        platform_fatal_error("No ROM file specified.");
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        platform_fatal_error("Failed to initialize SDL: %s", SDL_GetError());
    window = SDL_CreateWindow(APPNAME,
      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, GB_DISPLAY_WIDTH, GB_DISPLAY_HEIGHT, SDL_WINDOW_RESIZABLE);
    if (window == NULL)
        platform_fatal_error("Failed to create window: %s", SDL_GetError());
    winSurface = SDL_GetWindowSurface(window);
    palette = SDL_AllocPalette(256);
    if (palette == NULL)
        platform_fatal_error("Failed to create palette: %s", SDL_GetError());
    palette->colors[0] = (SDL_Color){255, 255, 255, 255};
    palette->colors[1] = (SDL_Color){160, 160, 160, 255};
    palette->colors[2] = (SDL_Color){80,  80,  80,  255};
    palette->colors[3] = (SDL_Color){0,   0,   0,   255};
    frameBufferSurface = SDL_CreateRGBSurface(0, GB_DISPLAY_WIDTH, GB_DISPLAY_HEIGHT,
      8, 0, 0, 0, 0);
    if (frameBufferSurface == NULL)
        platform_fatal_error("Failed to create surface: %s", SDL_GetError());
    if (SDL_SetSurfacePalette(frameBufferSurface, palette) != 0)
        platform_fatal_error("Failed to set palette: %s", SDL_GetError());
    if (!gameboy_load_rom(argv[1]))
        platform_fatal_error("Failed to load ROM '%s'", argv[1]);
    
    while (1)
    {
        SDL_Event event;
        
        SDL_PollEvent(&event);
        switch (event.type)
        {
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym)
                {
                    case SDLK_UP:
                        gameboy_joypad_press(KEY_DPAD_UP);
                        break;
                    case SDLK_DOWN:
                        gameboy_joypad_press(KEY_DPAD_DOWN);
                        break;
                    case SDLK_LEFT:
                        gameboy_joypad_press(KEY_DPAD_LEFT);
                        break;
                    case SDLK_RIGHT:
                        gameboy_joypad_press(KEY_DPAD_RIGHT);
                        break;
                    case SDLK_c:
                        gameboy_joypad_press(KEY_A_BUTTON);
                        break;
                    case SDLK_x:
                        gameboy_joypad_press(KEY_B_BUTTON);
                        break;
                    case SDLK_RETURN:
                        gameboy_joypad_press(KEY_START_BUTTON);
                        break;
                    case SDLK_BACKSPACE:
                        gameboy_joypad_press(KEY_SELECT_BUTTON);
                        break;
                }
                break;
            case SDL_KEYUP:
                switch (event.key.keysym.sym)
                {
                    case SDLK_UP:
                        gameboy_joypad_release(KEY_DPAD_UP);
                        break;
                    case SDLK_DOWN:
                        gameboy_joypad_release(KEY_DPAD_DOWN);
                        break;
                    case SDLK_LEFT:
                        gameboy_joypad_release(KEY_DPAD_LEFT);
                        break;
                    case SDLK_RIGHT:
                        gameboy_joypad_release(KEY_DPAD_RIGHT);
                        break;
                    case SDLK_c:
                        gameboy_joypad_release(KEY_A_BUTTON);
                        break;
                    case SDLK_x:
                        gameboy_joypad_release(KEY_B_BUTTON);
                        break;
                    case SDLK_RETURN:
                        gameboy_joypad_release(KEY_START_BUTTON);
                        break;
                    case SDLK_BACKSPACE:
                        gameboy_joypad_release(KEY_SELECT_BUTTON);
                        break;
                }
                break;
            case SDL_WINDOWEVENT:
                switch (event.window.event)
                {
                    case SDL_WINDOWEVENT_RESIZED:
                        winSurface = SDL_GetWindowSurface(window);
                        break;
                    case SDL_WINDOWEVENT_CLOSE:
                        goto done;
                }
                break;
        }
        gameboy_run_frame();
        unsigned long int newTicks = SDL_GetTicks();
        if (newTicks < ticks + 1000 / 60)
        {
            printf("sleeping for %u\n", ticks + 1000 / 60 - newTicks);
            SDL_Delay(ticks + 1000 / 60 - newTicks);
        }
        ticks = SDL_GetTicks();
        //SDL_Delay(10);
        //render();
    }
  done:
    printf("PC = 0x%04X\n", regs.pc);
    
    return 0;
}
