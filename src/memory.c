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

uint8_t *gamePAK;
uint8_t *rom0;
uint8_t *rom1;
uint8_t vram[VRAM_SIZE];
uint8_t eram[ERAM_SIZE];
uint8_t iwram[IWRAM_SIZE];
uint8_t io[IO_SIZE];
uint8_t oam[OAM_SIZE];
uint8_t hram[HRAM_SIZE];
uint8_t ie;

static FILE *saveFile;

struct MBCDriver
{
    uint8_t (*readByte)(uint16_t addr);
    void (*writeByte)(uint16_t addr, uint8_t val);
    void (*loadSaveFile)(const char *filename);
    void (*saveSaveFile)(const char *filename);
};

static struct MBCDriver mbcDriver;

//------------------------------------------------------------------------------
// Null MBC Driver
//------------------------------------------------------------------------------

static uint8_t mbc_null_read_byte(uint16_t addr)
{
    // 0x4000-0x7FFF: Cartridge ROM Bank 1 (fixed)
    if (addr >= 0x4000 && addr <= 0x7FFF)
        return rom1[addr - 0x4000];
    
    platform_fatal_error("Read byte from invalid address 0x%04X", addr);
    return 0;
}

static void mbc_null_write_byte(uint16_t addr, uint8_t val)
{
    (void)val;
    
    // Tetris hacks. I don't know why the game feels like poking this memory
    if (addr <= 0x7FFF)  // Silently ignore any writes to ROM
        return;
    if (addr >= 0x9800 && addr <= 0x9FFF)
        return;
    if (addr >= 0xFEA0 && addr <= 0xFEFF)
    {
        //oam[addr - 0xFE00] = val;
        return;
    }
    
    platform_fatal_error("Wrote byte to invalid address 0x%04X, pc = 0x%04X", addr, regs.pc);
}

static void mbc_null_init(void)
{
    mbcDriver.readByte = mbc_null_read_byte;
    mbcDriver.writeByte = mbc_null_write_byte;
}

//------------------------------------------------------------------------------
// MBC1
//------------------------------------------------------------------------------

static enum
{
    MBC1_MODE_ROM,  // up to 8KByte RAM, 2MByte ROM (default)
    MBC1_MODE_RAM,  // up to 32KByte RAM, 512KByte ROM
} mbc1Mode;
static bool mbc1RamEnabled;
static uint8_t mbc1Reg1;
static uint8_t mbc1Reg2;
static uint8_t mbc1RomBankNum;
static uint8_t mbc1RamBankNum;
static uint8_t mbc1RamBanks[4][0x2000];

static void mbc1_update_banks(void)
{
    if (mbc1Mode == MBC1_MODE_ROM)
        mbc1RomBankNum = (mbc1Reg1) | (mbc1Reg2 << 5);
    else
    {
        mbc1RomBankNum = mbc1Reg1;
        mbc1RamBankNum = mbc1Reg2;
    }
    
    rom1 = gamePAK + 0x4000 * mbc1RomBankNum;
}

// Write to 0x0000-0x1FFF
static void mbc1_reg0(uint8_t val)
{
    // RAMCS gate data (serves as write-protection for RAM)
    // Writing 0x0A allows access to RAM.
    if ((val & 0xF) == 0xA)
    {
        mbc1RamEnabled = true;
        dbg_puts("mbc1: enabled RAM");
    }
    else
    {
        mbc1RamEnabled = false;
        dbg_puts("mbc1: disabled RAM");
    }
}

// Write to 0x2000-0x3FFF
static void mbc1_reg1(uint8_t val)
{
    // The ROM bank can be selected
    // Set the lower 5 bits of the bank number
    val &= 0x1F;
    if (val == 0)
        val = 1;
    mbc1Reg1 = val;
    mbc1_update_banks();
    //dbg_printf("mbc1_reg1: selected ROM bank 0x%02X, PC = 0x%04X\n", mbc1RomBankNum, regs.pc);
}

// Write to 0x4000-0x5FFF
static void mbc1_reg2(uint8_t val)
{
    // Set the upper 2 bits of the bank number
    val &= 3;
    mbc1Reg2 = val;
    mbc1_update_banks();
    //dbg_printf("mbc1_reg2: selected ROM bank 0x%02X\n", mbc1RomBankNum);
}

