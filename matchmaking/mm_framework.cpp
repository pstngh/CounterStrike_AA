//===== Copyright © 1996-2009, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#include "mm_framework.h"
#include "mm_netmsgcontroller.h"

#include "matchsystem.h"

#include "mm_title_main.h"
#include "x360_lobbyapi.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// Stress testing events listeners every frame
ConVar mm_events_listeners_validation( "mm_events_listeners_validation", "0", FCVAR_DEVELOPMENTONLY );


//
// Implementation of Steam invite listener
//
uint64 g_uiLastInviteFlags = 0ull;
#if !defined( NO_STEAM ) && !defined( SWDS )
class CMatchSteamInviteListener
{
public:
	void RunFrame();
	void Register();
	STEAM_CALLBACK_MANUAL( CMatchSteamInviteListener, Steam_OnGameLobbyJoinRequested, GameLobbyJoinRequested_t, m_CallbackOnGameLobbyJoinRequested );

protected:
	GameLobbyJoinRequested_t m_msgPending;
}
g_MatchSteamInviteListener;

void CMatchSteamInviteListener::Register()
{
	m_CallbackOnGameLobbyJoinRequested.Register( this, &CMatchSteamInviteListener::Steam_OnGameLobbyJoinRequested );
}
#else
class CMatchSteamInviteListener
{
public:
	void RunFrame() {}
	void Register() {}
}
g_MatchSteamInviteListener;
#endif



//
// Implementation
//

CMatchFramework::CMatchFramework() :
	m_pMatchSession( NULL ),
	m_bJoinTeamSession( false ),
	m_pTeamSessionSettings( NULL )
{
}

CMatchFramework::~CMatchFramework()
{
	;
}

InitReturnVal_t CMatchFramework::Init()
{
	InitReturnVal_t ret = INIT_OK;

	ret = MM_Title_Init();
	if ( ret != INIT_OK )
		return ret;

	g_MatchSteamInviteListener.Register();

	return INIT_OK;
}

void CMatchFramework::Shutdown()
{
	// Shutdown event system
	g_pMatchEventsSubscription->Shutdown();

	// Shutdown the title
	MM_Title_Shutdown();

	// Cancel any pending server updates before shutdown
	g_pServerManager->EnableServersUpdate( false );

	// Cancel any pending datacenter queries
	g_pDatacenter->EnableUpdate( false );
}

void CMatchFramework::RunFrame()
{
	// Run frame listeners validation if requested
	if ( mm_events_listeners_validation.GetBool() )
	{
		g_pMatchEventsSubscription->BroadcastEvent( new KeyValues( "mm_events_listeners_validation" ) );
	}

	RunFrame_Invite();
	g_MatchSteamInviteListener.RunFrame();

	// Let the network mgr run
	g_pConnectionlessLanMgr->Update();

	// Let the matchsystem run
	g_pMatchSystem->Update();

	// Let the match session run
	if ( m_pMatchSession )
		m_pMatchSession->Update();

	// Let the match title run frame
	g_pIMatchTitle->RunFrame();

	if ( m_bJoinTeamSession )
	{
		m_bJoinTeamSession = false;
		MatchSession( m_pTeamSessionSettings );
		m_pTeamSessionSettings->deleteThis();
		m_pTeamSessionSettings = NULL;
	}
}

IMatchExtensions * CMatchFramework::GetMatchExtensions()
{
	return g_pMatchExtensions;
}

IMatchEventsSubscription * CMatchFramework::GetEventsSubscription()
{
	return g_pMatchEventsSubscription;
}

IMatchTitle * CMatchFramework::GetMatchTitle()
{
	return g_pIMatchTitle;
}

IMatchTitleGameSettingsMgr * CMatchFramework::GetMatchTitleGameSettingsMgr()
{
	return g_pIMatchTitleGameSettingsMgr;
}

IMatchNetworkMsgController * CMatchFramework::GetMatchNetworkMsgController()
{
	return g_pMatchNetMsgControllerBase;
}

