#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "gameboy.h"
#include "gpu.h"
#include "memory.h"
#include "platform/platform.h"

struct Registers regs;

#define FLAG_BIT_C 4
#define FLAG_BIT_H 5
#define FLAG_BIT_N 6
#define FLAG_BIT_Z 7

#define FLAG_C (1 << 4)
#define FLAG_H (1 << 5)
#define FLAG_N (1 << 6)
#define FLAG_Z (1 << 7)
#define IS_FLAG_SET(n) (regs.f & (n))
#define SET_FLAG(n) (regs.f |= (n))
#define CLEAR_FLAG(n) (regs.f &= ~(n))

struct RomInfo gRomInfo;

static bool interruptsEnabled;
static bool cpuHalted;
static bool needUpdateTiles;

#define INTR_FLAG_VBLANK (1 << 0)
#define INTR_FLAG_LCDC   (1 << 1)
#define INTR_FLAG_TIMER  (1 << 2)
#define INTR_FLAG_SERIAL (1 << 3)
#define INTR_FLAG_JOYPAD (1 << 4)

uint8_t joypadState;
uint32_t cpuClock;
uint32_t gpuClock;
uint32_t timerClock;
uint32_t timerClock2;

static void initialize_cart_info(const char *filename)
{
    static const char *const mapperNames[] =
    {
        [MAPPER_UNKNOWN]  = "unknown",
        [MAPPER_NONE]     = "",
        [MAPPER_MBC1]     = "MBC1",
        [MAPPER_MBC2]     = "MBC2",
        [MAPPER_MBC3]     = "MBC3",
        [MAPPER_MBC5]     = "MBC5",
        [MAPPER_MMM01]    = "MMM01",
    };
    static const struct {uint8_t mapper; uint8_t flags;} cartridgeTypes[256] =
    {
        [0x00] = {MAPPER_NONE,  0},
        [0x01] = {MAPPER_MBC1,  0},
        [0x02] = {MAPPER_MBC1,  CART_FLAG_RAM},
        [0x03] = {MAPPER_MBC1,  CART_FLAG_RAM | CART_FLAG_BATTERY},
        [0x05] = {MAPPER_MBC2,  0},
        [0x06] = {MAPPER_MBC2,  CART_FLAG_BATTERY},
        [0x08] = {MAPPER_NONE,  CART_FLAG_RAM | CART_FLAG_BATTERY},
        [0x09] = {MAPPER_NONE,  CART_FLAG_RAM | CART_FLAG_BATTERY},
        [0x0B] = {MAPPER_MMM01, 0},
        [0x0C] = {MAPPER_MMM01, CART_FLAG_SRAM},
        [0x0D] = {MAPPER_MMM01, CART_FLAG_SRAM | CART_FLAG_BATTERY},
        [0x10] = {MAPPER_MBC3,  CART_FLAG_TIMER | CART_FLAG_RAM | CART_FLAG_BATTERY},
        [0x12] = {MAPPER_MBC3,  CART_FLAG_RAM},
        [0x13] = {MAPPER_MBC3,  CART_FLAG_RAM | CART_FLAG_BATTERY},
        [0x19] = {MAPPER_MBC5,  0},
        [0x1A] = {MAPPER_MBC5,  CART_FLAG_RAM},
        [0x1B] = {MAPPER_MBC5,  CART_FLAG_RAM | CART_FLAG_BATTERY},
        [0x1C] = {MAPPER_MBC5,  CART_FLAG_RUMBLE},
        [0x1D] = {MAPPER_MBC5,  CART_FLAG_RUMBLE | CART_FLAG_SRAM},
        [0x1E] = {MAPPER_MBC5,  CART_FLAG_RUMBLE | CART_FLAG_SRAM | CART_FLAG_BATTERY},
    };
    static const uint8_t ninLogo[] =
    {
        0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
        0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
        0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
    };
    static const uint16_t ramSizes[] = {0, 2, 8, 32, 128};
    
    strcpy(gRomInfo.romFileName, filename);
    memcpy(gRomInfo.gameTitle, gamePAK + 0x134, 16);
    gRomInfo.gameTitle[16] = '\0';
    gRomInfo.logoCheck = (memcmp(ninLogo, gamePAK + 0x104, sizeof(ninLogo)) == 0);
    gRomInfo.cartridgeType = gamePAK[0x147];
    gRomInfo.mapper = cartridgeTypes[gRomInfo.cartridgeType].mapper;
    gRomInfo.cartridgeFlags = cartridgeTypes[gRomInfo.cartridgeType].flags;
    gRomInfo.isGameBoyColor = (gamePAK[0x143] == 0x80);
    if (gamePAK[0x149] < 5)
        gRomInfo.ramSizeKbyte = ramSizes[gamePAK[0x149]];
    else
        gRomInfo.ramSizeKbyte = 0;
    
    dbg_puts("ROM INFO:");
    dbg_printf("File Name: '%s'\n", gRomInfo.romFileName);
    dbg_printf("Title: '%s'\n", gRomInfo.gameTitle);
    dbg_printf("Cartridge Type: 0x%02X ", gRomInfo.cartridgeType);
    if (gRomInfo.mapper == MAPPER_UNKNOWN)
        dbg_puts("(unknown)");
    else
    {
        uint8_t flags = gRomInfo.cartridgeFlags;
        
        dbg_fputs("(ROM", stdout);
        if (gRomInfo.mapper != MAPPER_NONE)
        {
            dbg_fputs("+", stdout);
            dbg_fputs(mapperNames[gRomInfo.mapper], stdout);
        }
        if (flags & CART_FLAG_RAM)
            dbg_fputs("+RAM", stdout);
        if (flags & CART_FLAG_BATTERY)
            dbg_fputs("+BATTERY", stdout);
        if (flags & CART_FLAG_SRAM)
            dbg_fputs("+SRAM", stdout);
        if (flags & CART_FLAG_RUMBLE)
            dbg_fputs("+RUMBLE", stdout);
        dbg_puts(")");
    }
    dbg_printf("Game Boy Color: %s\n", gRomInfo.isGameBoyColor ? "yes" : "no");
    dbg_printf("Nintendo Logo: %s\n", gRomInfo.logoCheck ? "OK" : "FAILED");
    dbg_printf("RAM size: %uKByte (%u)\n", gRomInfo.ramSizeKbyte, gamePAK[0x149]);
    
    if (gRomInfo.mapper == MAPPER_UNKNOWN)
        platform_fatal_error("Unknown cartridge type: 0x%02X", gRomInfo.cartridgeType);
    else if (gRomInfo.mapper == MAPPER_MBC2 || gRomInfo.mapper == MAPPER_MMM01)
        platform_fatal_error("Mapper %s is not supported", mapperNames[gRomInfo.mapper]);
    memory_initialize_mapper();
    if (gRomInfo.cartridgeFlags & CART_FLAG_BATTERY)
    {
        char *ext;
        
        strcpy(gRomInfo.saveFileName, gRomInfo.romFileName);
        ext = strrchr(gRomInfo.saveFileName, '.');
        if (ext == NULL)
            ext = gRomInfo.saveFileName + strlen(gRomInfo.saveFileName) - 1;
        if (ext + 5 < gRomInfo.saveFileName + sizeof(gRomInfo.saveFileName))
            strcpy(ext, ".sav");
        else
            platform_fatal_error("Cannot save. File name is too long.");
        memory_load_save_file(gRomInfo.saveFileName);
    }
}

bool gameboy_load_rom(const char *filename)
{
    FILE *file;
    size_t fileSize;

    file = fopen(filename, "rb");
    if (file == NULL)
        return false;
    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    gamePAK = malloc(fileSize);
    fseek(file, 0, SEEK_SET);
    fread(gamePAK, 1, fileSize, file);
    fclose(file);
    initialize_cart_info(filename);
    
    memset(vram, 0, sizeof(vram));
    memset(eram, 0, sizeof(eram));
    memset(iwram, 0, sizeof(iwram));
    memset(io, 0, sizeof(io));
    memset(oam, 0, sizeof(oam));
    memset(hram, 0, sizeof(hram));
    
    REG_TAC = 0xF8;
    REG_LCDC = 0x91;
    
    joypadState = 0;
    
    cpuClock = 0;
    gpuClock = 0;
    timerClock = 0;
    rom0 = gamePAK;
    rom1 = gamePAK + 0x4000;
    regs.af = 0x01B0;
    regs.bc = 0x0013;
    regs.de = 0x00D8;
    regs.hl = 0x014D;
    regs.sp = 0xFFFE;
    regs.pc = 0x100;
    
    interruptsEnabled = true;
    cpuHalted = false;
    needUpdateTiles = false;
    return true;
}

void gameboy_close_rom(void)
{
    if (gRomInfo.cartridgeFlags & CART_FLAG_BATTERY)
        memory_save_save_file(gRomInfo.saveFileName);
    free(gamePAK);
}

void dump_regs(void)
{
    uint16_t sp = regs.sp;
    
    puts("CPU Registers:");
    printf("AF = %02X%02X\n", regs.a, regs.f);
    printf("BC = %02X%02X\n", regs.b, regs.c);
    printf("DE = %02X%02X\n", regs.d, regs.e);
    printf("HL = %02X%02X\n", regs.h, regs.l);
    printf("SP = %04X\n", regs.sp);
    printf("PC = %04X\n", regs.pc);
    printf("IME = %s\n", interruptsEnabled ? "on" : "off");
    printf("IE = %02X\n", ie);
    printf("IF = %02X\n", 0xF0 | REG_IF);
    
    puts("Stack Trace:");
    for (unsigned int i = 0; i < 10 && sp < 0xDFFF; i++)
    {
        printf("0x%04X: 0x%04X\n", sp, memory_read_word(sp));
        sp += 2;
    }
    puts("IO Registers:");
    printf("TAC = %02X\n", REG_TAC);
    printf("TIMA = %02X\n", REG_TIMA);
    printf("DIV = %02X\n", REG_DIV);
}

