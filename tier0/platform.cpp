//===== Copyright 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "pch_tier0.h"
#include <time.h>

#if defined(_WIN32)
#define WINDOWS_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0403
#include <windows.h>
#include <direct.h>
#include <io.h>
#endif
#include <errno.h>
#include <assert.h>
#include "tier0/platform.h"
#include "tier0/threadtools.h"
#include "tier0/minidump.h"

#include "tier0/memalloc.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Benchmark mode uses this heavy-handed method 
static bool g_bBenchmarkMode = false;
#ifdef _WIN32
static double g_FakeBenchmarkTime = 0;
static double g_FakeBenchmarkTimeInc = 1.0 / 66.0;
#endif

static CThreadFastMutex g_LocalTimeMutex;

//our global error callback function. Note that this is not initialized, but static space guarantees this is NULL at app start.
//If you initialize, it will set to zero again when the CPP runs its static initializers, which could stomp the value if another
//CPP sets this value while initializing its static space
static ExitProcessWithErrorCBFn g_pfnExitProcessWithErrorCB; //= NULL

bool Plat_IsInBenchmarkMode()
{
	return g_bBenchmarkMode;
}

void Plat_SetBenchmarkMode( bool bBenchmark )
{
	g_bBenchmarkMode = bBenchmark;
}

#if defined( PLATFORM_WINDOWS )

static LARGE_INTEGER g_PerformanceFrequency;
static double g_PerformanceCounterToS;
static double g_PerformanceCounterToMS;
static double g_PerformanceCounterToUS;
static LARGE_INTEGER g_ClockStart;
static bool s_bTimeInitted;

static void InitTime()
{
	if( !s_bTimeInitted )
	{
		s_bTimeInitted = true;
		QueryPerformanceFrequency(&g_PerformanceFrequency);
		g_PerformanceCounterToS = 1.0 / g_PerformanceFrequency.QuadPart;
		g_PerformanceCounterToMS = 1e3 / g_PerformanceFrequency.QuadPart;
		g_PerformanceCounterToUS = 1e6 / g_PerformanceFrequency.QuadPart;
		QueryPerformanceCounter(&g_ClockStart);
	}
}

double Plat_FloatTime()
{
	if (! s_bTimeInitted )
		InitTime();
	if ( g_bBenchmarkMode )
	{
		g_FakeBenchmarkTime += g_FakeBenchmarkTimeInc;
		return g_FakeBenchmarkTime;
	}

	LARGE_INTEGER CurrentTime;

	QueryPerformanceCounter( &CurrentTime );

	double fRawSeconds = (double)( CurrentTime.QuadPart - g_ClockStart.QuadPart ) * g_PerformanceCounterToS;

	return fRawSeconds;
}

uint32 Plat_MSTime()
{
	if (! s_bTimeInitted )
		InitTime();
	if ( g_bBenchmarkMode )
	{
		g_FakeBenchmarkTime += g_FakeBenchmarkTimeInc;
		return (uint32)(g_FakeBenchmarkTime * 1000.0);
	}

	LARGE_INTEGER CurrentTime;

	QueryPerformanceCounter( &CurrentTime );

	return (uint32) ( ( CurrentTime.QuadPart - g_ClockStart.QuadPart ) * g_PerformanceCounterToMS );
}

uint64 Plat_USTime()
{
	if (! s_bTimeInitted )
		InitTime();
	if ( g_bBenchmarkMode )
	{
		g_FakeBenchmarkTime += g_FakeBenchmarkTimeInc;
		return (uint64)(g_FakeBenchmarkTime * 1e6);
	}

	LARGE_INTEGER CurrentTime;

	QueryPerformanceCounter( &CurrentTime );

	return (uint64) ( ( CurrentTime.QuadPart - g_ClockStart.QuadPart ) * g_PerformanceCounterToUS );
}

uint64 Plat_GetClockStart()
{
	if ( !s_bTimeInitted )
		InitTime();
	return g_ClockStart.QuadPart;
}

