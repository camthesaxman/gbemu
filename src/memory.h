#ifndef GUARD_MEMORY_H
#define GUARD_MEMORY_H

#define ADDR_TAC

#define ROM0_BASE 0x0000
#define ROM0_SIZE 0x4000
#define ROM1_BASE 0x4000
#define ROM1_SIZE 0x4000
#define VRAM_BASE 0x8000
#define VRAM_SIZE 0x2000
#define ERAM_BASE 0xA000
#define ERAM_SIZE 0x2000
#define IWRAM_BASE 0xC000
#define IWRAM_SIZE 0x2000
#define OAM_BASE 0xFE00  // This is really supposed to be from 0xFE00-0xFE9F, but
#define OAM_SIZE 0x0100  // some games (Tetris) write to the invalid memory above it
#define HRAM_BASE 0xFF80
#define HRAM_SIZE 0x007F
#define IO_BASE 0xFF00
#define IO_SIZE 0x0080
#define ECHO_BASE 0xE000
#define ECHO_SIZE 0x1E00
#define IE_ADDR 0xFFFF

#define REG_OFFSET_JOYP 0x00
#define REG_OFFSET_DIV  0x04
#define REG_OFFSET_TIMA 0x05
#define REG_OFFSET_TMA  0x06
#define REG_OFFSET_TAC  0x07

#define REG_OFFSET_NR10 0x10
#define REG_OFFSET_NR11 0x11
#define REG_OFFSET_NR12 0x12
#define REG_OFFSET_NR13 0x13
#define REG_OFFSET_NR14 0x14

#define REG_OFFSET_LCDC 0x40
#define REG_OFFSET_STAT 0x41
#define REG_OFFSET_SCY  0x42
#define REG_OFFSET_SCX  0x43
#define REG_OFFSET_LY   0x44
#define REG_OFFSET_LYC  0x45
#define REG_OFFSET_DMA  0x46
#define REG_OFFSET_BGP  0x47
#define REG_OFFSET_OBP0 0x48
#define REG_OFFSET_OBP1 0x49
#define REG_OFFSET_WY   0x4A
#define REG_OFFSET_WX   0x4B

#define REG_ADDR_JOYP (IO_BASE + REG_OFFSET_JOYP)
#define REG_ADDR_DIV  (IO_BASE + REG_OFFSET_DIV)
#define REG_ADDR_TIMA (IO_BASE + REG_OFFSET_TIMA)
#define REG_ADDR_TMA  (IO_BASE + REG_OFFSET_TMA)
#define REG_ADDR_TAC  (IO_BASE + REG_OFFSET_TAC)

#define REG_ADDR_NR10 (IO_BASE + REG_OFFSET_NR10)
#define REG_ADDR_NR11 (IO_BASE + REG_OFFSET_NR11)
#define REG_ADDR_NR12 (IO_BASE + REG_OFFSET_NR12)
#define REG_ADDR_NR13 (IO_BASE + REG_OFFSET_NR13)
#define REG_ADDR_NR14 (IO_BASE + REG_OFFSET_NR14)

#define REG_ADDR_LCDC (IO_BASE + REG_OFFSET_LCDC)
#define REG_ADDR_STAT (IO_BASE + REG_OFFSET_STAT)
#define REG_ADDR_SCY  (IO_BASE + REG_OFFSET_SCY)
#define REG_ADDR_SCX  (IO_BASE + REG_OFFSET_SCX)
#define REG_ADDR_BGP  (IO_BASE + REG_OFFSET_BGP)
#define REG_ADDR_OBP0 (IO_BASE + REG_OFFSET_OBP0)
#define REG_ADDR_OBP1 (IO_BASE + REG_OFFSET_OBP1)

#define REG_JOYP          io[REG_OFFSET_JOYP]
#define REG_DIV           io[REG_OFFSET_DIV]
#define REG_TIMA          io[REG_OFFSET_TIMA]
#define REG_TMA           io[REG_OFFSET_TMA]
#define REG_TAC           io[REG_OFFSET_TAC]

#define REG_NR10          io[REG_OFFSET_NR10]
#define REG_NR11          io[REG_OFFSET_NR11]
#define REG_NR12          io[REG_OFFSET_NR12]
#define REG_NR13          io[REG_OFFSET_NR13]
#define REG_NR14          io[REG_OFFSET_NR14]

#define REG_LCDC          io[REG_OFFSET_LCDC]
#define REG_STAT          io[REG_OFFSET_STAT]
#define REG_SCY           io[REG_OFFSET_SCY]
#define REG_SCX           io[REG_OFFSET_SCX]
#define REG_LY            io[REG_OFFSET_LY]
#define REG_LYC           io[REG_OFFSET_LYC]
#define REG_DMA           io[REG_OFFSET_DMA]
#define REG_BGP           io[REG_OFFSET_BGP]
#define REG_OBP0          io[REG_OFFSET_OBP0]
#define REG_OBP1          io[REG_OFFSET_OBP1]
#define REG_WY            io[REG_OFFSET_WY]
#define REG_WX            io[REG_OFFSET_WX]

#define REG_IF io[0xF]

#define INTR_FLAG_VBLANK (1 << 0)
#define INTR_FLAG_LCDC   (1 << 1)
#define INTR_FLAG_TIMER  (1 << 2)
#define INTR_FLAG_SERIAL (1 << 3)
#define INTR_FLAG_JOYPAD (1 << 4)

struct OamEntry
{
    uint8_t y;
    uint8_t x;
    uint8_t tileNum;
    uint8_t flags;
} ATTRIBUTE_PACKED;

#define OBJ_FLAG_PAL   (1 << 4)
#define OBJ_FLAG_XFLIP (1 << 5)
#define OBJ_FLAG_YFLIP (1 << 6)

enum
{
    MAPPER_UNKNOWN,
    MAPPER_NONE,
    MAPPER_MBC1,
    MAPPER_MBC2,
    MAPPER_MBC3,
    MAPPER_MBC5,
    MAPPER_MMM01,
};

extern uint8_t *gamePAK;
extern uint8_t *rom0;
extern uint8_t *rom1;
extern uint8_t vram[VRAM_SIZE];
extern uint8_t eram[ERAM_SIZE];
extern uint8_t iwram[IWRAM_SIZE];
extern uint8_t io[IO_SIZE];
extern uint8_t oam[OAM_SIZE];
extern uint8_t hram[HRAM_SIZE];
extern uint8_t ie;

void memory_initialize_mapper(void);
uint8_t memory_read_byte(uint16_t addr);
void memory_write_byte(uint16_t addr, uint8_t val);
uint16_t memory_read_word(uint16_t addr);
void memory_write_word(uint16_t addr, uint16_t val);
void memory_load_save_file(const char *filename);
void memory_save_save_file(const char *filename);

#endif  // GUARD_MEMORY_H
