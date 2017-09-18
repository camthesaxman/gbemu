#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL/SDL.h>

#include "../gameboy.h"
#include "platform.h"

static SDL_Surface *winSurface;
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

static SDL_Surface *frameBufferSurface;

uint8_t *platform_get_framebuffer(void)
{
    if (SDL_MUSTLOCK(frameBufferSurface))
        SDL_LockSurface(frameBufferSurface);
    return frameBufferSurface->pixels;
}

void platform_draw_done(void)
{
    if (SDL_MUSTLOCK(frameBufferSurface))
        SDL_UnlockSurface(frameBufferSurface);
    SDL_BlitSurface(frameBufferSurface, NULL, winSurface, NULL);
    SDL_UpdateRect(winSurface, 0, 0, 0, 0);
}

int main(int argc, char **argv)
{
    SDL_Color colors[4];
    
    if (argc < 2)
        platform_fatal_error("No ROM file specified.");
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        platform_fatal_error("Failed to initialize SDL: %s", SDL_GetError());
    winSurface = SDL_SetVideoMode(GB_DISPLAY_WIDTH, GB_DISPLAY_HEIGHT, 0, SDL_ANYFORMAT);
    if (winSurface == NULL)
        platform_fatal_error("Failed to create window: %s", SDL_GetError());
    SDL_WM_SetCaption("Bad Boy Emulator", NULL);
    colors[0] = (SDL_Color){255, 255, 255, 255};
    colors[1] = (SDL_Color){160, 160, 160, 255};
    colors[2] = (SDL_Color){80,  80,  80,  255};
    colors[3] = (SDL_Color){0,   0,   0,   255};
    frameBufferSurface = SDL_CreateRGBSurface(0, GB_DISPLAY_WIDTH, GB_DISPLAY_HEIGHT,
      8, 0, 0, 0, 0);
    if (frameBufferSurface == NULL)
        platform_fatal_error("Failed to create surface: %s", SDL_GetError());
    if (SDL_SetColors(frameBufferSurface, colors, 0, 4) == 0)
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
          case SDL_QUIT:
            goto done;
        }
        gameboy_run_frame();
        unsigned long int newTicks = SDL_GetTicks();
        if (newTicks < ticks + 1000 / 60)
            SDL_Delay(ticks + 1000 / 60 - newTicks);
        ticks = SDL_GetTicks();
        //SDL_Delay(10);
        //render();
    }
  done:
    printf("PC = 0x%04X\n", regs.pc);
    
    return 0;
}
