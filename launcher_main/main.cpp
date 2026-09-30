//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: A redirection tool that allows the DLLs to reside elsewhere.
//
//=====================================================================================//

#if defined( _WIN32 )
#include <windows.h>
#include <stdio.h>
#include <assert.h>
#include <direct.h>
#endif
#ifdef POSIX
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <limits.h>
#include <string.h>
#define MAX_PATH PATH_MAX
#endif

#include "tier0/platform.h"
#include "tier0/basetypes.h"

#if defined( VPCGAME )
#define _VPCGAME_STRING_HACK2(x) #x
#define _VPCGAME_STRING_HACK1(x) _VPCGAME_STRING_HACK2(x)
#define VPCGAME_STRING _VPCGAME_STRING_HACK1(VPCGAME)
#endif

#ifdef WIN32
typedef int (*LauncherMain_t)( HINSTANCE hInstance, HINSTANCE hPrevInstance, 
							  LPSTR lpCmdLine, int nCmdShow );
#elif POSIX
typedef int (*LauncherMain_t)( int argc, char **argv );
#else
#error
#endif


#ifdef WIN32
// hinting the nvidia driver to use the dedicated graphics card in an optimus configuration
// for more info, see: http://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf
extern "C" { __declspec( dllexport ) DWORD NvOptimusEnablement = 0x00000001; }

// same thing for AMD GPUs using v13.35 or newer drivers
extern "C" { __declspec( dllexport ) int AmdPowerXpressRequestHighPerformance = 1; }

#endif



//-----------------------------------------------------------------------------
// Purpose: Return the directory where this .exe is running from
// Output : char
//-----------------------------------------------------------------------------

#ifdef WIN32

static char *GetBaseDir( const char *pszBuffer )
{
	static char	basedir[ MAX_PATH ];
	char szBuffer[ MAX_PATH ];
	size_t j;
	char *pBuffer = NULL;

	strcpy( szBuffer, pszBuffer );

	pBuffer = strrchr( szBuffer,'\\' );
	if ( pBuffer )
	{
		*(pBuffer+1) = '\0';
	}

	strcpy( basedir, szBuffer );

	j = strlen( basedir );
	if (j > 0)
	{
		if ( ( basedir[ j-1 ] == '\\' ) || 
			 ( basedir[ j-1 ] == '/' ) )
		{
			basedir[ j-1 ] = 0;
		}
	}

	return basedir;
}

int APIENTRY WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
{
	// Must add 'bin' to the path....
	char* pPath = getenv("PATH");

	// Use the .EXE name to determine the root directory
	char moduleName[ MAX_PATH ];
	char szBuffer[4096];
	if ( !GetModuleFileName( hInstance, moduleName, MAX_PATH ) )
	{
		MessageBox( 0, "Failed calling GetModuleFileName", "Launcher Error", MB_OK );
		return 0;
	}

	// Get the root directory the .exe is in
	char* pRootDir = GetBaseDir( moduleName );

	const char* pBinPath = 
#ifdef _WIN64
		"\\x64"
#else
		""
#endif
	;

#ifdef _DEBUG
	int len = 
#endif
	_snprintf( szBuffer, sizeof( szBuffer ), "PATH=%s\\bin%s\\;%s", pRootDir, pBinPath, pPath );
	szBuffer[sizeof( szBuffer ) - 1] = '\0';
	assert( len < sizeof( szBuffer ) );
	_putenv( szBuffer );

	// Assemble the full path to our "launcher.dll"
	_snprintf( szBuffer, sizeof( szBuffer ), "%s\\bin%s\\launcher.dll", pRootDir, pBinPath );
	szBuffer[sizeof( szBuffer ) - 1] = '\0';

	// STEAM OK ... filesystem not mounted yet
	HINSTANCE launcher = LoadLibraryEx( szBuffer, NULL, LOAD_WITH_ALTERED_SEARCH_PATH );
	if ( !launcher )
	{
		char *pszError;
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&pszError, 0, NULL);

		char szBuf[1024];
		_snprintf(szBuf, sizeof( szBuf ), "Failed to load the launcher DLL:\n\n%s", pszError);
		szBuf[sizeof( szBuf ) - 1] = '\0';
		MessageBox( 0, szBuf, "Launcher Error", MB_OK );

		LocalFree(pszError);
		return 0;
	}

	LauncherMain_t main = (LauncherMain_t)GetProcAddress( launcher, "LauncherMain" );
	return main( hInstance, hPrevInstance, lpCmdLine, nCmdShow );
}

#elif defined (POSIX)

int main( int argc, char *argv[] )
{
#ifdef PLATFORM_64BITS
	#ifdef OSX
		const char *pLauncherPath = "bin/osx64/launcher" DLL_EXT_STRING;
	#else
		const char *pLauncherPath = "bin/linux64/launcher" DLL_EXT_STRING;
	#endif
#else
	const char *pLauncherPath = "bin/launcher" DLL_EXT_STRING;
#endif

	void *launcher = dlopen( pLauncherPath, RTLD_NOW );
	
	if ( !launcher )
	{
	    //lwss - add dll path in error
		printf( "Failed to load the launcher(%s) (%s)\n", pLauncherPath, dlerror() );
		//lwss end
		while(1);
		return 0;
	}
	
	LauncherMain_t main = (LauncherMain_t)dlsym( launcher, "LauncherMain" );
	if ( !main )
	{
		printf( "Failed to load the launcher entry proc\n" );
		while(1);
		return 0;
	}

	return main( argc, argv );
}

#else
#error
#endif // WIN32 || POSIX


