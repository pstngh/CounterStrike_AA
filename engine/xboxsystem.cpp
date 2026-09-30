//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Interface to Xbox 360 system functions. Helps deal with the async system and Live
//			functions by either providing a handle for the caller to check results or handling
//			automatic cleanup of the async data when the caller doesn't care about the results.
//
//=====================================================================================//

#include "host.h"
#include "tier3/tier3.h"
#include "vgui/ILocalize.h"
#include "ixboxsystem.h"

#ifdef IS_WINDOWS_PC
#include "winerror.h"
#endif

#include "vstdlib/random.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


static wchar_t g_szModSaveContainerDisplayName[XCONTENT_MAX_DISPLAYNAME_LENGTH] = L"";
static char g_szModSaveContainerName[XCONTENT_MAX_FILENAME_LENGTH] = "";

#define XBX_USER_SETTINGS_CONTAINER_ENABLED 1

#if !defined( CSTRIKE_TRIAL_MODE )
#	define CSTRIKE_TRIAL_MODE 0
#endif

//-----------------------------------------------------------------------------
// Implementation of IXboxSystem interface
//-----------------------------------------------------------------------------
class CXboxSystem : public IXboxSystem
{
public:
	CXboxSystem( void );

	virtual	~CXboxSystem( void );

	virtual AsyncHandle_t	CreateAsyncHandle( void );
	virtual void			ReleaseAsyncHandle( AsyncHandle_t handle );
	virtual int				GetOverlappedResult( AsyncHandle_t handle, uint *pResultCode, bool bWait );
	virtual void			CancelOverlappedOperation( AsyncHandle_t handle );

	// Save/Load
	virtual bool			GameHasSavegames( void );
	virtual void			GetModSaveContainerNames( const char *pchModName, const wchar_t **ppchDisplayName, const char **ppchName );
	virtual uint			GetContainerRemainingSpace( DWORD nDeviceID );
	virtual bool			DeviceCapacityAdequate( int iController, DWORD nDeviceID, const char *pModName );
	virtual DWORD			DiscoverUserData( DWORD nUserID, const char *pModName );

	// XUI
	virtual bool			ShowDeviceSelector( int iController, bool bForce, uint *pStorageID, AsyncHandle_t *pHandle );
	virtual void			ShowSigninUI( uint nPanes, uint nFlags );

	// Rich Presence and Matchmaking
	virtual int				UserSetContext( uint nUserIdx, XUSER_CONTEXT const &xc, bool bAsync, AsyncHandle_t *pHandle);
	virtual int				UserSetProperty( uint nUserIndex, XUSER_PROPERTY const &xp, bool bAsync, AsyncHandle_t *pHandle );
	virtual int				UserGetContext( uint nUserIdx, uint nContextID, uint &nContextValue);
	virtual int				UserGetPropertyInt( uint nUserIndex, uint nPropertyId, uint &nPropertyValue);