// Write to 0x6000-0x7FFF
static void mbc1_reg3(uint8_t val)
{
    mbc1Mode = (val & 1) ? MBC1_MODE_RAM : MBC1_MODE_ROM;
    //dbg_printf("mbc1: selected mode %u\n", val & 1);
    mbc1_update_banks();
}

static uint8_t mbc1_read_byte(uint16_t addr)
{
    switch (addr >> 12)
    {
      case 4:
      case 5:
      case 6:
      case 7:
        return rom1[addr - 0x4000];
      case 0xA:
      case 0xB:
        if (mbc1RamEnabled)
            return mbc1RamBanks[mbc1RamBankNum][addr - 0xA000];
        break;
    }
    platform_fatal_error("Read byte from invalid address 0x%04X", addr);
    return 0;
}

static void mbc1_write_byte(uint16_t addr, uint8_t val)
{
    // Super Mario Land hack
    if (addr >= 0xFEA0 && addr <= 0xFEFF)
        return;
    
    switch (addr >> 12)
    {
      //0x0000-0x1FFF
      case 0:
      case 1:
        mbc1_reg0(val);
        return;
      //0x2000-0x3FFF
      case 2:
      case 3:
        mbc1_reg1(val);
        return;
      //0x4000-0x5FFF
      case 4:
      case 5:
        mbc1_reg2(val);
        return;
      //0x6000-0x7FFF
      case 6:
      case 7:
        mbc1_reg3(val);
        return;
      case 0xA:
      case 0xB:
        if (mbc1RamEnabled)
        {
            mbc1RamBanks[mbc1RamBankNum][addr - 0xA000] = val;
            break;
        }
        
        // Super Mario Land hack
        return;
        
        break;
    }
    platform_fatal_error("Wrote byte to invalid address 0x%04X, pc = 0x%04X", addr, regs.pc);
}

static void mbc1_load_save_file(const char *filename)
{
    size_t ramSize = gRomInfo.ramSizeKbyte * 1024;
    
    dbg_printf("mbc1: loading save file from '%s'\n", filename);
    saveFile = fopen(filename, "rb");
    if (saveFile == NULL)
    {
        // Try to create it
        saveFile = fopen(filename, "wb");
        if (saveFile == NULL)
            platform_fatal_error("Failed to load save file '%s'", filename);
        fclose(saveFile);
        return;
    }
    assert(ramSize <= sizeof(mbc1RamBanks));
    fread(mbc1RamBanks, 1, gRomInfo.ramSizeKbyte * 1024, saveFile);
    fclose(saveFile);
}

static void mbc1_save_save_file(const char *filename)
{
    size_t ramSize = gRomInfo.ramSizeKbyte * 1024;
    
    dbg_printf("mbc1: saving save file to '%s'\n", filename);
    saveFile = fopen(filename, "wb");
    if (saveFile == NULL)
        platform_fatal_error("Failed to save file '%s'", filename);
    assert(ramSize <= sizeof(mbc1RamBanks));
    fwrite(mbc1RamBanks, 1, gRomInfo.ramSizeKbyte * 1024, saveFile);
    fclose(saveFile);
}

static void mbc1_init(void)
{
    dbg_puts("initializing MBC1");
    
    mbcDriver.readByte = mbc1_read_byte;
    mbcDriver.writeByte = mbc1_write_byte;
    mbcDriver.loadSaveFile = mbc1_load_save_file;
    mbcDriver.saveSaveFile = mbc1_save_save_file;
    
    mbc1Mode = MBC1_MODE_ROM;
    mbc1RamEnabled = false;
    mbc1RomBankNum = 1; 
    mbc1RamBankNum = 0;  // I don't know what the default is.
}

//------------------------------------------------------------------------------
// MBC3
//------------------------------------------------------------------------------

static uint8_t mbc3RomBankNum;
static uint8_t mbc3RamBankNum;
uint8_t mbc3CartRamBanks[4][0x2000];
static bool mbc3RamRTCWriteEnabled;
static enum
{
    MBC3_MODE_RAM,
    MBC3_MODE_RTC,
} mbc3Mode;

static void mbc3_update_rom_bank(void)
{
    rom1 = gamePAK + 0x4000 * mbc3RomBankNum;
}

