#ifndef GUARD_GAMEBOY_H
#define GUARD_GAMEBOY_H

#define GB_DISPLAY_WIDTH 160
#define GB_DISPLAY_HEIGHT 144

struct Registers
{
    union
    {
        struct
        {
            uint8_t f;
            uint8_t a;
        };
        uint16_t af;
    };
    union
    {
        struct
        {
            uint8_t c;
            uint8_t b;
        };
        uint16_t bc;
    };
    union
    {
        struct
        {
            uint8_t e;
            uint8_t d;
        };
        uint16_t de;
    };
    union
    {
        struct
        {
            uint8_t l;
            uint8_t h;
        };
        uint16_t hl;
    };
    uint16_t sp;
    uint16_t pc;
};

extern struct Registers regs;

struct RomInfo
{
    char romFileName[256];
    char saveFileName[256];
    char gameTitle[17];
    bool logoCheck;
    uint8_t cartridgeType;
    bool isGameBoyColor;
    uint16_t ramSizeKbyte;
    uint8_t mapper;
    uint8_t cartridgeFlags;
};

#define CART_FLAG_RAM     (1 << 0)
#define CART_FLAG_SRAM    (1 << 1)
#define CART_FLAG_BATTERY (1 << 2)
#define CART_FLAG_RUMBLE  (1 << 3)
#define CART_FLAG_TIMER   (1 << 4)

extern struct RomInfo gRomInfo;

#define KEY_A_BUTTON      (1 << 0)
#define KEY_B_BUTTON      (1 << 1)
#define KEY_SELECT_BUTTON (1 << 2)
#define KEY_START_BUTTON  (1 << 3)
#define KEY_DPAD_RIGHT    (1 << 4)
#define KEY_DPAD_LEFT     (1 << 5)
#define KEY_DPAD_UP       (1 << 6)
#define KEY_DPAD_DOWN     (1 << 7)

extern uint8_t joypadState;

bool gameboy_load_rom(const char *filename);
void gameboy_close_rom(void);
void dump_regs(void);
void gameboy_run_frame(void);
void gameboy_joypad_press(unsigned int keys);
void gameboy_joypad_release(unsigned int keys);

#endif  // GUARD_GAMEBOY_H