#endif // PLATFORM_WINDOWS/PLATFORM_PS3

uint64 Plat_GetTime()
{
	// We just provide a wrapper on this function so we can protect access to time() everywhere.
	time_t ltime;
	time( &ltime );
	return ltime;
}

void Plat_GetLocalTime( struct tm *pNow )
{
	// We just provide a wrapper on this function so we can protect access to time() everywhere.
	time_t ltime;
	time( &ltime );

	Plat_ConvertToLocalTime( ltime, pNow );
}

void Plat_ConvertToLocalTime( uint64 nTime, struct tm *pNow )
{
	// Since localtime() returns a global, we need to protect against multiple threads stomping it.
	g_LocalTimeMutex.Lock();

	time_t ltime = (time_t)nTime;
	tm *pTime = localtime( &ltime );
	if ( pTime )
		*pNow = *pTime;
	else
		memset( pNow, 0, sizeof( *pNow ) );

	g_LocalTimeMutex.Unlock();
}

void Plat_GetTimeString( struct tm *pTime, char *pOut, int nMaxBytes )
{
	g_LocalTimeMutex.Lock();

	char *pStr = asctime( pTime );
	strncpy( pOut, pStr, nMaxBytes );
	pOut[nMaxBytes-1] = 0;

	g_LocalTimeMutex.Unlock();
}

/*
// timezone
int32 Plat_timezone( void )
{
#ifdef _PS3
	g_LocalTimeMutex.Lock();
	int timezone = 0;
	if ( cellSysutilGetSystemParamInt (CELL_SYSUTIL_SYSTEMPARAM_ID_TIMEZONE, &timezone ) < CELL_OK ) // Not Threadsafe
		timezone = 0;
	g_LocalTimeMutex.Unlock();
	return timezone;
#else
	return timezone;
#endif
}

// daylight savings
int32 Plat_daylight( void )
{
#ifdef _PS3
	g_LocalTimeMutex.Lock();
	int32 daylight = 0;
	if ( cellSysutilGetSystemParamInt (CELL_SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME, &daylight ) < CELL_OK )
		daylight = 0;
	g_LocalTimeMutex.Unlock();
	return daylight;
#else
	return daylight;
#endif
}
*/
void Platform_gmtime( uint64 nTime, struct tm *pTime )
{
	time_t tmtTime = nTime;
	gmtime_s( pTime, &tmtTime );
}

time_t Plat_timegm( struct tm *timeptr )
{
	return _mkgmtime( timeptr );
}

void Plat_GetModuleFilename( char *pOut, int nMaxBytes )
{
#ifdef PLATFORM_WINDOWS_PC
	SetLastError( ERROR_SUCCESS ); // clear the error code
	GetModuleFileName( NULL, pOut, nMaxBytes );
	if ( GetLastError() != ERROR_SUCCESS )
		Error( "Plat_GetModuleFilename: The buffer given is too small (%d bytes).", nMaxBytes );
#else
	// We shouldn't need this on POSIX.
	Assert( false );
	pOut[0] = 0x00;    // Null the returned string in release builds
#endif
}

void Plat_ExitProcess( int nCode )
{
#if defined( _WIN32 )
	// We don't want global destructors in our process OR in any DLL to get executed.
	// _exit() avoids calling global destructors in our module, but not in other DLLs.
	const char *pchCmdLineA = Plat_GetCommandLineA();
	if ( nCode || ( strstr( pchCmdLineA, "gc.exe" ) && strstr( pchCmdLineA, "gc.dll" ) && strstr( pchCmdLineA, "-gc" ) ) )
	{
		int *x = NULL; *x = 1; // cause a hard crash, GC is not allowed to exit voluntarily from gc.dll
	}
	TerminateProcess( GetCurrentProcess(), nCode );
#else	
	_exit( nCode );
#endif
}

