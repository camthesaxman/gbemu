#ifndef GUARD_GPU_H
#define GUARD_GPU_H

extern uint32_t gpuClock;
extern bool gpuFrameDone;

void gpu_handle_vram_write(uint16_t addr, uint8_t val);
void gpu_set_screen_palette(unsigned int bytesPerPixel, const void *palette);
void gpu_frame_init(void);
void gpu_step(void);

#endif  // GUARD_GPU_H