void breakpoint(void)
{
    printf("BREAKPOINT AT 0x%04X\n", regs.pc);
    dump_regs();
    getc(stdin);
}

void gameboy_joypad_press(unsigned int keys)
{
    joypadState |= keys;
}

void gameboy_joypad_release(unsigned int keys)
{
    joypadState &= ~keys;
}

static void update_clocks(unsigned int val)
{
    cpuClock += val;
    gpuClock += val;
    if (REG_TAC & 4)
        timerClock += val;
    timerClock2 += val;
}

static inline uint8_t inc(uint8_t val)
{
    val++;
    regs.f = ((val == 0) << FLAG_BIT_Z)
           | (((val & 0xF) == 0) << FLAG_BIT_H)  // If lower nibble is zero (was 15) after the inc, then half-carry has occurred.
           | (regs.f & FLAG_C);
    return val;
}

static inline uint8_t dec(uint8_t val)
{
    val--;
    regs.f = ((val == 0) << FLAG_BIT_Z)
           | (1 << FLAG_BIT_N)
           | (((val & 0xF) == 0xF) << FLAG_BIT_H)  // If (lower nibble is 15 (was 0) after the dec, then half-carry has occurred.
           | (regs.f & FLAG_C);
    return val;
}

static inline void add(uint8_t val)
{
    unsigned int result = regs.a + val;
    
    regs.f = (((result & 0xFF) == 0) << FLAG_BIT_Z)
           | (((regs.a & 0xF) + (val & 0xF) > 0xF) << FLAG_BIT_H)
           | (((result & 0x100) != 0) << FLAG_BIT_C);
    regs.a = result;
}

static inline void adc(uint8_t val)
{
    unsigned int result = regs.a + val + ((regs.f & FLAG_C) != 0);
    
    regs.f = (((result & 0xFF) == 0) << FLAG_BIT_Z)
           | (((regs.a & 0xF) + (val & 0xF) > 0xF) << FLAG_BIT_H)
           | (((result & 0x100) != 0) << FLAG_BIT_C);
    regs.a = result;
}

static inline void sub(uint8_t val)
{
    uint8_t result = regs.a - val;
    
    regs.f = ((result == 0) << FLAG_BIT_Z)
           | FLAG_N
           | (((val & 0xF) > (regs.a & 0xF)) << FLAG_BIT_H)
           | ((val > regs.a) << FLAG_BIT_C);
    regs.a = result;
}

static inline void sbc(uint8_t val)
{
    unsigned int val2 = val + ((regs.f & FLAG_C) != 0);
    uint8_t result = regs.a - val2;
    
    regs.f = ((result == 0) << FLAG_BIT_Z)
           | FLAG_N
           | (((val2 & 0xF) > (regs.a & 0xF)) << FLAG_BIT_H)
           | ((val2 > regs.a) << FLAG_BIT_C);
    regs.a = result;
}

static inline void cp(uint8_t val)
{
    uint8_t result = regs.a - val;
    
    regs.f = ((result == 0) << FLAG_BIT_Z)
           | FLAG_N
           | (((val & 0xF) > (regs.a & 0xF)) << FLAG_BIT_H)
           | ((val > regs.a) << FLAG_BIT_C);
}

static inline void add_hl(uint16_t val)
{
    unsigned long int result = regs.hl + val;
    
    regs.f = (regs.f & FLAG_Z)
           | (((regs.hl & 0x0FFF) + (val & 0x0FFF) > 0x0FFF) << FLAG_BIT_H)
           | (((result & 0x10000) != 0) << FLAG_BIT_C);
    regs.hl = result;
}

static inline void push(uint16_t val)
{
    regs.sp -= 2;
    memory_write_word(regs.sp, val);
}

static inline uint16_t pop(void)
{
    uint16_t val = memory_read_word(regs.sp);

    regs.sp += 2;
    return val;
}

//------------------------------------------------------------------------------
// Load/Store Instructions
//------------------------------------------------------------------------------

#define GEN_INST_LD_REG_REG(regDst, regSrc)   \
static void FASTCALL inst_ld_##regDst##_##regSrc(void) \
{                                             \
    regs.regDst = regs.regSrc;                \
}
GEN_INST_LD_REG_REG(a, a)
GEN_INST_LD_REG_REG(a, b)
GEN_INST_LD_REG_REG(a, c)
GEN_INST_LD_REG_REG(a, d)
GEN_INST_LD_REG_REG(a, e)
GEN_INST_LD_REG_REG(a, h)
GEN_INST_LD_REG_REG(a, l)
GEN_INST_LD_REG_REG(b, a)
GEN_INST_LD_REG_REG(b, b)
GEN_INST_LD_REG_REG(b, c)
GEN_INST_LD_REG_REG(b, d)
GEN_INST_LD_REG_REG(b, e)
GEN_INST_LD_REG_REG(b, h)
GEN_INST_LD_REG_REG(b, l)
GEN_INST_LD_REG_REG(c, a)
GEN_INST_LD_REG_REG(c, b)
GEN_INST_LD_REG_REG(c, c)
GEN_INST_LD_REG_REG(c, d)
GEN_INST_LD_REG_REG(c, e)
GEN_INST_LD_REG_REG(c, h)
GEN_INST_LD_REG_REG(c, l)
GEN_INST_LD_REG_REG(d, a)
GEN_INST_LD_REG_REG(d, b)
GEN_INST_LD_REG_REG(d, c)
GEN_INST_LD_REG_REG(d, d)
GEN_INST_LD_REG_REG(d, e)
GEN_INST_LD_REG_REG(d, h)
GEN_INST_LD_REG_REG(d, l)
GEN_INST_LD_REG_REG(e, a)
GEN_INST_LD_REG_REG(e, b)
GEN_INST_LD_REG_REG(e, c)
GEN_INST_LD_REG_REG(e, d)
GEN_INST_LD_REG_REG(e, e)
GEN_INST_LD_REG_REG(e, h)
GEN_INST_LD_REG_REG(e, l)
GEN_INST_LD_REG_REG(h, a)
GEN_INST_LD_REG_REG(h, b)
GEN_INST_LD_REG_REG(h, c)
GEN_INST_LD_REG_REG(h, d)
GEN_INST_LD_REG_REG(h, e)
GEN_INST_LD_REG_REG(h, h)
GEN_INST_LD_REG_REG(h, l)
GEN_INST_LD_REG_REG(l, a)
GEN_INST_LD_REG_REG(l, b)
GEN_INST_LD_REG_REG(l, c)
GEN_INST_LD_REG_REG(l, d)
GEN_INST_LD_REG_REG(l, e)
GEN_INST_LD_REG_REG(l, h)
GEN_INST_LD_REG_REG(l, l)

#define GEN_INST_LD_REG_ADDRHL(regDst)          \
static void FASTCALL inst_ld_##regDst##_addrhl(void)     \
{                                            \
    regs.regDst = memory_read_byte(regs.hl); \
}
GEN_INST_LD_REG_ADDRHL(a)
GEN_INST_LD_REG_ADDRHL(b)
GEN_INST_LD_REG_ADDRHL(c)
GEN_INST_LD_REG_ADDRHL(d)
GEN_INST_LD_REG_ADDRHL(e)
GEN_INST_LD_REG_ADDRHL(h)
GEN_INST_LD_REG_ADDRHL(l)

#define GEN_INST_LD_ADDRHL_REG(regSrc)          \
static void FASTCALL inst_ld_addrhl_##regSrc(void)       \
{                                            \
    memory_write_byte(regs.hl, regs.regSrc); \
}
GEN_INST_LD_ADDRHL_REG(a)
GEN_INST_LD_ADDRHL_REG(b)
GEN_INST_LD_ADDRHL_REG(c)
GEN_INST_LD_ADDRHL_REG(d)
GEN_INST_LD_ADDRHL_REG(e)
GEN_INST_LD_ADDRHL_REG(h)
GEN_INST_LD_ADDRHL_REG(l)

#define GEN_INST_LD_REG_IMM8(regDst)                 \
static void FASTCALL inst_ld_##regDst##_imm8(uint8_t operand) \
{                                                    \
    regs.regDst = operand;                           \
}
GEN_INST_LD_REG_IMM8(a)
GEN_INST_LD_REG_IMM8(b)
GEN_INST_LD_REG_IMM8(c)
GEN_INST_LD_REG_IMM8(d)
GEN_INST_LD_REG_IMM8(e)
GEN_INST_LD_REG_IMM8(h)
GEN_INST_LD_REG_IMM8(l)

static void FASTCALL inst_ld_addrhl_imm8(uint8_t operand)
{
    memory_write_byte(regs.hl, operand);
}

static void FASTCALL inst_ld_bc_imm16(uint16_t operand) {regs.bc = operand;}
static void FASTCALL inst_ld_de_imm16(uint16_t operand) {regs.de = operand;}
static void FASTCALL inst_ld_hl_imm16(uint16_t operand) {regs.hl = operand;}
static void FASTCALL inst_ld_sp_imm16(uint16_t operand) {regs.sp = operand;}

static void FASTCALL inst_ld_addrbc_a(void) {memory_write_byte(regs.bc, regs.a);}
static void FASTCALL inst_ld_addrde_a(void) {memory_write_byte(regs.de, regs.a);}

static void FASTCALL inst_ld_a_addrbc(void) {regs.a = memory_read_byte(regs.bc);}
static void FASTCALL inst_ld_a_addrde(void) {regs.a = memory_read_byte(regs.de);}

static void FASTCALL inst_ld_addrc_a(void)
{
    memory_write_byte(0xFF00 + regs.c, regs.a);
}

static void FASTCALL inst_ld_addr8_a(uint8_t operand)
{
    memory_write_byte(0xFF00 + operand, regs.a);
}

