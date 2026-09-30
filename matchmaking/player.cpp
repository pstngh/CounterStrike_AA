//========= Copyright © 1996-2009, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#include "mm_framework.h"

#include "xbox/xboxstubs.h"

#include "smartptr.h"
#include "utlvector.h"

#include "x360_lobbyapi.h"
#include "leaderboards.h"

#include "igameevents.h"

#include "GameUI/IGameUI.h"
#include "filesystem.h"

// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"

#define STEAM_PACK_BITFIELDS

ConVar cl_names_debug( "cl_names_debug", "0", FCVAR_DEVELOPMENTONLY );
#define PLAYER_DEBUG_NAME "WWWWWWWWWWWWWWW"

#ifndef NO_STEAM
static float s_flSteamStatsRequestTime = 0;
static bool s_bSteamStatsRequestFailed = false;
bool g_bSteamStatsReceived = false;
#endif

static DWORD GetTitleSpecificDataId( int idx )
{
	static DWORD arrTSDI[3] = {
		XPROFILE_TITLE_SPECIFIC1,
		XPROFILE_TITLE_SPECIFIC2,
		XPROFILE_TITLE_SPECIFIC3
	};

	if ( idx >= 0 && idx < ARRAYSIZE( arrTSDI ) )
		return arrTSDI[idx];

	return DWORD(-1);
}

static int GetTitleSpecificDataIndex( DWORD TSDataId )
{
	switch( TSDataId )
	{
	case XPROFILE_TITLE_SPECIFIC1: return 0;
	case XPROFILE_TITLE_SPECIFIC2: return 1;
	case XPROFILE_TITLE_SPECIFIC3: return 2;
	default: return -1;
	}
}

static MM_XWriteOpportunity s_arrXWO[ XUSER_MAX_COUNT ]; // rely on static memory being zero'd

void SignalXWriteOpportunity( MM_XWriteOpportunity eXWO )
{
	if ( !eXWO )
	{
		Warning( "SignalXWriteOpportunity called with MMXWO_NONE!\n" );
		return;
	}
	else
	{
		Msg( "SignalXWriteOpportunity(%d)\n", eXWO );
	}

	// In case session has just started we bump every player's last write time
	// to the current time since player shouldn't write within the first 5 mins
	// from when the session started (TCR)
	if ( eXWO == MMXWO_SESSION_STARTED )
	{
		for ( int k = 0; k < XUSER_MAX_COUNT; ++ k )
		{
			IPlayerLocal *pLocalPlayer = g_pPlayerManager->GetLocalPlayer( k );
			if ( PlayerLocal *pPlayer = dynamic_cast< PlayerLocal * >( pLocalPlayer ) )
			{
				pPlayer->SetTitleDataWriteTime( Plat_FloatTime() );
			}
		}
		return;
	}

	for ( int k = 0; k < ARRAYSIZE( s_arrXWO ); ++ k )
	{
		// Only elevate write opportunity:
		// this way any other code can signal CHECKPOINT after SESSION_FINISHED
		// and the write will happen as SESSION_FINISHED
		if ( s_arrXWO[k] < eXWO )
			s_arrXWO[k] = eXWO;
	}
}

MM_XWriteOpportunity GetXWriteOpportunity( int iCtrlr )
{
	if ( iCtrlr >= 0 && iCtrlr < ARRAYSIZE( s_arrXWO ) )
	{
		MM_XWriteOpportunity result = s_arrXWO[ iCtrlr ];
		s_arrXWO[ iCtrlr ] = MMXWO_NONE; // reset
		return result;
	}
	else
		return MMXWO_NONE;
}

static CUtlVector< XUID > s_arrSessionSearchesQueue;
static int s_numSearchesOutstanding = 0;

ConVar mm_player_search_count( "mm_player_search_count", "5", FCVAR_DEVELOPMENTONLY );

void PumpSessionSearchQueue()
{
	while ( s_arrSessionSearchesQueue.Count() > 0 &&
		    s_numSearchesOutstanding < mm_player_search_count.GetInt() )
	{
		XUID xid = s_arrSessionSearchesQueue[0];
		s_arrSessionSearchesQueue.Remove( 0 );

		if ( PlayerFriend *player = g_pPlayerManager->FindPlayerFriend( xid ) )
		{
			player->StartSearchForSessionInfoImpl();
		}
	}
}


//
// PlayerFriend implementation
//

PlayerFriend::PlayerFriend( XUID xuid, FriendInfo_t const *pFriendInfo /* = NULL */ ) :
	m_uFriendMark( 0 ),
	m_bIsStale( false ),
	m_eSearchState( SEARCH_NONE ),
	m_pDetails( NULL ),
	m_pPublishedPresence( NULL )
{
	memset( m_wszRichPresence, 0, sizeof( m_wszRichPresence ) );
	memset( &m_xSessionID, 0, sizeof( m_xSessionID ) );
	memset( &m_GameSessionInfo, 0, sizeof( m_GameSessionInfo ) );
	m_uiTitleID = 0;
	m_uiGameServerIP = 0;

	m_xuid = xuid;
	m_eOnlineState = STATE_ONLINE;
	UpdateFriendInfo( pFriendInfo );
}

wchar_t const * PlayerFriend::GetRichPresence()
{
	return m_wszRichPresence;
}

KeyValues * PlayerFriend::GetGameDetails()
{
	return m_pDetails;
}

KeyValues * PlayerFriend::GetPublishedPresence()
{
	return m_pPublishedPresence;
}

bool PlayerFriend::IsJoinable()
{
	if ( m_pPublishedPresence && m_pPublishedPresence->GetString( "connect" )[0] )
		return true; // joining via connect string
	
	if ( !( const uint64 & ) m_xSessionID )
		return false;

	if ( m_pDetails->GetInt( "members/numSlots" ) <= m_pDetails->GetInt( "members/numPlayers" ) )
		return false;
	if ( *m_pDetails->GetString( "system/lock" ) )
		return false;
	if ( !Q_stricmp( "private", m_pDetails->GetString( "system/access" ) ) )
		return false;
	return true; // joining via lobby
}

uint64 PlayerFriend::GetTitleID()
{
	return m_uiTitleID;
}

uint32 PlayerFriend::GetGameServerIP()
{
	return m_uiGameServerIP;
}

void PlayerFriend::Join()
{
	// Requesting to join this player
	KeyValues *pSettings = KeyValues::FromString(
		"settings",
		" system { "
			" network LIVE "
		" } "
		" options { "
			" action joinsession "
		" } "
		);
	
	if ( m_eSearchState == SEARCH_NONE )
	{
		pSettings->SetString( "system/network", m_pDetails->GetString( "system/network", "LIVE" ) );
	}

	pSettings->SetUint64( "options/sessionid", ( const uint64 & ) m_xSessionID );
	pSettings->SetUint64( "options/friendxuid", m_xuid );

	KeyValues::AutoDelete autodelete( pSettings );

	g_pMatchFramework->MatchSession( pSettings );
}

void PlayerFriend::Update()
{
	if ( !m_xuid )
		return;

	if ( m_eSearchState == SEARCH_COMPLETED )
	{
		m_eSearchState = SEARCH_NONE;

		-- s_numSearchesOutstanding;
		PumpSessionSearchQueue();

		if( V_memcmp( &( m_GameSessionInfo.sessionID ), &m_xSessionID, sizeof( m_xSessionID ) ) != 0)
		{
			// Re-discover everything again since session ID changed
			StartSearchForSessionInfo();
		}

		// Signal that we are finished with a search
		KeyValues *kvEvent = new KeyValues( "OnMatchPlayerMgrUpdate", "update", "friend" );
		kvEvent->SetUint64( "xuid", GetXUID() );
		g_pMatchFramework->GetEventsSubscription()->BroadcastEvent( kvEvent );
	}
}

#if !defined( NO_STEAM )

void PlayerFriend::Steam_OnLobbyDataUpdate( LobbyDataUpdate_t *pParam )
{
	// Only callbacks about lobby itself
	if ( pParam->m_ulSteamIDLobby != pParam->m_ulSteamIDMember )
		return;

	// Listening for only callbacks related to current player
	if ( pParam->m_ulSteamIDLobby != ( const uint64 & ) m_xSessionID )
		return;

	// Unregister the callback
	m_CallbackOnLobbyDataUpdate.Unregister();

	// Set session info
	memset( &m_GameSessionInfo, 0, sizeof( m_GameSessionInfo ) );
	m_GameSessionInfo.sessionID = m_xSessionID;

	// Describe the lobby
	if ( m_pDetails )
		m_pDetails->deleteThis();
	m_pDetails = NULL;

	m_pDetails = g_pMatchFramework->GetMatchNetworkMsgController()->UnpackGameDetailsFromSteamLobby( pParam->m_ulSteamIDLobby );

	if ( m_pDetails )
	{
		// Set AUX fields like session id
		if ( KeyValues *kvOptions = m_pDetails->FindKey( "options", true ) )
		{
			kvOptions->SetUint64( "sessionid", pParam->m_ulSteamIDLobby );
		}

		// Set the "player" key
		if ( KeyValues *kvPlayer = m_pDetails->FindKey( "player", true ) )
		{
			kvPlayer->SetUint64( "xuid", GetXUID() );
			kvPlayer->SetUint64( "xuidOnline", GetXUID() );
			kvPlayer->SetString( "name", GetName() );
			kvPlayer->SetWString( "richpresence", GetRichPresence() );
		}
	}

	m_eSearchState = SEARCH_COMPLETED;
}

