// MinGW's <stddef.h> redefines offsetof as the compiler builtin every time it
// is included, undoing tier0/platform.h's version, which accepts the
// overloaded operator[] of networked arrays. Put Valve's back afterwards.
#include_next <stddef.h>

#if defined( GNUC ) && defined( PLATFORM_H )
#undef offsetof
#define offsetof(s,m)	( (size_t)&(((s *)0x1000000)->m) - 0x1000000u )
#endif