// Write to 0x0000-0x1FFF
static void mbc3_reg0(uint8_t val)
{
    // Write protects RAM and the clock counters (default: 0)
    // Writing 0x0A allows access to RAM and the clock counter registers.
    if ((val & 0xF) == 0x0A)
    {
        mbc3RamRTCWriteEnabled = true;
        dbg_puts("mbc3: enabled writing to RAM and RTC");
    }
    else
    {
        mbc3RamRTCWriteEnabled = false;
        dbg_puts("mbc3: disabled writing to RAM and RTC");
    }
}

// Write to 0x2000-0x3FFF
static void mbc3_reg1(uint8_t val)
{
    // Selects the ROM bank. A value of 0x00 will select bank 0x01 instead.
    mbc3RomBankNum = val & 0x7F;
    if (mbc3RomBankNum == 0)
        mbc3RomBankNum = 1;
    //printf("mbc3: selected ROM bank 0x%02X\n", mbc3RomBankNum);
    mbc3_update_rom_bank();
}

// Write to 0x4000-0x5FFF
static void mbc3_reg2(uint8_t val)
{
    // RAM bank / RTC register select
    // Writing a value in the range of 0x00-0x03 maps the corresponding RAM bank
    // into memory at 0xA000-0xBFFF. Writing a value in the range of 0x08-0x0C
    // will map the corresponding RTC register into 0xA000-0xBFFF.
    if (val <= 0x03)
    {
        mbc3Mode = MBC3_MODE_RAM;
        mbc3RamBankNum = val;
        dbg_printf("mbc3: selected RAM bank 0x%02X\n", mbc3RamBankNum);
    }
    else if (val >= 0x08 && val <= 0x0C)
    {
        mbc3Mode = MBC3_MODE_RTC;
        dbg_puts("mbc3: RTC not implemented");
    }
}

// Write to 0x6000-0x7FFF
static void mbc3_reg3(uint8_t val)
{
    dbg_puts("mbc3: RTC not implemented");
}

static uint8_t mbc3_read_byte(uint16_t addr)
{
    // Pokemon Gold/Silver hack
    if (addr == 0xFEB6)
        return 0;
        
    switch (addr >> 12)
    {
      case 4:
      case 5:
      case 6:
      case 7:
        return rom1[addr - 0x4000];
      case 0xA:
      case 0xB:
        if (mbc3Mode == MBC3_MODE_RAM)
            return mbc3CartRamBanks[mbc3RamBankNum][addr - 0xA000];
        else
        {
            dbg_puts("RTC not implemented");
            return 0;
        }
        break;
    }
    platform_fatal_error("Read byte from invalid address 0x%04X, pc = 0x%X:0x%04X", addr, mbc3RamBankNum, regs.pc);
    return 0;
}

static void mbc3_write_byte(uint16_t addr, uint8_t val)
{
    // Pokemon Gold/Silver hack
    if (addr == 0xFEB6)
        return;
    
    switch (addr >> 12)
    {
      case 0:
      case 1:
        mbc3_reg0(val);
        break;
      case 2:
      case 3:
        mbc3_reg1(val);
        break;
      case 4:
      case 5:
        mbc3_reg2(val);
        break;
      case 6:
      case 7:
        mbc3_reg3(val);
        break;
      case 0xA:
      case 0xB:
        if (mbc3Mode == MBC3_MODE_RAM)
            mbc3CartRamBanks[mbc3RamBankNum][addr - 0xA000] = val;
        else
            dbg_puts("RTC not implemented");
        break;
      default:
        platform_fatal_error("Wrote byte 0x%02X to invalid address 0x%04X, pc = 0x%X:0x%04X", val, addr, mbc3RamBankNum, regs.pc);
        break;
    }
}

static void mbc3_load_save_file(const char *filename)
{
    size_t ramSize = gRomInfo.ramSizeKbyte * 1024;
    
    dbg_printf("mbc3: loading save file from '%s'\n", filename);
    saveFile = fopen(filename, "rb");
    if (saveFile == NULL)
    {
        // Try to create it
        saveFile = fopen(filename, "wb");
        if (saveFile == NULL)
            platform_fatal_error("Failed to load save file '%s'", filename);
        fclose(saveFile);
        return;
    }
    assert(ramSize <= sizeof(mbc3CartRamBanks));
    fread(mbc3CartRamBanks, 1, gRomInfo.ramSizeKbyte * 1024, saveFile);
    fclose(saveFile);
}

