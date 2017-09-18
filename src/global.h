#ifndef GUARD_GLOBAL_H
#define GUARD_GLOBAL_H

#ifdef __GNUC__
#define ATTRIBUTE_ALIGNED(n) __attribute__((aligned(n)))
#define ATTRIBUTE_PACKED __attribute__((packed))
#ifndef FASTCALL
#define FASTCALL __attribute__((fastcall))
//#define FASTCALL
#endif
#endif

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define UNUSED(a) (void)(a)
#undef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define APPNAME "Game Boy Emulator"

#ifdef DEBUG
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_puts(...) puts(__VA_ARGS__)
#define dbg_fputs(...) fputs(__VA_ARGS__)
#else
#define dbg_printf(...) ((void)0)
#define dbg_puts(...) ((void)0)
#define dbg_fputs(...) ((void)0)
#endif

#endif  // GUARD_GLOBAL_H