void Plat_ExitProcessWithError( int nCode, bool bGenerateMinidump )
{
	//try to delegate out if they have registered a callback
	if( g_pfnExitProcessWithErrorCB )
	{
		if( g_pfnExitProcessWithErrorCB( nCode ) )
			return;
	}

	//handle default behavior
	if( bGenerateMinidump )
	{
		//don't generate mini dumps in the debugger
		if( !Plat_IsInDebugSession() )
		{
			WriteMiniDump();
		}
	}

	//and exit our process
	Plat_ExitProcess( nCode );
}

void Plat_SetExitProcessWithErrorCB( ExitProcessWithErrorCBFn pfnCB )
{
	g_pfnExitProcessWithErrorCB = pfnCB;	
}

void GetCurrentDate( int *pDay, int *pMonth, int *pYear )
{
	struct tm long_time;
	Plat_GetLocalTime( &long_time );

	*pDay = long_time.tm_mday;
	*pMonth = long_time.tm_mon + 1;
	*pYear = long_time.tm_year + 1900;
}

// Wraps the thread-safe versions of asctime. buf must be at least 26 bytes 
char *Plat_asctime( const struct tm *tm, char *buf, size_t bufsize )
{
	if ( EINVAL == asctime_s( buf, bufsize, tm ) )
		return NULL;
	else
		return buf;
}


// Wraps the thread-safe versions of ctime. buf must be at least 26 bytes 
char *Plat_ctime( const time_t *timep, char *buf, size_t bufsize )
{
	if ( EINVAL == ctime_s( buf, bufsize, timep ) )
		return NULL;
	else
		return buf;
}


// Wraps the thread-safe versions of gmtime
struct tm *Platform_gmtime( const time_t *timep, struct tm *result )
{
	if ( EINVAL == gmtime_s( result, timep ) )
		return NULL;
	else
		return result;
}


// Wraps the thread-safe versions of localtime
struct tm *Plat_localtime( const time_t *timep, struct tm *result )
{
	if ( EINVAL == localtime_s( result, timep ) )
		return NULL;
	else
		return result;
}

bool vtune( bool resume )
{
#if IS_WINDOWS_PC
	static bool bInitialized = false;
	static void (__cdecl *VTResume)(void) = NULL;
	static void (__cdecl *VTPause) (void) = NULL;

	// Grab the Pause and Resume function pointers from the VTune DLL the first time through:
	if( !bInitialized )
	{
		bInitialized = true;

		HINSTANCE pVTuneDLL = LoadLibrary( "vtuneapi.dll" );

		if( pVTuneDLL )
		{
			VTResume = (void(__cdecl *)())GetProcAddress( pVTuneDLL, "VTResume" );
			VTPause  = (void(__cdecl *)())GetProcAddress( pVTuneDLL, "VTPause" );
		}
	}

	// Call the appropriate function, as indicated by the argument:
	if( resume && VTResume )
	{
		VTResume();
		return true;

	} 
	else if( !resume && VTPause )
	{
		VTPause();
		return true;
	}
#endif
	return false;
}

bool Plat_IsInDebugSession()
{
#if   defined( _WIN32 )
	return (IsDebuggerPresent() != 0);
#else
	return false;
#endif
}

void Plat_DebugString( const char * psz )
{
#ifdef _CERT
	return; // do nothing!
#endif

#if   defined( _WIN32 )
	::OutputDebugStringA( psz );
#else 
	// do nothing?
#endif
}


#if defined( PLATFORM_WINDOWS_PC )
void Plat_MessageBox( const char *pTitle, const char *pMessage )
{
	MessageBox( NULL, pMessage, pTitle, MB_OK );
}
#endif

PlatOSVersion_t Plat_GetOSVersion()
{
#ifdef PLATFORM_WINDOWS_PC
	OSVERSIONINFO info;
	info.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	if ( GetVersionEx( &info ) )
	{
		if ( info.dwMajorVersion > 24 )
			info.dwMajorVersion = 24;	// clamp to fit the version in single byte
		return PlatOSVersion_t( ( info.dwMajorVersion*10 ) + ( info.dwMinorVersion%10 ) );
	}
	return PLAT_OS_VERSION_UNKNOWN;
#else
	return PLAT_OS_VERSION_UNKNOWN;
#endif
}

