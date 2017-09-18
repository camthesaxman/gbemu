#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "global.h"
#include "gameboy.h"
#include "gpu.h"
#include "memory.h"
#include "platform/platform.h"

#define LCDC_DISP_ENABLE (1 << 7)
#define LCDC_BG_TILE_DATA (1 << 4)
#define LCDC_BG_TILE_MAP (1 << 3)

uint32_t gpuClock;
bool gpuFrameDone;
static uint8_t *frameBuffer;
static const void *screenPalette;
static unsigned int screenBytesPerPixel;
static uint8_t screenTileData[384][8][8];
static void (*gpuFunc)(void);

static void gpu_state_oam_search(void);
static void gpu_state_data_transfer(void);
static void gpu_state_hblank(void);
static void gpu_state_vblank(void);

// TODO: Optimize this

static void render_scanline(unsigned int scanline)
{
    const uint8_t bgPalette[] =
    {
        REG_BGP & 3,
        (REG_BGP >> 2) & 3,
        (REG_BGP >> 4) & 3,
        (REG_BGP >> 6) & 3,
    };
    const uint8_t objPalette0[] =
    {
        0,
        (REG_OBP0 >> 2) & 3,
        (REG_OBP0 >> 4) & 3,
        (REG_OBP0 >> 6) & 3,
    };
    const uint8_t objPalette1[] =
    {
        0,
        (REG_OBP1 >> 2) & 3,
        (REG_OBP1 >> 4) & 3,
        (REG_OBP1 >> 6) & 3,
    };
    uint8_t *const linePtr = frameBuffer + scanline * GB_DISPLAY_WIDTH;
    const uint8_t *bgTileMap = vram + ((REG_LCDC & (1 << 3)) ? 0x1C00 : 0x1800);
    const uint8_t *winTileMap = vram + ((REG_LCDC & (1 << 6)) ? 0x1C00 : 0x1800);
    unsigned int bgScrollX = REG_SCX;
    unsigned int bgScrollY = REG_SCY;
    unsigned int winPosX = REG_WX - 7;
    unsigned int winPosY = REG_WY;
    
    // Render Background
    if (REG_LCDC & (1 << 4))
    {
        unsigned int tileRow = ((scanline + bgScrollY) / 8) % 32;
        unsigned int tilePixelRow = (scanline + bgScrollY) % 8;
        const uint8_t *tileNums = bgTileMap + tileRow * 32;
        
        for (unsigned int screenX = 0, bgX = bgScrollX; screenX < GB_DISPLAY_WIDTH; screenX++, bgX++)
        {
            unsigned int tileCol = (bgX / 8) % 32;
            unsigned int tilePixelCol = bgX % 8;
            unsigned int tileNum = tileNums[tileCol];
            
            linePtr[screenX] = bgPalette[screenTileData[tileNum][tilePixelRow][tilePixelCol]];
        }
    }
    else
    {
        unsigned int tileRow = ((scanline + bgScrollY) / 8) % 32;
        unsigned int tilePixelRow = (scanline + bgScrollY) % 8;
        const int8_t *tileNums = (int8_t *)bgTileMap + tileRow * 32;
        
        for (unsigned int screenX = 0, bgX = bgScrollX; screenX < GB_DISPLAY_WIDTH; screenX++, bgX++)
        {
            unsigned int tileCol = (bgX / 8) % 32;
            unsigned int tilePixelCol = bgX % 8;
            unsigned int tileNum = 256 + tileNums[tileCol];
            
            linePtr[screenX] = bgPalette[screenTileData[tileNum][tilePixelRow][tilePixelCol]];
        }
    }
    
    // Render Window
    if ((REG_LCDC & (1 << 5)) && scanline >= winPosY)
    {   
        if (REG_LCDC & (1 << 4))
        {
            unsigned int tileRow = (scanline - winPosY) / 8;
            unsigned int tilePixelRow = (scanline - winPosY) % 8;
            const uint8_t *tileNums = winTileMap + tileRow * 32;
            
            for (unsigned int screenX = winPosX, winX = 0; screenX < GB_DISPLAY_WIDTH; screenX++, winX++)
            {
                unsigned int tileCol = winX / 8;
                unsigned int tilePixelCol = winX % 8;
                unsigned int tileNum = tileNums[tileCol];
                
                assert(tileCol < 32);
                linePtr[screenX] = screenTileData[tileNum][tilePixelRow][tilePixelCol];
            }
        }
        else
        {
            unsigned int tileRow = (scanline - winPosY) / 8;
            unsigned int tilePixelRow = (scanline - winPosY) % 8;
            const int8_t *tileNums = winTileMap + tileRow * 32;
            
            for (unsigned int screenX = winPosX, winX = 0; screenX < GB_DISPLAY_WIDTH; screenX++, winX++)
            {
                unsigned int tileCol = winX / 8;
                unsigned int tilePixelCol = winX % 8;
                unsigned int tileNum = 256 + tileNums[tileCol];
                
                assert(tileCol < 32);
                linePtr[screenX] = screenTileData[tileNum][tilePixelRow][tilePixelCol];
            }
        }
    }
    
    // Render Sprites
    if (REG_LCDC & (1 << 1))
    {
        struct OamEntry *sprite;
        unsigned int x = 0;
        unsigned int xMax = 8;
        
        for (int i = 39; i >= 0; i--)
        {
            int spriteX, spriteY;
            
            sprite = &((struct OamEntry *)oam)[i];
            spriteX = sprite->x - 8;
            spriteY = sprite->y - 16;
            if (REG_LCDC & (1 << 2))
            {
                // 8x16 sprites
                if ((int)scanline >= spriteY && (int)scanline < spriteY + 16
                  && spriteX + 8 > 0 && spriteX < GB_DISPLAY_WIDTH)
                {
                    unsigned int tilePixelRow = scanline - spriteY;
                    unsigned int tilePixelCol = 0;
                    unsigned int tileWidth = 8;
                    const uint8_t *palette = (sprite->flags & OBJ_FLAG_PAL) ? objPalette1 : objPalette0;
                    uint8_t tileNum;
                    uint8_t pixel;
                    
                    if (sprite->flags & (1 << 6))
                        tilePixelRow = 15 - tilePixelRow;
                    if (tilePixelRow < 8)
                        tileNum = sprite->tileNum & ~1;
                    else
                        tileNum = sprite->tileNum | 1;
                    tilePixelRow %= 8;
                    if (spriteX < 0)
                        tilePixelCol = 0 - spriteX;
                    else if (spriteX + 8 > GB_DISPLAY_WIDTH)
                    {
                        tileWidth = GB_DISPLAY_WIDTH - spriteX;
                        assert(spriteX + tileWidth == GB_DISPLAY_WIDTH);
                        if (tileWidth >= 8)
                        {
                            printf("spriteX = %i\n", spriteX);
                            printf("GB_DISPLAY_WIDTH = %i\n", GB_DISPLAY_WIDTH);
                            printf("tileWidth = %u\n", tileWidth);
                        }
                        assert(tileWidth < 8);
                    }
                    
                    if (sprite->flags & OBJ_FLAG_XFLIP)
                    {
                        for (; tilePixelCol < tileWidth; tilePixelCol++)
                        {
                            assert(spriteX + tilePixelCol < GB_DISPLAY_WIDTH);
                            pixel = screenTileData[tileNum][tilePixelRow][7 - tilePixelCol];
                            if (pixel != 0)
                                linePtr[spriteX + tilePixelCol] = palette[pixel];
                        }
                    }
                    else
                    {
                        for (; tilePixelCol < tileWidth; tilePixelCol++)
                        {
                            assert(spriteX + tilePixelCol < GB_DISPLAY_WIDTH);
                            pixel = screenTileData[tileNum][tilePixelRow][tilePixelCol];
                            if (pixel != 0)
                                linePtr[spriteX + tilePixelCol] = palette[pixel];
                        }
                    }
                }
            }
            else
            {
                // 8x8 sprites
                if ((int)scanline >= spriteY && (int)scanline < spriteY + 8
                 && spriteX + 8 > 0 && spriteX < GB_DISPLAY_WIDTH)
                {
                    unsigned int tilePixelRow = scanline - spriteY;
                    unsigned int tilePixelCol = 0;
                    unsigned int tileWidth = 8;
                    const uint8_t *palette = (sprite->flags & OBJ_FLAG_PAL) ? objPalette1 : objPalette0;
                    uint8_t pixel;
                    
                    assert(tilePixelRow < 8);
                    if (spriteX < 0)
                        tilePixelCol = 0 - spriteX;
                    else if (spriteX + 8 > GB_DISPLAY_WIDTH)
                    {
                        tileWidth = GB_DISPLAY_WIDTH - spriteX;
                        assert(tileWidth < 8);
                        assert(spriteX + tileWidth == GB_DISPLAY_WIDTH);
                    }
                    
                    if (sprite->flags & OBJ_FLAG_XFLIP)
                    {
                        for (; tilePixelCol < tileWidth; tilePixelCol++)
                        {
                            assert(spriteX + tilePixelCol < GB_DISPLAY_WIDTH);
                            pixel = screenTileData[sprite->tileNum][tilePixelRow][7 - tilePixelCol];
                            if (pixel != 0)
                                linePtr[spriteX + tilePixelCol] = palette[pixel];
                        }
                    }
                    else
                    {
                        for (; tilePixelCol < tileWidth; tilePixelCol++)
                        {
                            assert(spriteX + tilePixelCol < GB_DISPLAY_WIDTH);
                            pixel = screenTileData[sprite->tileNum][tilePixelRow][tilePixelCol];
                            if (pixel != 0)
                                linePtr[spriteX + tilePixelCol] = palette[pixel];
                        }
                    }
                }
            }
        }
    }
}

