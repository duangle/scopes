//#define HAVE_UCONTEXT_H
#if defined WINDOWS
// TODO: untested
#define CORO_LOSER 1
#else
#define CORO_ASM 1
//#define CORO_UCONTEXT 1
#endif
#define CORO_STACKALLOC 1
//#define CORO_GUARDPAGES 1