static void mbc3_save_save_file(const char *filename)
{
    size_t ramSize = gRomInfo.ramSizeKbyte * 1024;
    
    dbg_printf("mbc3: saving save file to '%s'\n", filename);
    saveFile = fopen(filename, "wb");
    if (saveFile == NULL)
        platform_fatal_error("Failed to save file '%s'", filename);
    assert(ramSize <= sizeof(mbc3CartRamBanks));
    fwrite(mbc3CartRamBanks, 1, gRomInfo.ramSizeKbyte * 1024, saveFile);
    fclose(saveFile);
}

static void mbc3_init(void)
{
    mbcDriver.readByte = mbc3_read_byte;
    mbcDriver.writeByte = mbc3_write_byte;
    mbcDriver.loadSaveFile = mbc3_load_save_file;
    mbcDriver.saveSaveFile = mbc3_save_save_file;
    
    mbc3Mode = MBC3_MODE_RAM;
    mbc3RamRTCWriteEnabled = false;
    mbc3RomBankNum = 1;
    mbc3RamBankNum = 0;
}

//------------------------------------------------------------------------------
// MBC5
//------------------------------------------------------------------------------

static bool mbc5RamEnabled;
static uint16_t mbc5RomBankNum;
static uint8_t mbc5RamBankNum;
static uint8_t mbc5RamBanks[16][0x2000];

static void mbc5_update_bank(void)
{
    rom1 = gamePAK + 0x4000 * mbc5RomBankNum;
}

// Write to 0x0000-0x1FFF
static void mbc5_ramg(uint8_t val)
{
    // Specifies whether external expansion RAM is accessible.
    // Access to this RAM is enabled by writing 0x0A to the RAMG register space.
    if ((val & 0xF) == 0xA)
    {
        mbc5RamEnabled = true;
        //printf("mbc5: enabled RAM, pc = 0x%04X\n", regs.pc);
    }
    else
    {
        mbc5RamEnabled = false;
        //puts("mbc5: disabled RAM");
    }
}

// Write to 0x2000-0x2FFF
static void mbc5_romb0(uint8_t val)
{
    // Specifies the lower-order 8 bits of a 9-bit ROM bank
    mbc5RomBankNum &= 0xFF00;
    mbc5RomBankNum |= val;
    mbc5_update_bank();
}

// Write to 0x3000-0x3FFF
static void mbc5_romb1(uint8_t val)
{
    // Specifies the higher-order 1 bit of a 9-bit ROM bank
    mbc5RomBankNum &= 0x00FF;
    mbc5RomBankNum |= (val & 1) << 8;
}

// Write to 0x4000-0x5FFF
static void mbc5_ramb(uint8_t val)
{
    // Specifies the RAM bank
    mbc5RamBankNum = val & 0xF;
}

static uint8_t mbc5_read_byte(uint16_t addr)
{
    switch (addr >> 12)
    {
      case 4:
      case 5:
      case 6:
      case 7:
        return rom1[addr - 0x4000];
      case 0xA:
      case 0xB:
        return mbc5RamBanks[mbc5RamBankNum][addr - 0xA000];
        break;
    }
    platform_fatal_error("Read byte from invalid address 0x%04X", addr);
    return 0;
}

static void mbc5_write_byte(uint16_t addr, uint8_t val)
{
    switch (addr >> 12)
    {
      // 0x0000-0x1FFF
      case 0:
      case 1:
        mbc5_ramg(val);
        return;
      // 0x2000-0x2FFF
      case 2:
        mbc5_romb0(val);
        return;
      // 0x3000-0x3FFF
      case 3:
        mbc5_romb1(val);
        return;
      // 0x4000-0x5FFF
      case 4:
      case 5:
        mbc5_ramb(val);
        return;
      // 0x6000-0x7FFF
      case 6:
      case 7:
        // This register has no effect in MBC5
        return;
      case 0xA:
      case 0xB:
        if (mbc5RamEnabled)
        {
            mbc5RamBanks[mbc5RamBankNum][addr - 0xA000] = val;
            return;
        }
        break;
    }
    dump_regs();
    platform_fatal_error("Read byte from invalid address 0x%04X", addr);
}