/*
static void render_scanline(unsigned int scanline)
{
    const uint8_t bgPalette[] =
    {
        REG_BGP & 3,
        (REG_BGP >> 2) & 3,
        (REG_BGP >> 4) & 3,
        (REG_BGP >> 6) & 3,
    };
    const uint8_t objPalette0[] =
    {
        0,
        (REG_OBP0 >> 2) & 3,
        (REG_OBP0 >> 4) & 3,
        (REG_OBP0 >> 6) & 3,
    };
    const uint8_t objPalette1[] =
    {
        0,
        (REG_OBP1 >> 2) & 3,
        (REG_OBP1 >> 4) & 3,
        (REG_OBP1 >> 6) & 3,
    };
    uint8_t *const linePtr = frameBuffer + scanline * GB_DISPLAY_WIDTH;
    const uint8_t *tileMap = vram + ((REG_LCDC & (1 << 3)) ? 0x1C00 : 0x1800);
    const uint8_t *winTileMap = vram + ((REG_LCDC & (1 << 6)) ? 0x1C00 : 0x1800);
    uint8_t *pix;
    unsigned int col;
    
    // Render background
    pix = linePtr;
    if (REG_LCDC & (1 << 4))
    {
        unsigned int tileRow = ((scanline + REG_SCY) / 8) % 32;
        const uint8_t *tileNums = tileMap + tileRow * 32;
        unsigned int tilePixelRow = (scanline + REG_SCY) % 8;
        const uint8_t *tilePatternTable = vram;
        int bit;
        uint8_t tileData1, tileData2;
        
        col = REG_SCX / 8;
        bit = 7 - (REG_SCX % 8);
        
        tileData1 = tilePatternTable[tileNums[col] * 16 + tilePixelRow * 2 + 0];
        tileData2 = tilePatternTable[tileNums[col] * 16 + tilePixelRow * 2 + 1];
        
        while (pix < linePtr + GB_DISPLAY_WIDTH)
        {
            *(pix++) = bgPalette[((tileData1 >> bit) & 1) | (((tileData2 >> bit) & 1) << 1)];
            bit--;
            if (bit < 0)
            {
                bit = 7;
                col = (col + 1) % 32;
                tileData1 = tilePatternTable[tileNums[col] * 16 + tilePixelRow * 2 + 0];
                tileData2 = tilePatternTable[tileNums[col] * 16 + tilePixelRow * 2 + 1];
            }
        }
    }
    else
    {
        unsigned int tileRow = ((scanline + REG_SCY) / 8) % 32;
        const int8_t *tileNums = (int8_t *)tileMap + tileRow * 32;
        unsigned int tilePixelRow = (scanline + REG_SCY) % 8;
        const uint8_t *tilePatternTable = vram + 0x800;
        int bit;
        uint8_t tileData1, tileData2;
        
        col = REG_SCX / 8;
        bit = 7 - (REG_SCX % 8);
        
        tileData1 = tilePatternTable[(128 + tileNums[col]) * 16 + tilePixelRow * 2 + 0];
        tileData2 = tilePatternTable[(128 + tileNums[col]) * 16 + tilePixelRow * 2 + 1];
        
        while (pix < linePtr + GB_DISPLAY_WIDTH)
        {
            *(pix++) = bgPalette[((tileData1 >> bit) & 1) | (((tileData2 >> bit) & 1) << 1)];
            bit--;
            if (bit < 0)
            {
                bit = 7;
                col = (col + 1) % 32;
                tileData1 = tilePatternTable[(128 + tileNums[col]) * 16 + tilePixelRow * 2 + 0];
                tileData2 = tilePatternTable[(128 + tileNums[col]) * 16 + tilePixelRow * 2 + 1];
            }
        }
    }
    
    // Render window
    pix = linePtr;
    if ((REG_LCDC & (1 << 5)) && scanline >= REG_WY)
    {
        //printf("x = %u, y = %u\n", REG_WX, REG_WY);
        if (REG_LCDC & (1 << 4))
        {
            const uint8_t *pTileNum = winTileMap + ((scanline - REG_WY) / 8) * 32;
            unsigned int tilePixelRow = (scanline - REG_WY) % 8;
            const uint8_t *tilePatternTable = vram;
            
            for (col = 0; col < 20; col++)
            {
                uint8_t tileData1 = tilePatternTable[*pTileNum * 16 + tilePixelRow * 2 + 0];
                uint8_t tileData2 = tilePatternTable[*pTileNum * 16 + tilePixelRow * 2 + 1];
                
                for (int i = 7; i >= 0; i--)
                {
                    *(pix++) = bgPalette[((tileData1 >> i) & 1) | (((tileData2 >> i) & 1) << 1)];
                }
                pTileNum++;
            }
        }
        else
        {
            const int8_t *pTileNum = (int8_t *)winTileMap + ((scanline - REG_WY) / 8) * 32;
            unsigned int tilePixelRow = (scanline - REG_WY) % 8;
            const uint8_t *tilePatternTable = vram + 0x800;
            
            for (col = 0; col < 20; col++)
            {
                uint8_t tileData1 = tilePatternTable[(128 + *pTileNum) * 16 + tilePixelRow * 2 + 0];
                uint8_t tileData2 = tilePatternTable[(128 + *pTileNum) * 16 + tilePixelRow * 2 + 1];
                
                for (int i = 7; i >= 0; i--)
                {
                    *(pix++) = bgPalette[((tileData1 >> i) & 1) | (((tileData2 >> i) & 1) << 1)];
                }
                pTileNum++;
            }
        }
    }
    
    // Render sprites
    for (unsigned int i = 0; i < 40; i++)
    {
        struct OamEntry *sprite = (struct OamEntry *)oam + i;
        
        if (sprite->x != 0 && sprite->y != 0)
        {
            if (REG_LCDC & 4)  // 8x16 sprites
            {
                if ((int)scanline >= sprite->y - 16 && (int)scanline < sprite->y - 16 + 16)
                {
                    unsigned int tilePixelRow = scanline - (sprite->y - 16);
                    unsigned int tileNum;
                    const uint8_t *palette;
                    
                    if (sprite->flags & (1 << 4))
                        palette = objPalette1;
                    else
                        palette = objPalette0;
                    if (sprite->flags & (1 << 6))
                        tilePixelRow = 15 - tilePixelRow;
                    if (tilePixelRow < 8)
                        tileNum = sprite->tileNum & ~1;
                    else
                        tileNum = (sprite->tileNum & ~1) + 1;
                    tilePixelRow %= 8;
                    uint8_t tileData1 = vram[tileNum * 16 + tilePixelRow * 2 + 0];
                    uint8_t tileData2 = vram[tileNum * 16 + tilePixelRow * 2 + 1];
                    uint8_t pixel;
                    
                    pix = linePtr + sprite->x - 8;
                    if (sprite->flags & (1 << 5))
                    {
                        for (int i = 0; i < 8 && pix < linePtr + GB_DISPLAY_WIDTH; i++)
                        {
                            pixel = ((tileData1 >> i) & 1) | (((tileData2 >> i) & 1) << 1);
                            if (pixel != 0)
                                *pix = palette[pixel];
                            pix++;                
                        }
                    }
                    else
                    {
                        for (int i = 7; i >= 0 && pix < linePtr + GB_DISPLAY_WIDTH; i--)
                        {
                            pixel = ((tileData1 >> i) & 1) | (((tileData2 >> i) & 1) << 1);
                            if (pixel != 0)
                                *pix = palette[pixel];
                            pix++;                
                        }
                    }
                }
            }
            else
            {
                if ((int)scanline >= sprite->y - 16 && (int)scanline < sprite->y - 16 + 8)
                {
                    unsigned int tilePixelRow = scanline - (sprite->y - 16);
                    if (sprite->flags & (1 << 6))
                        tilePixelRow = 7 - tilePixelRow;
                    uint8_t tileData1 = vram[sprite->tileNum * 16 + tilePixelRow * 2 + 0];
                    uint8_t tileData2 = vram[sprite->tileNum * 16 + tilePixelRow * 2 + 1];
                    uint8_t pixel;
                    const uint8_t *palette;
                    
                    if (sprite->flags & (1 << 4))
                        palette = objPalette1;
                    else
                        palette = objPalette0;
                    pix = linePtr + sprite->x - 8;
                    if (sprite->flags & (1 << 5))
                    {
                        for (int i = 0; i < 8 && pix < linePtr + GB_DISPLAY_WIDTH; i++)
                        {
                            pixel = ((tileData1 >> i) & 1) | (((tileData2 >> i) & 1) << 1);
                            if (pixel != 0)
                                *pix = palette[pixel];
                            pix++;                
                        }
                    }
                    else
                    {
                        for (int i = 7; i >= 0 && pix < linePtr + GB_DISPLAY_WIDTH; i--)
                        {
                            pixel = ((tileData1 >> i) & 1) | (((tileData2 >> i) & 1) << 1);
                            if (pixel != 0)
                                *pix = palette[pixel];
                            pix++;                
                        }
                    }
                }
            }
        }
    }
}
*/