IMatchSystem * CMatchFramework::GetMatchSystem()
{
	return g_pMatchSystem;
}

void CMatchFramework::ApplySettings( KeyValues* keyValues )
{
	g_pMatchExtensions->GetIServerGameDLL()->ApplyGameSettings( keyValues );
}


static uint64 s_InviteInfo;
static bool s_bInviteSessionDelayedJoin;
static int s_nInviteConfirmed;

template < int datasize >
static bool IsZeroData( void const *pvData )
{
	static char s_zerodata[ datasize ];
	return !memcmp( s_zerodata, pvData, datasize );
}

static bool ValidateInviteController( int iController )
{

	return true;
}

static bool ValidateInviteControllers()
{
	for ( DWORD k = 0; k < XBX_GetNumGameUsers(); ++ k )
	{
		if ( !ValidateInviteController( XBX_GetUserId( k ) ) )
			return false;
	}
	return true;
}

static bool VerifyInviteEligibility()
{

	// Check that every user is valid
	return ValidateInviteControllers();
}

static void JoinInviteSession()
{
	s_bInviteSessionDelayedJoin = false;

	if ( !s_InviteInfo )
		return;
	
	if ( g_pMatchExtensions->GetIVEngineClient()->IsDrawingLoadingImage() )
	{
		s_bInviteSessionDelayedJoin = true;
		return;
	}

	// Invites cannot be accepted from inside an event broadcast
	// internally used events must be top-level events since they
	// operate on signed in / active users, trigger playermanager,
	// account access and other events
	// Wait until next frame in such case
	if ( g_pMatchEventsSubscription && g_pMatchEventsSubscription->IsBroacasting() )
	{
		s_bInviteSessionDelayedJoin = true;
		return;
	}

#if !defined( NO_STEAM ) && !defined( SWDS )
	extern bool g_bSteamStatsReceived;
	if ( !g_bSteamStatsReceived && ( g_uiLastInviteFlags & MM_INVITE_FLAG_PCBOOT ) )
	{
		s_bInviteSessionDelayedJoin = true;
		return;
	}
#endif

	DevMsg( "JoinInviteSession: sessionid = %llx\n", s_InviteInfo );

	//
	// Validate the user accepting the invite
	//

	//
	// Check if the currently-involved user is accepting the invite
	//

	// Validate storage device
	s_nInviteConfirmed = -1;
	if ( KeyValues *notify = new KeyValues( "OnInvite" ) )
	{
		notify->SetUint64( "sessionid", s_InviteInfo );
		notify->SetString( "action", "storage" );
		notify->SetPtr( "confirmed", &s_nInviteConfirmed );

		g_pMatchEventsSubscription->BroadcastEvent( notify );

		// If handlers decided they need to confirm storage devices, etc.
		if ( s_nInviteConfirmed != -1 )
		{
			DevMsg( "JoinInviteSession: waiting for storage device selection...\n" );
			return;
		}
	}

	// Verify eligibility
	DevMsg( "JoinInviteSession: verifying eligibility...\n" );
	if ( !VerifyInviteEligibility() )
		return;
	DevMsg( "JoinInviteSession: connecting...\n" );

	//
	// Argument validation
	//

	// Requesting to join the stored off session
	KeyValues *pSettings = KeyValues::FromString(
		"settings",
		" system { "
			" network LIVE "
		" } "
		" options { "
			" action joinsession "
		" } "
		);
	
	pSettings->SetUint64( "options/sessionid", s_InviteInfo );
	
	KeyValues::AutoDelete autodelete( pSettings );
	Q_memset( &s_InviteInfo, 0, sizeof( s_InviteInfo ) );

	g_pMatchFramework->MatchSession( pSettings );
}