#endif

void PlayerFriend::Destroy()
{
	AbortSearch();

	if ( m_pPublishedPresence )
		m_pPublishedPresence->deleteThis();
	m_pPublishedPresence = NULL;

	delete this;
}

void PlayerFriend::AbortSearch()
{
#if !defined( NO_STEAM )
	m_CallbackOnLobbyDataUpdate.Unregister();
#endif

	// Clean up the queue
	while ( s_arrSessionSearchesQueue.FindAndRemove( m_xuid ) )
		continue;

	bool bAbortedSearch = false;

	switch ( m_eSearchState )
	{
#if !defined( NO_STEAM )
	case SEARCH_WAIT_LOBBY_DATA:
		bAbortedSearch = true;
		break;
#endif

	case SEARCH_COMPLETED:
		bAbortedSearch = true;
		break;
	}

	if ( bAbortedSearch )
	{
		-- s_numSearchesOutstanding;
		PumpSessionSearchQueue();
	}

	m_eSearchState = SEARCH_NONE;

	if ( m_pDetails )
		m_pDetails->deleteThis();
	m_pDetails = NULL;
}

void PlayerFriend::SetFriendMark( unsigned maskSetting )
{
	m_uFriendMark = maskSetting;
}

unsigned PlayerFriend::GetFriendMark()
{
	return m_uFriendMark;
}

void PlayerFriend::SetIsStale( bool bStale )
{
	m_bIsStale = bStale;
}

bool PlayerFriend::GetIsStale()
{
	return m_bIsStale;
}

void PlayerFriend::UpdateFriendInfo( FriendInfo_t const *pFriendInfo )
{
	if ( !pFriendInfo )
		return;

	if ( pFriendInfo->m_szName )
		Q_strncpy( m_szName, pFriendInfo->m_szName, ARRAYSIZE( m_szName ) );

	if ( pFriendInfo->m_wszRichPresence )
		Q_wcsncpy( m_wszRichPresence, pFriendInfo->m_wszRichPresence, ARRAYSIZE( m_wszRichPresence ) );

	m_uiTitleID = pFriendInfo->m_uiTitleID;
	
	if ( pFriendInfo->m_uiGameServerIP != ~0 )
		m_uiGameServerIP = pFriendInfo->m_uiGameServerIP;
	
	if ( cl_names_debug.GetBool() )
	{
		Q_strncpy( m_szName, PLAYER_DEBUG_NAME, ARRAYSIZE( m_szName ) );
	}

	if ( pFriendInfo->m_pGameDetails )
	{
		AbortSearch();
		
		if ( m_pDetails )
			m_pDetails->deleteThis();
		m_pDetails = pFriendInfo->m_pGameDetails->MakeCopy();
		
#if !defined( NO_STEAM )
		uint64 uiSessionId = m_pDetails->GetUint64( "options/sessionid" );
		m_xSessionID = ( XNKID & ) uiSessionId;
#endif

		// Signal that we are finished with a search
		KeyValues *kvEvent = new KeyValues( "OnMatchPlayerMgrUpdate", "update", "friend" );
		kvEvent->SetUint64( "xuid", GetXUID() );
		g_pMatchFramework->GetEventsSubscription()->BroadcastEvent( kvEvent );
	}
	else if ( pFriendInfo->m_uiTitleID &&
		pFriendInfo->m_uiTitleID != g_pMatchFramework->GetMatchTitle()->GetTitleID() )
	{
		if ( m_pDetails )
			m_pDetails->deleteThis();

		m_pDetails = new KeyValues( "TitleSettings" );
		m_pDetails->SetUint64( "titleid", pFriendInfo->m_uiTitleID );
	}
	else
	{
		m_xSessionID = pFriendInfo->m_xSessionID;

		if( m_eSearchState == SEARCH_NONE )
		{
			StartSearchForSessionInfo();
		}
	}

#if !defined( NO_STEAM )
	// Update published presence for the friend too
	if ( m_pPublishedPresence )
	{
		m_pPublishedPresence->deleteThis();
		m_pPublishedPresence = NULL;
	}

	ISteamFriends *pf = steamapicontext->SteamFriends();
	if ( pf && ( g_pMatchFramework->GetMatchTitle()->GetTitleID() == m_uiTitleID ) )
	{
		pf->RequestFriendRichPresence( GetXUID() ); // refresh friend's rich presence
		int numRichPresenceKeys = pf->GetFriendRichPresenceKeyCount( GetXUID() );
		for ( int j = 0; j < numRichPresenceKeys; ++ j )
		{
			const char *pszKey = pf->GetFriendRichPresenceKeyByIndex( GetXUID(), j );
			if ( pszKey && *pszKey )
			{
				char const *pszValue = pf->GetFriendRichPresence( GetXUID(), pszKey );
				if ( pszValue && *pszValue )
				{
					if ( !m_pPublishedPresence )
						m_pPublishedPresence = new KeyValues( "RP" );
					
					CFmtStr fmtNewKey( "%s", pszKey );
					while ( char *szFixChar = strchr( fmtNewKey.Access(), ':' ) )
						*szFixChar = '/';
					m_pPublishedPresence->SetString( fmtNewKey, pszValue );
				}
			}
		}
	}
#endif
}

bool PlayerFriend::IsUpdatingInfo()
{
	return m_eSearchState != SEARCH_NONE;
}


void PlayerFriend::StartSearchForSessionInfo()
{
	if ( !m_xuid )
		return;

	if ( m_eSearchState != SEARCH_NONE )
		return;

	m_eSearchState = SEARCH_QUEUED;

	// Check if we are not already in the queue
	if ( s_arrSessionSearchesQueue.Find( m_xuid ) == s_arrSessionSearchesQueue.InvalidIndex() )
	{
		s_arrSessionSearchesQueue.AddToTail( m_xuid );
	}

	PumpSessionSearchQueue();
}

