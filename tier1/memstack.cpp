//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#if defined( _WIN32 )
#define WIN_32_LEAN_AND_MEAN
#include <windows.h>
#define VA_COMMIT_FLAGS MEM_COMMIT
#define VA_RESERVE_FLAGS MEM_RESERVE
#endif

#include "tier0/dbg.h"
#include "memstack.h"
#include "utlmap.h"
#include "tier0/memdbgon.h"

#ifdef _WIN32
#pragma warning(disable:4073)
#pragma init_seg(lib)
#endif

static volatile bool bSpewAllocations = false; // TODO: Register CMemoryStacks with g_pMemAlloc, so it can spew a summary

//-----------------------------------------------------------------------------

MEMALLOC_DEFINE_EXTERNAL_TRACKING(CMemoryStack);

//-----------------------------------------------------------------------------

void PrintStatus( void* p )
{
	CMemoryStack* pMemoryStack = (CMemoryStack*)p;

	pMemoryStack->PrintContents();
}

CMemoryStack::CMemoryStack()
 : 	m_pNextAlloc( NULL )
	, m_pCommitLimit( NULL )
	, m_pAllocLimit( NULL )
	, m_pHighestAllocLimit( NULL )
	, m_pBase( NULL )
	, m_bRegisteredAllocation( false )
 	, m_maxSize( 0 )
	, m_alignment( 16 )
#ifdef MEMSTACK_VIRTUAL_MEMORY_AVAILABLE
 	, m_commitIncrement( 0 )
	, m_minCommit( 0 )
#endif
{
	AddMemoryInfoCallback( this );
	m_pszAllocOwner = strdup( "CMemoryStack unattributed" );
}
	
//-------------------------------------

CMemoryStack::~CMemoryStack()
{
	if ( m_pBase )
		Term();

	RemoveMemoryInfoCallback( this );
	free( m_pszAllocOwner );
}

//-------------------------------------

bool CMemoryStack::Init( const char *pszAllocOwner, unsigned maxSize, unsigned commitIncrement, unsigned initialCommit, unsigned alignment )
{
	Assert( !m_pBase );

	m_bPhysical = false;

	m_maxSize = maxSize;
	m_alignment = AlignValue( alignment, 4 );

	Assert( m_alignment == alignment );
	Assert( m_maxSize > 0 );

	SetAllocOwner( pszAllocOwner );

#ifdef MEMSTACK_VIRTUAL_MEMORY_AVAILABLE

	if ( commitIncrement != 0 )
	{
		m_commitIncrement = commitIncrement;
	}

	unsigned pageSize;

	SYSTEM_INFO sysInfo;
	GetSystemInfo( &sysInfo );
	Assert( !( sysInfo.dwPageSize & (sysInfo.dwPageSize-1)) );
	pageSize = sysInfo.dwPageSize;

	if ( m_commitIncrement == 0 )
	{
		m_commitIncrement = pageSize;
	}
	else
	{
		m_commitIncrement = AlignValue( m_commitIncrement, pageSize );
	}

	m_maxSize = AlignValue( m_maxSize, m_commitIncrement );
	
	Assert( m_maxSize % pageSize == 0 && m_commitIncrement % pageSize == 0 && m_commitIncrement <= m_maxSize );

#ifdef _WIN32
	m_pBase = (unsigned char *)VirtualAlloc( NULL, m_maxSize, VA_RESERVE_FLAGS, PAGE_NOACCESS );
#else
	m_pVirtualMemorySection = g_pMemAlloc->AllocateVirtualMemorySection( m_maxSize );
	if ( !m_pVirtualMemorySection )
	{
		Warning( "AllocateVirtualMemorySection failed( size=%d )\n", m_maxSize );
		Assert( 0 );
		m_pBase = NULL;
	}
	else
	{
		m_pBase = ( byte* ) m_pVirtualMemorySection->GetBaseAddress();
	}
#endif
	if ( !m_pBase )
	{
#if !defined( NO_MALLOC_OVERRIDE )
		g_pMemAlloc->OutOfMemory();
#endif
		return false;
	}
	m_pCommitLimit = m_pNextAlloc = m_pBase;

	if ( initialCommit )
	{
		initialCommit = AlignValue( initialCommit, m_commitIncrement );
		Assert( initialCommit <= m_maxSize );
		bool bInitialCommitSucceeded = false;
#ifdef _WIN32
		bInitialCommitSucceeded = !!VirtualAlloc( m_pCommitLimit, initialCommit, VA_COMMIT_FLAGS, PAGE_READWRITE );
#else
		m_pVirtualMemorySection->CommitPages( m_pCommitLimit, initialCommit );
		bInitialCommitSucceeded = true;
#endif
		if ( !bInitialCommitSucceeded )
		{
#if !defined( NO_MALLOC_OVERRIDE )
			g_pMemAlloc->OutOfMemory( initialCommit );
#endif
			return false;
		}
		m_minCommit = initialCommit;
		m_pCommitLimit += initialCommit;
		RegisterAllocation();
	}

#else
	m_pBase = (byte*)MemAlloc_AllocAligned( m_maxSize, alignment ? alignment : 1 );
	m_pNextAlloc = m_pBase;
	m_pCommitLimit = m_pBase + m_maxSize;
#endif

	m_pHighestAllocLimit = m_pNextAlloc;

	m_pAllocLimit = m_pBase + m_maxSize;

	return ( m_pBase != NULL );
}