static void OnInviteAccepted()
{
	// Verify eligibility
	DevMsg( "OnInviteAccepted: verifying eligibility...\n" );
	if ( !VerifyInviteEligibility() )
		return;
	DevMsg( "OnInviteAccepted: confirming...\n" );

	// Make sure the user confirms the invite
	s_nInviteConfirmed = -1;
	if ( KeyValues *notify = new KeyValues( "OnInvite" ) )
	{
		notify->SetUint64( "sessionid", s_InviteInfo );
		notify->SetString( "action", "accepted" );
		notify->SetPtr( "confirmed", &s_nInviteConfirmed );

		g_pMatchEventsSubscription->BroadcastEvent( notify );

		// If handlers decided they need to confirm destructive actions or
		// select storage devices, etc.
		if ( s_nInviteConfirmed != -1 )
		{
			DevMsg( "OnInviteAccepted: waiting for confirmation...\n" );
			return;
		}
	}
	DevMsg( "OnInviteAccepted: accepting...\n" );

	// Otherwise, launch depending on our current MOD
	// if ( !Q_stricmp( GetCurrentMod(), "left4dead2" ) ) <-- for multi-game package
	{
		// Kick off our join
		JoinInviteSession();
	}
	// 	else	<-- for multi-game package supporting cross-game invites
	// 	{
	// 		// Save off our session ID for later retrieval
	// 		// NOTE: We may need to actually save off the inviter's XID and search for them later on if we took too long or the
	// 		//		 session they were a part of went away
	// 
	// 		XBX_SetInviteSessionId( inviteInfo.hostInfo.sessionID );
	// 
	// 		// Quit via the menu path "QuitNoConfirm"
	// 		EngineVGui()->SystemNotification( SYSTEMNOTIFY_INVITE_SHUTDOWN, NULL );
	// 	}
}

void CMatchFramework::RunFrame_Invite()
{
	if ( s_bInviteSessionDelayedJoin )
		JoinInviteSession();
}

void CMatchFramework::AcceptInvite( int iController )
{
}

#if !defined( NO_STEAM ) && !defined( SWDS )
void CMatchSteamInviteListener::Steam_OnGameLobbyJoinRequested( GameLobbyJoinRequested_t *pJoinInvite )
{

	g_uiLastInviteFlags = ( pJoinInvite->m_steamIDFriend.ConvertToUint64() == ~0ull ) ? MM_INVITE_FLAG_PCBOOT : 0;

	m_msgPending = GameLobbyJoinRequested_t();
	s_bInviteSessionDelayedJoin = false;
	s_InviteInfo = pJoinInvite->m_steamIDLobby.ConvertToUint64();
	if ( !s_InviteInfo )
		return;
	
	// Whether we have to make invite go pending
	char chBuffer[2] = {};
	if ( g_pMatchExtensions->GetIVEngineClient()->IsDrawingLoadingImage() ||
		( g_pMatchEventsSubscription && g_pMatchEventsSubscription->IsBroacasting() ) ||
		( g_pMatchExtensions->GetIBaseClientDLL()->GetStatus( chBuffer, 2 ), ( chBuffer[0] != '+' ) ) )
	{
		m_msgPending = *pJoinInvite;
		return;
	}

	// Invite accepted logic after globals have been setup
	OnInviteAccepted();
}

void CMatchSteamInviteListener::RunFrame()
{
	if ( m_msgPending.m_steamIDLobby.IsValid() )
	{
		GameLobbyJoinRequested_t msgRequest = m_msgPending;
		Steam_OnGameLobbyJoinRequested( &msgRequest );
	}
}
#endif


IMatchSession *CMatchFramework::GetMatchSession()
{
	return m_pMatchSession;
}

void CMatchFramework::CreateSession( KeyValues *pSettings )
{
	DevMsg( "CreateSession: \n");
	KeyValuesDumpAsDevMsg( pSettings );

#ifndef SWDS
	if ( !pSettings )
		return;

	IMatchSessionInternal *pMatchSessionNew = NULL;

	//
	// Analyze the type of session requested to create
	//

	char const *szNetwork = pSettings->GetString( "system/network", "offline" );

	// Recompute XUIDs for the session type that we are creating
	g_pPlayerManager->RecomputePlayerXUIDs( szNetwork );

	//
	// Process create session request
	//
	if ( !Q_stricmp( "offline", szNetwork ) )
	{
		CMatchSessionOfflineCustom *pSession = new CMatchSessionOfflineCustom( pSettings );
		pMatchSessionNew = pSession;
	}
	else
	{
		CMatchSessionOnlineHost *pSession = new CMatchSessionOnlineHost( pSettings );
		pMatchSessionNew = pSession;
	}

	if ( pMatchSessionNew )
	{
		CloseSession();
		m_pMatchSession = pMatchSessionNew;
	}
#endif
}