static void FASTCALL inst_ld_addr16_a(uint16_t operand)
{
    memory_write_byte(operand, regs.a);
}

static void FASTCALL inst_ld_a_addrc(void)
{
    regs.a = memory_read_byte(0xFF00 + regs.c);
}

static void FASTCALL inst_ld_a_addr8(uint8_t operand)
{
    regs.a = memory_read_byte(0xFF00 + operand);
}

static void FASTCALL inst_ld_a_addr16(uint16_t operand)
{
    regs.a = memory_read_byte(operand);
}

static void FASTCALL inst_ld_addr16_sp(uint16_t operand)
{
    memory_write_word(operand, regs.sp);
}

static void FASTCALL inst_ld_hl_sp_offs8(uint8_t operand)
{
    int8_t offset = operand;
    unsigned long int result = regs.sp + offset;
    
    regs.f = (((result & 0x10000) != 0) << FLAG_BIT_C)
           | (((regs.sp & 0x0FFF) + (offset & 0x0FFF) > 0x0FFF) << FLAG_BIT_H);
    regs.hl = result;
}

static void FASTCALL inst_ld_sp_hl(void)
{
    regs.sp = regs.hl;
}

static void FASTCALL inst_ld_a_inc_addrhl(void)
{
    regs.a = memory_read_byte(regs.hl);
    regs.hl++;
}

static void FASTCALL inst_ld_a_dec_addrhl(void)
{
    regs.a = memory_read_byte(regs.hl);
    regs.hl--;
}

static void FASTCALL inst_ld_inc_addrhl_a(void)
{
    memory_write_byte(regs.hl, regs.a);
    regs.hl++;
}

static void FASTCALL inst_ld_dec_addrhl_a(void)
{
    memory_write_byte(regs.hl, regs.a);
    regs.hl--;
}

static void FASTCALL inst_push_af(void) {push(regs.af);}
static void FASTCALL inst_push_bc(void) {push(regs.bc);}
static void FASTCALL inst_push_de(void) {push(regs.de);}
static void FASTCALL inst_push_hl(void) {push(regs.hl);}

static void FASTCALL inst_pop_af(void) {regs.af = pop() & 0xFFF0;}  // The lower bits of f must remain zero
static void FASTCALL inst_pop_bc(void) {regs.bc = pop();}
static void FASTCALL inst_pop_de(void) {regs.de = pop();}
static void FASTCALL inst_pop_hl(void) {regs.hl = pop();}

//------------------------------------------------------------------------------
// Arithmetic Instructions
//------------------------------------------------------------------------------

#define GEN_INST_ADD_A_REG(regSrc)    \
static void FASTCALL inst_add_a_##regSrc(void) \
{                                     \
    add(regs.regSrc);                 \
}
GEN_INST_ADD_A_REG(a)
GEN_INST_ADD_A_REG(b)
GEN_INST_ADD_A_REG(c)
GEN_INST_ADD_A_REG(d)
GEN_INST_ADD_A_REG(e)
GEN_INST_ADD_A_REG(h)
GEN_INST_ADD_A_REG(l)

static void FASTCALL inst_add_a_addrhl(void)
{
    add(memory_read_byte(regs.hl));
}

static void FASTCALL inst_add_a_imm8(uint8_t operand)
{
    add(operand);
}

static void FASTCALL inst_add_hl_bc(void) {add_hl(regs.bc);}
static void FASTCALL inst_add_hl_de(void) {add_hl(regs.de);}
static void FASTCALL inst_add_hl_hl(void) {add_hl(regs.hl);}
static void FASTCALL inst_add_hl_sp(void) {add_hl(regs.sp);}

static void FASTCALL inst_add_sp_imm8(uint8_t operand)
{
    int8_t offset = operand;
    
    printf("warning: flags not implemented for ADD SP, imm8, pc = %04X\n", regs.pc);
    regs.sp += offset;
}

#define GEN_INST_INC_REG(regDst)    \
static void FASTCALL inst_inc_##regDst(void) \
{                                   \
    regs.regDst = inc(regs.regDst); \
}
GEN_INST_INC_REG(a)
GEN_INST_INC_REG(b)
GEN_INST_INC_REG(c)
GEN_INST_INC_REG(d)
GEN_INST_INC_REG(e)
GEN_INST_INC_REG(h)
GEN_INST_INC_REG(l)

static void FASTCALL inst_inc_addrhl(void)
{
    memory_write_byte(regs.hl, inc(memory_read_byte(regs.hl)));
}

static void FASTCALL inst_inc_bc(void) {regs.bc++;}
static void FASTCALL inst_inc_de(void) {regs.de++;}
static void FASTCALL inst_inc_hl(void) {regs.hl++;}
static void FASTCALL inst_inc_sp(void) {regs.sp++;}

#define GEN_INST_DEC_REG(regDst)    \
static void FASTCALL inst_dec_##regDst(void) \
{                                   \
    regs.regDst = dec(regs.regDst); \
}
GEN_INST_DEC_REG(a)
GEN_INST_DEC_REG(b)
GEN_INST_DEC_REG(c)
GEN_INST_DEC_REG(d)
GEN_INST_DEC_REG(e)
GEN_INST_DEC_REG(h)
GEN_INST_DEC_REG(l)

static void FASTCALL inst_dec_addrhl(void)
{
    memory_write_byte(regs.hl, dec(memory_read_byte(regs.hl)));
}

static void FASTCALL inst_dec_bc(void) {regs.bc--;}
static void FASTCALL inst_dec_de(void) {regs.de--;}
static void FASTCALL inst_dec_hl(void) {regs.hl--;}
static void FASTCALL inst_dec_sp(void) {regs.sp--;}

#define GEN_INST_SUB_REG(regSrc)    \
static void FASTCALL inst_sub_##regSrc(void) \
{                                   \
    sub(regs.regSrc);               \
}
GEN_INST_SUB_REG(a)
GEN_INST_SUB_REG(b)
GEN_INST_SUB_REG(c)
GEN_INST_SUB_REG(d)
GEN_INST_SUB_REG(e)
GEN_INST_SUB_REG(h)
GEN_INST_SUB_REG(l)

static void FASTCALL inst_sub_addrhl(void)
{
    sub(memory_read_byte(regs.hl));
}

static void FASTCALL inst_sub_imm8(uint8_t operand)
{
    sub(operand);
}

#define GEN_INST_ADC_A_REG(regSrc)    \
static void FASTCALL inst_adc_a_##regSrc(void) \
{                                     \
    adc(regs.regSrc);                 \
}
GEN_INST_ADC_A_REG(a)
GEN_INST_ADC_A_REG(b)
GEN_INST_ADC_A_REG(c)
GEN_INST_ADC_A_REG(d)
GEN_INST_ADC_A_REG(e)
GEN_INST_ADC_A_REG(h)
GEN_INST_ADC_A_REG(l)

static void FASTCALL inst_adc_a_addrhl(void)
{
    adc(memory_read_byte(regs.hl));
}

static void FASTCALL inst_adc_a_imm8(uint8_t operand)
{
    adc(operand);
}

#define GEN_INST_SBC_REG(regSrc)      \
static void FASTCALL inst_sbc_a_##regSrc(void) \
{                                     \
    sbc(regs.regSrc);                 \
}
GEN_INST_SBC_REG(a)
GEN_INST_SBC_REG(b)
GEN_INST_SBC_REG(c)
GEN_INST_SBC_REG(d)
GEN_INST_SBC_REG(e)
GEN_INST_SBC_REG(h)
GEN_INST_SBC_REG(l)

static void FASTCALL inst_sbc_a_addrhl(void)
{
    sbc(memory_read_byte(regs.hl));
}

static void FASTCALL inst_sbc_a_imm8(uint8_t operand)
{
    sbc(operand);
}

#define GEN_INST_AND_REG(regSrc)           \
static void FASTCALL inst_and_##regSrc(void)        \
{                                          \
    regs.a &= regs.regSrc;                 \
    regs.f = ((regs.a == 0) << FLAG_BIT_Z) \
           | FLAG_H;                       \
}
GEN_INST_AND_REG(a)
GEN_INST_AND_REG(b)
GEN_INST_AND_REG(c)
GEN_INST_AND_REG(d)
GEN_INST_AND_REG(e)
GEN_INST_AND_REG(h)
GEN_INST_AND_REG(l)

static void FASTCALL inst_and_addrhl(void)
{
    regs.a &= memory_read_byte(regs.hl);
    regs.f = ((regs.a == 0) << FLAG_BIT_Z)
           | FLAG_H;
}

static void FASTCALL inst_and_imm8(uint8_t operand)
{
    regs.a &= operand;
    regs.f = ((regs.a == 0) << FLAG_BIT_Z)
           | FLAG_H;
}

#define GEN_INST_OR_REG(regSrc)             \
static void FASTCALL inst_or_##regSrc(void)          \
{                                           \
    regs.a |= regs.regSrc;                  \
    regs.f = ((regs.a == 0) << FLAG_BIT_Z); \
}
GEN_INST_OR_REG(a)
GEN_INST_OR_REG(b)
GEN_INST_OR_REG(c)
GEN_INST_OR_REG(d)
GEN_INST_OR_REG(e)
GEN_INST_OR_REG(h)
GEN_INST_OR_REG(l)

static void FASTCALL inst_or_addrhl(void)
{
    regs.a |= memory_read_byte(regs.hl);
    regs.f = ((regs.a == 0) << FLAG_BIT_Z);
}

static void FASTCALL inst_or_imm8(uint8_t operand)
{
    regs.a |= operand;
    regs.f = ((regs.a == 0) << FLAG_BIT_Z);
}

#define GEN_INST_XOR_REG(regSrc)            \
static void FASTCALL inst_xor_##regSrc(void)         \
{                                           \
    regs.a ^= regs.regSrc;                  \
    regs.f = ((regs.a == 0) << FLAG_BIT_Z); \
}
GEN_INST_XOR_REG(a)
GEN_INST_XOR_REG(b)
GEN_INST_XOR_REG(c)
GEN_INST_XOR_REG(d)
GEN_INST_XOR_REG(e)
GEN_INST_XOR_REG(h)
GEN_INST_XOR_REG(l)

