#ifndef GUARD_PLATFORM_H
#define GUARD_PLATFORM_H

void platform_fatal_error(char *fmt, ...);
uint8_t *platform_get_framebuffer(void);
void platform_draw_done(void);

#endif  // GUARD_PLATFORM_H
