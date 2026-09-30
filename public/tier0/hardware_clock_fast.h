//========= Copyright c Valve Corporation, All rights reserved. ============//
#ifndef TIER0_HARDWARE_TIMER
#define TIER0_HARDWARE_TIMER

#include "tier0/platform.h"

#if defined( __aarch64__ )
inline int GetHardwareClockFast( void )
{
	return ( int )Plat_Rdtsc();
}

#elif defined GNUC && !defined __e2k__
inline int GetHardwareClockFast( void )
{
	unsigned long long int nRet;
	__asm__ volatile (".byte 0x0f, 0x31" : "=A" (nRet)); // rdtsc
	return ( int ) nRet;
}

#else

#ifdef __e2k__
#include <x86intrin.h>
#else
#include <intrin.h>
#endif // ifdef __e2k__

inline int GetHardwareClockFast()
{
	return __rdtsc();
}

#endif // defined GNUC && !defined __e2k__

#endif // ifndef TIER0_HARDWARE_TIMER