void CMatchFramework::MatchSession( KeyValues *pSettings )
{
#ifndef SWDS
	if ( !pSettings )
		return;

	DevMsg( "MatchSession: \n");
	KeyValuesDumpAsDevMsg( pSettings );

	IMatchSessionInternal *pMatchSessionNew = NULL;

	//
	// Analyze what kind of client-side matchmaking
	// needs to happen.
	//

	char const *szNetwork = pSettings->GetString( "system/network", "LIVE" );
	char const *szAction = pSettings->GetString( "options/action", "" );

	// Recompute XUIDs for the session type that we are creating
	g_pPlayerManager->RecomputePlayerXUIDs( szNetwork );

	//
	// Process match session request
	//
	if ( !Q_stricmp( "joinsession", szAction ) )
	{

		// We have an explicit session to join
		CMatchSessionOnlineClient *pSession = new CMatchSessionOnlineClient( pSettings );
		pMatchSessionNew = pSession;
	}
	else if ( !Q_stricmp( "joininvitesession", szAction ) )
	{
	}
	else // "quickmatch" or "custommatch"
	{

		CMatchSessionOnlineSearch *pSession = new CMatchSessionOnlineSearch( pSettings );
		pMatchSessionNew = pSession;
	}

	if ( pMatchSessionNew )
	{
		CloseSession();
		m_pMatchSession = pMatchSessionNew;
	}
#endif
}


void CMatchFramework::CloseSession()
{
	// Destroy the session
	if ( m_pMatchSession )
	{
		IMatchSessionInternal *pMatchSession = m_pMatchSession;
		m_pMatchSession = NULL;
		pMatchSession->Destroy();

		g_pMatchEventsSubscription->BroadcastEvent( new KeyValues( "OnMatchSessionUpdate", "state", "closed" ) );
	}
}

bool CMatchFramework::IsOnlineGame( void )
{
	IMatchSession *pMatchSession = GetMatchSession();

	if ( pMatchSession ) 
	{
		KeyValues* kv = pMatchSession->GetSessionSettings();
		if ( kv )
		{
			char const *szMode = kv->GetString( "system/network", NULL );
			if ( szMode && !V_stricmp( "LIVE", szMode ) )
			{
				return true;
			}
		}
	}
	return false;
}

void CMatchFramework::UpdateTeamProperties( KeyValues *pTeamProperties )
{
	IMatchSession *pMatchSession = GetMatchSession();
	IMatchTitleGameSettingsMgr *pMatchTitleGameSettingsMgr = GetMatchTitleGameSettingsMgr();

	if ( pMatchSession && pMatchTitleGameSettingsMgr )
	{
		pMatchSession->UpdateTeamProperties( pTeamProperties );
		KeyValues *pCurrentSettings = pMatchSession->GetSessionSettings();
		pMatchTitleGameSettingsMgr->UpdateTeamProperties( pCurrentSettings, pTeamProperties );
	}
}

