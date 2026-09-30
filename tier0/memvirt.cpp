//===== Copyright © 2010, Valve Corporation, All rights reserved. ======//
//
// Purpose: Virtual memory sections management!
//
// $NoKeywords: $
//===========================================================================//


#include "pch_tier0.h"

#if !defined(STEAM) && !defined(NO_MALLOC_OVERRIDE)

//#include <malloc.h>
#include <string.h>
#include "tier0/dbg.h"
#include "tier0/stacktools.h"
#include "tier0/memalloc.h"
#include "tier0/memvirt.h"
#include "tier0/fasttimer.h"
#include "mem_helpers.h"
#ifdef PLATFORM_WINDOWS_PC
#undef WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <crtdbg.h>
#endif
#ifdef OSX
#include <malloc/malloc.h>
#include <stdlib.h>
#endif

#include <map>
#include <set>
#include <limits.h>
#include "tier0/threadtools.h"

CInterlockedInt VmmMsgFlag = 0; // Prevents re-entrancy within VmmMsg (printf allocates a large buffer!)

#ifdef _DEBUG
#define VmmMsg( mutex, ... ) ( (VmmMsgFlag|mutex.GetOwnerId()) ? 0 : ( ++VmmMsgFlag, DevMsg( __VA_ARGS__ ), VmmMsgFlag-- ) )
#else
#define VmmMsg( mutex, ... ) ((void)0)
#endif

#define TRACE_CALL( ... ) ((void)0)


#ifdef VIRTUAL_MEMORY_MANAGER_SUPPORTED

IVirtualMemorySection * VirtualMemoryManager_AllocateVirtualMemorySection( size_t numMaxBytes )
{
	return GetVirtualMemoryManager().AllocateVirtualMemorySection( numMaxBytes );
}

void VirtualMemoryManager_Shutdown()
{
	GetVirtualMemoryManager().Shutdown();
}

IVirtualMemorySection *GetMemorySectionForAddress( void *pAddress )
{
	return GetVirtualMemoryManager().GetMemorySectionForAddress( pAddress );
}

void VirtualMemoryManager_GetStats( size_t &nReserved, size_t &nReservedMax, size_t &nCommitted, size_t &nCommittedMax )
{
	GetVirtualMemoryManager().GetStats( nReserved, nReservedMax, nCommitted, nCommittedMax );
}

#else

IVirtualMemorySection * VirtualMemoryManager_AllocateVirtualMemorySection( size_t numMaxBytes )
{
	return NULL;
}

void VirtualMemoryManager_Shutdown()
{
}

IVirtualMemorySection *GetMemorySectionForAddress( void *pAddress )
{
	return NULL;
}

#endif


#endif // !STEAM && !NO_MALLOC_OVERRIDE