static void mbc5_load_save_file(const char *filename)
{
    size_t ramSize = gRomInfo.ramSizeKbyte * 1024;
    
    dbg_printf("mbc5: loading save file from '%s'\n", filename);
    saveFile = fopen(filename, "rb");
    if (saveFile == NULL)
    {
        // Try to create it
        saveFile = fopen(filename, "wb");
        if (saveFile == NULL)
            platform_fatal_error("Failed to load save file '%s'", filename);
        fclose(saveFile);
        return;
    }
    assert(ramSize <= sizeof(mbc5RamBanks));
    fread(mbc5RamBanks, 1, gRomInfo.ramSizeKbyte * 1024, saveFile);
    fclose(saveFile);
}

static void mbc5_save_save_file(const char *filename)
{
    size_t ramSize = gRomInfo.ramSizeKbyte * 1024;
    
    dbg_printf("mbc5: saving save file to '%s'\n", filename);
    saveFile = fopen(filename, "wb");
    if (saveFile == NULL)
        platform_fatal_error("Failed to save file '%s'", filename);
    assert(ramSize <= sizeof(mbc5RamBanks));
    fwrite(mbc5RamBanks, 1, gRomInfo.ramSizeKbyte * 1024, saveFile);
    fclose(saveFile);
}

static void mbc5_init(void)
{
    puts("initializing MBC5");
    
    mbcDriver.readByte = mbc5_read_byte;
    mbcDriver.writeByte = mbc5_write_byte;
    mbcDriver.loadSaveFile = mbc5_load_save_file;
    mbcDriver.saveSaveFile = mbc5_save_save_file;
    
    mbc5RamEnabled = false;
    mbc5RomBankNum = 1;
    mbc5RamBankNum = 0;
}

//------------------------------------------------------------------------------
// General Read/Write functions
//------------------------------------------------------------------------------

void memory_initialize_mapper(void)
{
    switch (gRomInfo.mapper)
    {
      case MAPPER_NONE:
        mbc_null_init();
        break;
      case MAPPER_MBC1:
        mbc1_init();
        break;
      case MAPPER_MBC3:
        mbc3_init();
        break;
      case MAPPER_MBC5:
        mbc5_init();
        break;
      default:
        assert(0);  // Should never happen
    }
}

void memory_load_save_file(const char *filename)
{
    if (mbcDriver.loadSaveFile == NULL)
        platform_fatal_error("Save files not implemented.");
    mbcDriver.loadSaveFile(filename);
}

void memory_save_save_file(const char *filename)
{
    if (mbcDriver.saveSaveFile == NULL)
        platform_fatal_error("Save files not implemented.");
    mbcDriver.saveSaveFile(filename);
}

// TODO: get rid of this function
void *memory_virt_to_phys(uint16_t addr)
{
    // ROM0
    if (addr < ROM0_BASE + ROM0_SIZE)
        return rom0 + addr - ROM0_BASE;
    
    // ROM1
    else if (addr >= ROM1_BASE && addr < ROM1_BASE + ROM1_SIZE)
        return rom1 + addr - ROM1_BASE;
    
    // VRAM
    else if (addr >= VRAM_BASE && addr < VRAM_BASE + VRAM_SIZE)
        return vram + addr - VRAM_BASE;
    
    // IWRAM
    else if (addr >= IWRAM_BASE && addr < IWRAM_BASE + IWRAM_SIZE)
        return iwram + addr - IWRAM_BASE;
    
    // IO
    else if (addr >= IO_BASE && addr < IO_BASE + IO_SIZE)
        return io + addr - IO_BASE;
    
    // OAM
    else if (addr >= OAM_BASE && addr < OAM_BASE + OAM_SIZE)
        return oam + addr - OAM_BASE;
    
    // HRAM
    else if (addr >= HRAM_BASE && addr < HRAM_BASE + HRAM_SIZE)
        return hram + addr - HRAM_BASE;
    
    // Echo RAM
    else if (addr >= ECHO_BASE && addr < ECHO_BASE + ECHO_SIZE)
        return iwram + addr - ECHO_BASE;
    
    // IE
    else if (addr == IE_ADDR)
        return &ie;
    
    return NULL;
}