static void FASTCALL inst_xor_addrhl(void)
{
    regs.a ^= memory_read_byte(regs.hl);
    regs.f = ((regs.a == 0) << FLAG_BIT_Z);
}

static void FASTCALL inst_xor_imm8(uint8_t operand)
{
    regs.a ^= operand;
    regs.f = (regs.a == 0) << FLAG_BIT_Z;
}

#define GEN_INST_CP_REG(regSrc)    \
static void FASTCALL inst_cp_##regSrc(void) \
{                                  \
    cp(regs.regSrc);               \
}
GEN_INST_CP_REG(a)
GEN_INST_CP_REG(b)
GEN_INST_CP_REG(c)
GEN_INST_CP_REG(d)
GEN_INST_CP_REG(e)
GEN_INST_CP_REG(h)
GEN_INST_CP_REG(l)

static void FASTCALL inst_cp_addrhl(void)
{
    cp(memory_read_byte(regs.hl));
}

static void FASTCALL inst_cp_imm8(uint8_t operand)
{
    cp(operand);
}

static void FASTCALL inst_cpl(void)
{
    regs.a = ~regs.a;
    regs.f |= FLAG_N | FLAG_H;
}

static void FASTCALL inst_scf(void)
{
    regs.f = (regs.f & FLAG_Z)
           | FLAG_C;
}

static void FASTCALL inst_ccf(void)
{
    regs.f = (regs.f & FLAG_Z)
           | ((regs.f ^ FLAG_C) & FLAG_C);
}

//------------------------------------------------------------------------------
// Rotate Instructions
//------------------------------------------------------------------------------

static void FASTCALL inst_rla(void)
{
    unsigned int old = regs.a;
    
    regs.a <<= 1;
    regs.a |= ((regs.f & FLAG_C) != 0);
    regs.f = ((regs.a == 0) << FLAG_BIT_Z)
           | (((old & 0x80) != 0) << FLAG_BIT_C);
}

static void FASTCALL inst_rra(void)
{
    unsigned int old = regs.a;
    
    regs.a = (((regs.f & FLAG_C) != 0) << 7) | (regs.a >> 1);
    regs.f = (old & 1) << FLAG_BIT_C;
}

static void FASTCALL inst_rlca(void)
{
    regs.f = (((regs.a & 0x80) != 0) << FLAG_BIT_C);
    regs.a = (regs.a << 1) | (regs.a >> 7);
    regs.f |= (regs.a == 0) << FLAG_BIT_Z;
}

static void FASTCALL inst_rrca(void)
{
    unsigned int old = regs.a;
    
    regs.a = (regs.a >> 1) | ((regs.a & 1) << 7);
    regs.f = (old & 1) << FLAG_BIT_C;
}

//------------------------------------------------------------------------------
// Jump/Call Instructions
//------------------------------------------------------------------------------

static void FASTCALL inst_jp_addr16(uint16_t operand)
{
    regs.pc = operand;
}

static void FASTCALL inst_jp_hl(void)
{
    regs.pc = regs.hl;
}

static void FASTCALL inst_jpz_addr16(uint16_t operand)
{
    if (regs.f & FLAG_Z)
    {
        regs.pc = operand;
        update_clocks(16);
    }
    else
    {
        update_clocks(12);
    }
}

static void FASTCALL inst_jpnz_addr16(uint16_t operand)
{
    if (!(regs.f & FLAG_Z))
    {
        regs.pc = operand;
        update_clocks(16);
    }
    else
    {
        update_clocks(12);
    }
}

static void FASTCALL inst_jpc_addr16(uint16_t operand)
{
    if (regs.f & FLAG_C)
    {
        regs.pc = operand;
        update_clocks(16);
    }
    else
    {
        update_clocks(12);
    }
}

static void FASTCALL inst_jpnc_addr16(uint16_t operand)
{
    if (!(regs.f & FLAG_C))
    {
        regs.pc = operand;
        update_clocks(16);
    }
    else
    {
        update_clocks(12);
    }
}

static void FASTCALL inst_jr_offs8(uint8_t operand)
{
    int8_t offset = operand;
    
    regs.pc += offset;
}

static void FASTCALL inst_jrz_offs8(uint8_t operand)
{
    if (regs.f & FLAG_Z)
    {
        int8_t offset = operand;
        
        regs.pc += offset;
        update_clocks(12);
    }
    else
    {
        update_clocks(8);
    }
}

static void FASTCALL inst_jrnz_offs8(uint8_t operand)
{
    if (!(regs.f & FLAG_Z))
    {
        int8_t offset = operand;
        
        regs.pc += offset;
        update_clocks(12);
    }
    else
    {
        update_clocks(8);
    }
}

static void FASTCALL inst_jrc_offs8(uint8_t operand)
{
    if (regs.f & FLAG_C)
    {
        int8_t offset = operand;
        
        regs.pc += offset;
        update_clocks(12);
    }
    else
    {
        update_clocks(8);
    }
}

static void FASTCALL inst_jrnc_offs8(uint8_t operand)
{
    if (!(regs.f & FLAG_C))
    {
        int8_t offset = operand;
        
        regs.pc += offset;
        update_clocks(12);
    }
    else
    {
        update_clocks(8);
    }
}

static void FASTCALL inst_call_addr16(uint16_t operand)
{
    push(regs.pc);
    regs.pc = operand;
}

static void FASTCALL inst_callz_addr16(uint16_t operand)
{
    if (regs.f & FLAG_Z)
    {
        push(regs.pc);
        regs.pc = operand;
        update_clocks(24);
    }
    else
    {
        update_clocks(12);
    }
}

static void FASTCALL inst_callnz_addr16(uint16_t operand)
{
    if (!(regs.f & FLAG_Z))
    {
        push(regs.pc);
        regs.pc = operand;
        update_clocks(24);
    }
    else
    {
        update_clocks(12);
    }
}

static void FASTCALL inst_callc_addr16(uint16_t operand)
{
    if (regs.f & FLAG_C)
    {
        push(regs.pc);
        regs.pc = operand;
        update_clocks(24);
    }
    else
    {
        update_clocks(12);
    }
}

static void FASTCALL inst_callnc_addr16(uint16_t operand)
{
    if (!(regs.f & FLAG_C))
    {
        push(regs.pc);
        regs.pc = operand;
        update_clocks(24);
    }
    else
    {
        update_clocks(12);
    }
}

static void FASTCALL inst_ret(void)
{
    regs.pc = pop();
}

static void FASTCALL inst_reti(void)
{
    regs.pc = pop();
    interruptsEnabled = true;
}

static void FASTCALL inst_retz(void)
{
    if (regs.f & FLAG_Z)
    {
        regs.pc = pop();
        update_clocks(20);
    }
    else
    {
        update_clocks(8);
    }
}

static void FASTCALL inst_retnz(void)
{
    if (!(regs.f & FLAG_Z))
    {
        regs.pc = pop();
        update_clocks(20);
    }
    else
    {
        update_clocks(8);
    }
}

static void FASTCALL inst_retc(void)
{
    if (regs.f & FLAG_C)
    {
        regs.pc = pop();
        update_clocks(20);
    }
    else
    {
        update_clocks(8);
    }
}

static void FASTCALL inst_retnc(void)
{
    if (!(regs.f & FLAG_C))
    {
        regs.pc = pop();
        update_clocks(20);
    }
    else
    {
        update_clocks(8);
    }
}

static void FASTCALL inst_rst_00(void)
{
    push(regs.pc);
    regs.pc = 0x0000;
}

static void FASTCALL inst_rst_08(void)
{
    push(regs.pc);
    regs.pc = 0x0008;
}

static void FASTCALL inst_rst_10(void)
{
    push(regs.pc);
    regs.pc = 0x0010;
}

static void FASTCALL inst_rst_18(void)
{
    push(regs.pc);
    regs.pc = 0x0018;
}

static void FASTCALL inst_rst_20(void)
{
    push(regs.pc);
    regs.pc = 0x0020;
}

static void FASTCALL inst_rst_28(void)
{
    push(regs.pc);
    regs.pc = 0x0028;
}

static void FASTCALL inst_rst_30(void)
{
    push(regs.pc);
    regs.pc = 0x0030;
}

static void FASTCALL inst_rst_38(void)
{
    push(regs.pc);
    regs.pc = 0x0038;
}

//------------------------------------------------------------------------------
// Special Instructions
//------------------------------------------------------------------------------

static void FASTCALL inst_nop(void)
{
}

static void FASTCALL inst_daa(void)
{
    if (regs.f & FLAG_N)
    {
        if (regs.f & FLAG_C)
            regs.a -= 0x60;
        if (regs.f & FLAG_H)
            regs.a -= 0x06;
    }
    else
    {
        if ((regs.f & FLAG_C) || (regs.a & 0xFF) > 0x99)
        {
            regs.a += 0x60;
            regs.f |= FLAG_C;
        }
        if ((regs.f & FLAG_H) || (regs.a & 0x0F) > 0x09)
            regs.a += 0x06;
    }
    regs.f &= ~(FLAG_H | FLAG_Z);  // Clear H and Z flags
    regs.f |= ((regs.a == 0) << FLAG_BIT_Z);  // Set Z flag is result is zero
}

static void FASTCALL inst_ei(void)
{
    interruptsEnabled = true;
}

static void FASTCALL inst_di(void)
{
    interruptsEnabled = false;
}

static void FASTCALL inst_halt(void)
{
    cpuHalted = true;
}

static void FASTCALL inst_stop(void)
{
    cpuHalted = true;
}

//------------------------------------------------------------------------------
// CB-Prefixed Instructions
//------------------------------------------------------------------------------