	// Matchmaking
	virtual int				CreateSession( uint nFlags, uint nUserIdx, uint nMaxPublicSlots, uint nMaxPrivateSlots, uint64 *pNonce, void *pSessionInfo, XboxHandle_t *pSessionHandle, bool bAsync, AsyncHandle_t *pAsyncHandle );
	virtual uint			DeleteSession( XboxHandle_t hSession, bool bAsync, AsyncHandle_t *pAsyncHandle = NULL );
	virtual uint			SessionSearch( uint nProcedureIndex, uint nUserIndex, uint nNumResults, uint nNumUsers, uint nNumProperties, uint nNumContexts, XUSER_PROPERTY *pSearchProperties, XUSER_CONTEXT *pSearchContexts, uint *pcbResultsBuffer, XSESSION_SEARCHRESULT_HEADER *pSearchResults, bool bAsync, AsyncHandle_t *pAsyncHandle );
	virtual uint			SessionStart( XboxHandle_t hSession, uint nFlags, bool bAsync, AsyncHandle_t *pAsyncHandle );
	virtual uint			SessionEnd( XboxHandle_t hSession, bool bAsync, AsyncHandle_t *pAsyncHandle );
	virtual int				SessionJoinLocal( XboxHandle_t hSession, uint nUserCount, const uint *pUserIndexes, const bool *pPrivateSlots, bool bAsync, AsyncHandle_t *pAsyncHandle );
	virtual int				SessionJoinRemote( XboxHandle_t hSession, uint nUserCount, const XUID *pXuids, const bool *pPrivateSlot, bool bAsync, AsyncHandle_t *pAsyncHandle );
	virtual int				SessionLeaveLocal( XboxHandle_t hSession, uint nUserCount, const uint *pUserIndexes, bool bAsync, AsyncHandle_t *pAsyncHandle );
	virtual int				SessionLeaveRemote( XboxHandle_t hSession, uint nUserCount, const XUID *pXuids, bool bAsync, AsyncHandle_t *pAsyncHandle );
	virtual int				SessionMigrate( XboxHandle_t hSession, uint nUserIndex, void *pSessionInfo, bool bAsync, AsyncHandle_t *pAsyncHandle );
	virtual int				SessionArbitrationRegister( XboxHandle_t hSession, uint nFlags, uint64 nonce, uint *pBytes, void *pBuffer, bool bAsync, AsyncHandle_t *pAsyncHandle );

	// Friends
	virtual int				EnumerateFriends( uint userIndex, void **pBuffer, bool bAsync, AsyncHandle_t *pAsyncHandle = NULL );

	// Stats
	virtual int				WriteStats( XboxHandle_t hSession, XUID xuid, uint nViews, void* pViews, bool bAsync, AsyncHandle_t *pAsyncHandle );
	virtual int				FlushStats( XboxHandle_t hSession, bool bAsync, AsyncHandle_t *pAsyncHandle );
	virtual int				EnumerateStatsByRank( uint nStartingRank, uint nNumRows, uint nNumSpecs, void *pSpecs, void **ppResults, bool bAsync, AsyncHandle_t *pAsyncHandle );
	virtual int				EnumerateStatsByXuid( XUID nUserId, uint nNumRows, uint nNumSpecs, void *pSpecs, void **ppResults, bool bAsync, AsyncHandle_t *pAsyncHandle );

	// Achievements
	virtual int				EnumerateAchievements( uint nUserIdx, uint64 xuid, uint nStartingIdx, uint nCount, void *pBuffer, uint nBufferBytes, bool bAsync, AsyncHandle_t *pAsyncHandle );
	virtual int				AwardAchievement( uint nUserIdx, uint nAchievementId, AsyncHandle_t *ppOverlappedResult );
	virtual int				AwardAvatarAsset( uint nUserIdx, uint nAwardId, AsyncHandle_t *ppOverlappedResult );

	// Arcade titles
	virtual void			ShowUnlockFullGameUI( void );
	virtual bool			UpdateArcadeTitleUnlockStatus( void );
	virtual bool			IsArcadeTitleUnlocked( void );
	virtual float			GetArcadeRemainingTrialTime( int nSlot = 0 );

	virtual void			FinishContainerWrites( int iController );
	virtual uint			GetContainerOpenResult( int iController );
	virtual uint			OpenContainers( int iController );
	virtual void			CloseContainers( int iController );

	virtual void			FinishAllContainerWrites( void );
	virtual void			CloseAllContainers( void );

	//
	// Overlapped
	//
	virtual int				Io_HasOverlappedIoCompleted( XOVERLAPPED *pOverlapped );

	//
	// XNet
	//
	virtual int				NetRandom( byte *pb, unsigned numBytes );
	virtual DWORD			NetGetTitleXnAddr( XNADDR *pxna );
	virtual int				NetXnAddrToMachineId( const XNADDR *pxnaddr, uint64 *pqwMachineId );
	virtual int				NetInAddrToXnAddr( const IN_ADDR ina, XNADDR *pxna, XNKID *pxnkid );
	virtual int				NetXnAddrToInAddr( const XNADDR *pxna, const XNKID *pxnkid, IN_ADDR *pina );