static uint8_t io_read(uint16_t addr)
{
    switch (addr)
    {
      case REG_ADDR_JOYP:
        if (!(REG_JOYP & 0x20))
            return (REG_JOYP | 0xCF) & ~joypadState;
        else if (!(REG_JOYP & 0x10))
            return (REG_JOYP | 0xCF) & ~(joypadState >> 4);
        else
            return REG_JOYP | 0xCF;
      case 0xFF4D:
        return 0xFF;
      default:
        return io[addr - 0xFF00];
    }
}

static void io_write(uint16_t addr, uint8_t val)
{
    switch (addr)
    {
      case REG_ADDR_DIV:
        REG_DIV = 0;
        break;
      case REG_ADDR_TAC:
        REG_TAC = 0xF8 | val;
        break;
      case 0xFF46:  // OAM DMA
        {
            // TODO: check this address
            const void *src = memory_virt_to_phys(val << 8);
            
            memcpy(oam, src, 0xA0);
        }
        break;
      default:
        io[addr - 0xFF00] = val;
    }
}

uint8_t memory_read_byte(uint16_t addr)
{
    switch (addr >> 12)
    {
      // 0x0000-0x3FFF: Cartridge ROM Bank 0
      case 0x0:
      case 0x1:
      case 0x2:
      case 0x3:
        return rom0[addr];
      
      // 0x8000-9FFF: Video RAM
      case 0x8:
      case 0x9:
        return vram[addr - 0x8000];
      
      // 0xC000-0xDFFF: Internal RAM
      case 0xC:
      case 0xD:
        return iwram[addr - 0xC000];
      
      case 0xE:
      case 0xF:
        // 0xE000-0xFDFF: Echo RAM
        if (addr <= 0xFDFF)
            return iwram[addr - 0xE000];
        // 0xFE00-0xFE9F: OAM
        if (addr <= 0xFE9F)
            return oam[addr - 0xFE00];
        if (addr <= 0xFEFF)  // 0xFEA0-0xFEFF: unusable
            break;
        // 0xFF00-0xFF7F: IO Registers
        if (addr <= 0xFF7F)
            return io_read(addr);
        // 0xFF80-0xFFFD: High RAM
        if (addr <= 0xFFFE)
            return hram[addr - 0xFF80];
        // 0xFFFF: Interrupt Enable Flag
        assert(addr == 0xFFFF);
        return ie;
    }
    
    // Anything not in standard memory is handled by the MBC driver
    return mbcDriver.readByte(addr);
}

void memory_write_byte(uint16_t addr, uint8_t val)
{
    switch (addr >> 12)
    {
      // 0x8000-9FFF: Video RAM
      case 0x8:
      case 0x9:
        vram[addr - 0x8000] = val;
        gpu_handle_vram_write(addr, val);
        return;
      
      // 0xC000-0xDFFF: Internal RAM
      case 0xC:
      case 0xD:
        iwram[addr - 0xC000] = val;
        return;
      
      case 0xE:
      case 0xF:
        // 0xE000-0xFDFF: Echo RAM
        if (addr <= 0xFDFF)  // 0xE000-0xFDFF: Echo RAM - unusable
            iwram[addr - 0xE000] = val;
        // 0xFE00-0xFE9F: OAM
        else if (addr <= 0xFE9F)
            oam[addr - 0xFE00] = val;
        else if (addr <= 0xFEFF)  // 0xFEA0-0xFEFF: unusable
            break;
        // 0xFF00-0xFF7F: IO Registers
        else if (addr <= 0xFF7F)
            io_write(addr, val);
        // 0xFF80-0xFFFD: High RAM
        else if (addr <= 0xFFFE)
            hram[addr - 0xFF80] = val;
        // 0xFFFF: Interrupt Enable Flag
        else
            ie = val;
        return;
    }
    
    // Anything not in standard memory is handled by the MBC driver
    mbcDriver.writeByte(addr, val);
}

uint16_t memory_read_word(uint16_t addr)
{
    uint16_t val;
    
    val = memory_read_byte(addr);  // Read low byte
    val |= memory_read_byte(addr + 1) << 8;  // Read high byte
    return val;
}

void memory_write_word(uint16_t addr, uint16_t val)
{
    memory_write_byte(addr, val);   // Write low byte
    memory_write_byte(addr + 1, val >> 8);  // Write high byte
}