static void FASTCALL cbinst_rlc(uint8_t *dst)
{
    regs.f = ((*dst & 0x80) != 0) << FLAG_BIT_C;
    *dst = (*dst >> 7) | (*dst << 1);
    regs.f |= (*dst == 0) << FLAG_BIT_Z;
}

static void FASTCALL cbinst_rrc(uint8_t *dst)
{
    unsigned int old = *dst;
    
    *dst >>= 1;
    *dst |= (old & 1) << 7;
    regs.f = ((*dst == 0) << FLAG_BIT_Z)
           | ((old & 1) << FLAG_BIT_C);
}

static void FASTCALL cbinst_rl(uint8_t *dst)
{
    unsigned int old = *dst;
    
    *dst <<= 1;
    *dst |= ((regs.f & FLAG_C) != 0);
    regs.f = ((*dst == 0) << FLAG_BIT_Z)
           | (((old & 0x80) != 0) << FLAG_BIT_C);
}

static void FASTCALL cbinst_rr(uint8_t *dst)
{
    unsigned int old = *dst;
    
    *dst >>= 1;
    *dst |= ((regs.f & FLAG_C) != 0) << 7;
    regs.f = ((*dst == 0) << FLAG_BIT_Z)
           | ((old & 1) << FLAG_BIT_C);
}

static void FASTCALL cbinst_sla(uint8_t *dst)
{
    unsigned int result = *dst << 1;
    
    *dst = result;
    regs.f = ((*dst == 0) << FLAG_BIT_Z)
           | (((result & 0x100) != 0) << FLAG_BIT_C);
}

static void FASTCALL cbinst_sra(uint8_t *dst)
{
    regs.f = ((*dst & 1) << FLAG_BIT_C);
    *dst = (*dst & 0x80) | (*dst >> 1);
    regs.f |= ((*dst == 0) << FLAG_BIT_Z);
}

static void FASTCALL cbinst_swap(uint8_t *dst)
{
    unsigned int temp = *dst & 0xF;
    
    *dst >>= 4;
    *dst |= temp << 4;
    regs.f = ((*dst == 0) << FLAG_BIT_Z);
}

static void FASTCALL cbinst_srl(uint8_t *dst)
{
    regs.f = ((*dst & 1) << FLAG_BIT_C);
    *dst >>= 1;
    regs.f |= ((*dst == 0) << FLAG_BIT_Z);
}

#define GEN_CBINST_BIT(n)                         \
static void FASTCALL cbinst_bit_##n(uint8_t *dst)          \
{                                                 \
    regs.f = ((!(*dst & (1 << n))) << FLAG_BIT_Z) \
           | FLAG_H                               \
           | (regs.f & FLAG_C);                   \
}
GEN_CBINST_BIT(0)
GEN_CBINST_BIT(1)
GEN_CBINST_BIT(2)
GEN_CBINST_BIT(3)
GEN_CBINST_BIT(4)
GEN_CBINST_BIT(5)
GEN_CBINST_BIT(6)
GEN_CBINST_BIT(7)

#define GEN_CBINST_RES(n)                \
static void FASTCALL cbinst_res_##n(uint8_t *dst) \
{                                        \
    *dst &= ~(1 << n);                   \
}
GEN_CBINST_RES(0)
GEN_CBINST_RES(1)
GEN_CBINST_RES(2)
GEN_CBINST_RES(3)
GEN_CBINST_RES(4)
GEN_CBINST_RES(5)
GEN_CBINST_RES(6)
GEN_CBINST_RES(7)

#define GEN_CBINST_SET(n)                \
static void FASTCALL cbinst_set_##n(uint8_t *dst) \
{                                        \
    *dst |= 1 << n;                      \
}
GEN_CBINST_SET(0)
GEN_CBINST_SET(1)
GEN_CBINST_SET(2)
GEN_CBINST_SET(3)
GEN_CBINST_SET(4)
GEN_CBINST_SET(5)
GEN_CBINST_SET(6)
GEN_CBINST_SET(7)

struct CBInstruction
{
    // disassembly format string
    const char *mnemonic;
    void (FASTCALL *func)(uint8_t *);
};

const struct CBInstruction cbInstructionTable[32] =
{
    {"RLC ", cbinst_rlc},         {"RRC ", cbinst_rrc},
    {"RL ", cbinst_rl},     {"RR ", cbinst_rr},
    {"SLA ", cbinst_sla},    {"SRA ", cbinst_sra},
    {"SWAP ", cbinst_swap},   {"SRL ", cbinst_srl},
    {"BIT 0, ", cbinst_bit_0}, {"BIT 1, ", cbinst_bit_1},
    {"BIT 2, ", cbinst_bit_2}, {"BIT 3, ", cbinst_bit_3},
    {"BIT 4, ", cbinst_bit_4}, {"BIT 5, ", cbinst_bit_5},
    {"BIT 6, ", cbinst_bit_6}, {"BIT 7, ", cbinst_bit_7},
    {"RES 0, ", cbinst_res_0}, {"RES 1, ", cbinst_res_1},
    {"RES 2, ", cbinst_res_2}, {"RES 3, ", cbinst_res_3},
    {"RES 4, ", cbinst_res_4}, {"RES 5, ", cbinst_res_5},
    {"RES 6, ", cbinst_res_6}, {"RES 7, ", cbinst_res_7},
    {"SET 0, ", cbinst_set_0}, {"SET 1, ", cbinst_set_1},
    {"SET 2, ", cbinst_set_2}, {"SET 3, ", cbinst_set_3},
    {"SET 4, ", cbinst_set_4}, {"SET 5, ", cbinst_set_5},
    {"SET 6, ", cbinst_set_6}, {"SET 7, ", cbinst_set_7},
};
uint8_t *const cbDstTable[8] = {&regs.b, &regs.c, &regs.d, &regs.e, &regs.h, &regs.l, NULL,   &regs.a};
const char cbDstNames[8][5]  = {"B",     "C",     "D",     "E",     "H",     "L",     "(HL)", "A"};

static void FASTCALL inst_cbinst(uint8_t operand)
{
    const struct CBInstruction *instr = &cbInstructionTable[operand >> 3];
    uint8_t *dst = cbDstTable[operand & 7];
    unsigned int cycles = 8;
    
    if ((operand & 7) == 6)
    {
        uint8_t val = memory_read_byte(regs.hl);
        
        instr->func(&val);
        // Don't write the dst if it's a BIT instruction
        if (operand < 0x40 || operand >= 0x80)
            memory_write_byte(regs.hl, val);
        cycles = 16;
    }
    else
    {
        instr->func(dst);
    }
    update_clocks(cycles);
}

static void FASTCALL inst_unknown(void)
{
    uint16_t addr = regs.pc - 1;
    uint8_t opcode = memory_read_byte(addr);
    
    platform_fatal_error("Unknown opcode %02X at 0x%04X", opcode, addr);
}

struct Instruction
{
    // disassembly format string
    const char *mnemonic;
    // number of clock cycles used by instruction.
    // If this is zero, then it depends on whether or not the 
    // branch was taken, and the clock will be updated by the instruction itself.
    unsigned int cycles;
    unsigned int operandSize;
    union
    {
        void (*FASTCALL func0op)(void);
        void (*FASTCALL func1op)(uint8_t);
        void (*FASTCALL func2op)(uint16_t);
    };
};

#define FUNC_0_OP(f) .operandSize = 0, .func0op = f
#define FUNC_1_OP(f) .operandSize = 1, .func1op = f
#define FUNC_2_OP(f) .operandSize = 2, .func2op = f