	//
	// User
	//
	virtual XUSER_SIGNIN_STATE UserGetSigninState( int iCtrlr );

private:
	virtual uint			CreateSavegameContainer( int iController, uint nCreationFlags );
	virtual uint			CreateUserSettingsContainer( int iController, uint nCreationFlags );

	uint					m_OpenContainerResult[ 4 ];
};

static CXboxSystem s_XboxSystem;
IXboxSystem *g_pXboxSystem = &s_XboxSystem;

EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CXboxSystem, IXboxSystem, XBOXSYSTEM_INTERFACE_VERSION, s_XboxSystem );

#define ASYNC_RESULT(ph) 	((AsyncResult_t*)*ph);

//-----------------------------------------------------------------------------
// Holds the overlapped object and any persistent data for async system calls
//-----------------------------------------------------------------------------
typedef struct AsyncResult_s
{
	XOVERLAPPED		overlapped;
	bool			bAutoRelease;
	void			*pInputData;
	AsyncResult_s	*pNext;
} AsyncResult_t;

static AsyncResult_t * g_pAsyncResultHead = NULL;

//-----------------------------------------------------------------------------
// Purpose: Remove an AsyncResult_t from the list
//-----------------------------------------------------------------------------
static void ReleaseAsyncResult( AsyncResult_t *pAsyncResult )
{
	if ( pAsyncResult == g_pAsyncResultHead )
	{
		g_pAsyncResultHead = pAsyncResult->pNext;
		free( pAsyncResult->pInputData );
		delete pAsyncResult;
		return;
	}

	AsyncResult_t *pNode = g_pAsyncResultHead;
	while ( pNode->pNext )
	{
		if ( pNode->pNext == pAsyncResult )
		{
			pNode->pNext = pAsyncResult->pNext;
			free( pAsyncResult->pInputData );
			delete pAsyncResult;
			return;
		}
		pNode = pNode->pNext;
	}
	Warning( "AsyncResult_t not found in ReleaseAsyncResult.\n" );
}

//-----------------------------------------------------------------------------
// Purpose: Remove an AsyncResult_t from the list
//-----------------------------------------------------------------------------
static void ReleaseAsyncResult( XOVERLAPPED *pOverlapped )
{
	AsyncResult_t *pResult = g_pAsyncResultHead;
	while ( pResult )
	{
		if ( &pResult->overlapped == pOverlapped )
		{
			ReleaseAsyncResult( pResult );
			return;
		}
	}
	Warning( "XOVERLAPPED couldn't be found in ReleaseAsyncResult.\n" );
}