PLATFORM_INTERFACE const tchar *Plat_GetCommandLine()
{
#if   defined( TCHAR_IS_WCHAR )
	return GetCommandLineW();
#else
	return GetCommandLine();
#endif
}

PLATFORM_INTERFACE const char *Plat_GetCommandLineA()
{
	return GetCommandLineA();
}


//-----------------------------------------------------------------------------
// Dynamically load a function
//-----------------------------------------------------------------------------
#ifdef PLATFORM_WINDOWS

void *Plat_GetProcAddress( const char *pszModule, const char *pszName )
{
	HMODULE hModule = ::LoadLibrary( pszModule );
	return ( hModule ) ? (void *)::GetProcAddress( hModule, pszName ) : NULL;
}

#endif



// -------------------------------------------------------------------------------------------------- //
// Memory stuff. 
//
// DEPRECATED. Still here to support binary back compatability of tier0.dll
//
// -------------------------------------------------------------------------------------------------- //
#if !defined(STEAM) && !defined(NO_MALLOC_OVERRIDE)

typedef void (*Plat_AllocErrorFn)( unsigned long size );

void Plat_DefaultAllocErrorFn( unsigned long size )
{
}

Plat_AllocErrorFn g_AllocError = Plat_DefaultAllocErrorFn;
#endif


CRITICAL_SECTION g_AllocCS;
class CAllocCSInit
{
public:
	CAllocCSInit()
	{
		InitializeCriticalSection( &g_AllocCS );
	}
} g_AllocCSInit;


PLATFORM_INTERFACE void* Plat_Alloc( unsigned long size )
{
	EnterCriticalSection( &g_AllocCS );
#if !defined(STEAM) && !defined(NO_MALLOC_OVERRIDE)
		void *pRet = MemAlloc_Alloc( size );
#else
		void *pRet = malloc( size );
#endif
	LeaveCriticalSection( &g_AllocCS );
	if ( pRet )
	{
		return pRet;
	}
	else
	{
#if !defined(STEAM) && !defined(NO_MALLOC_OVERRIDE)
		g_AllocError( size );
#endif
		return 0;
	}
}


PLATFORM_INTERFACE void* Plat_Realloc( void *ptr, unsigned long size )
{
	EnterCriticalSection( &g_AllocCS );
#if !defined(STEAM) && !defined(NO_MALLOC_OVERRIDE)
		void *pRet = g_pMemAlloc->Realloc( ptr, size );
#else
		void *pRet = realloc( ptr, size );
#endif
	LeaveCriticalSection( &g_AllocCS );
	if ( pRet )
	{
		return pRet;
	}
	else
	{
#if !defined(STEAM) && !defined(NO_MALLOC_OVERRIDE)
		g_AllocError( size );
#endif
		return 0;
	}
}


PLATFORM_INTERFACE void Plat_Free( void *ptr )
{
	EnterCriticalSection( &g_AllocCS );
#if !defined(STEAM) && !defined(NO_MALLOC_OVERRIDE)
		g_pMemAlloc->Free( ptr );
#else
		free( ptr );
#endif
	LeaveCriticalSection( &g_AllocCS );
}

#if !defined(STEAM) && !defined(NO_MALLOC_OVERRIDE)
PLATFORM_INTERFACE void Plat_SetAllocErrorFn( Plat_AllocErrorFn fn )
{
	g_AllocError = fn;
}
#endif


void Plat_getwd( char *pWorkingDirectory, size_t nBufLen )
{
#if   defined( PLATFORM_WINDOWS )
	_getcwd( pWorkingDirectory, ( int )nBufLen );
	V_tier0_strncat( pWorkingDirectory, "\\", ( int )nBufLen );
#else  // PLATFORM_X360/PLATFORM_WINDOWS
	if ( getcwd( pWorkingDirectory, nBufLen ) )
	{
		V_tier0_strncat( pWorkingDirectory, "/", ( int )nBufLen );
	}
	else
	{
		V_tier0_strncpy( pWorkingDirectory, "./", ( int )nBufLen );
	}
#endif // PLATFORM_X360/PLATFORM_WINDOWS
}