static const struct Instruction instructionTable[256] =
{
    [0x00] = {"NOP",               4,  FUNC_0_OP(inst_nop)},
    [0x01] = {"LD BC, $%04X",      12, FUNC_2_OP(inst_ld_bc_imm16)},
    [0x02] = {"LD (BC), A",        8,  FUNC_0_OP(inst_ld_addrbc_a)},
    [0x03] = {"INC BC",            8,  FUNC_0_OP(inst_inc_bc)},
    [0x04] = {"INC B",             4,  FUNC_0_OP(inst_inc_b)},
    [0x05] = {"DEC B",             4,  FUNC_0_OP(inst_dec_b)},
    [0x06] = {"LD B, $%02X",       8,  FUNC_1_OP(inst_ld_b_imm8)},
    [0x07] = {"RLCA",              4,  FUNC_0_OP(inst_rlca)},
    [0x08] = {"LD ($%04X), SP",    20, FUNC_2_OP(inst_ld_addr16_sp)},
    [0x09] = {"ADD HL, BC",        8,  FUNC_0_OP(inst_add_hl_bc)},
    [0x0A] = {"LD A, (BC)",        8,  FUNC_0_OP(inst_ld_a_addrbc)},
    [0x0B] = {"DEC BC",            8,  FUNC_0_OP(inst_dec_bc)},
    [0x0C] = {"INC C",             4,  FUNC_0_OP(inst_inc_c)},
    [0x0D] = {"DEC C",             4,  FUNC_0_OP(inst_dec_c)},
    [0x0E] = {"LD C, $%02X",       8,  FUNC_1_OP(inst_ld_c_imm8)},
    [0x0F] = {"RRCA",              4,  FUNC_0_OP(inst_rrca)},
    [0x10] = {"STOP",              4,  FUNC_0_OP(inst_stop)},
    [0x11] = {"LD DE, $%04X",      12, FUNC_2_OP(inst_ld_de_imm16)},
    [0x12] = {"LD (DE), A",        8,  FUNC_0_OP(inst_ld_addrde_a)},
    [0x13] = {"INC DE",            8,  FUNC_0_OP(inst_inc_de)},
    [0x14] = {"INC D",             4,  FUNC_0_OP(inst_inc_d)},
    [0x15] = {"DEC D",             4,  FUNC_0_OP(inst_dec_d)},
    [0x16] = {"LD D, $%02X",       8,  FUNC_1_OP(inst_ld_d_imm8)},
    [0x17] = {"RLA",               4,  FUNC_0_OP(inst_rla)},
    [0x18] = {"JR %i",             12, FUNC_1_OP(inst_jr_offs8)},
    [0x19] = {"ADD HL, DE",        8,  FUNC_0_OP(inst_add_hl_de)},
    [0x1A] = {"LD A, (DE)",        8,  FUNC_0_OP(inst_ld_a_addrde)},
    [0x1B] = {"DEC DE",            8,  FUNC_0_OP(inst_dec_de)},
    [0x1C] = {"INC E",             4,  FUNC_0_OP(inst_inc_e)},
    [0x1D] = {"DEC E",             4,  FUNC_0_OP(inst_dec_e)},
    [0x1E] = {"LD E, $%02X",       8,  FUNC_1_OP(inst_ld_e_imm8)},
    [0x1F] = {"RRA",               4,  FUNC_0_OP(inst_rra)},
    [0x20] = {"JR NZ, %i",         0,  FUNC_1_OP(inst_jrnz_offs8)},
    [0x21] = {"LD HL, $%04X",      12, FUNC_2_OP(inst_ld_hl_imm16)},
    [0x22] = {"LD (HL+), A",       8,  FUNC_0_OP(inst_ld_inc_addrhl_a)},
    [0x23] = {"INC HL",            8,  FUNC_0_OP(inst_inc_hl)},
    [0x24] = {"INC H",             4,  FUNC_0_OP(inst_inc_h)},
    [0x25] = {"DEC H",             4,  FUNC_0_OP(inst_dec_h)},
    [0x26] = {"LD H, $%02X",       8,  FUNC_1_OP(inst_ld_h_imm8)},
    [0x27] = {"DAA",               4,  FUNC_0_OP(inst_daa)},
    [0x28] = {"JR Z, %i",          0,  FUNC_1_OP(inst_jrz_offs8)},
    [0x29] = {"ADD HL, HL",        8,  FUNC_0_OP(inst_add_hl_hl)},
    [0x2A] = {"LD A, (HL+)",       8,  FUNC_0_OP(inst_ld_a_inc_addrhl)},
    [0x2B] = {"DEC HL",            8,  FUNC_0_OP(inst_dec_hl)},
    [0x2C] = {"INC L",             4,  FUNC_0_OP(inst_inc_l)},
    [0x2D] = {"DEC L",             4,  FUNC_0_OP(inst_dec_l)},
    [0x2E] = {"LD L, $%02X",       8,  FUNC_1_OP(inst_ld_l_imm8)},
    [0x2F] = {"CPL",               4,  FUNC_0_OP(inst_cpl)},
    [0x30] = {"JR NC, %i",         0,  FUNC_1_OP(inst_jrnc_offs8)},
    [0x31] = {"LD SP, $%04X",      12, FUNC_2_OP(inst_ld_sp_imm16)},
    [0x32] = {"LD (HL-), A",       8,  FUNC_0_OP(inst_ld_dec_addrhl_a)},
    [0x33] = {"INC SP",            8,  FUNC_0_OP(inst_inc_sp)},
    [0x34] = {"INC (HL)",          12, FUNC_0_OP(inst_inc_addrhl)},
    [0x35] = {"DEC (HL)",          12, FUNC_0_OP(inst_dec_addrhl)},
    [0x36] = {"LD (HL), $%02X",    12, FUNC_1_OP(inst_ld_addrhl_imm8)},
    [0x37] = {"SCF",               4,  FUNC_0_OP(inst_scf)},
    [0x38] = {"JR C, %i",          0,  FUNC_1_OP(inst_jrc_offs8)},
    [0x39] = {"ADD HL, SP",        8,  FUNC_0_OP(inst_add_hl_sp)},
    [0x3A] = {"LD A, (HL-)",       8,  FUNC_0_OP(inst_ld_a_dec_addrhl)},
    [0x3B] = {"DEC SP",            8,  FUNC_0_OP(inst_dec_sp)},
    [0x3C] = {"INC A",             4,  FUNC_0_OP(inst_inc_a)},
    [0x3D] = {"DEC A",             4,  FUNC_0_OP(inst_dec_a)},
    [0x3E] = {"LD A, $%02X",       8,  FUNC_1_OP(inst_ld_a_imm8)},
    [0x3F] = {"CCF",               4,  FUNC_0_OP(inst_ccf)},
    [0x40] = {"LD B, B",           4,  FUNC_0_OP(inst_ld_b_b)},
    [0x41] = {"LD B, C",           4,  FUNC_0_OP(inst_ld_b_c)},
    [0x42] = {"LD B, D",           4,  FUNC_0_OP(inst_ld_b_d)},
    [0x43] = {"LD B, E",           4,  FUNC_0_OP(inst_ld_b_e)},
    [0x44] = {"LD B, H",           4,  FUNC_0_OP(inst_ld_b_h)},
    [0x45] = {"LD B, L",           4,  FUNC_0_OP(inst_ld_b_l)},
    [0x46] = {"LD B, (HL)",        8,  FUNC_0_OP(inst_ld_b_addrhl)},
    [0x47] = {"LD B, A",           4,  FUNC_0_OP(inst_ld_b_a)},
    [0x48] = {"LD C, B",           4,  FUNC_0_OP(inst_ld_c_b)},
    [0x49] = {"LD C, C",           4,  FUNC_0_OP(inst_ld_c_c)},
    [0x4A] = {"LD C, D",           4,  FUNC_0_OP(inst_ld_c_d)},
    [0x4B] = {"LD C, E",           4,  FUNC_0_OP(inst_ld_c_e)},
    [0x4C] = {"LD C, H",           4,  FUNC_0_OP(inst_ld_c_h)},
    [0x4D] = {"LD C, L",           4,  FUNC_0_OP(inst_ld_c_l)},
    [0x4E] = {"LD C, (HL)",        8,  FUNC_0_OP(inst_ld_c_addrhl)},
    [0x4F] = {"LD C, A",           4,  FUNC_0_OP(inst_ld_c_a)},
    [0x50] = {"LD D, B",           4,  FUNC_0_OP(inst_ld_d_b)},
    [0x51] = {"LD D, C",           4,  FUNC_0_OP(inst_ld_d_c)},
    [0x52] = {"LD D, D",           4,  FUNC_0_OP(inst_ld_d_d)},
    [0x53] = {"LD D, E",           4,  FUNC_0_OP(inst_ld_d_e)},
    [0x54] = {"LD D, H",           4,  FUNC_0_OP(inst_ld_d_h)},
    [0x55] = {"LD D, L",           4,  FUNC_0_OP(inst_ld_d_l)},
    [0x56] = {"LD D, (HL)",        8,  FUNC_0_OP(inst_ld_d_addrhl)},
    [0x57] = {"LD D, A",           4,  FUNC_0_OP(inst_ld_d_a)},
    [0x58] = {"LD E, B",           4,  FUNC_0_OP(inst_ld_e_b)},
    [0x59] = {"LD E, C",           4,  FUNC_0_OP(inst_ld_e_c)},
    [0x5A] = {"LD E, D",           4,  FUNC_0_OP(inst_ld_e_d)},
    [0x5B] = {"LD E, E",           4,  FUNC_0_OP(inst_ld_e_e)},
    [0x5C] = {"LD E, H",           4,  FUNC_0_OP(inst_ld_e_h)},
    [0x5D] = {"LD E, L",           4,  FUNC_0_OP(inst_ld_e_l)},
    [0x5E] = {"LD E, (HL)",        8,  FUNC_0_OP(inst_ld_e_addrhl)},
    [0x5F] = {"LD E, A",           4,  FUNC_0_OP(inst_ld_e_a)},
    [0x60] = {"LD H, B",           4,  FUNC_0_OP(inst_ld_h_b)},
    [0x61] = {"LD H, C",           4,  FUNC_0_OP(inst_ld_h_c)},
    [0x62] = {"LD H, D",           4,  FUNC_0_OP(inst_ld_h_d)},
    [0x63] = {"LD H, E",           4,  FUNC_0_OP(inst_ld_h_e)},
    [0x64] = {"LD H, H",           4,  FUNC_0_OP(inst_ld_h_h)},
    [0x65] = {"LD H, L",           4,  FUNC_0_OP(inst_ld_h_l)},
    [0x66] = {"LD H, (HL)",        8,  FUNC_0_OP(inst_ld_h_addrhl)},
    [0x67] = {"LD H, A",           4,  FUNC_0_OP(inst_ld_h_a)},
    [0x68] = {"LD L, B",           4,  FUNC_0_OP(inst_ld_l_b)},
    [0x69] = {"LD L, C",           4,  FUNC_0_OP(inst_ld_l_c)},
    [0x6A] = {"LD L, D",           4,  FUNC_0_OP(inst_ld_l_d)},
    [0x6B] = {"LD L, E",           4,  FUNC_0_OP(inst_ld_l_e)},
    [0x6C] = {"LD L, H",           4,  FUNC_0_OP(inst_ld_l_h)},
    [0x6D] = {"LD L, L",           4,  FUNC_0_OP(inst_ld_l_l)},
    [0x6E] = {"LD L, (HL)",        8,  FUNC_0_OP(inst_ld_l_addrhl)},
    [0x6F] = {"LD L, A",           4,  FUNC_0_OP(inst_ld_l_a)},
    [0x70] = {"LD (HL), B",        8,  FUNC_0_OP(inst_ld_addrhl_b)},
    [0x71] = {"LD (HL), C",        8,  FUNC_0_OP(inst_ld_addrhl_c)},
    [0x72] = {"LD (HL), D",        8,  FUNC_0_OP(inst_ld_addrhl_d)},
    [0x73] = {"LD (HL), E",        8,  FUNC_0_OP(inst_ld_addrhl_e)},
    [0x74] = {"LD (HL), H",        8,  FUNC_0_OP(inst_ld_addrhl_h)},
    [0x75] = {"LD (HL), L",        8,  FUNC_0_OP(inst_ld_addrhl_l)},
    [0x76] = {"HALT",              4,  FUNC_0_OP(inst_halt)},
    [0x77] = {"LD (HL), A",        8,  FUNC_0_OP(inst_ld_addrhl_a)},
    [0x78] = {"LD A, B",           4,  FUNC_0_OP(inst_ld_a_b)},
    [0x79] = {"LD A, C",           4,  FUNC_0_OP(inst_ld_a_c)},
    [0x7A] = {"LD A, D",           4,  FUNC_0_OP(inst_ld_a_d)},
    [0x7B] = {"LD A, E",           4,  FUNC_0_OP(inst_ld_a_e)},
    [0x7C] = {"LD A, H",           4,  FUNC_0_OP(inst_ld_a_h)},
    [0x7D] = {"LD A, L",           4,  FUNC_0_OP(inst_ld_a_l)},
    [0x7E] = {"LD A, (HL)",        8,  FUNC_0_OP(inst_ld_a_addrhl)},
    [0x7F] = {"LD A, A",           4,  FUNC_0_OP(inst_ld_a_a)},
    [0x80] = {"ADD A, B",          4,  FUNC_0_OP(inst_add_a_b)},
    [0x81] = {"ADD A, C",          4,  FUNC_0_OP(inst_add_a_c)},
    [0x82] = {"ADD A, D",          4,  FUNC_0_OP(inst_add_a_d)},
    [0x83] = {"ADD A, E",          4,  FUNC_0_OP(inst_add_a_e)},
    [0x84] = {"ADD A, H",          4,  FUNC_0_OP(inst_add_a_h)},
    [0x85] = {"ADD A, L",          4,  FUNC_0_OP(inst_add_a_l)},
    [0x86] = {"ADD A, (HL)",       8,  FUNC_0_OP(inst_add_a_addrhl)},
    [0x87] = {"ADD A, A",          4,  FUNC_0_OP(inst_add_a_a)},
    [0x88] = {"ADC A, B",          4,  FUNC_0_OP(inst_adc_a_b)},
    [0x89] = {"ADC A, C",          4,  FUNC_0_OP(inst_adc_a_c)},
    [0x8A] = {"ADC A, D",          4,  FUNC_0_OP(inst_adc_a_d)},
    [0x8B] = {"ADC A, E",          4,  FUNC_0_OP(inst_adc_a_e)},
    [0x8C] = {"ADC A, H",          4,  FUNC_0_OP(inst_adc_a_h)},
    [0x8D] = {"ADC A, L",          4,  FUNC_0_OP(inst_adc_a_l)},
    [0x8E] = {"ADC A, (HL)",       8,  FUNC_0_OP(inst_adc_a_addrhl)},
    [0x8F] = {"ADC A, A",          4,  FUNC_0_OP(inst_adc_a_a)},
    [0x90] = {"SUB B",             4,  FUNC_0_OP(inst_sub_b)},
    [0x91] = {"SUB C",             4,  FUNC_0_OP(inst_sub_c)},
    [0x92] = {"SUB D",             4,  FUNC_0_OP(inst_sub_d)},
    [0x93] = {"SUB E",             4,  FUNC_0_OP(inst_sub_e)},
    [0x94] = {"SUB H",             4,  FUNC_0_OP(inst_sub_h)},
    [0x95] = {"SUB L",             4,  FUNC_0_OP(inst_sub_l)},
    [0x96] = {"SUB (HL)",          8,  FUNC_0_OP(inst_sub_addrhl)},
    [0x97] = {"SUB A",             4,  FUNC_0_OP(inst_sub_a)},
    [0x98] = {"SBC A, B",          4,  FUNC_0_OP(inst_sbc_a_b)},
    [0x99] = {"SBC A, C",          4,  FUNC_0_OP(inst_sbc_a_c)},
    [0x9A] = {"SBC A, D",          4,  FUNC_0_OP(inst_sbc_a_d)},
    [0x9B] = {"SBC A, E",          4,  FUNC_0_OP(inst_sbc_a_e)},
    [0x9C] = {"SBC A, H",          4,  FUNC_0_OP(inst_sbc_a_h)},
    [0x9D] = {"SBC A, L",          4,  FUNC_0_OP(inst_sbc_a_l)},
    [0x9E] = {"SBC A, (HL)",       8,  FUNC_0_OP(inst_sbc_a_addrhl)},
    [0x9F] = {"SBC A, A",          4,  FUNC_0_OP(inst_sbc_a_a)},
    [0xA0] = {"AND B",             4,  FUNC_0_OP(inst_and_b)},
    [0xA1] = {"AND C",             4,  FUNC_0_OP(inst_and_c)},
    [0xA2] = {"AND D",             4,  FUNC_0_OP(inst_and_d)},
    [0xA3] = {"AND E",             4,  FUNC_0_OP(inst_and_e)},
    [0xA4] = {"AND H",             4,  FUNC_0_OP(inst_and_h)},
    [0xA5] = {"AND L",             4,  FUNC_0_OP(inst_and_l)},
    [0xA6] = {"AND (HL)",          8,  FUNC_0_OP(inst_and_addrhl)},
    [0xA7] = {"AND A",             4,  FUNC_0_OP(inst_and_a)},
    [0xA8] = {"XOR B",             4,  FUNC_0_OP(inst_xor_b)},
    [0xA9] = {"XOR C",             4,  FUNC_0_OP(inst_xor_c)},
    [0xAA] = {"XOR D",             4,  FUNC_0_OP(inst_xor_d)},
    [0xAB] = {"XOR E",             4,  FUNC_0_OP(inst_xor_e)},
    [0xAC] = {"XOR H",             4,  FUNC_0_OP(inst_xor_h)},
    [0xAD] = {"XOR L",             4,  FUNC_0_OP(inst_xor_l)},
    [0xAE] = {"XOR (HL)",          8,  FUNC_0_OP(inst_xor_addrhl)},
    [0xAF] = {"XOR A",             4,  FUNC_0_OP(inst_xor_a)},
    [0xB0] = {"OR B",              4,  FUNC_0_OP(inst_or_b)},
    [0xB1] = {"OR C",              4,  FUNC_0_OP(inst_or_c)},
    [0xB2] = {"OR D",              4,  FUNC_0_OP(inst_or_d)},
    [0xB3] = {"OR E",              4,  FUNC_0_OP(inst_or_e)},
    [0xB4] = {"OR H",              4,  FUNC_0_OP(inst_or_h)},
    [0xB5] = {"OR L",              4,  FUNC_0_OP(inst_or_l)},
    [0xB6] = {"OR (HL)",           8,  FUNC_0_OP(inst_or_addrhl)},
    [0xB7] = {"OR A",              4,  FUNC_0_OP(inst_or_a)},
    [0xB8] = {"CP B",              4,  FUNC_0_OP(inst_cp_b)},
    [0xB9] = {"CP C",              4,  FUNC_0_OP(inst_cp_c)},
    [0xBA] = {"CP D",              4,  FUNC_0_OP(inst_cp_d)},
    [0xBB] = {"CP E",              4,  FUNC_0_OP(inst_cp_e)},
    [0xBC] = {"CP H",              4,  FUNC_0_OP(inst_cp_h)},
    [0xBD] = {"CP L",              4,  FUNC_0_OP(inst_cp_l)},
    [0xBE] = {"CP (HL)",           8,  FUNC_0_OP(inst_cp_addrhl)},
    [0xBF] = {"CP A",              4,  FUNC_0_OP(inst_cp_a)},
    [0xC0] = {"RET NZ",            0,  FUNC_0_OP(inst_retnz)},
    [0xC1] = {"POP BC",            12, FUNC_0_OP(inst_pop_bc)},
    [0xC2] = {"JP NZ, $%04X",      0,  FUNC_2_OP(inst_jpnz_addr16)},
    [0xC3] = {"JP $%02X",          16, FUNC_2_OP(inst_jp_addr16)},
    [0xC4] = {"CALL NZ, $%04X",    0,  FUNC_2_OP(inst_callnz_addr16)},
    [0xC5] = {"PUSH BC",           16, FUNC_0_OP(inst_push_bc)},
    [0xC6] = {"ADD A, $%02X",      8,  FUNC_1_OP(inst_add_a_imm8)},
    [0xC7] = {"RST $00",           16, FUNC_0_OP(inst_rst_00)},
    [0xC8] = {"RET Z",             0,  FUNC_0_OP(inst_retz)},
    [0xC9] = {"RET",               16, FUNC_0_OP(inst_ret)},
    [0xCA] = {"JP Z, $%04X",       0,  FUNC_2_OP(inst_jpz_addr16)},
    [0xCB] = {"",                  0,  FUNC_1_OP(inst_cbinst)},
    [0xCC] = {"CALL Z, $%04X",     0,  FUNC_2_OP(inst_callz_addr16)},
    [0xCD] = {"CALL $%04X",        24, FUNC_2_OP(inst_call_addr16)},
    [0xCE] = {"ADC A, $%02X",      8,  FUNC_1_OP(inst_adc_a_imm8)},
    [0xCF] = {"RST $08",           16, FUNC_0_OP(inst_rst_08)},
    [0xD0] = {"RET NC",            0,  FUNC_0_OP(inst_retnc)},
    [0xD1] = {"POP DE",            12, FUNC_0_OP(inst_pop_de)},
    [0xD2] = {"JP NC, $%04X",      0,  FUNC_2_OP(inst_jpnc_addr16)},
    [0xD3] = {"UNKNOWN 0xD3",      0,  FUNC_0_OP(inst_unknown)},
    [0xD4] = {"CALL NC, $%04X",    0,  FUNC_2_OP(inst_callnc_addr16)},
    [0xD5] = {"PUSH DE",           16, FUNC_0_OP(inst_push_de)},
    [0xD6] = {"SUB $%02X",         8,  FUNC_1_OP(inst_sub_imm8)},
    [0xD7] = {"RST $10",           16, FUNC_0_OP(inst_rst_10)},
    [0xD8] = {"RET C",             0,  FUNC_0_OP(inst_retc)},
    [0xD9] = {"RETI",              16, FUNC_0_OP(inst_reti)},
    [0xDA] = {"JP C, $%04X",       0,  FUNC_2_OP(inst_jpc_addr16)},
    [0xDB] = {"UNKNOWN 0xDB",      0,  FUNC_0_OP(inst_unknown)},
    [0xDC] = {"CALL C, $%04X",     0,  FUNC_2_OP(inst_callc_addr16)},
    [0xDD] = {"UNKNOWN 0xDD",      0,  FUNC_0_OP(inst_unknown)},
    [0xDE] = {"SBC A, $%02X",      8,  FUNC_1_OP(inst_sbc_a_imm8)},
    [0xDF] = {"RST $18",           16, FUNC_0_OP(inst_rst_18)},
    [0xE0] = {"LD ($FF%02X), A",   12, FUNC_1_OP(inst_ld_addr8_a)},
    [0xE1] = {"POP HL",            12, FUNC_0_OP(inst_pop_hl)},
    [0xE2] = {"LD ($FF00 + C), A", 8,  FUNC_0_OP(inst_ld_addrc_a)},
    [0xE3] = {"UNKNOWN 0xE3",      0,  FUNC_0_OP(inst_unknown)},
    [0xE4] = {"UNKNOWN 0xE4",      0,  FUNC_0_OP(inst_unknown)},
    [0xE5] = {"PUSH HL",           16, FUNC_0_OP(inst_push_hl)},
    [0xE6] = {"AND $%02X",         8,  FUNC_1_OP(inst_and_imm8)},
    [0xE7] = {"RST $20",           16, FUNC_0_OP(inst_rst_20)},
    [0xE8] = {"ADD SP, %i",        16, FUNC_1_OP(inst_add_sp_imm8)},
    [0xE9] = {"JP (HL)",           4,  FUNC_0_OP(inst_jp_hl)},
    [0xEA] = {"LD ($%04X), A",     16, FUNC_2_OP(inst_ld_addr16_a)},
    [0xEB] = {"UNKNOWN 0xEB",      0,  FUNC_0_OP(inst_unknown)},
    [0xEC] = {"UNKNOWN 0xEC",      0,  FUNC_0_OP(inst_unknown)},
    [0xED] = {"UNKNOWN 0xED",      0,  FUNC_0_OP(inst_unknown)},
    [0xEE] = {"XOR $%02X",         8,  FUNC_1_OP(inst_xor_imm8)},
    [0xEF] = {"RST $28",           16, FUNC_0_OP(inst_rst_28)},
    [0xF0] = {"LD A, ($FF%02X)",   12, FUNC_1_OP(inst_ld_a_addr8)},
    [0xF1] = {"POP AF",            12, FUNC_0_OP(inst_pop_af)},
    [0xF2] = {"LD A, ($FF00 + C)", 8,  FUNC_0_OP(inst_ld_a_addrc)},
    [0xF3] = {"DI",                4,  FUNC_0_OP(inst_di)},
    [0xF4] = {"UNKNOWN 0xF4",      0,  FUNC_0_OP(inst_unknown)},
    [0xF5] = {"PUSH AF",           16, FUNC_0_OP(inst_push_af)},
    [0xF6] = {"OR $%02X",          8,  FUNC_1_OP(inst_or_imm8)},
    [0xF7] = {"RST $30",           16, FUNC_0_OP(inst_rst_30)},
    [0xF8] = {"LD HL, SP + %i",    12, FUNC_1_OP(inst_ld_hl_sp_offs8)},
    [0xF9] = {"LD SP, HL",         8,  FUNC_0_OP(inst_ld_sp_hl)},
    [0xFA] = {"LD A, ($%04X)",     16, FUNC_2_OP(inst_ld_a_addr16)},
    [0xFB] = {"EI",                4,  FUNC_0_OP(inst_ei)},
    [0xFC] = {"UNKNOWN 0xFC",      0,  FUNC_0_OP(inst_unknown)},
    [0xFD] = {"UNKNOWN 0xFD",      0,  FUNC_0_OP(inst_unknown)},
    [0xFE] = {"CP $%02X",          8,  FUNC_1_OP(inst_cp_imm8)},
    [0xFF] = {"RST $38",           16, FUNC_0_OP(inst_rst_38)},
};