//-----------------------------------------------------------------------------
// Purpose: Release async results that were marked for auto-release.
//-----------------------------------------------------------------------------
static void CleanupFinishedAsyncResults()
{
	AsyncResult_t *pResult = g_pAsyncResultHead;
	AsyncResult_t *pNext;
	while( pResult )
	{
		pNext = pResult->pNext;
		if ( pResult->bAutoRelease )
		{
			bool bCompleted = true;
			if ( bCompleted )
			{
				ReleaseAsyncResult( pResult );
			}
		}
		pResult = pNext;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Add a new AsyncResult_t object to the list
//-----------------------------------------------------------------------------
static AsyncResult_t *CreateAsyncResult( bool bAutoRelease )
{
	// Take this opportunity to clean up finished operations
	CleanupFinishedAsyncResults();

	AsyncResult_t *pAsyncResult = new AsyncResult_t;
	memset( pAsyncResult, 0, sizeof( AsyncResult_t ) );

	pAsyncResult->pNext = g_pAsyncResultHead;
	g_pAsyncResultHead = pAsyncResult;

	if ( bAutoRelease )
	{
		pAsyncResult->bAutoRelease = true;
	}

	return pAsyncResult;
}

//-----------------------------------------------------------------------------
// Purpose: Return an AsyncResult_t object to the pool
//-----------------------------------------------------------------------------
static void InitializeAsyncHandle( AsyncHandle_t *pHandle )
{
	XOVERLAPPED *pOverlapped = &((AsyncResult_t *)*pHandle)->overlapped;
	memset( pOverlapped, 0, sizeof( XOVERLAPPED ) );
}

//-----------------------------------------------------------------------------
// Purpose: Initialize or create and async handle
//-----------------------------------------------------------------------------
static AsyncResult_t *InitializeAsyncResult( AsyncHandle_t **ppAsyncHandle )
{
	AsyncResult_t *pResult = NULL;
	if ( *ppAsyncHandle )
	{
		InitializeAsyncHandle( *ppAsyncHandle );
		pResult = ASYNC_RESULT( *ppAsyncHandle );
	}
	else
	{
		// No handle provided, create one
		pResult = CreateAsyncResult( true );
	}
	return pResult;
}

CXboxSystem::CXboxSystem( void )
{
	memset( m_OpenContainerResult, 0, sizeof( m_OpenContainerResult ) );
}

//-----------------------------------------------------------------------------
// Purpose: Force overlapped operations to finish and clean up
//-----------------------------------------------------------------------------
CXboxSystem::~CXboxSystem()
{
	// Force async operations to finish.
	AsyncResult_t *pResult = g_pAsyncResultHead;
	while ( pResult )
	{
		AsyncResult_t *pNext = pResult->pNext;
		GetOverlappedResult( (AsyncHandle_t)pResult, NULL, true );
		pResult = pNext;
	}

	// Release any remaining handles - should have been released by the client that created them.
	int ct = 0;
	while ( g_pAsyncResultHead )
	{
		ReleaseAsyncResult( g_pAsyncResultHead );
		++ct;
	}

	if ( ct )
	{
		Warning( "Released %d async handles\n", ct );
	}
}

//-----------------------------------------------------------------------------
//	Purpose: Check on the result of an overlapped operation
//-----------------------------------------------------------------------------
int CXboxSystem::GetOverlappedResult( AsyncHandle_t handle, uint *pResultCode, bool bWait )
{
	if ( pResultCode )
		*pResultCode = ERROR_SUCCESS;
	return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
//	Purpose: Cancel an overlapped operation
//-----------------------------------------------------------------------------
void CXboxSystem::CancelOverlappedOperation( AsyncHandle_t handle )
{
	(void) 0;
}

//-----------------------------------------------------------------------------
// Purpose: Create a new AsyncHandle_t
//-----------------------------------------------------------------------------
AsyncHandle_t CXboxSystem::CreateAsyncHandle( void )
{
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Delete an AsyncHandle_t
//-----------------------------------------------------------------------------
void CXboxSystem::ReleaseAsyncHandle( AsyncHandle_t handle )
{
	(void) 0;
}

//-----------------------------------------------------------------------------
// Purpose: Close the open containers
//-----------------------------------------------------------------------------
void CXboxSystem::CloseContainers( int iController )
{
}

//-----------------------------------------------------------------------------
// Purpose: Close all open containers
//-----------------------------------------------------------------------------
void CXboxSystem::CloseAllContainers( void )
{
	for ( DWORD k = 0; k < XUSER_MAX_COUNT; ++ k )
		CloseContainers( k );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
uint CXboxSystem::OpenContainers( int iController )
{
	m_OpenContainerResult[ iController ] = ERROR_SUCCESS;

	return m_OpenContainerResult[ iController ];
}

//-----------------------------------------------------------------------------
// Purpose: Returns the results from the last container opening
//-----------------------------------------------------------------------------
uint CXboxSystem::GetContainerOpenResult( int iController )
{
	m_OpenContainerResult[ iController ] = ERROR_SUCCESS;

	return m_OpenContainerResult[ iController ];
}

//-----------------------------------------------------------------------------
//	Purpose: Open the save game container for the current mod
//-----------------------------------------------------------------------------
uint CXboxSystem::CreateSavegameContainer( int iController, uint nCreationFlags )
{
	return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
//	Purpose: Open the user settings container for the current mod
//-----------------------------------------------------------------------------
uint CXboxSystem::CreateUserSettingsContainer( int iController, uint nCreationFlags )
{
	return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CXboxSystem::FinishContainerWrites( int iController )
{
}

void CXboxSystem::FinishAllContainerWrites( void )
{
	for ( DWORD k = 0; k < XUSER_MAX_COUNT; ++ k )
		FinishContainerWrites( k );
}

//-----------------------------------------------------------------------------
// Purpose: Determine if game has savegame containers
//-----------------------------------------------------------------------------
bool CXboxSystem::GameHasSavegames( void )
{
	static bool s_bInitialized = false;
	static bool s_bHasSavegames = true;

	if ( !s_bInitialized )
	{
		const char *pszMod = GetCurrentMod();

		if ( !Q_stricmp( pszMod, "left4dead2" ) )
			s_bHasSavegames = false;
		else if ( !Q_stricmp( pszMod, "tf" ) )
			s_bHasSavegames = false;

		s_bInitialized = true;
	}

	return s_bHasSavegames;
}

//-----------------------------------------------------------------------------
// Purpose: Retrieve the names used for our save game container
// Input  : *pchModName - Name of the mod we're running (tf, hl2, etc)
//			**ppchDisplayName - Display name that will be presented to users by the console
//			**ppchName - Filename of the container
//-----------------------------------------------------------------------------
void CXboxSystem::GetModSaveContainerNames( const char *pchModName, const wchar_t **ppchDisplayName, const char **ppchName )
{
	// If the strings haven't been setup
	if ( g_szModSaveContainerDisplayName[ 0 ] == '\0' )
	{
		char chFmtString[256] = {0};
		Q_snprintf( chFmtString, sizeof( chFmtString ), "#GameUI_Console_%s_Saves", pchModName );
		wchar_t const *wszLocStr = g_pVGuiLocalize->Find( chFmtString );
		if ( !wszLocStr || !*wszLocStr )
			wszLocStr = g_pVGuiLocalize->Find( "#GameUI_Console_SaveGames" );
		if ( !wszLocStr || !*wszLocStr )
			wszLocStr = L"SAVES";

		Q_wcsncpy( g_szModSaveContainerDisplayName, wszLocStr, sizeof( g_szModSaveContainerDisplayName ) );

		// Create a filename with the format "mod_saves"
		Q_snprintf( g_szModSaveContainerName, sizeof( g_szModSaveContainerName ), "%s_saves", pchModName );
	}

	// Return pointers to these internally kept strings
	*ppchDisplayName = g_szModSaveContainerDisplayName;
	*ppchName = g_szModSaveContainerName;
}

//-----------------------------------------------------------------------------
// Purpose: Search the device and find out if we have adequate space to start a game
// Input  : nStorageID - Device to check
//			*pModName - Name of the mod we want to check for
//-----------------------------------------------------------------------------
bool CXboxSystem::DeviceCapacityAdequate( int iController, DWORD nStorageID, const char *pModName )
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Enumerate all devices and search for game data already present.  If only one device has it, we return it
// Input  : nUserID - User whose data we're searching for
//			*pModName - Name of the mod we're searching for
// Output : Device ID which contains our data (-1 if no data was found, or data resided on multiple devices)
//-----------------------------------------------------------------------------
DWORD CXboxSystem::DiscoverUserData( DWORD nUserID, const char *pModName )
{
	return XBX_INVALID_STORAGE_ID;
}

//-----------------------------------------------------------------------------
// Purpose: Space free on the current device
//-----------------------------------------------------------------------------
uint CXboxSystem::GetContainerRemainingSpace( DWORD nStorageID )
{
	return 1024*1024*1024; // 1 Gb
}

//-----------------------------------------------------------------------------
//	Purpose: Show the storage device selector
//-----------------------------------------------------------------------------
bool CXboxSystem::ShowDeviceSelector( int iController, bool bForce, uint *pStorageID, AsyncHandle_t *pAsyncHandle  )
{
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Show the user sign in screen
//-----------------------------------------------------------------------------
void CXboxSystem::ShowSigninUI( uint nPanes, uint nFlags )
{
}

//-----------------------------------------------------------------------------
//	Purpose: Set a user context
//-----------------------------------------------------------------------------
int CXboxSystem::UserSetContext( uint nUserIdx, XUSER_CONTEXT const &xc, bool bAsync, AsyncHandle_t *pAsyncHandle )
{
	return 0;
}

//-----------------------------------------------------------------------------
//	Purpose: Set a user property
//-----------------------------------------------------------------------------
int CXboxSystem::UserSetProperty( uint nUserIndex, XUSER_PROPERTY const &xp, bool bAsync, AsyncHandle_t *pAsyncHandle )
{
	return 0;
}

//-----------------------------------------------------------------------------
//	Purpose: Get a user context
//-----------------------------------------------------------------------------
int	CXboxSystem::UserGetContext( uint nUserIdx, uint nContextID, uint &nContextValue) 
{
	return 0;
};

int CXboxSystem::UserGetPropertyInt( uint nUserIndex, uint nPropertyId, uint &nPropertyValue)
{
	return 0;
}

//-----------------------------------------------------------------------------
//	Purpose: Create a matchmaking session
//-----------------------------------------------------------------------------
int CXboxSystem::CreateSession( uint nFlags, 
							    uint nUserIdx, 
								uint nMaxPublicSlots, 
								uint nMaxPrivateSlots, 
								uint64 *pNonce,  
								void *pSessionInfo,
								XboxHandle_t *pSessionHandle,
								bool bAsync,
								AsyncHandle_t *pAsyncHandle 
								)
{
	return ERROR_ACCESS_DISABLED_BY_POLICY;
}

//-----------------------------------------------------------------------------
//	Purpose: Destroy a matchmaking session
//-----------------------------------------------------------------------------
uint CXboxSystem::DeleteSession( XboxHandle_t hSession, bool bAsync, AsyncHandle_t *pAsyncHandle )
{
	return ERROR_ACCESS_DISABLED_BY_POLICY;
}

//-----------------------------------------------------------------------------
//	Purpose: Create a matchmaking session
//-----------------------------------------------------------------------------
uint CXboxSystem::SessionSearch( uint nProcedureIndex,
								 uint nUserIndex,
								 uint nNumResults,
								 uint nNumUsers,
								 uint nNumProperties,
								 uint nNumContexts,
								 XUSER_PROPERTY *pSearchProperties,
								 XUSER_CONTEXT *pSearchContexts,
								 uint *pcbResultsBuffer,
								 XSESSION_SEARCHRESULT_HEADER *pSearchResults,
								 bool	bAsync,
								 AsyncHandle_t *pAsyncHandle
								 )
{
	return ERROR_ACCESS_DISABLED_BY_POLICY;
}

//-----------------------------------------------------------------------------
//	Purpose: Starting a multiplayer game
//-----------------------------------------------------------------------------
uint CXboxSystem::SessionStart( XboxHandle_t hSession, uint nFlags, bool bAsync, AsyncHandle_t *pAsyncHandle )
{
	return ERROR_ACCESS_DISABLED_BY_POLICY;
}

//-----------------------------------------------------------------------------
//	Purpose: Finished a multiplayer game
//-----------------------------------------------------------------------------
uint CXboxSystem::SessionEnd( XboxHandle_t hSession, bool bAsync, AsyncHandle_t *pAsyncHandle )
{
	return ERROR_ACCESS_DISABLED_BY_POLICY;
}

//-----------------------------------------------------------------------------
//	Purpose: Join local users to a session
//-----------------------------------------------------------------------------
int	CXboxSystem::SessionJoinLocal( XboxHandle_t hSession, uint nUserCount, const uint *pUserIndexes, const bool *pPrivateSlots, bool bAsync, AsyncHandle_t *pAsyncHandle )
{
	return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
//	Purpose: Join remote users to a session
//-----------------------------------------------------------------------------
int	CXboxSystem::SessionJoinRemote( XboxHandle_t hSession, uint nUserCount, const XUID *pXuids, const bool *pPrivateSlots, bool bAsync, AsyncHandle_t *pAsyncHandle )
{
	return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
//	Purpose: Remove local users from a session
//-----------------------------------------------------------------------------
int	CXboxSystem::SessionLeaveLocal( XboxHandle_t hSession, uint nUserCount, const uint *pUserIndexes, bool bAsync, AsyncHandle_t *pAsyncHandle )
{
	return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
//	Purpose: Remove remote users from a session
//-----------------------------------------------------------------------------
int	CXboxSystem::SessionLeaveRemote( XboxHandle_t hSession, uint nUserCount, const XUID *pXuids, bool bAsync, AsyncHandle_t *pAsyncHandle )
{
	return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
//	Purpose: Migrate a session to a new host
//-----------------------------------------------------------------------------
int	CXboxSystem::SessionMigrate( XboxHandle_t hSession, uint nUserIndex, void *pSessionInfo, bool bAsync, AsyncHandle_t *pAsyncHandle )
{
	return ERROR_SUCCESS;	// On PC migration is not necessary because sessions are server-side
}

//-----------------------------------------------------------------------------
//	Purpose: Register for arbitration
//-----------------------------------------------------------------------------
int	CXboxSystem::SessionArbitrationRegister( XboxHandle_t hSession, uint nFlags, uint64 nonce, uint *pBytes, void *pBuffer, bool bAsync, AsyncHandle_t *pAsyncHandle )
{
	return ERROR_ACCESS_DISABLED_BY_POLICY;
}

//-----------------------------------------------------------------------------
//	Purpose: Get a list of the players friends
//-----------------------------------------------------------------------------
int CXboxSystem::EnumerateFriends( uint userIndex, void **ppBuffer, bool bAsync, AsyncHandle_t *pAsyncHandle )
{
	return ERROR_ACCESS_DISABLED_BY_POLICY;
}

//-----------------------------------------------------------------------------
//	Purpose: Upload player stats to Xbox Live
//-----------------------------------------------------------------------------
int	CXboxSystem::WriteStats( XboxHandle_t hSession, XUID xuid, uint nViews, void* pViews, bool bAsync, AsyncHandle_t *pAsyncHandle )
{
	return ERROR_ACCESS_DISABLED_BY_POLICY;
}

//-----------------------------------------------------------------------------
//	Purpose: Upload player stats to Xbox Live
//-----------------------------------------------------------------------------
int	CXboxSystem::FlushStats( XboxHandle_t hSession, bool bAsync, AsyncHandle_t *pAsyncHandle )
{
	return ERROR_ACCESS_DISABLED_BY_POLICY;
}

//-----------------------------------------------------------------------------
//	Purpose: Enumerate player stats for a specific range starting at a particular rank
//-----------------------------------------------------------------------------
int CXboxSystem::EnumerateStatsByRank( 	uint nStartingRank, 
										uint nNumRows, 
										uint nNumSpecs,
										void *pSpecs,
										void **ppResults, 
										bool bAsync,
										AsyncHandle_t *pAsyncHandle 
										)
{
	return ERROR_ACCESS_DISABLED_BY_POLICY;
}

//-----------------------------------------------------------------------------
//	Purpose: Enumerate player stats for a specific range starting at a particular rank
//-----------------------------------------------------------------------------
int CXboxSystem::EnumerateStatsByXuid( 	XUID nUserId, 
										uint nNumRows, 
										uint nNumSpecs,
										void *pSpecs,
										void **ppResults, 
										bool bAsync,
										AsyncHandle_t *pAsyncHandle 
										)
{
	return ERROR_ACCESS_DISABLED_BY_POLICY;
}

//-----------------------------------------------------------------------------
//	Purpose: Enumerate a player's achievements
//-----------------------------------------------------------------------------
int CXboxSystem::EnumerateAchievements( uint nUserIdx, 
									    uint64 xuid, 
										uint nStartingIdx, 
										uint nCount, 
										void *pBuffer, 
										uint nBufferBytes,
										bool bAsync,
										AsyncHandle_t *pAsyncHandle 
										)
{
	Error( "This function is obsolete and should not be used!\nReturn code cannot be an error code and number of results at the same time!\n" );
	return ERROR_NO_SUCH_PRIVILEGE;
}

//-----------------------------------------------------------------------------
//	Purpose: Award an achievement to the current user
//-----------------------------------------------------------------------------
int CXboxSystem::AwardAchievement( uint nUserIdx, uint nAchievementId, AsyncHandle_t *ppOverlappedResult )
{
	return ERROR_ACCESS_DISABLED_BY_POLICY;
}

//-----------------------------------------------------------------------------
//	Purpose: Grant an avatar asset award to the current user
//-----------------------------------------------------------------------------
int CXboxSystem::AwardAvatarAsset( uint nUserIdx, uint nAwardId, AsyncHandle_t *ppOverlappedResult )
{
	return ERROR_ACCESS_DISABLED_BY_POLICY;
}


// trial mode

//-----------------------------------------------------------------------------
//	Purpose: Show the "unlock trial game" blade
//-----------------------------------------------------------------------------
void CXboxSystem::ShowUnlockFullGameUI( void )
{
}

//=============================================================================
static ConVar xbox_arcade_title_unlocked( "xbox_arcade_title_unlocked", CSTRIKE_TRIAL_MODE ? "0": "1", FCVAR_DEVELOPMENTONLY, "debug unlocking arcade title" );
static bool g_bTitleUnlocked = false;
static ConVar xbox_arcade_remaining_trial_time( "xbox_arcade_remaining_trial_time", "2700.0", FCVAR_ARCHIVE_GAMECONSOLE | FCVAR_SS | FCVAR_DEVELOPMENTONLY, "time remaining in trial mode" );
//=============================================================================

float CXboxSystem::GetArcadeRemainingTrialTime( int nSlot )
{
	SplitScreenConVarRef trialTime( "xbox_arcade_remaining_trial_time" );
	return trialTime.GetFloat( nSlot );
}

//-----------------------------------------------------------------------------
//	Purpose: Determine whether this arcade game is unlocked
//-----------------------------------------------------------------------------
bool CXboxSystem::UpdateArcadeTitleUnlockStatus( void )
{
	return true;
}


//-----------------------------------------------------------------------------
//	Purpose: Determine whether this arcade game is unlocked
//-----------------------------------------------------------------------------
bool CXboxSystem::IsArcadeTitleUnlocked( void )
{
	return true;
}


int CXboxSystem::Io_HasOverlappedIoCompleted( XOVERLAPPED *pOverlapped )
{
	return 1;
}

int CXboxSystem::NetRandom( byte *pb, unsigned numBytes )
{
	if ( pb )
	{
		for ( byte * const pbEnd = pb + numBytes; pb < pbEnd; ++ pb )
			*pb = ( byte ) ( unsigned ) RandomInt( 0, 255 );
	}
	return 0;
}

DWORD CXboxSystem::NetGetTitleXnAddr( XNADDR *pxna )
{
	if ( pxna )
		memset( pxna, 0, sizeof( *pxna ) );
	return XNET_GET_XNADDR_NONE;
}

int CXboxSystem::NetXnAddrToMachineId( const XNADDR *pxnaddr, uint64 *pqwMachineId )
{
	if ( pqwMachineId )
		*pqwMachineId = 0ull;
	return 0;
}

int CXboxSystem::NetInAddrToXnAddr( const IN_ADDR ina, XNADDR *pxna, XNKID *pxnkid )
{
	if ( pxnkid )
	{
		memset( pxnkid, 0, sizeof( *pxnkid ) );
	}

	if ( pxna )
	{
		memset( pxna, 0, sizeof( *pxna ) );
		pxna->ina = ina;
	}
	return 0;
}

int CXboxSystem::NetXnAddrToInAddr( const XNADDR *pxna, const XNKID *pxnkid, IN_ADDR *pina )
{
	if ( pina )
	{
		if ( pxna )
		{
			*pina = pxna->ina;
		}
		else
		{
			memset( pina, 0, sizeof( *pina ) );
		}
	}
	return 0;
}

XUSER_SIGNIN_STATE CXboxSystem::UserGetSigninState( int iCtrlr )
{
	return eXUserSigninState_SignedInToLive;
}