//-------------------------------------

//-------------------------------------

void CMemoryStack::Term()
{
	FreeAll();
	if ( m_pBase )
	{

#ifdef MEMSTACK_VIRTUAL_MEMORY_AVAILABLE
#if defined(_WIN32)
		VirtualFree( m_pBase, 0, MEM_RELEASE );
#else
		m_pVirtualMemorySection->Release();
		m_pVirtualMemorySection = NULL;
#endif
#else
		MemAlloc_FreeAligned( m_pBase );
#endif
		m_pBase = NULL;
		// Zero these variables to avoid getting misleading mem_dump
		// results when m_pBase is NULL.
		m_pNextAlloc = NULL;
		m_pCommitLimit = NULL;
		m_pHighestAllocLimit = NULL;
		m_maxSize = 0;
		RegisterDeallocation(true);
	}
}

//-------------------------------------

int CMemoryStack::GetSize() const
{ 
	if ( m_bPhysical )
		return m_maxSize;

#ifdef MEMSTACK_VIRTUAL_MEMORY_AVAILABLE
	return m_pCommitLimit - m_pBase; 
#else
	return m_maxSize;
#endif
}


//-------------------------------------

bool CMemoryStack::CommitTo( byte *pNextAlloc ) RESTRICT
{
	if ( m_bPhysical )
	{
		return NULL;
	}

#ifdef MEMSTACK_VIRTUAL_MEMORY_AVAILABLE
	unsigned char *	pNewCommitLimit = AlignValue( pNextAlloc, m_commitIncrement );
	ptrdiff_t commitIncrement 		= pNewCommitLimit - m_pCommitLimit;
	
	if( m_pCommitLimit + commitIncrement > m_pAllocLimit )
	{
#if !defined( NO_MALLOC_OVERRIDE )
		g_pMemAlloc->OutOfMemory( commitIncrement );
#endif
		return false;
	}

	if ( pNewCommitLimit > m_pCommitLimit )
	{
		RegisterDeallocation(false);
		bool bAllocationSucceeded = false;
#ifdef _WIN32
		bAllocationSucceeded = !!VirtualAlloc( m_pCommitLimit, commitIncrement, VA_COMMIT_FLAGS, PAGE_READWRITE );
#else
		bAllocationSucceeded = m_pVirtualMemorySection->CommitPages( m_pCommitLimit, commitIncrement );
#endif
		if ( !bAllocationSucceeded )
		{
#if !defined( NO_MALLOC_OVERRIDE )
			g_pMemAlloc->OutOfMemory( commitIncrement );
#endif
			return false;
		}
		m_pCommitLimit = pNewCommitLimit;

		RegisterAllocation();
	}
	else if ( pNewCommitLimit < m_pCommitLimit )
	{
		if  ( m_pNextAlloc > pNewCommitLimit )
		{
			Warning( "ATTEMPTED TO DECOMMIT OWNED MEMORY STACK SPACE\n" );
			pNewCommitLimit = AlignValue( m_pNextAlloc, m_commitIncrement );
		}

		if ( pNewCommitLimit < m_pCommitLimit )
		{
			RegisterDeallocation(false);
			ptrdiff_t decommitIncrement = m_pCommitLimit - pNewCommitLimit;
#ifdef _WIN32
			VirtualFree( pNewCommitLimit, decommitIncrement, MEM_DECOMMIT );
#else
			m_pVirtualMemorySection->DecommitPages( pNewCommitLimit, decommitIncrement );
#endif
			m_pCommitLimit = pNewCommitLimit;
			RegisterAllocation();
		}
	}

	return true;
#else
	return false;
#endif
}

// Identify the owner of this memory stack's memory
void CMemoryStack::SetAllocOwner( const char *pszAllocOwner )
{
	if ( !pszAllocOwner || !Q_strcmp( m_pszAllocOwner, pszAllocOwner ) )
		return;
	free( m_pszAllocOwner );
	m_pszAllocOwner = strdup( pszAllocOwner );
}