static void gpu_state_oam_search(void)
{
    if (gpuClock >= 80)
    {
        gpuClock -= 80;    
        REG_STAT &= ~3;
        REG_STAT |= 3;   
        gpuFunc = gpu_state_data_transfer;
    }
}

static void gpu_state_data_transfer(void)
{
    if (gpuClock >= 172)
    {
        gpuClock -= 172;
        render_scanline(REG_LY);
        REG_STAT &= ~3;
        gpuFunc = gpu_state_hblank;
    }
}

static void gpu_state_hblank(void)
{
    if (gpuClock >= 204)
    {
        gpuClock -= 204;
        
        REG_LY++;
        if (REG_LY == 144)
        {            
            //if (interruptsEnabled && (ie & INTR_FLAG_VBLANK))
            {
                // Trigger VBLANK interrupt
                REG_IF |= INTR_FLAG_VBLANK;
            }
            REG_STAT &= ~3;
            REG_STAT |= 1;
            gpuFunc = gpu_state_vblank;
        }
        else
        {
            if ((REG_STAT & (1 << 6)) && (REG_LY == REG_LYC))
            {
                // Trigger LCDC interrupt
                REG_IF |= INTR_FLAG_LCDC;
            }
            REG_STAT &= ~3;
            REG_STAT |= 2;
            gpuFunc = gpu_state_oam_search;
        }
    }
}