static void disassemble_instruction(uint16_t addr)
{
    uint8_t opcode = memory_read_byte(addr);
    
    printf("%04X:  ", addr);
    if (opcode == 0xCB)
    {
        opcode = memory_read_byte(addr + 1);
        printf("CB %02X        %s %s\n",
          opcode, cbInstructionTable[opcode >> 3].mnemonic, cbDstNames[opcode & 7]);
    }
    else
    {
        const struct Instruction *instr = &instructionTable[opcode];
        uint8_t op1, op2;
        
        switch (instr->operandSize)
        {
          case 0:
            printf("%02X           %s\n", opcode, instr->mnemonic);
            break;
          case 1:
            op1 = memory_read_byte(addr + 1);
            printf("%02X %02X        ", opcode, op1);
            printf(instr->mnemonic, op1);
            putchar('\n');
            break;
          case 2:
            op1 = memory_read_byte(addr + 1);
            op2 = memory_read_byte(addr + 2);
            printf("%02X %02X %02X     ", opcode, op1, op2);
            printf(instr->mnemonic, op1 | (op2 << 8));
            putchar('\n');
            break;
        }
    }
}

static bool disassemble = false;
static bool singleStep = false;

static void check_breakpoints(uint16_t currAddr)
{
    //if (currAddr == 0x1747)
    //    breakpoint();
}

