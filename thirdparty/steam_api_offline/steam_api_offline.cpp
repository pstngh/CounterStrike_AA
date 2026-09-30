// Offline Steam API entry points for the macOS and Windows builds.  The game still
// uses the Steamworks interfaces at compile time, but no Steam client or
// proprietary Steam API runtime is loaded for insecure LAN play.

#include "steam/steam_gameserver.h"

S_API bool S_CALLTYPE SteamAPI_Init() { return false; }
S_API bool S_CALLTYPE SteamAPI_InitSafe() { return false; }
S_API void S_CALLTYPE SteamAPI_Shutdown() {}
S_API bool S_CALLTYPE SteamAPI_RestartAppIfNecessary( uint32 ) { return false; }
S_API bool S_CALLTYPE SteamAPI_IsSteamRunning() { return false; }
S_API void S_CALLTYPE SteamAPI_RunCallbacks() {}
S_API void S_CALLTYPE SteamAPI_ReleaseCurrentThreadMemory() {}

S_API HSteamPipe SteamAPI_GetHSteamPipe() { return 0; }
S_API HSteamUser SteamAPI_GetHSteamUser() { return 0; }
S_API HSteamPipe GetHSteamPipe() { return 0; }
S_API HSteamUser GetHSteamUser() { return 0; }
S_API HSteamUser Steam_GetHSteamUserCurrent() { return 0; }
S_API const char *SteamAPI_GetSteamInstallPath() { return ""; }

S_API void * S_CALLTYPE SteamInternal_CreateInterface( const char * ) { return NULL; }
S_API void * S_CALLTYPE SteamGameServerInternal_CreateInterface( const char * ) { return NULL; }
S_API bool S_CALLTYPE SteamInternal_Init() { return false; }

S_API void S_CALLTYPE SteamAPI_RegisterCallback( CCallbackBase *, int ) {}
S_API void S_CALLTYPE SteamAPI_UnregisterCallback( CCallbackBase * ) {}
S_API void S_CALLTYPE SteamAPI_RegisterCallResult( CCallbackBase *, SteamAPICall_t ) {}
S_API void S_CALLTYPE SteamAPI_UnregisterCallResult( CCallbackBase *, SteamAPICall_t ) {}
S_API void Steam_RunCallbacks( HSteamPipe, bool ) {}
S_API void Steam_RegisterInterfaceFuncs( void * ) {}
S_API void SteamAPI_SetTryCatchCallbacks( bool ) {}

S_API void S_CALLTYPE SteamAPI_WriteMiniDump( uint32, void *, uint32 ) {}
S_API void S_CALLTYPE SteamAPI_SetMiniDumpComment( const char * ) {}
S_API void S_CALLTYPE SteamAPI_UseBreakpadCrashHandler( const char *, const char *, const char *, bool, void *, PFNPreMinidumpCallback ) {}
S_API void S_CALLTYPE SteamAPI_SetBreakpadAppID( uint32 ) {}

S_API HSteamPipe S_CALLTYPE SteamGameServer_GetHSteamPipe() { return 0; }
S_API HSteamUser S_CALLTYPE SteamGameServer_GetHSteamUser() { return 0; }
S_API bool S_CALLTYPE SteamInternal_GameServer_Init( uint32, uint16, uint16, uint16, EServerMode, const char * ) { return false; }
S_API void SteamGameServer_Shutdown() {}
S_API void SteamGameServer_RunCallbacks() {}
S_API bool SteamGameServer_BSecure() { return false; }
S_API uint64 SteamGameServer_GetSteamID() { return 0; }
S_API int SteamGameServer_GetIPCCallCount() { return 0; }