static void gpu_state_vblank(void)
{
    if (gpuClock >= 456)
    {
        gpuClock -= 456;
        
        REG_LY++;
        if (REG_LY == 154)
        {
            REG_LY = 0;
            gpuFrameDone = true;
            return;
        }
    }
}

void gpu_handle_vram_write(uint16_t addr, uint8_t val)
{
    // Tile memory
    if (addr >= 0x8000 && addr <= 0x97FF)
    {
        unsigned int tileNum = (addr - 0x8000) / 16;
        uint8_t *vramData = vram + tileNum * 16;
        
        for (unsigned int y = 0; y < 8; y++)
        {
            uint8_t tileData1 = *(vramData++);
            uint8_t tileData2 = *(vramData++);
            
            for (unsigned int x = 0; x < 8; x++)
            {
                unsigned int bit = 7 - x;
                uint8_t pixel = ((tileData1 >> bit) & 1) | (((tileData2 >> bit) & 1) << 1);
                
                screenTileData[tileNum][y][x] = pixel;
            }
        }
    }
}

void gpu_set_screen_palette(unsigned int bytesPerPixel, const void *palette)
{
    assert(bytesPerPixel == 1 || bytesPerPixel == 2 || bytesPerPixel == 3);
    screenBytesPerPixel = bytesPerPixel;
    screenPalette = palette;
    
    // TODO: convert buffers
}

void gpu_frame_init(void)
{
    gpuFrameDone = false;
    frameBuffer = platform_get_framebuffer();
    REG_STAT &= ~3;
    REG_STAT |= 2;
    gpuFunc = gpu_state_oam_search;
}

void gpu_step(void)
{
    gpuFunc();
}