void Plat_chdir( const char *pDir )
{
	int nErr = _chdir( pDir );
	NOTE_UNUSED( nErr );
}

char const * Plat_GetEnv(char const *pEnvVarName)
{
	return getenv(pEnvVarName);
}

bool Plat_GetExecutablePath(char *pBuff, size_t nBuff)
{
	return ::GetModuleFileNameA(NULL, pBuff, (DWORD)nBuff) > 0;
}

int Plat_chmod(const char *filename, int pmode)
{
#if defined( PLATFORM_WINDOWS )
	return _chmod(filename, pmode);
#else
	return chmod(filename, pmode);
#endif
}

bool Plat_FileExists(const char *pFileName)
{
#if defined( PLATFORM_WINDOWS )
	// VS2015 CRT _stat on Windows XP always returns -1 (lol)
	// https://connect.microsoft.com/VisualStudio/feedback/details/1557168/wstat64-returns-1-on-xp-always
	DWORD dwResult = GetFileAttributes(pFileName);
	return (dwResult != INVALID_FILE_ATTRIBUTES);
#else
	struct _stat fileInfo;
	return (_stat(pFileName, &fileInfo) != -1);
#endif
}

size_t Plat_FileSize(const char *pFileName)
{
#if defined( PLATFORM_WINDOWS )
	// VS2015 CRT _stat on Windows XP always returns -1 (lol)
	// https://connect.microsoft.com/VisualStudio/feedback/details/1557168/wstat64-returns-1-on-xp-always
	WIN32_FILE_ATTRIBUTE_DATA fileAttributes;
	BOOL bSuccess = GetFileAttributesEx(pFileName, GetFileExInfoStandard, &fileAttributes);
	if (!bSuccess)
		return 0;

	uint64 ulFileSize;
	ulFileSize = fileAttributes.nFileSizeHigh;
	ulFileSize <<= 32;
	ulFileSize |= fileAttributes.nFileSizeLow;
	return (size_t)ulFileSize;
#else
	struct _stat fileInfo;
	if (_stat(pFileName, &fileInfo) == -1)
		return 0;

	return fileInfo.st_size;
#endif
}

bool Plat_IsDirectory(const char *pFilepath)
{
#if defined( PLATFORM_WINDOWS )
	// VS2015 CRT _stat on Windows XP always returns -1 (lol)
	// https://connect.microsoft.com/VisualStudio/feedback/details/1557168/wstat64-returns-1-on-xp-always
	DWORD dwResult = GetFileAttributes(pFilepath);
	if (dwResult == INVALID_FILE_ATTRIBUTES)
		return false;

	return (dwResult & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
	struct _stat fileInfo;
	if (_stat(pFilepath, &fileInfo) == -1)
		return false;

	return ((fileInfo.st_mode & _S_IFDIR) != 0);
#endif
}

bool Plat_FileIsReadOnly(const char *pFileName)
{
#if defined( PLATFORM_WINDOWS )
	// VS2015 CRT _stat on Windows XP always returns -1 (lol)
	// https://connect.microsoft.com/VisualStudio/feedback/details/1557168/wstat64-returns-1-on-xp-always
	DWORD dwResult = GetFileAttributes(pFileName);
	if (dwResult == INVALID_FILE_ATTRIBUTES)
		return false; // See below.

	return (dwResult & FILE_ATTRIBUTE_READONLY) != 0;
#else
	struct _stat fileInfo;
	if (_stat(pFileName, &fileInfo) == -1)
		return false; //for the purposes of this test, a nonexistent file is more writable than it is readable.

	return ((fileInfo.st_mode & _S_IWRITE) == 0);
#endif
}