void CMatchFramework::OnEvent( KeyValues *pEvent )
{
	char const *szEvent = pEvent->GetName();

	if ( !Q_stricmp( "mmF->CloseSession", szEvent ) )
	{
		CloseSession();
		return;
	}
	else if ( !Q_stricmp( "OnInvite", szEvent ) )
	{
		if ( !Q_stricmp( "join", pEvent->GetString( "action" ) ) )
		{
			s_bInviteSessionDelayedJoin = true;
		}
		else if ( !Q_stricmp( "deny", pEvent->GetString( "action" ) ) )
		{
			Q_memset( &s_InviteInfo, 0, sizeof( s_InviteInfo ) );
			s_bInviteSessionDelayedJoin = false;
		}
		return;
	}
	else if ( !Q_stricmp( "OnSteamOverlayCall::LobbyJoin", szEvent ) )
	{
#if !defined( NO_STEAM ) && !defined( SWDS )
		GameLobbyJoinRequested_t msg;
		msg.m_steamIDLobby.SetFromUint64( pEvent->GetUint64( "sessionid" ) );
		msg.m_steamIDFriend.SetFromUint64( ~0ull );
		g_MatchSteamInviteListener.Steam_OnGameLobbyJoinRequested( &msg );
#endif
		return;
	}
	else if ( !Q_stricmp( "OnMatchSessionUpdate", szEvent ) )
	{	
		KeyValues *pUpdate = pEvent->FindKey( "update" );
		
		if ( pUpdate )
		{
			const char *pAction = pUpdate->GetString( "options/action", "" );
			if ( !Q_stricmp( "joinsession", pAction ) )
			{
				KeyValues *pTeamMembers = pUpdate->FindKey( "teamMembers" );

				if ( pTeamMembers )
				{
					// Received console team match settings from host
					// Find what team we are on

					int numPlayers = pTeamMembers->GetInt( "numPlayers" );
					int playerTeam = -1;

					int activeUer = XBX_GetPrimaryUserId();
					IPlayerLocal *player = g_pPlayerManager->GetLocalPlayer( activeUer );
					uint64 localPlayerId = player->GetXUID();

					for ( int i = 0; i < numPlayers; i++ )
					{
						KeyValues *pTeamPlayer = pTeamMembers->FindKey( CFmtStr( "player%d", i ) );
						uint64 playerId = pTeamPlayer->GetUint64( "xuid" );

						if ( playerId == localPlayerId )
						{
							int team = pTeamPlayer->GetInt( "team" );
							DevMsg( "Adding player %llu to team %d\n", playerId, team );
							playerTeam = team;
							break;
						}
					}
				
					m_pTeamSessionSettings = pUpdate->MakeCopy();
					m_pTeamSessionSettings->SetName( "settings ");

					// Delete the "teamMembers" key
					m_pTeamSessionSettings->RemoveSubKey( m_pTeamSessionSettings->FindKey( "teamMembers" ) );

					// Add "conteam" value
					m_pTeamSessionSettings->SetInt( "conteam", playerTeam );

					// Add the "sessionHostDataUnpacked" key
					KeyValues *pSessionHostDataSrc = pUpdate->FindKey( "sessionHostDataUnpacked" );
					if ( pSessionHostDataSrc )
					{
						KeyValues *pSessionHostDataDst = m_pTeamSessionSettings->CreateNewKey();
						pSessionHostDataDst->SetName( "sessionHostDataUnpacked" );
					
						pSessionHostDataSrc->CopySubkeys( pSessionHostDataDst );
					}

					m_bJoinTeamSession = true;
				}
			}
		}
	}

	//
	// Delegate to the managers
	//
	if ( g_pPlayerManager )
		g_pPlayerManager->OnEvent( pEvent );
	if ( g_pServerManager )
		g_pServerManager->OnEvent( pEvent );
	if ( g_pDatacenter )
		g_pDatacenter->OnEvent( pEvent );
	if ( g_pDlcManager )
		g_pDlcManager->OnEvent( pEvent );

	//
	// Delegate to the title
	//
	if ( g_pIMatchTitleEventsSink )
		g_pIMatchTitleEventsSink->OnEvent( pEvent );

	//
	// Delegate to the session
	//
	if ( m_pMatchSession )
		m_pMatchSession->OnEvent( pEvent );
}

void CMatchFramework::SetCurrentMatchSession( IMatchSessionInternal *pNewMatchSession )
{
	m_pMatchSession = pNewMatchSession;
}

uint64 CMatchFramework::GetLastInviteFlags()
{
	return g_uiLastInviteFlags;
}