void CMemoryStack::RegisterAllocation()
{
	// 'physical' allocations on PS3 come from RSX local memory, so we don't count them here:
	if ( IsPS3() && m_bPhysical )
		return;

	if ( GetSize() )
	{
		if ( m_bRegisteredAllocation )
			Warning( "CMemoryStack: ERROR - mismatched RegisterAllocation/RegisterDeallocation!\n" );

		// NOTE: we deliberately don't use MemAlloc_RegisterExternalAllocation. CMemoryStack needs to bypass 'GetActualDbgInfo'
		// due to the way it allocates memory: there's just one representative memory address (m_pBase), it grows at unpredictable
		// times (in CommitTo, not every Alloc call) and it is freed en-masse (instead of freeing each individual allocation).
		MemAlloc_RegisterAllocation( m_pszAllocOwner, 0, GetSize(), GetSize(), 0 );
	}
	m_bRegisteredAllocation = true;

	// Temp memorystack spew: very useful when we crash out of memory
	if ( IsGameConsole() && bSpewAllocations ) Msg( "CMemoryStack: %4.1fMB (%s)\n", GetSize()/(float)(1024*1024), m_pszAllocOwner );
}

void CMemoryStack::RegisterDeallocation( bool bShouldSpewSize )
{
	// 'physical' allocations on PS3 come from RSX local memory, so we don't count them here:
	if ( IsPS3() && m_bPhysical )
		return;

	if ( GetSize() )
	{
		if ( !m_bRegisteredAllocation )
			Warning( "CMemoryStack: ERROR - mismatched RegisterAllocation/RegisterDeallocation!\n" );
		MemAlloc_RegisterDeallocation( m_pszAllocOwner, 0, GetSize(), GetSize(), 0 );
	}
	m_bRegisteredAllocation = false;

	// Temp memorystack spew: very useful when we crash out of memory
	if ( bShouldSpewSize && IsGameConsole() && bSpewAllocations ) Msg( "CMemoryStack: %4.1fMB (%s)\n", GetSize()/(float)(1024*1024), m_pszAllocOwner );
}

//-------------------------------------

void CMemoryStack::FreeToAllocPoint( MemoryStackMark_t mark, bool bDecommit )
{
	mark = AlignValue( mark, m_alignment );
	byte *pAllocPoint = m_pBase + mark;

	Assert( pAllocPoint >= m_pBase && pAllocPoint <= m_pNextAlloc );
	if ( pAllocPoint >= m_pBase && pAllocPoint <= m_pNextAlloc )
	{
		m_pNextAlloc = pAllocPoint;
#ifdef MEMSTACK_VIRTUAL_MEMORY_AVAILABLE
		if ( bDecommit && !m_bPhysical )
		{
			CommitTo( MAX( m_pNextAlloc, (m_pBase + m_minCommit) ) );
		}
#endif
	}
}

//-------------------------------------

void CMemoryStack::FreeAll( bool bDecommit )
{
	if ( m_pBase && ( m_pBase < m_pCommitLimit ) )
	{
		FreeToAllocPoint( 0, bDecommit );
	}
}

//-------------------------------------

void CMemoryStack::Access( void **ppRegion, unsigned *pBytes )
{
	*ppRegion = m_pBase;
	*pBytes = ( m_pNextAlloc - m_pBase);
}

const char* CMemoryStack::GetMemoryName() const
{
	return m_pszAllocOwner;
}

size_t CMemoryStack::GetAllocatedBytes() const
{
	return GetUsed();
}

size_t CMemoryStack::GetCommittedBytes() const
{
	return GetSize();
}

size_t CMemoryStack::GetReservedBytes() const
{
	return GetMaxSize();
}

size_t CMemoryStack::GetHighestBytes() const
{
	size_t highest = m_pHighestAllocLimit - m_pBase;
	return highest;
}

//-------------------------------------

void CMemoryStack::PrintContents() const
{
	size_t highest = m_pHighestAllocLimit - m_pBase;
#ifdef PLATFORM_WINDOWS_PC
	MEMORY_BASIC_INFORMATION info;
	char moduleName[260];
	strcpy( moduleName, "unknown module" );
	// Because this code is statically linked into each DLL, this function and the PrintStatus
	// function will be in the DLL that constructed the CMemoryStack object. We can then
	// retrieve the DLL name to give slightly more verbose memory dumps.
	if ( VirtualQuery( (LPCVOID)&PrintStatus, &info, sizeof( info ) ) == sizeof( info ) )
	{
		GetModuleFileName( (HMODULE) info.AllocationBase, moduleName, _countof( moduleName ) );
		moduleName[ _countof( moduleName )-1 ] = 0;
	}
	Msg( "CMemoryStack %s in %s\n", m_pszAllocOwner, moduleName );
#else
	Msg( "CMemoryStack %s\n", m_pszAllocOwner );
#endif
	Msg( "    Total used memory:      %d KB\n", GetUsed() / 1024 );
	Msg( "    Total committed memory: %d KB\n", GetSize() / 1024 );
	Msg( "    Max committed memory: %u KB out of %d KB\n", (unsigned)highest / 1024, GetMaxSize() / 1024 );
}