void PlayerFriend::StartSearchForSessionInfoImpl()
{

	if( m_eSearchState == SEARCH_NONE ||
		m_eSearchState == SEARCH_QUEUED )
	{
#if !defined( NO_STEAM )

		if ( steamapicontext->SteamMatchmaking() &&
			( const uint64 & ) m_xSessionID &&
			steamapicontext->SteamMatchmaking()->RequestLobbyData( ( const uint64 & ) m_xSessionID ) )
		{
			// Enable the callback
			m_CallbackOnLobbyDataUpdate.Register( this, &PlayerFriend::Steam_OnLobbyDataUpdate );

			m_eSearchState = SEARCH_WAIT_LOBBY_DATA;

#else

		if ( 0 )
		{

#endif
			
			++ s_numSearchesOutstanding;
		}
		else
		{
			memset( &m_xSessionID, 0, sizeof( m_xSessionID ) );
			memset( &m_GameSessionInfo, 0, sizeof( m_GameSessionInfo ) );
			if ( m_pDetails )
				m_pDetails->deleteThis();
			m_pDetails = NULL;

			m_eSearchState = SEARCH_NONE;
		}
	}
}



//
// PlayerLocal implementation
//

PlayerLocal::PlayerLocal( int iController ) :
	m_eLoadedTitleData( eXUserSigninState_NotSignedIn ),
	m_flLastSave( 0.0f ),
	m_uiPlayerFlags( 0 ),
	m_pLeaderboardData( new KeyValues( "Leaderboard" ) ),
	m_autodelete_pLeaderboardData( m_pLeaderboardData )
{
	Assert( iController >= 0 && iController < XUSER_MAX_COUNT );

	memset( &m_ProfileData, 0, sizeof( m_ProfileData ) );
	memset( m_bufTitleData, 0, sizeof( m_bufTitleData ) );
	memset( m_bSaveTitleData, 0, sizeof( m_bSaveTitleData ) );

	m_iController = iController;
	GetXWriteOpportunity( iController ); // reset

#if !defined( NO_STEAM )
	CSteamID steamIDPlayer;
	if ( steamapicontext->SteamUser() )
	{
		m_eOnlineState = steamapicontext->SteamUser()->BLoggedOn() ? IPlayer::STATE_ONLINE : IPlayer::STATE_OFFLINE;
		steamIDPlayer = steamapicontext->SteamUser()->GetSteamID();
		m_xuid = steamIDPlayer.IsValid() ? steamIDPlayer.ConvertToUint64() : 0;
	}
	else
	{
		m_xuid = 0;
	}
#else
	m_xuid = 1ull;
	m_eOnlineState = IPlayer::STATE_OFFLINE;
#endif

#if !defined( NO_STEAM )
	// Get user name from Steam
	if ( steamIDPlayer.IsValid() && steamapicontext->SteamUser() && steamapicontext->SteamFriends() )
	{
		const char *pszName = steamapicontext->SteamFriends()->GetFriendPersonaName( steamIDPlayer );
		if ( pszName )
		{
			Q_strncpy( m_szName, pszName, ARRAYSIZE( m_szName ) );
		}			
	}
	m_CallbackOnPersonaStateChange.Register( this, &PlayerLocal::Steam_OnPersonaStateChange );
	m_CallbackOnServersConnected.Register( this, &PlayerLocal::Steam_OnServersConnected );
	m_CallbackOnServersDisconnected.Register( this, &PlayerLocal::Steam_OnServersDisconnected );
	LoadPlayerProfileData();
#else
	if ( char const *szGuestName = g_pMatchFramework->GetMatchTitle()->GetGuestPlayerName( m_iController ) )
	{
		Q_strncpy( m_szName, szGuestName, ARRAYSIZE( m_szName ) );
	}
	else
	{
		m_szName[0] = 0;
	}
#endif

	if ( cl_names_debug.GetBool() )
	{
		Q_strncpy( m_szName, PLAYER_DEBUG_NAME, ARRAYSIZE( m_szName ) );
	}
}

PlayerLocal::~PlayerLocal()
{
}

void PlayerLocal::LoadTitleData()
{
	if ( m_eLoadedTitleData == eXUserSigninState_SignedInToLive )
		// already processed
		return;

	if ( m_eLoadedTitleData >= GetAssumedSigninState() )
		// already processed
		return;

#if !defined ( NO_STEAM )

	// Always request user stats from Steam
	if ( steamapicontext->SteamUserStats() )
	{
		m_eLoadedTitleData = GetAssumedSigninState();
		m_CallbackOnUserStatsReceived.Register( this, &PlayerLocal::Steam_OnUserStatsReceived );
		steamapicontext->SteamUserStats()->RequestCurrentStats();

		s_flSteamStatsRequestTime = Plat_FloatTime();
		if ( !s_bSteamStatsRequestFailed )
		{
			DevMsg( "Requesting Steam stats... (%2.2f)\n", Plat_FloatTime() );
		}
	}

#else

	m_eLoadedTitleData = GetAssumedSigninState();
	// Since we don't have Steam, we reset all configuration values and stats.
	IGameEvent *event = g_pMatchExtensions->GetIGameEventManager2()->CreateEvent( "reset_game_titledata" );
	if ( event )
	{
		event->SetInt( "controllerId", m_iController );
		g_pMatchExtensions->GetIGameEventManager2()->FireEventClientSide( event );
	}
	g_pMatchEventsSubscription->BroadcastEvent( new KeyValues( "ResetConfiguration", "iController", m_iController ) );


#endif
}

#if !defined ( NO_STEAM )

ConVar mm_cfgoverride_file( "mm_cfgoverride_file", "", FCVAR_DEVELOPMENTONLY );
ConVar mm_cfgoverride_commit( "mm_cfgoverride_commit", "", FCVAR_DEVELOPMENTONLY );
ConVar mm_cfgdebug_mode( "mm_cfgdebug_mode", "0", FCVAR_DEVELOPMENTONLY );

static bool SetSteamStatWithPotentialOverride( char const *szField, int32 iValue )
{
	char const *szStatFieldSteamDB = szField;
	return steamapicontext->SteamUserStats() ? steamapicontext->SteamUserStats()->SetStat( szStatFieldSteamDB, iValue ) : false;
}

static bool SetSteamStatWithPotentialOverride( char const *szField, float fValue )
{
	char const *szStatFieldSteamDB = szField;
	return steamapicontext->SteamUserStats() ? steamapicontext->SteamUserStats()->SetStat( szStatFieldSteamDB, fValue ) : false;
}

template < typename T >
static inline bool ApplySteamStatPotentialOverride( char const *szField, T *pValue, bool bResult, T (KeyValues::*pfn)( char const *, T ) )
{
#ifdef _CERT
	return bResult;
#else
	if ( mm_cfgdebug_mode.GetInt() > 0 )
	{
		DevMsg( "[PlayerStats] '%s' = %d (0x%08X)\n", szField, (int32)*pValue, *(int32*)pValue );
	}

	char const *szFile = mm_cfgoverride_file.GetString();
	if ( !szFile || !*szFile )
		return bResult;
	
	KeyValues *kvOverride = new KeyValues( "cfgoverride.kv" );
	KeyValues::AutoDelete autodelete( kvOverride );
	if ( !kvOverride->LoadFromFile( g_pFullFileSystem, szFile ) )
		return bResult;

	if ( KeyValues *kvItemOverride = kvOverride->FindKey( "items" )->FindKey( szField ) )
	{
		*pValue = ( kvItemOverride->*pfn )( "", 0 );
		DevMsg( "[PlayerStats] '%s' overrides '%s' = '%s'\n", szFile, szField, kvItemOverride->GetString( "", "" ) );
		if ( mm_cfgoverride_commit.GetBool() )
			SetSteamStatWithPotentialOverride( szField, *pValue );
		return true;
	}
	
	// Match by wildcard
	for ( KeyValues *kvWildcard = kvOverride->FindKey( "wildcards" )->GetFirstValue(); kvWildcard; kvWildcard = kvWildcard->GetNextValue() )
	{
		char const *szWildcard = kvWildcard->GetName();
		int nLen = Q_strlen( szWildcard );
		if ( !nLen || ( szWildcard[nLen-1] != '*' ) )
			continue;
		if ( (nLen <= 1) || !Q_strnicmp( szWildcard, szField, nLen - 1 ) )
		{
			*pValue = ( kvWildcard->*pfn )( "", 0 );
			DevMsg( "[PlayerStats] '%s' overrides '%s' = '%s' [wildcard match '%s']\n", szFile, szField, kvWildcard->GetString( "", "" ), szWildcard );
			if ( mm_cfgoverride_commit.GetBool() )
				SetSteamStatWithPotentialOverride( szField, *pValue );
			return true;
		}
	}

	return bResult;
#endif
}

static bool GetSteamStatWithPotentialOverride( char const *szField, int32 *pValue )
{
	char const *szStatFieldSteamDB = szField;
	bool bResult = steamapicontext->SteamUserStats()->GetStat( szStatFieldSteamDB, pValue );
	return ApplySteamStatPotentialOverride<int32>( szField, pValue, bResult, &KeyValues::GetInt );
}

static bool GetSteamStatWithPotentialOverride( char const *szField, float *pValue )
{
	char const *szStatFieldSteamDB = szField;
	bool bResult = steamapicontext->SteamUserStats()->GetStat( szStatFieldSteamDB, pValue );
	return ApplySteamStatPotentialOverride<float>( szField, pValue, bResult, &KeyValues::GetFloat );
}

void PlayerLocal::Steam_OnUserStatsReceived( UserStatsReceived_t *pParam )
{
#ifndef NO_STEAM
	if ( !s_bSteamStatsRequestFailed || ( pParam->m_eResult == k_EResultOK ) )
	{
		DevMsg( "PlayerLocal::Steam_OnUserStatsReceived... (%2.2f sec since request)\n", s_flSteamStatsRequestTime ? ( Plat_FloatTime() - s_flSteamStatsRequestTime ) : 0.0f );
	}
	s_flSteamStatsRequestTime = 0;
#endif

	// If failed, we'll request one more time
	if ( pParam->m_eResult != k_EResultOK )
	{
		if ( !s_bSteamStatsRequestFailed )
		{
			DevWarning( "PlayerLocal::Steam_OnUserStatsReceived (failed with error %d)\n", pParam->m_eResult );
			s_bSteamStatsRequestFailed = true;
		}
		m_eLoadedTitleData = eXUserSigninState_NotSignedIn;
		return;
	}
	s_bSteamStatsRequestFailed = false;
	g_bSteamStatsReceived = true;

	//
	// Achievements state
	//
	for ( TitleAchievementsDescription_t const *pAchievement = g_pMatchFramework->GetMatchTitle()->DescribeTitleAchievements();
		pAchievement && pAchievement->m_szAchievementName; ++ pAchievement )
	{
		bool bAchieved;
		if ( steamapicontext->SteamUserStats()->GetAchievement( pAchievement->m_szAchievementName, &bAchieved ) && bAchieved )
		{
			m_arrAchievementsEarned.FindAndFastRemove( pAchievement->m_idAchievement );
			m_arrAchievementsEarned.AddToTail( pAchievement->m_idAchievement );
		}
	}

	//
	// Load all our stats data
	//
	TitleDataFieldsDescription_t const *pTitleDataTable = g_pMatchFramework->GetMatchTitle()->DescribeTitleDataStorage();
	for ( ; pTitleDataTable && pTitleDataTable->m_szFieldName; ++ pTitleDataTable )
	{
		switch( pTitleDataTable->m_eDataType )
		{
		case TitleDataFieldsDescription_t::DT_uint8:
		case TitleDataFieldsDescription_t::DT_uint16:
		case TitleDataFieldsDescription_t::DT_uint32:
			{
				uint32 i32field[3] = {0};

				if ( GetSteamStatWithPotentialOverride( pTitleDataTable->m_szFieldName, ( int32 * ) &i32field[0] ) )
				{
					*( uint16 * )( &i32field[1] ) = uint16( i32field[0] );
					*( uint8  * )( &i32field[2] ) = uint8 ( i32field[0] );
					
					memcpy( &m_bufTitleData[ pTitleDataTable->m_iTitleDataBlock ][ pTitleDataTable->m_numBytesOffset ],
						&i32field[ 2 - ( pTitleDataTable->m_eDataType / 16 ) ], pTitleDataTable->m_eDataType / 8 );
				}
			}
			break;
		
		case TitleDataFieldsDescription_t::DT_float:
			{
				float flField = 0.0f;
				if ( GetSteamStatWithPotentialOverride( pTitleDataTable->m_szFieldName, &flField ) )
				{
					memcpy( &m_bufTitleData[ pTitleDataTable->m_iTitleDataBlock ][ pTitleDataTable->m_numBytesOffset ],
						&flField, pTitleDataTable->m_eDataType / 8 );
				}
			}
			break;

		case TitleDataFieldsDescription_t::DT_uint64:
			{
				uint32 i32field[2] = { 0 };

				char chBuffer[ 256 ] = {0};

				for ( int k = 0; k < ARRAYSIZE( i32field ); ++ k )
				{
					Q_snprintf( chBuffer, ARRAYSIZE( chBuffer ), "%s.%d", pTitleDataTable->m_szFieldName, k );
					if ( !GetSteamStatWithPotentialOverride( chBuffer, ( int32 * ) &i32field[k] ) )
						i32field[k] = 0;
				}
				
				memcpy( &m_bufTitleData[ pTitleDataTable->m_iTitleDataBlock ][ pTitleDataTable->m_numBytesOffset ],
					&i32field[0], pTitleDataTable->m_eDataType / 8 );
			}
			break;

		case TitleDataFieldsDescription_t::DT_BITFIELD:
			{
			#ifdef STEAM_PACK_BITFIELDS
				char chStatField[64] = {0};
				uint32 uiOffsetInTermsOfUINT32 = pTitleDataTable->m_numBytesOffset/32;
				V_snprintf( chStatField, sizeof( chStatField ), "bitfield_%02u_%03X", pTitleDataTable->m_iTitleDataBlock + 1, uiOffsetInTermsOfUINT32*4 );
				int32 iCombinedBitValue = 0;
				if ( GetSteamStatWithPotentialOverride( chStatField, &iCombinedBitValue ) )
				{
					( reinterpret_cast< uint32 * >( &m_bufTitleData[pTitleDataTable->m_iTitleDataBlock][0] ) )[ uiOffsetInTermsOfUINT32 ] = iCombinedBitValue;
				}
			#else
				int i32field = 0;
				if ( GetSteamStatWithPotentialOverride( pTitleDataTable->m_szFieldName, &i32field ) )
				{
					char &rByte = m_bufTitleData[ pTitleDataTable->m_iTitleDataBlock ][ pTitleDataTable->m_numBytesOffset/8 ];
					char iMask = ( 1 << ( pTitleDataTable->m_numBytesOffset % 8 ) );
					if ( i32field )
						rByte |= iMask;
					else
						rByte &=~iMask;
				}
			#endif
			}
			break;
		}
	}


	// send an event to anyone else who needs Steam user stat data
	IGameEvent *event =  g_pMatchExtensions->GetIGameEventManager2()->CreateEvent( "user_data_downloaded" );
	if ( event )
	{
#ifdef GAME_DLL
		g_pMatchExtensions->GetIGameEventManager2()->FireEvent( event );
#else
		// not sure this event is caught anywhere but we brought it over from orange box just in case
		g_pMatchExtensions->GetIGameEventManager2()->FireEventClientSide( event );
#endif
	}

	// After we loaded some title data, see if we need to retrospectively award achievements
	EvaluateAwardsStateBasedOnStats();

	// Flush stats if we are clearing for debugging
	if ( mm_cfgoverride_commit.GetBool() )
		steamapicontext->SteamUserStats()->StoreStats();

	//
	// Finished reading stats
	//
	DevMsg( "User%d stats retrieved.\n", m_iController );
	OnProfileTitleDataLoaded( 0 );

#ifdef _DEBUG

	static bool debugDumpStats = true;
	if ( debugDumpStats )
	{
		debugDumpStats = false;
		// Debug code.
		// Dump the stats once loaded so we can see what they are.
		g_pMatchExtensions->GetIVEngineClient()->ExecuteClientCmd( "ms_player_dump_properties" );
	}

#endif

}

void PlayerLocal::Steam_OnPersonaStateChange( PersonaStateChange_t *pParam )
{
	if ( !steamapicontext ||
		!steamapicontext->SteamUtils() ||
		!steamapicontext->SteamFriends() ||
		!steamapicontext->SteamUser() ||
		!pParam ||
		!m_xuid )
		return;

	// Check that something changed about local user
	if ( m_xuid == pParam->m_ulSteamID )
	{
		if ( pParam->m_nChangeFlags & k_EPersonaChangeName )
		{
			CSteamID steamID;
			steamID.SetFromUint64( m_xuid );

			if ( char const *szName = steamapicontext->SteamFriends()->GetFriendPersonaName( steamID ) )
			{
				Q_strncpy( m_szName, szName, ARRAYSIZE( m_szName ) );
			}
			
			if ( cl_names_debug.GetBool() )
			{
				Q_strncpy( m_szName, PLAYER_DEBUG_NAME, ARRAYSIZE( m_szName ) );
			}
		}
	}
}

void PlayerLocal::UpdatePlayersSteamLogon()
{
	if ( !steamapicontext->SteamUser() )
		return;

	IPlayer::OnlineState_t eState = steamapicontext->SteamUser()->BLoggedOn() ? IPlayer::STATE_ONLINE : IPlayer::STATE_OFFLINE;

	m_eOnlineState = eState;

	// Update XUID on PS3:
	CSteamID cSteamId = steamapicontext->SteamUser()->GetSteamID();
	if ( !m_xuid && cSteamId.IsValid() )
	{
		m_xuid = steamapicontext->SteamUser()->GetSteamID().ConvertToUint64();
	}
}

void PlayerLocal::Steam_OnServersConnected( SteamServersConnected_t *pParam )
{
	DevMsg( "Steam_OnServersConnected\n" );
	
	UpdatePlayersSteamLogon();
}

void PlayerLocal::Steam_OnServersDisconnected( SteamServersDisconnected_t *pParam )
{
	DevWarning( "Steam_OnServersDisconnected\n" );

	UpdatePlayersSteamLogon();
}

#endif

void PlayerLocal::SetTitleDataWriteTime( float flTime )
{
	m_flLastSave = flTime;
}

// Test if we can still read from profile; used when storage device is removed and we
// want to verify the profile still has a storage connection
bool PlayerLocal::IsTitleDataStorageConnected( void )
{

	return true;

}

void PlayerLocal::WriteTitleData()
{

	//
	// Determine if XWrite is required first
	//

	DWORD numXWritesRequired = 0;

	for ( int iData = 0; iData < ( IsX360() ? TITLE_DATA_COUNT_X360 : TITLE_DATA_COUNT ); ++ iData )
	{
		if ( !m_bSaveTitleData[iData] )
			continue;

		++ numXWritesRequired;
	}

	if ( !numXWritesRequired )
		// early out if nothing to do here
		return;

#if !defined( NO_STEAM )

	//
	//	Steam stats have been written earlier
	//	Clear dirty state
	//
	V_memset( m_bSaveTitleData, 0, sizeof( m_bSaveTitleData ) );

	g_pPlayerManager->RequestStoreStats();
	GetXWriteOpportunity( m_iController ); // clear XWrite opportunity

	DevMsg( "User stats written.\n" );
#endif

	g_pMatchEventsSubscription->BroadcastEvent( new KeyValues( "OnProfileDataSaved", "iController", m_iController ) );
}

void PlayerLocal::Update()
{

	// Load title data if not loaded yet
	LoadTitleData();

	// Save title data if time has come to do so
	WriteTitleData();

#ifndef NO_STEAM
	// Re-request only if we got our callback with an error code. 
	if ( s_flSteamStatsRequestTime && ( Plat_FloatTime() - s_flSteamStatsRequestTime > 10.0 ) && s_bSteamStatsRequestFailed )
	{
		DevWarning( "=========== Failed to retrieve Steam stats (%2.2f sec) =================\n", Plat_FloatTime() - s_flSteamStatsRequestTime );
		steamapicontext->SteamUserStats()->RequestCurrentStats();
		s_flSteamStatsRequestTime = Plat_FloatTime();
	}
#endif
}

void PlayerLocal::Destroy()
{
	m_iController = XUSER_INDEX_NONE;
	m_xuid = 0;
	m_eOnlineState = STATE_OFFLINE;

#if !defined( NO_STEAM )
	m_CallbackOnUserStatsReceived.Unregister();
	m_CallbackOnPersonaStateChange.Unregister();
#endif

	delete this;
}

void PlayerLocal::RecomputeXUID( char const *szNetwork )
{
	if ( !m_xuid )
		return;

}

void PlayerLocal::DetectOnlineState()
{
}

const UserProfileData & PlayerLocal::GetPlayerProfileData()
{
	return m_ProfileData;
}

void PlayerLocal::LoadPlayerProfileData()
{

	DevMsg( "Player %d : LoadPlayerProfileData finished\n", m_iController );
}

MatchmakingData* PlayerLocal::GetPlayerMatchmakingData( void )
{
	return &m_MatchmakingData;
}

void PlayerLocal::UpdatePlayerMatchmakingData( int mmDataType )
{
}

void PlayerLocal::ResetPlayerMatchmakingData( int mmDataScope )
{
}

const void * PlayerLocal::GetPlayerTitleData( int iTitleDataIndex )
{
	if ( iTitleDataIndex >= 0 && iTitleDataIndex < TITLE_DATA_COUNT )
	{
		return m_bufTitleData[ iTitleDataIndex ];
	}
	else
	{
		return NULL;
	}
}

void PlayerLocal::UpdatePlayerTitleData( TitleDataFieldsDescription_t const *fdKey, const void *pvNewTitleData, int numNewBytes )
{
	if ( !fdKey || (fdKey->m_eDataType == fdKey->DT_0) || !pvNewTitleData || (numNewBytes <= 0) || !fdKey->m_szFieldName )
		return;
	Assert( TITLE_DATA_COUNT == TitleDataFieldsDescription_t::DB_TD_COUNT );
	if ( fdKey->m_iTitleDataBlock < 0 || fdKey->m_iTitleDataBlock >= TitleDataFieldsDescription_t::DB_TD_COUNT )
		return;

#if !defined( NO_STEAM ) && !defined( _CERT )
	if ( steamapicontext->SteamUtils() && steamapicontext->SteamUtils()->GetConnectedUniverse() == k_EUniverseBeta )
#endif
	{
	}

	// Validate data size
	if ( fdKey->m_eDataType/8 != numNewBytes )
	{
		DevWarning( "PlayerLocal::UpdatePlayerTitleData( %s ) new data size %d != description size %d!\n", fdKey->m_szFieldName, numNewBytes, fdKey->m_eDataType/8 );
		Assert( 0 );
		return;
	}

	// Debug output for the write attempt
	switch ( fdKey->m_eDataType )
	{
	case TitleDataFieldsDescription_t::DT_uint8:
	case TitleDataFieldsDescription_t::DT_BITFIELD:
//		DevMsg( "PlayerLocal(%s)::UpdatePlayerTitleData: %s = %d (0x%02X)\n", GetName(), fdKey->m_szFieldName, *( uint8 const * ) pvNewTitleData, *( uint8 const * ) pvNewTitleData );
		break;
	case TitleDataFieldsDescription_t::DT_uint16:
//		DevMsg( "PlayerLocal(%s)::UpdatePlayerTitleData: %s = %d (0x%04X)\n", GetName(), fdKey->m_szFieldName, *( uint16 const * ) pvNewTitleData, *( uint16 const * ) pvNewTitleData );
		break;
	case TitleDataFieldsDescription_t::DT_uint32:
//		DevMsg( "PlayerLocal(%s)::UpdatePlayerTitleData: %s = %d (0x%08X)\n", GetName(), fdKey->m_szFieldName, *( uint32 const * ) pvNewTitleData, *( uint32 const * ) pvNewTitleData );
		break;
	case TitleDataFieldsDescription_t::DT_uint64:
//		DevMsg( "PlayerLocal(%s)::UpdatePlayerTitleData: %s = %llu (0x%llX)\n", GetName(), fdKey->m_szFieldName, *( uint64 const * ) pvNewTitleData, *( uint64 const * ) pvNewTitleData );
		break;
	case TitleDataFieldsDescription_t::DT_float:
//		DevMsg( "PlayerLocal(%s)::UpdatePlayerTitleData: %s = %f (%0.2f)\n", GetName(), fdKey->m_szFieldName, *( float const * ) pvNewTitleData, *( float const * ) pvNewTitleData );
		break;
	}

	// Check if the title allows the requested stat update
	KeyValues *kvTitleRequest = new KeyValues( "stat" );
	KeyValues::AutoDelete autodelete_kvTitleRequest( kvTitleRequest );
	kvTitleRequest->SetString( "name", fdKey->m_szFieldName );
	if ( !g_pMMF->GetMatchTitleGameSettingsMgr()->AllowClientProfileUpdate( kvTitleRequest ) )
		return;

	//
	// Copy off the old data and check that the new data is different
	// At the same time update internal buffers with new data
	//
	uint8 *pvData = ( uint8 * ) m_bufTitleData[ fdKey->m_iTitleDataBlock ];
	switch ( fdKey->m_eDataType )
	{
	case TitleDataFieldsDescription_t::DT_BITFIELD:
		{
			uint8 *pvOldData = ( uint8 * ) stackalloc( numNewBytes );
			uint8 bBitValue = !!*( ( uint8 const * ) pvNewTitleData );
			uint8 bBitMask = ( 1 << (fdKey->m_numBytesOffset%8) );
			memcpy( pvOldData, pvData + fdKey->m_numBytesOffset/8, numNewBytes );
			if ( !!( bBitMask & pvOldData[0] ) == bBitValue )
				return;
			if ( bBitValue )
				pvOldData[0] |= bBitMask;
			else
				pvOldData[0] &=~bBitMask;
			memcpy( pvData + fdKey->m_numBytesOffset/8, pvOldData, numNewBytes );
		}
		break;
	default:
		if ( !memcmp( pvData + fdKey->m_numBytesOffset, pvNewTitleData, numNewBytes ) )
			return;
		memcpy( pvData + fdKey->m_numBytesOffset, pvNewTitleData, numNewBytes );
		break;
	}

	// Check our "guest" status

	// Mark stats to be stored at next available opportunity
	m_bSaveTitleData[ fdKey->m_iTitleDataBlock ] = true;

#ifndef NO_STEAM
	// On Steam we can freely upload stats rights now
	//
	// Prepare the data
	//
	switch( fdKey->m_eDataType )
	{
	case TitleDataFieldsDescription_t::DT_uint8:
	case TitleDataFieldsDescription_t::DT_uint16:
	case TitleDataFieldsDescription_t::DT_uint32:
		{
			uint32 i32field[4] = {0};

			memcpy( &i32field[3],
				&m_bufTitleData[ fdKey->m_iTitleDataBlock ][ fdKey->m_numBytesOffset ],
				fdKey->m_eDataType / 8 );

			i32field[0] = *( uint32 * )( &i32field[3] );
			i32field[1] = *( uint16 * )( &i32field[3] );
			i32field[2] = *( uint8  * )( &i32field[3] );

			SetSteamStatWithPotentialOverride( fdKey->m_szFieldName, ( int32 ) i32field[ 2 - ( fdKey->m_eDataType / 16 ) ] );
		}
		break;

	case TitleDataFieldsDescription_t::DT_float:
		{
			float flField = 0.0f;

			memcpy( &flField,
				&m_bufTitleData[ fdKey->m_iTitleDataBlock ][ fdKey->m_numBytesOffset ],
				fdKey->m_eDataType / 8 );

			SetSteamStatWithPotentialOverride( fdKey->m_szFieldName, flField );
		}
		break;

	case TitleDataFieldsDescription_t::DT_uint64:
		{
			uint32 i32field[2] = { 0 };

			memcpy( &i32field[0],
				&m_bufTitleData[ fdKey->m_iTitleDataBlock ][ fdKey->m_numBytesOffset ],
				fdKey->m_eDataType / 8 );

			char chBuffer[ 256 ] = {0};

			for ( int k = 0; k < ARRAYSIZE( i32field ); ++ k )
			{
				Q_snprintf( chBuffer, ARRAYSIZE( chBuffer ), "%s.%d", fdKey->m_szFieldName, k );
				SetSteamStatWithPotentialOverride( chBuffer, ( int32 ) i32field[k] );
			}
		}
		break;

	case TitleDataFieldsDescription_t::DT_BITFIELD:
		{
		#ifdef STEAM_PACK_BITFIELDS
			char chStatField[64] = {0};
			uint32 uiOffsetInTermsOfUINT32 = fdKey->m_numBytesOffset/32;
			V_snprintf( chStatField, sizeof( chStatField ), "bitfield_%02u_%03X", fdKey->m_iTitleDataBlock + 1, uiOffsetInTermsOfUINT32*4 );
			int32 iCombinedBitValue = ( reinterpret_cast< uint32 * >( &m_bufTitleData[fdKey->m_iTitleDataBlock][0] ) )[ uiOffsetInTermsOfUINT32 ];
			SetSteamStatWithPotentialOverride( chStatField, iCombinedBitValue );
		#else
			int32 i32field = !!(
				m_bufTitleData[ fdKey->m_iTitleDataBlock ][ fdKey->m_numBytesOffset/8 ]
			& ( 1 << ( fdKey->m_numBytesOffset % 8 ) ) );
			SetSteamStatWithPotentialOverride( fdKey->m_szFieldName, i32field );
		#endif
		}
		break;
	}
#endif // NO_STEAM

	// If a component achievement was affected, evaluate its state
	int numFieldNameChars = Q_strlen( fdKey->m_szFieldName );
	if ( ( numFieldNameChars > 0 ) && ( fdKey->m_szFieldName[numFieldNameChars - 1] == ']' ) )
	{
		EvaluateAwardsStateBasedOnStats();
	}
}

void PlayerLocal::GetLeaderboardData( KeyValues *pLeaderboardInfo )
{
	// Iterate over all views specified
	for ( KeyValues *pView = pLeaderboardInfo->GetFirstTrueSubKey(); pView; pView = pView->GetNextTrueSubKey() )
	{
		char const *szViewName = pView->GetName();

		KeyValues *pCurrentData = m_pLeaderboardData->FindKey( szViewName );

		// If no such data yet or refresh was requested, then queue this request
		if ( pView->GetInt( ":refresh" ) || !pCurrentData )
		{
			if ( g_pLeaderboardRequestQueue )
				g_pLeaderboardRequestQueue->Request( pView );
		}

		// If we have data, then fill in the values
		pView->MergeFrom( pCurrentData, KeyValues::MERGE_KV_BORROW );
	}
}

void PlayerLocal::UpdateLeaderboardData( KeyValues *pLeaderboardInfo )
{
	DevMsg( "PlayerLocal::UpdateLeaderboardData for %s ...\n", GetName() );

	// Iterate over all views specified
	for ( KeyValues *pView = pLeaderboardInfo->GetFirstTrueSubKey(); pView; pView = pView->GetNextTrueSubKey() )
	{
		char const *szViewName = pView->GetName();

		// Check if the title allows the requested leaderboard update
		KeyValues *kvTitleRequest = new KeyValues( "leaderboard" );
		KeyValues::AutoDelete autodelete_kvTitleRequest( kvTitleRequest );
		kvTitleRequest->SetString( "name", szViewName );
		if ( !g_pMMF->GetMatchTitleGameSettingsMgr()->AllowClientProfileUpdate( kvTitleRequest ) )
			continue;

		// Find leaderboard description
		KeyValues *pDescription = g_pMMF->GetMatchTitle()->DescribeTitleLeaderboard( szViewName );
		if ( !pDescription )
		{
			DevWarning( "   View %s failed to allocate description!\n", szViewName );
		}
		KeyValues::AutoDelete autodelete_pDescription( pDescription );
		KeyValues *pCurrentData = m_pLeaderboardData->FindKey( szViewName );
		if ( !pDescription->GetBool( ":nocache" ) && !pCurrentData )
		{
			pCurrentData = new KeyValues( szViewName );
			m_pLeaderboardData->AddSubKey( pCurrentData );
		}

		// On PC we just issue a write per each request, no batching
		if ( !IsX360() )
		{
			if ( pDescription )
			{
#if !defined( NO_STEAM )
				Steam_WriteLeaderboardData( pDescription, pView );
#endif
			}
			continue;
		}

		// Check if the rating field passes rule requirement
		if ( KeyValues *pValDesc = pDescription->FindKey( ":rating" ) )
		{
			char const *szValue = pDescription->GetString( ":rating/name" );
			KeyValues *val = pView->FindKey( szValue );
			if ( !val || !*szValue )
			{
				DevWarning( "   View %s is rated, but no :rating field '%s' in update!\n", szViewName, szValue );
				continue;
			}

			char const *szRule = pValDesc->GetString( "rule" );
			IPropertyRule *pRuleObj = GetRuleByName( szRule );
			if ( !pRuleObj )
			{
				DevWarning( "   View %s is rated, but invalid rule '%s' specified!\n", szViewName, szRule );
				continue;
			}

			uint64 uiValue = val->GetUint64();
			uint64 uiCurrent = pCurrentData->GetUint64( szValue );
			if ( !pRuleObj->ApplyRuleUint64( uiCurrent, uiValue ) )
			{
				DevMsg( "   View %s is rated, rejected by rule '%s' ( old %lld, new %lld )!\n", szViewName, szRule, uiCurrent, val->GetUint64() );
				continue;
			}

			DevMsg( "   View %s is rated, rule '%s' passed ( old %lld, new %lld )!\n", szViewName, szRule, uiCurrent, uiValue );
			pCurrentData->SetUint64( ":rating", uiValue );
			// ... and proceed setting other contexts and properties
		}

		// Iterate over all values
		for ( KeyValues *val = pView->GetFirstValue(); val; val = val->GetNextValue() )
		{
			char const *szValue = val->GetName();

			if ( szValue[0] == ':' )
				continue;	// no update for system fields

			if ( KeyValues *pValDesc = pDescription->FindKey( szValue ) )
			{
				char const *szRule = pValDesc->GetString( "rule" );
				IPropertyRule *pRuleObj = GetRuleByName( szRule );
				if ( !pRuleObj )
				{
					DevWarning( "   View %s/%s has invalid rule '%s' specified!\n", szViewName, szValue, szRule );
					continue;
				}

				char const *szType = pValDesc->GetString( "type" );

				if ( !Q_stricmp( "uint64", szType ) )
				{
					uint64 uiValue = val->GetUint64();
					uint64 uiCurrent = pCurrentData->GetUint64( szValue );

					// Check if new value passes the rule
					if ( !pRuleObj->ApplyRuleUint64( uiCurrent, uiValue ) )
					{
						DevMsg( "   View %s/%s rejected by rule '%s' ( old %lld, new %lld )!\n",
							szViewName, szValue, szRule, uiCurrent, val->GetUint64() );
						continue;
					}

					DevMsg( "   View %s/%s updated by rule '%s' ( old %lld, new %lld )!\n",
						szViewName, szValue, szRule, uiCurrent, uiValue );
					pCurrentData->SetUint64( szValue, uiValue );
					
				}
				else if ( !Q_stricmp( "float", szType ) )
				{
					float flValue = val->GetFloat();
					float flCurrent = pCurrentData->GetFloat( szValue );

					// Check if new value passes the rule
					if ( !pRuleObj->ApplyRuleFloat( flCurrent, flValue ) )
					{
						DevMsg( "   View %s/%s rejected by rule '%s' ( old %.4f, new %.4f )!\n",
							szViewName, szValue, szRule, flCurrent, val->GetFloat() );
						continue;
					}

					DevMsg( "   View %s/%s updated by rule '%s' ( old %.4f, new %.4f )!\n",
						szViewName, szValue, szRule, flCurrent, flValue );
					pCurrentData->SetFloat( szValue, flValue );
					
				}
				else
				{
					DevWarning( "   View %s/%s has invalid type '%s' specified!\n", szViewName, szValue, szType );
				}
			}
			else
			{
				DevWarning( "   View %s/%s is missing description!\n", szViewName, szValue );
			}
		}
	}

	//
	// Now submit all accumulated leaderboard writes
	//

	DevMsg( "PlayerLocal::UpdateLeaderboardData for %s finished.\n", GetName() );
}

void PlayerLocal::OnLeaderboardRequestFinished( KeyValues *pLeaderboardData )
{
	m_pLeaderboardData->MergeFrom( pLeaderboardData, KeyValues::MERGE_KV_UPDATE );

	KeyValues *kvEvent = new KeyValues( "OnProfileLeaderboardData" );
	kvEvent->SetInt( "iController", m_iController );
	kvEvent->MergeFrom( pLeaderboardData, KeyValues::MERGE_KV_UPDATE );
	g_pMatchEventsSubscription->BroadcastEvent( kvEvent );
}

void PlayerLocal::GetAwardsData( KeyValues *pAwardsData )
{
	for ( KeyValues *kvValue = pAwardsData->GetFirstValue(); kvValue; kvValue = kvValue->GetNextValue() )
	{
		char const *szName = kvValue->GetName();

		// If title is requesting all achievement IDs
		if ( !Q_stricmp( "@achievements", szName ) )
		{
			for ( int k = 0; k < m_arrAchievementsEarned.Count(); ++ k )
			{
				KeyValues *kvAchValue = new KeyValues( "" );
				kvAchValue->SetInt( NULL, m_arrAchievementsEarned[k] );
				kvValue->AddSubKey( kvAchValue );
			}
			continue;
		}

		// If title is requesting all awards IDs
		if ( !Q_stricmp( "@awards", szName ) )
		{
			for ( int k = 0; k < m_arrAvatarAwardsEarned.Count(); ++ k )
			{
				KeyValues *kvAchValue = new KeyValues( "" );
				kvAchValue->SetInt( NULL, m_arrAvatarAwardsEarned[k] );
				kvValue->AddSubKey( kvAchValue );
			}
			continue;
		}

		// Try to find the achievement in the achievement map
		for ( TitleAchievementsDescription_t const *pAchievement = g_pMatchFramework->GetMatchTitle()->DescribeTitleAchievements();
			pAchievement && pAchievement->m_szAchievementName; ++ pAchievement )
		{
			if ( !Q_stricmp( szName, pAchievement->m_szAchievementName ) )
			{
				kvValue->SetInt( "", ( m_arrAchievementsEarned.Find( pAchievement->m_idAchievement ) != m_arrAchievementsEarned.InvalidIndex() ) ? 1 : 0 );
				szName = NULL;
				break;
			}
		}
		if ( !szName )
			continue;

		// Try to find the avatar award in the map
		for ( TitleAvatarAwardsDescription_t const *pAvAward = g_pMatchFramework->GetMatchTitle()->DescribeTitleAvatarAwards();
			pAvAward && pAvAward->m_szAvatarAwardName; ++ pAvAward )
		{
			if ( !Q_stricmp( szName, pAvAward->m_szAvatarAwardName ) )
			{
				kvValue->SetInt( "", ( m_arrAvatarAwardsEarned.Find( pAvAward->m_idAvatarAward ) != m_arrAvatarAwardsEarned.InvalidIndex() ) ? 1 : 0 );
				szName = NULL;
				break;
			}
		}
		if ( !szName )
			continue;

		DevWarning( "pPlayerLocal(%s)->GetAwardsData(%s) UNKNOWN NAME!\n", GetName(), szName );
	}
}

void PlayerLocal::UpdateAwardsData( KeyValues *pAwardsData )
{
	if ( !pAwardsData ) 
		return;

	// Check our "guest" status

	for ( KeyValues *kvValue = pAwardsData->GetFirstValue(); kvValue; kvValue = kvValue->GetNextValue() )
	{
		char const *szName = kvValue->GetName();
		if ( !kvValue->GetInt( "" ) )
			continue;

		// Check if the title allows the requested award
		KeyValues *kvTitleRequest = new KeyValues( "award" );
		KeyValues::AutoDelete autodelete_kvTitleRequest( kvTitleRequest );
		kvTitleRequest->SetString( "name", szName );
		if ( !g_pMMF->GetMatchTitleGameSettingsMgr()->AllowClientProfileUpdate( kvTitleRequest ) )
			continue;

		// Try to find the achievement in the achievement map
		for ( TitleAchievementsDescription_t const *pAchievement = g_pMatchFramework->GetMatchTitle()->DescribeTitleAchievements();
			pAchievement && pAchievement->m_szAchievementName; ++ pAchievement )
		{
			if ( !Q_stricmp( szName, pAchievement->m_szAchievementName ) )
			{
				// Found the achievement to award
				if ( m_arrAchievementsEarned.Find( pAchievement->m_idAchievement ) == m_arrAchievementsEarned.InvalidIndex() )
				{
#if !defined( NO_STEAM )
					bool bSteamResult = steamapicontext->SteamUserStats()->SetAchievement( pAchievement->m_szAchievementName );
					if ( bSteamResult )
					{
						m_bSaveTitleData[0] = true; // signal that stats must be stored
						m_arrAchievementsEarned.AddToTail( pAchievement->m_idAchievement );
						DevMsg( "pPlayerLocal(%s)->UpdateAwardsData(%s) set achievement and stored stats.\n", GetName(), pAchievement->m_szAchievementName );
						
						KeyValues *kvAwardedEvent = new KeyValues( "OnPlayerAward" );
						kvAwardedEvent->SetInt( "iController", m_iController );
						kvAwardedEvent->SetString( "award", pAchievement->m_szAchievementName );
						g_pMatchEventsSubscription->BroadcastEvent( kvAwardedEvent );
					}
					else
					{
						DevWarning( "pPlayerLocal(%s)->UpdateAwardsData(%s) failed to set in Steam.\n", GetName(), pAchievement->m_szAchievementName );
					}
#else
					DevMsg( "pPlayerLocal(%s)->UpdateAwardsData(%s) skipped.\n", GetName(), pAchievement->m_szAchievementName );
#endif
				}
				else
				{
					DevMsg( "pPlayerLocal(%s)->UpdateAwardsData(%s) already earned.\n", GetName(), pAchievement->m_szAchievementName );
				}
				szName = NULL;
				break;
			}
		}
		if ( !szName )
			continue;

		// Try to find the avatar award in the map
		for ( TitleAvatarAwardsDescription_t const *pAvAward = g_pMatchFramework->GetMatchTitle()->DescribeTitleAvatarAwards();
			pAvAward && pAvAward->m_szAvatarAwardName; ++ pAvAward )
		{
			if ( !Q_stricmp( szName, pAvAward->m_szAvatarAwardName ) )
			{
				// Found the avaward to award
				if ( m_arrAvatarAwardsEarned.Find( pAvAward->m_idAvatarAward ) == m_arrAvatarAwardsEarned.InvalidIndex() )
				{
					DevMsg( "pPlayerLocal(%s)->UpdateAwardsData(%s) skipped.\n", GetName(), pAvAward->m_szAvatarAwardName );
				}
				else
				{
					DevMsg( "pPlayerLocal(%s)->UpdateAwardsData(%s) already earned.\n", GetName(), pAvAward->m_szAvatarAwardName );
				}
				szName = NULL;
				break;
			}
		}
		if ( !szName )
			continue;

		DevWarning( "pPlayerLocal(%s)->write_awards(%s) UNKNOWN NAME!\n", GetName(), szName );
	}
}

void PlayerLocal::EvaluateAwardsStateBasedOnStats()
{
	TitleDataFieldsDescription_t const *pTitleDataStorage = g_pMatchFramework->GetMatchTitle()->DescribeTitleDataStorage();
	
	KeyValues *kvAwards = NULL;
	KeyValues::AutoDelete autodelete_kvAwards( kvAwards );

	//
	// Evaluate the state of component achievements
	//
	for ( TitleAchievementsDescription_t const *pAchievement = g_pMatchFramework->GetMatchTitle()->DescribeTitleAchievements();
		pAchievement && pAchievement->m_szAchievementName; ++ pAchievement )
	{
		if ( pAchievement->m_numComponents <= 1 )
			continue;
		if ( m_arrAchievementsEarned.Find( pAchievement->m_idAchievement ) != m_arrAchievementsEarned.InvalidIndex() )
			continue;
		TitleDataFieldsDescription_t const *fdBitfield = TitleDataFieldsDescriptionFindByString( pTitleDataStorage,
			CFmtStr( "%s[1]", pAchievement->m_szAchievementName ) );
		int numComponentsSet = 0;
		for ( ; numComponentsSet < pAchievement->m_numComponents; ++ numComponentsSet, ++ fdBitfield )
		{
			if ( !fdBitfield || !fdBitfield->m_szFieldName || (fdBitfield->m_eDataType != fdBitfield->DT_BITFIELD) )
			{
				DevWarning( "EvaluateAwardsStateBasedOnStats for achievement [%s] error: invalid component configuration (comp#%d)!\n", pAchievement->m_szAchievementName, numComponentsSet + 1 );
				break;
			}
#ifdef _DEBUG
			// In debug make sure bitfields names match
			if ( Q_stricmp( fdBitfield->m_szFieldName, CFmtStr( "%s[%d]", pAchievement->m_szAchievementName, numComponentsSet + 1 ) ) )
			{
				Assert( 0 );
			}
#endif
			if ( !TitleDataFieldsDescriptionGetBit( fdBitfield, this ) )
				break;
		}
		if ( numComponentsSet == pAchievement->m_numComponents )
		{
			// Achievement should be earned based on components
			if ( !kvAwards )
			{
				kvAwards = new KeyValues( "write_award" );
				autodelete_kvAwards.Assign( kvAwards );
			}
			DevMsg( "PlayerLocal(%s)::EvaluateAwardsStateBasedOnStats is awarding %s\n", GetName(), pAchievement->m_szAchievementName );
			kvAwards->SetInt( pAchievement->m_szAchievementName, 1 );
		}
	}

	//
	// Evaluate the state of all avatar awards
	//
	for ( TitleAvatarAwardsDescription_t const *pAvatarAward = g_pMatchFramework->GetMatchTitle()->DescribeTitleAvatarAwards();
		pAvatarAward && pAvatarAward->m_szAvatarAwardName; ++ pAvatarAward )
	{
		if ( TitleDataFieldsDescription_t const *fdBitfield = TitleDataFieldsDescriptionFindByString( pTitleDataStorage, pAvatarAward->m_szTitleDataBitfieldStatName ) )
		{
			if ( TitleDataFieldsDescriptionGetBit( fdBitfield, this ) )
			{
				m_arrAvatarAwardsEarned.FindAndFastRemove( pAvatarAward->m_idAvatarAward );
				m_arrAvatarAwardsEarned.AddToTail( pAvatarAward->m_idAvatarAward );
			}
		}
	}

	//
	// Award all accumulated awards
	//
	UpdateAwardsData( kvAwards );
}

void PlayerLocal::LoadGuestsTitleData()
{
}


void PlayerLocal::OnProfileTitleDataLoaded( int iErrorCode )
{
	if ( iErrorCode )
	{
		g_pMatchEventsSubscription->BroadcastEvent( new KeyValues( "OnProfileDataLoadFailed", "iController", m_iController, "error", iErrorCode ) );
	}
	else
	{
		LoadGuestsTitleData();
		g_pMatchEventsSubscription->BroadcastEvent( new KeyValues( "OnProfileDataLoaded", "iController", m_iController ) );
	}

	// Invite awaiting our title data
	if ( m_uiPlayerFlags & PLAYER_INVITE_AWAITING_TITLEDATA )
	{
		m_uiPlayerFlags &=~PLAYER_INVITE_AWAITING_TITLEDATA;

		g_pMatchFramework->GetEventsSubscription()->BroadcastEvent( new KeyValues(
			"OnInvite", "action", "join" ) );
	}
}

XUSER_SIGNIN_STATE PlayerLocal::GetAssumedSigninState()
{
#if !defined( NO_STEAM )
	if ( steamapicontext->SteamUser() && steamapicontext->SteamUser()->BLoggedOn() )
		return eXUserSigninState_SignedInToLive;
	else
		return eXUserSigninState_SignedInLocally;
#else // No steam.


	return eXUserSigninState_NotSignedIn;


#endif
}

void PlayerLocal::SetNeedsSave()
{
	for ( int ii=0; ii<TITLE_DATA_COUNT; ++ii )
	{
		m_bSaveTitleData[ii] = true;
	}
}

CON_COMMAND_F( ms_player_dump_properties, "Prints a dump the current players property data", FCVAR_CHEAT )
{
	Msg( "[DMM] ms_player_dump_properties...\n" );
	Msg( "        Num game users: %d\n", XBX_GetNumGameUsers() );
	for ( unsigned int iUserSlot = 0; iUserSlot < XBX_GetNumGameUsers(); ++ iUserSlot )
	{
		int iCtrlr = iUserSlot;
		bool bGuest = false;
		Msg( "Slot%d ctrlr%d: %s\n", iUserSlot, iCtrlr, bGuest ? "guest" : "profile" );
		IPlayerLocal *pPlayerLocal = g_pPlayerManager->GetLocalPlayer( iCtrlr );
		if ( !pPlayerLocal )
			continue;
		Msg( "  Name = %s\n", pPlayerLocal->GetName() );
		TitleDataFieldsDescription_t const *fields = g_pMatchFramework->GetMatchTitle()->DescribeTitleDataStorage();
		for ( ; fields && fields->m_szFieldName; ++ fields )
		{
			switch ( fields->m_eDataType )
			{
			case TitleDataFieldsDescription_t::DT_BITFIELD:
				Msg( "BITFIELD %s = %u\n", fields->m_szFieldName, TitleDataFieldsDescriptionGetBit( fields, pPlayerLocal ) ? 1 : 0 );
				break;
			case TitleDataFieldsDescription_t::DT_uint8:
				Msg( "UINT8    %s = %u\n", fields->m_szFieldName, TitleDataFieldsDescriptionGetValue<uint8>( fields, pPlayerLocal ) );
				break;
			case TitleDataFieldsDescription_t::DT_uint16:
				Msg( "UINT16   %s = %u\n", fields->m_szFieldName, TitleDataFieldsDescriptionGetValue<uint16>( fields, pPlayerLocal ) );
				break;
			case TitleDataFieldsDescription_t::DT_uint32:
				Msg( "UINT32   %s = %u\n", fields->m_szFieldName, TitleDataFieldsDescriptionGetValue<uint32>( fields, pPlayerLocal ) );
				break;
			case TitleDataFieldsDescription_t::DT_float:
				Msg( "FLOAT    %s = %.3f\n", fields->m_szFieldName, TitleDataFieldsDescriptionGetValue<float>( fields, pPlayerLocal ) );
				break;
			case TitleDataFieldsDescription_t::DT_uint64:
				Msg( "UINT64   %s = 0x%llX\n", fields->m_szFieldName, TitleDataFieldsDescriptionGetValue<uint64>( fields, pPlayerLocal ) );
				break;
			}
		}
	}
	Msg( "        ms_player_dump_properties finished.\n" );
}

#ifdef _DEBUG
CON_COMMAND_F( ms_player_award, "Awards the current player an award", FCVAR_CHEAT )
{
	int iCtrlr = args.FindArgInt( "ctrlr", XBX_GetPrimaryUserId() );
	IPlayerLocal *pPlayer = g_pPlayerManager->GetLocalPlayer( iCtrlr );
	if ( !pPlayer )
	{
		DevWarning( "ERROR: Controller %d is not registered!\n", iCtrlr );
		return;
	}
	
	KeyValues *kvAwards = new KeyValues( "write_awards", args.FindArg( "award" ), 1 );
	KeyValues::AutoDelete autodelete( kvAwards );
	(( PlayerLocal * )pPlayer)->UpdateAwardsData( kvAwards );
}
#endif

#if !defined(NO_STEAM) && !defined(_CERT)
CON_COMMAND_F( ms_player_unaward, "UnAwards the current player an award", FCVAR_DEVELOPMENTONLY )
{
	if ( !CommandLine()->FindParm( "+ms_player_unaward" ) )
	{
		Warning( "Error: You must pass +ms_player_unaward from command line!\n" );
		return;
	}

	if ( !args.FindArg( "unaward" ) )
	{
		Warning( "Syntax: +ms_player_unaward unaward ACHIEVEMENT|everything\n" );
		return;
	}

	if ( !V_stricmp( "everything", args.FindArg( "unaward" ) ) )
	{
		steamapicontext->SteamUserStats()->ResetAllStats( false );
		steamapicontext->SteamUserStats()->ResetAllStats( true );
		while ( steamapicontext->SteamRemoteStorage()->GetFileCount() > 0 )
		{
			int nUnused;
			steamapicontext->SteamRemoteStorage()->FileDelete( steamapicontext->SteamRemoteStorage()->GetFileNameAndSize( 0, &nUnused ) );
		}
		steamapicontext->SteamUserStats()->StoreStats();
		Msg( "Everything was reset!\n" );
		if ( !IsGameConsole() )
		{
			g_pMatchExtensions->GetIVEngineClient()->ExecuteClientCmd( "exec config_default.cfg; exec joy_preset_1.cfg; host_writeconfig;" );
		}
		return;
	}

	if ( steamapicontext->SteamUserStats()->ClearAchievement( args.FindArg( "unaward" ) ) )
	{
		steamapicontext->SteamUserStats()->StoreStats();
		Msg( "%s unawarded!\n", args.FindArg( "unaward" ) );
	}
	else
	{
		Warning( "%s failed\n", args.FindArg( "unaward" ) );
	}
}
#endif