static void cpu_step(void)
{
    uint16_t currAddr = regs.pc;
    uint8_t opcode;
    const struct Instruction *instr;
    
    assert((regs.f & 0xF) == 0);
    
    if (disassemble)
        disassemble_instruction(currAddr);
    
    if (cpuHalted)
    {
        update_clocks(4);
        return;
    }
    
    opcode = memory_read_byte(regs.pc++);
    instr = &instructionTable[opcode];
    switch (instr->operandSize)
    {
      case 0:
        instr->func0op();
        break;
      case 1:
        instr->func1op(memory_read_byte(regs.pc++));
        break;
      case 2:
        {
            uint16_t operand;
            
            operand = memory_read_byte(regs.pc++);
            operand |= memory_read_byte(regs.pc++) << 8;
            instr->func2op(operand);
        }
        break;
    }
    update_clocks(instr->cycles);
}

static void dispatch_interrupts(void)
{
    // Get all interrupts that have both the IF and IE bit set
    uint8_t triggeredInterrupts = (ie & REG_IF) & 0xF;
    
    if (triggeredInterrupts != 0)
    {
        // CPU will be taken out of halt mode when an interrupt occurrs, regardless of whether
        // IME is enabled
        cpuHalted = false;
        
        // Dispatch interrupt handlers when IME is enabled
        if (interruptsEnabled)
        {
            interruptsEnabled = false;
            push(regs.pc);
            if (triggeredInterrupts & INTR_FLAG_VBLANK)
            {
                REG_IF &= ~INTR_FLAG_VBLANK;
                regs.pc = 0x40;
            }
            else if (triggeredInterrupts & INTR_FLAG_LCDC)
            {
                REG_IF &= ~INTR_FLAG_LCDC;
                regs.pc = 0x48;
            }
            else if (triggeredInterrupts & INTR_FLAG_TIMER)
            {
                REG_IF &= ~INTR_FLAG_TIMER;
                regs.pc = 0x50;
            }
            else if (triggeredInterrupts & INTR_FLAG_SERIAL)
            {
                REG_IF &= ~INTR_FLAG_SERIAL;
                regs.pc = 0x58;
            }
            else if (triggeredInterrupts & INTR_FLAG_JOYPAD)
            {
                REG_IF &= ~INTR_FLAG_JOYPAD;
                regs.pc = 0x60;
            }
            else
            {
                assert(0);  // should not happen
            }
        }
    }
}

//------------------------------------------------------------------------------
// Audio
//------------------------------------------------------------------------------

static void audio_step(void)
{
    // TODO: implement audio
}

//------------------------------------------------------------------------------
// Timer
//------------------------------------------------------------------------------

static void increment_tima(void)
{
    REG_TIMA++;
    // Trigger timer interrupt when REG_TIMA overflows
    if (REG_TIMA == 0)
    {
        REG_TIMA = REG_DIV;
        REG_IF |= INTR_FLAG_TIMER;
    }
    
}

static void timer_step(void)
{
    if (timerClock2 >= 256)
    {
        timerClock2 -= 256;
        REG_DIV++;
    }
    if (REG_TAC & 4)
    {
        switch (REG_TAC & 3)
        {
          case 0:
            if (timerClock >= 1024)
            {
                timerClock -= 1024;
                increment_tima();
            }
            break;
          case 1:
            if (timerClock >= 16)
            {
                timerClock -= 16;
                increment_tima();
            }
            break;
          case 2:
            if (timerClock >= 64)
            {
                timerClock -= 64;
                increment_tima();
            }
            break;
          case 3:
            if (timerClock >= 256)
            {
                timerClock -= 256;
                increment_tima();
            }
            break;
        }
    }
}

void gameboy_run_frame(void)
{
    gpu_frame_init();
    while (!gpuFrameDone)
    {
        cpu_step();
        gpu_step();
        audio_step();
        timer_step();
        dispatch_interrupts();
    }
    platform_draw_done();
}

void gameboy_step(void)
{
    cpu_step();
    gpu_step();
    audio_step();
    timer_step();
    dispatch_interrupts();
}
