//===== Copyright © 1996-2009, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#include "mm_framework.h"

#include "fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar mm_session_sys_delay_create( "mm_session_sys_delay_create", "0", FCVAR_DEVELOPMENTONLY );
ConVar mm_session_sys_delay_create_host( "mm_session_sys_delay_create_host", "1.2", FCVAR_DEVELOPMENTONLY );
ConVar mm_session_sys_timeout( "mm_session_sys_timeout", "3", FCVAR_DEVELOPMENTONLY );
ConVar mm_session_sys_connect_timeout( "mm_session_sys_connect_timeout", "8", FCVAR_DEVELOPMENTONLY );
ConVar mm_session_team_res_timeout( "mm_session_team_res_timeout", "30", FCVAR_DEVELOPMENTONLY );
ConVar mm_session_voice_loading( "mm_session_voice_loading", "0", FCVAR_DEVELOPMENTONLY );
ConVar mm_session_sys_ranking_timeout( "mm_session_sys_ranking_timeout", "12", FCVAR_DEVELOPMENTONLY );
ConVar mm_session_sys_pkey( "mm_session_sys_pkey", "", FCVAR_RELEASE );
ConVar mm_session_sys_kick_ban_duration( "mm_session_sys_kick_ban_duration", "180", FCVAR_RELEASE );

#define STEAM_LOBBY_CHAT_MSG_BUFFER_SIZE 65536


static bool SysSession_AllowCreate()
{

	static float s_fAllowCreateTime = 0.0f;
	float flDelay = mm_session_sys_delay_create.GetFloat();
	if ( flDelay <= 0 )
	{
		s_fAllowCreateTime = 0.0f;
		return true;
	}
	else
	{
		if ( s_fAllowCreateTime > 0.0f )
		{
			if ( Plat_FloatTime() < s_fAllowCreateTime )
				return false; // Delay time not elapsed yet

			s_fAllowCreateTime = 0.0f;
			return true; // Delay time has elapsed already
		}
		else
		{
			s_fAllowCreateTime = Plat_FloatTime() + flDelay;
			return false; // Starting the delay time
		}
	}
}


CSysSessionBase::CSysSessionBase( KeyValues *pSettings ) :
#if !defined( NO_STEAM )
	m_CallbackOnServersConnected( this, &CSysSessionBase::Steam_OnServersConnected ),
	m_CallbackOnServersDisconnected( this, &CSysSessionBase::Steam_OnServersDisconnected ),
	m_CallbackOnP2PSessionRequest( this, &CSysSessionBase::Steam_OnP2PSessionRequest ),
	m_bVoiceUsingSessionP2P( false ),
#endif
	m_Voice_flLastHeadsetStatusCheck( -1.0f ),
	m_pSettings( pSettings ),
	m_xuidMachineId( g_pPlayerManager->GetLocalPlayer( XBX_GetPrimaryUserId() )->GetXUID() ),
	m_result( RESULT_UNDEFINED )
{
}


CSysSessionBase::~CSysSessionBase()
{
	;
}

bool CSysSessionBase::Update()
{
#if !defined( NO_STEAM )
	if ( !IsServiceSession() )
	{
		// Process P2P network
		uint32 uiSteamMsgSize = 0;
		CUtlMemory< byte > utlMemory;
		CSteamID idRemote;
		while( steamapicontext->SteamNetworking()->IsP2PPacketAvailable( &uiSteamMsgSize, INetSupport::SP2PC_LOBBY ) )
		{
			utlMemory.EnsureCapacity( uiSteamMsgSize );
			if ( steamapicontext->SteamNetworking()->ReadP2PPacket( utlMemory.Base(), uiSteamMsgSize, &uiSteamMsgSize, &idRemote, INetSupport::SP2PC_LOBBY ) )
				UnpackAndReceiveMessage( utlMemory.Base(), uiSteamMsgSize, true, idRemote.ConvertToUint64() );
		}
	}
#endif

	Voice_UpdateLocalHeadsetsStatus();
	Voice_CaptureAndTransmitLocalVoiceData();

	return true;
}

bool CSysSessionBase::IsServiceSession()
{
	if ( !m_pSettings )
		return true;

	if ( char const *szNetFlag = m_pSettings->GetString( "system/netflag", NULL ) )
	{
		if ( !Q_stricmp( "teamlink", szNetFlag ) )
			return true;
	}

	return false;
}

void CSysSessionBase::OnSessionEvent( KeyValues *notify )
{
	g_pMatchEventsSubscription->BroadcastEvent( notify );
}

void CSysSessionBase::SendEventsNotification( KeyValues *notify )
{
	OnSessionEvent( notify );
}

void CSysSessionBase::Destroy()
{
#if !defined( NO_STEAM )
	Voice_ProcessTalkers( NULL, false );

	if ( m_lobby.m_uiLobbyID )
	{
		if ( !IsServiceSession() )
		{
			for ( int k = 0, kNum = steamapicontext->SteamMatchmaking()->GetNumLobbyMembers( m_lobby.m_uiLobbyID ); k < kNum; ++ k )
			{
				CSteamID idRemote = steamapicontext->SteamMatchmaking()->GetLobbyMemberByIndex( m_lobby.m_uiLobbyID, k );
				steamapicontext->SteamNetworking()->CloseP2PChannelWithUser( idRemote, INetSupport::SP2PC_LOBBY );
			}
		}

		steamapicontext->SteamMatchmaking()->LeaveLobby( m_lobby.m_uiLobbyID );
	}
	m_lobby = CSteamLobbyObject();
#endif

	delete this;
}

void CSysSessionBase::DebugPrint()
{
	DevMsg( "CSysSessionBase\n" );
	DevMsg( "    machineid: %llx\n", m_xuidMachineId );
#if !defined( NO_STEAM )
	DevMsg( "    lobby id:  %llx\n", m_lobby.m_uiLobbyID );
	DevMsg( "    lbystate:  %d\n", m_lobby.m_eLobbyState );

	if ( m_lobby.m_uiLobbyID )
	{
		int numMembers = steamapicontext->SteamMatchmaking()->GetNumLobbyMembers( m_lobby.m_uiLobbyID );
		DevMsg( "    owner:     %llx\n", steamapicontext->SteamMatchmaking()->GetLobbyOwner( m_lobby.m_uiLobbyID ).ConvertToUint64() );
		DevMsg( "    members:   %d/%s/%d\n", numMembers,
			steamapicontext->SteamMatchmaking()->GetLobbyData( m_lobby.m_uiLobbyID, "members:numSlots" ),
			steamapicontext->SteamMatchmaking()->GetLobbyMemberLimit( m_lobby.m_uiLobbyID ) );
		for ( int k = 0; k < numMembers; ++ k )
		{
			XUID xuid = steamapicontext->SteamMatchmaking()->GetLobbyMemberByIndex( m_lobby.m_uiLobbyID, k ).ConvertToUint64();
			KeyValues *kvPlayer = SessionMembersFindPlayer( m_pSettings, xuid );
			if ( kvPlayer )
			{
				DevMsg( "    member%02d:  %llx '%s'\n", k, xuid, kvPlayer->GetString( "name" ) );
			}
			else
			{
				DevMsg( "    member%02d:  %llx <n/a>\n", k, xuid );
			}
		}
		DevMsg( "    ldata:net: %s\n", steamapicontext->SteamMatchmaking()->GetLobbyData( m_lobby.m_uiLobbyID, "system:network" ) );
	}
#endif
}

void CSysSessionBase::Command( KeyValues *pCommand )
{
	KeyValues *msg = new KeyValues( "SysSession::Command" );
	KeyValues::AutoDelete autodelete( msg );

	msg->AddSubKey( pCommand->MakeCopy() );

	SendMessage( msg );
}

uint64 CSysSessionBase::GetReservationCookie()
{
	if ( uint64 uiReservationCookieOverride = m_pSettings->GetUint64( "server/reservationid", 0ull ) )
		return uiReservationCookieOverride;

	return GetNonceCookie();
}

uint64 CSysSessionBase::GetNonceCookie()
{
#if !defined( NO_STEAM )
	return m_lobby.GetSessionId();
#else
	Assert( false ); // Not implemented for this platform
	return 0;
#endif
}

uint64 CSysSessionBase::GetSessionID()
{
	return GetNonceCookie();
}

void CSysSessionBase::ReplyLanSearch( KeyValues *msg )
{
	// Assemble a search reply message
	KeyValues *reply = new KeyValues( "GameDetailsPlayer" );
	KeyValues::AutoDelete autodelete( reply );

	// Put information about our session
#if !defined( NO_STEAM )
	reply->SetUint64( "options/sessionid", m_lobby.m_uiLobbyID );
#endif

	// Information about primary player
	if ( IPlayerLocal *pPlayer = g_pPlayerManager->GetLocalPlayer( XBX_GetPrimaryUserId() ) )
	{
		reply->SetString( "player/name", pPlayer->GetName() );
		reply->SetUint64( "player/xuid", pPlayer->GetXUID() );

	}

	// Compose the binary encoding of game details
	CUtlBuffer bufGameDetails;
	bufGameDetails.ActivateByteSwapping( !CByteswap::IsMachineBigEndian() );
	g_pMatchFramework->GetMatchNetworkMsgController()->PackageGameDetailsForQOS( m_pSettings, bufGameDetails );

	reply->SetPtr( "binary/ptr", bufGameDetails.Base() );
	reply->SetInt( "binary/size", bufGameDetails.TellMaxPut() );

	// Reply to sender
	g_pConnectionlessLanMgr->SendPacket( reply,
		( !IsX360() && msg ) ? msg->GetString( "from", NULL ) : NULL );
}

void CSysSessionBase::SendMessage( KeyValues *msg )
{
#if !defined( NO_STEAM )

	CUtlBuffer buf;
	buf.ActivateByteSwapping( !CByteswap::IsMachineBigEndian() );
	buf.PutInt( g_pMatchExtensions->GetINetSupport()->GetEngineBuildNumber() );

	msg->WriteAsBinary( buf );

	// Special case when encoding binary data
	KeyValues *kvPtr = msg->FindKey( "binary/ptr" );
	KeyValues *kvSize = msg->FindKey( "binary/size" );
	if ( kvPtr && kvSize )
	{
		void *pvData = kvPtr->GetPtr();
		int nSize = kvSize->GetInt();

		if ( pvData && nSize )
		{
			buf.Put( pvData, nSize );
		}
	}

	if ( char const *szP2P = msg->GetString( "p2p", NULL ) )
	{
		Assert( !IsServiceSession() );

		// Determine P2P type
		EP2PSend eSendType = k_EP2PSendUnreliableNoDelay; // "nodelay"
		if ( !Q_stricmp( szP2P, "reliable" ) )
			eSendType = k_EP2PSendReliable;
		else if ( !Q_stricmp( szP2P, "buffer" ) )
			eSendType = k_EP2PSendReliableWithBuffering;
		else if ( !Q_stricmp( szP2P, "unreliable" ) )
			eSendType = k_EP2PSendUnreliable;

		// Transmit P2P message
// 		for ( int k = 0, kNum = steamapicontext->SteamMatchmaking()->GetNumLobbyMembers( m_lobby.m_uiLobbyID ); k < kNum; ++ k )
// 		{
// 			CSteamID idRemote = steamapicontext->SteamMatchmaking()->GetLobbyMemberByIndex( m_lobby.m_uiLobbyID, k );
// 			if ( idRemote.ConvertToUint64() != m_xuidMachineId )
// 				steamapicontext->SteamNetworking()->SendP2PPacket( idRemote, buf.Base(), buf.TellMaxPut(), eSendType, INetSupport::SP2PC_LOBBY );
// 		}
		for ( int k = 0, numMachines = m_pSettings->GetInt( "members/numMachines" ); k < numMachines; ++ k )
		{
			for ( int ic = 0, numPlayers = m_pSettings->GetInt( CFmtStr( "members/machine%d/numPlayers", k ) ); ic < numPlayers; ++ ic )
			{
				XUID xuidPlayer = m_pSettings->GetUint64( CFmtStr( "members/machine%d/player%d/xuid", k, ic ) );
				if ( xuidPlayer && xuidPlayer != m_xuidMachineId )
				{
					steamapicontext->SteamNetworking()->SendP2PPacket( xuidPlayer, buf.Base(), buf.TellMaxPut(), eSendType, INetSupport::SP2PC_LOBBY );
					m_bVoiceUsingSessionP2P = true;
				}
			}
		}

		// Receive on our own side
		ReceiveMessage( msg, true, m_xuidMachineId );
	}
	else if ( m_lobby.m_uiLobbyID )
	{
		steamapicontext->SteamMatchmaking()->SendLobbyChatMsg( m_lobby.m_uiLobbyID, buf.Base(), buf.TellMaxPut() );		
	}
	else
	{
		ReceiveMessage( msg, true, m_xuidMachineId );
	}
#endif
}

void CSysSessionBase::ReceiveMessage( KeyValues *msg, bool bValidatedLobbyMember, XUID xuidSrc )
{
	char const *szMsg = msg->GetName();

	if ( !Q_stricmp( "SysSession::Quit", szMsg ) )
	{
		XUID xuidMachine = msg->GetUint64( "id" );
		Assert( xuidMachine == xuidSrc );
		// assuming that xuidMachine and xuid of primary user are same
		OnPlayerLeave( xuidSrc );
	}
	else if ( !Q_stricmp( "SysSession::Command", szMsg ) )
	{
		KeyValues *pCommand = msg->GetFirstTrueSubKey();
		if ( !pCommand )
			return;

		char const *szRun = pCommand->GetString( "run", "" );
		bool bRun = false;

		if ( !Q_stricmp( szRun, "all" ) )
			bRun = true;
		else if ( !Q_stricmp( szRun, "host" ) && dynamic_cast< CSysSessionHost * >( this ) )
			bRun = true;
		else if ( !Q_stricmp( szRun, "clients" ) && dynamic_cast< CSysSessionClient * >( this ) )
			bRun = true;
		else if ( !Q_stricmp( szRun, "xuid" ) && m_xuidMachineId == pCommand->GetUint64( "runxuid" ) )
			bRun = true;

		if ( bRun )
		{
			KeyValues *kv = new KeyValues( "mmF->SysSessionCommand" );
			
			msg->RemoveSubKey( pCommand );
			kv->AddSubKey( pCommand );

			// We always set these field regardless of what was in command to indicate that it arrived from a remote machine
			bool bHostSrcXuid = ( xuidSrc == GetHostXuid() );
			pCommand->SetBool( "_remote_host", bHostSrcXuid );
			pCommand->SetUint64( "_remote_xuidsrc", xuidSrc );

			kv->SetBool( "host", bHostSrcXuid );
			kv->SetUint64( "xuidsrc", xuidSrc );
			kv->SetPtr( "syssession", this );

			OnSessionEvent( kv );
		}
	}
	else if ( !Q_stricmp( "SysSession::Voice", szMsg ) )
	{
		Voice_Playback( msg, xuidSrc );
	}
}

#if !defined( NO_STEAM )

void CSysSessionBase::UnpackAndReceiveMessage( const void *pvBuffer, int numBytes, bool bValidatedLobbyMember, XUID xuidSrc )
{
	if ( numBytes <= 0 )
		return;

	CUtlBuffer buf( pvBuffer, numBytes, CUtlBuffer::READ_ONLY );
	buf.ActivateByteSwapping( !CByteswap::IsMachineBigEndian() );
	if ( buf.GetInt() != g_pMatchExtensions->GetINetSupport()->GetEngineBuildNumber() )
		return;

	KeyValues *msg = new KeyValues( "" );
	KeyValues::AutoDelete autodelete( msg );
	if ( !msg->ReadAsBinary( buf ) )
		return;

	// Special case when decoding binary data
	static byte chBuffer2[ STEAM_LOBBY_CHAT_MSG_BUFFER_SIZE ];
	KeyValues *kvPtr = msg->FindKey( "binary/ptr" );
	KeyValues *kvSize = msg->FindKey( "binary/size" );
	if ( kvPtr && kvSize )
	{
		void *pvData = kvPtr->GetPtr();
		int nSize = kvSize->GetInt();

		if ( nSize < 0 || nSize >= STEAM_LOBBY_CHAT_MSG_BUFFER_SIZE )
			return;

		if ( pvData && nSize )
		{
			if ( !buf.Get( chBuffer2, nSize ) )
				return;

			kvPtr->SetPtr( NULL, chBuffer2 );
		}
	}

	ReceiveMessage( msg, bValidatedLobbyMember, xuidSrc );
}

// We should keep this global since client DLL writes a value here
volatile uint32 *g_hRankingSetupCallHandle = 0;

void CSysSessionBase::SetupSteamRankingConfiguration()
{
	KeyValues *kvNotification = new KeyValues( "SetupSteamRankingConfiguration" );
	kvNotification->SetPtr( "settingsptr", m_pSettings );
	kvNotification->SetPtr( "callhandleptr", ( void * ) &g_hRankingSetupCallHandle );

	SendEventsNotification( kvNotification );
}

bool CSysSessionBase::IsSteamRankingConfigured() const
{
	return !g_hRankingSetupCallHandle || !*g_hRankingSetupCallHandle;
}

void CSysSessionBase::Steam_OnLobbyChatMsg( LobbyChatMsg_t *pLobbyChatMsg )
{
	if ( pLobbyChatMsg->m_ulSteamIDLobby != m_lobby.m_uiLobbyID )
		return;

	static byte chBuffer[ STEAM_LOBBY_CHAT_MSG_BUFFER_SIZE ];
	
	CSteamID steamIDSender;
	EChatEntryType ecet;

	int numBytes = steamapicontext->SteamMatchmaking()->GetLobbyChatEntry(
		m_lobby.m_uiLobbyID, pLobbyChatMsg->m_iChatID,
		&steamIDSender,
		chBuffer, sizeof( chBuffer ),
		&ecet );

	// Is this a validated lobby member?
	bool bValidatedLobbyMember = ( SessionMembersFindPlayer( m_pSettings, steamIDSender.ConvertToUint64() ) != NULL );

	UnpackAndReceiveMessage( chBuffer, numBytes, bValidatedLobbyMember, steamIDSender.ConvertToUint64() );
}

void CSysSessionBase::Steam_OnLobbyChatUpdate( LobbyChatUpdate_t *pLobbyChatUpdate )
{
	if ( pLobbyChatUpdate->m_ulSteamIDLobby != m_lobby.m_uiLobbyID )
		return;

	if ( BChatMemberStateChangeRemoved( pLobbyChatUpdate->m_rgfChatMemberStateChange ) )
	{
		XUID xuidLocal = g_pPlayerManager->GetLocalPlayer( XBX_GetPrimaryUserId() )->GetXUID();
		if ( pLobbyChatUpdate->m_ulSteamIDUserChanged == xuidLocal )
		{
			if ( pLobbyChatUpdate->m_ulSteamIDMakingChange != xuidLocal )
			{
				// Prepare the update notification
				KeyValues *kv = new KeyValues( "mmF->SysSessionUpdate" );
				kv->SetPtr( "syssession", this );
				kv->SetString( "error", "kicked" );

				SendEventsNotification( kv );
			}
		}
		else
		{
			OnPlayerLeave( pLobbyChatUpdate->m_ulSteamIDUserChanged );
		}
	}
}

void CSysSessionBase::Steam_OnP2PSessionRequest( P2PSessionRequest_t *pParam )
{
	uint64 idRemote = pParam->m_steamIDRemote.ConvertToUint64();
	if ( m_lobby.m_uiLobbyID && g_pMatchExtensions->GetIVEngineClient() &&
		( !g_pMatchExtensions->GetIVEngineClient()->IsConnected() || g_pMatchExtensions->GetIVEngineClient()->IsClientLocalToActiveServer() ) &&
		 SessionMembersFindPlayer( m_pSettings, idRemote ) &&
		 ( !IsPS3() || ( m_lobby.m_eLobbyState == CSteamLobbyObject::STATE_DEFAULT ) ) )
	{
		// We are in the lobby together, accept P2P session request
		steamapicontext->SteamNetworking()->AcceptP2PSessionWithUser( idRemote );
		m_bVoiceUsingSessionP2P = true;
	}
}

void CSysSessionBase::Steam_OnServersConnected( SteamServersConnected_t *pParam )
{
}

void CSysSessionBase::Steam_OnServersDisconnected( SteamServersDisconnected_t *pParam )
{
	// In case we are not in active gameplay we should just drop
	// back to main menu while Steam is disconnected
	if ( m_lobby.m_eLobbyState == CSteamLobbyObject::STATE_DEFAULT )
	{
		// Prepare the update notification
		KeyValues *kv = new KeyValues( "mmF->SysSessionUpdate" );
		kv->SetPtr( "syssession", this );
		kv->SetString( "error", "SteamServersDisconnected" );

		SendEventsNotification( kv );
		return;
	}
	
	// If we already got disconnected once prevent re-entry
	if ( m_lobby.m_eLobbyState == CSteamLobbyObject::STATE_DISCONNECTED_FROM_STEAM )
		return;
	m_lobby.m_eLobbyState = CSteamLobbyObject::STATE_DISCONNECTED_FROM_STEAM;

	// Otherwise we should manually leave the lobby
	steamapicontext->SteamMatchmaking()->LeaveLobby( m_lobby.m_uiLobbyID );
	m_lobby.m_uiLobbyID = 0ull;
	
	// set the session lock
	m_pSettings->SetString( "system/lock", "SteamServersDisconnected" );

	// Check if we can remove all remote players from the session
	if ( !V_stricmp( m_pSettings->GetString( "system/netflag" ), "noleave" ) )
	{
		DevMsg( "CSysSessionBase::Steam_OnServersDisconnected keeping players in noleave mode.\n" );
		return;
	}

	//
	// Remove all the remote players from the session
	//
	XUID xuidLocal = g_pPlayerManager->GetLocalPlayer( XBX_GetPrimaryUserId() )->GetXUID();
	KeyValues *pMembers = m_pSettings->FindKey( "members" );
	Assert( pMembers );
	if ( !pMembers )
		return;

	int numMachines = pMembers->GetInt( "numMachines", 0 );
	for ( int k = 0; k < numMachines; ++ k )
	{
		KeyValues *kvMachine = pMembers->FindKey( CFmtStr( "machine%d", k ) );
		if ( !kvMachine )
			continue;
		XUID idMachine = kvMachine->GetUint64( "id" );
		if ( idMachine == xuidLocal )
		{
			kvMachine->SetName( "machine0" );
			
			pMembers->SetInt( "numPlayers", kvMachine->GetInt( "numPlayers", 1 ) );
			pMembers->SetInt( "numSlots", kvMachine->GetInt( "numPlayers", 1 ) );
		}
		else
		{
			pMembers->RemoveSubKey( kvMachine );
			kvMachine->deleteThis();
		}
	}
	pMembers->SetInt( "numMachines", 1 );
}

char const * CSysSessionBase::LobbyEnterErrorAsString( LobbyEnter_t *pLobbyEnter )
{
	switch ( pLobbyEnter->m_EChatRoomEnterResponse )
	{
	case k_EChatRoomEnterResponseFull:
		return "full";

	case k_EChatRoomEnterResponseBanned:
	case k_EChatRoomEnterResponseNotAllowed:
		return "notwanted";
	case k_EChatRoomEnterResponseMemberBlockedYou:
		return "blockedyou";
	case k_EChatRoomEnterResponseYouBlockedMember:
		return "youblocked";

	case k_EChatRoomEnterResponseDoesntExist:
		return "doesntexist";

	case k_EChatRoomEnterResponseRatelimitExceeded:
		return "ratelimit";

	default:
		return "create";
	}
}

void CSysSessionBase::LobbySetDataFromKeyValues( char const *szPath, KeyValues *key, bool bRecurse )
{
	if ( !key || !szPath )
		return;

	char chKey[ 256 ];
	char chValue[ 256 ];

	if ( key->GetDataType() != KeyValues::TYPE_NONE )
	{
		PrintValue( key, chValue, ARRAYSIZE( chValue ) );
		DevMsg( "LobbySetData: '%s' = '%s'\n", szPath, chValue );
		steamapicontext->SteamMatchmaking()->SetLobbyData( m_lobby.m_uiLobbyID, szPath, chValue );
	}
	else for ( KeyValues *sub = key->GetFirstSubKey(); sub; sub = sub->GetNextKey() )
	{
		if ( !bRecurse && sub->GetDataType() == KeyValues::TYPE_NONE )
			continue;

		Q_snprintf( chKey, ARRAYSIZE( chKey ), "%s:%s", szPath, sub->GetName() );
		if ( Q_stricmp( chKey, "System:dependentlobby" ) == 0 )
		{
			// set magic dependent lobby something
			steamapicontext->SteamMatchmaking()->SetLinkedLobby( m_lobby.m_uiLobbyID, sub->GetUint64() );
		}
		else
		LobbySetDataFromKeyValues( chKey, sub, bRecurse );
	}

	// Expose lobby members too because Steam doesn't do it for us (lobby owner is first)
	if ( !V_stricmp( szPath, "members" ) )
	{
		CUtlVector< AccountID_t > arrAccounts;
		int numMachines = key->GetInt( "numMachines", 0 );
		arrAccounts.EnsureCapacity( numMachines + 1 );
		arrAccounts.AddToTail( CSteamID( GetHostXuid() ).GetAccountID() );
		for ( int k = 0; k < numMachines; ++k )
		{
			KeyValues *kvMachine = m_pSettings->FindKey( CFmtStr( "members/machine%d", k ) );
			if ( kvMachine )
			{
				uint64 ullMachineID = kvMachine->GetUint64( "id" );
				if ( AccountID_t unAccountID = CSteamID( ullMachineID ).GetAccountID() )
				{
					if ( unAccountID != arrAccounts.Head() )
						arrAccounts.AddToTail( unAccountID );
				}
			}
		}

		// We are going to write out these bytes as "varints" encoding,
		// which also ensures that they can fit in lobby metadata as no
		// single byte will be 0x00
		const int numBytesStackAlloc = ( 1 + arrAccounts.Count() ) * 5; // 5 bytes max per varint
		uint8 * const packedData = ( uint8 * ) stackalloc( numBytesStackAlloc );
		uint8 *pWrite = packedData;
		FOR_EACH_VEC( arrAccounts, k )
		{
			uint32 data = arrAccounts[k];
			while ( data > 0x7F )
			{
				*( pWrite ++ ) = uint8( ( data & 0x7F ) | 0x80 );
				data >>= 7;
			}
			Assert( data & 0x7F );
			*( pWrite ++ ) = uint8( data & 0x7F );
		}
		*( pWrite ) = 0; // null-terminator for the "string"
		steamapicontext->SteamMatchmaking()->SetLobbyData( m_lobby.m_uiLobbyID, "uids", ( char * ) packedData );
	}
}

#endif

void CSysSessionBase::Voice_ProcessTalkers( KeyValues *pMachine, bool bAdd )
{
	if ( IsServiceSession() )
		return;

	if ( !pMachine )	// Process all members as talkers
	{
		int numMachines = m_pSettings->GetInt( "members/numMachines", 0 );
		for ( int k = 0; k < numMachines; ++ k )
		{
			KeyValues *kvMachine = m_pSettings->FindKey( CFmtStr( "members/machine%d", k ) );
			if ( kvMachine )
				Voice_ProcessTalkers( kvMachine, bAdd );
		}

		if ( bAdd && m_Voice_flLastHeadsetStatusCheck < 0 )
			m_Voice_flLastHeadsetStatusCheck = 0;

		return;
	}

	XUID xuidMachine = pMachine->GetUint64( "id", 0ull );
	if ( !xuidMachine )
		return;

	int numPlayers = pMachine->GetInt( "numPlayers", 0 );
	uint64 uiMachineFlags = pMachine->GetUint64( "flags" );
	for ( int k = 0; k < numPlayers; ++ k )
	{
		XUID xuid = pMachine->GetUint64( CFmtStr( "player%d/xuid", k ), 0ull );
		if ( !xuid )
			continue;
		
		int iCtrlr = -1;
		if ( xuidMachine == m_xuidMachineId )
		{
			iCtrlr = XBX_GetUserId( k );
			xuid = 0ull; // for local users we use only controller index
		}

		if ( IEngineVoice *pIEngineVoice = g_pMatchExtensions->GetIEngineVoice() )
		{
			if ( bAdd )
				pIEngineVoice->AddPlayerToVoiceList( xuid, iCtrlr, (uiMachineFlags & MACHINE_PLATFORM_PS3) ? ENGINE_VOICE_FLAG_PS3 : 0 );
			else
			{
				pIEngineVoice->RemovePlayerFromVoiceList( xuid, iCtrlr );
#if !defined( NO_STEAM )
				// When removing from voice list tear down P2P session
				steamapicontext->SteamNetworking()->CloseP2PSessionWithUser( xuid );
#endif
			}
		}
	}
}

void CSysSessionBase::Voice_CaptureAndTransmitLocalVoiceData()
{
	if ( IsServiceSession() )
		return;

	IEngineVoice *v = g_pMatchExtensions->GetIEngineVoice();
	if ( !v )
		return;

	if ( g_pMatchFramework->GetMatchTitle()->GetTitleSettingsFlags() & MATCHTITLE_VOICE_INGAME )
	{
#if !defined( NO_STEAM )
		if ( m_lobby.m_eLobbyState != m_lobby.STATE_DEFAULT )
		{
			if ( IsPS3() && m_bVoiceUsingSessionP2P )
			{
				m_bVoiceUsingSessionP2P = false;
				// As soon as we start loading into a game we should shutdown all P2P communication
				for ( int k = 0, numMachines = m_pSettings->GetInt( "members/numMachines" ); k < numMachines; ++ k )
				{
					for ( int ic = 0, numPlayers = m_pSettings->GetInt( CFmtStr( "members/machine%d/numPlayers", k ) ); ic < numPlayers; ++ ic )
					{
						XUID xuidPlayer = m_pSettings->GetUint64( CFmtStr( "members/machine%d/player%d/xuid", k, ic ) );
						if ( xuidPlayer && xuidPlayer != m_xuidMachineId )
						{
							steamapicontext->SteamNetworking()->CloseP2PSessionWithUser( xuidPlayer );
							DevMsg( "PS3 Session has shut down P2P session with %llx!\n", xuidPlayer );
						}
					}
				}
			}
			return;
		}
#endif
	}

	for ( DWORD k = 0; k < XBX_GetNumGameUsers(); ++ k )
	{
		int iCtrlr = XBX_GetPrimaryUserId();

		if ( v->VoiceUpdateData( iCtrlr ) )
		{
			// Capture the voice data buffers
			const byte *pbVoiceData = NULL;
			unsigned int numBytes = 0;
			v->GetVoiceData( iCtrlr, &pbVoiceData, &numBytes );

			if ( mm_session_voice_loading.GetBool() || !(
				g_pMatchExtensions->GetIVEngineClient()->IsDrawingLoadingImage() ||
				g_pMatchExtensions->GetIVEngineClient()->IsTransitioningToLoad() ) )
			{
				// Package it up as a message
				KeyValues *msg = new KeyValues( "SysSession::Voice" );
				KeyValues::AutoDelete autodelete_msg( msg );
				
				msg->SetString( "p2p", "nodelay" );	// marks the message as preferring p2p transfer
				msg->SetUint64( "xuid", g_pPlayerManager->GetLocalPlayer( iCtrlr )->GetXUID() );

				unsigned int numBytesRemaining = numBytes, numBytesOffset = 0;
				while ( numBytesRemaining > 0 )
				{
					numBytes = MIN( 1024, numBytesRemaining );
					msg->SetPtr( "binary/ptr", numBytesOffset + const_cast< byte * >( pbVoiceData ) );
					msg->SetInt( "binary/size", numBytes );

					numBytesRemaining -= numBytes;
					numBytesOffset += numBytes;


					// Deliver the message to peers
					SendMessage( msg );
				}
			}

			// Reset voice buffers
			v->VoiceResetLocalData( iCtrlr );
		}
	}
}

void CSysSessionBase::Voice_Playback( KeyValues *msg, XUID xuidSrc )
{
	XUID xuid = msg->GetUint64( "xuid", 0ull );
	Assert( xuid == xuidSrc );
	if ( xuid != xuidSrc )
		return;

	const byte *pbVoiceData = ( const byte * ) msg->GetPtr( "binary/ptr" );
	unsigned int numBytes = msg->GetInt( "binary/size" );

	if ( !pbVoiceData || !numBytes )
		 // We cannot playback the data or muting the player
		 return;

	if ( mm_session_voice_loading.GetBool() || !(
		g_pMatchExtensions->GetIVEngineClient()->IsDrawingLoadingImage() ||
		g_pMatchExtensions->GetIVEngineClient()->IsTransitioningToLoad() ) )
	{
		g_pMatchExtensions->GetIEngineVoice()->PlayIncomingVoiceData( xuid, pbVoiceData, numBytes );

		if ( g_pMatchFramework->GetMatchSystem()->GetMatchVoice()->CanPlaybackTalker( xuid ) )
		{
			if ( KeyValues *notify = new KeyValues( "OnPlayerActivity", "act", "voice" ) )
			{
				notify->SetUint64( "xuid", xuid );
				OnSessionEvent( notify );
			}
		}
	}
}

void CSysSessionBase::Voice_UpdateLocalHeadsetsStatus()
{
	if ( IsServiceSession() )
		return;

	if ( m_Voice_flLastHeadsetStatusCheck < 0 )
		return;

	// Not too frequently check for the headset status changes
	if ( Plat_FloatTime() - m_Voice_flLastHeadsetStatusCheck < 1.0f )
		return;
	m_Voice_flLastHeadsetStatusCheck = Plat_FloatTime();

	// Find the local machine
	KeyValues *pMachine = NULL;
	SessionMembersFindPlayer( m_pSettings, m_xuidMachineId, &pMachine );
	if ( !pMachine )
		return;

	// Whether current status is different from session settings
	for ( uint k = 0; k < XBX_GetNumGameUsers(); ++ k )
	{
		bool bHeadset = false;

#if !defined( NO_STEAM )
		bHeadset = g_pMatchExtensions->GetIEngineVoice()->IsHeadsetPresent( XBX_GetPrimaryUserId() );
#endif
		
		char const *szCurValue = pMachine->GetString( CFmtStr( "player%d/voice", k ), "" );
		char const *szHeadsetValue = bHeadset ? "headset" : "";
		
		if ( szCurValue[0] != szHeadsetValue[0] )
		{
			KeyValues *msg = new KeyValues( "SysSession::VoiceStatus" );
			KeyValues::AutoDelete autodelete_msg( msg );

			XUID xuid = pMachine->GetUint64( CFmtStr( "player%d/xuid", k ), 0ull );
			msg->SetUint64( "xuid", xuid );
			msg->SetString( "voice", szHeadsetValue );

			SendMessage( msg );
		}
	}
}

void CSysSessionBase::Voice_UpdateMutelist()
{
	if ( IsServiceSession() )
		return;

	// Generate the mutelist and send it if it is different from the current settings
	KeyValues *msg = new KeyValues( "SysSession::VoiceMutelist" );
	KeyValues::AutoDelete autodelete_msg( msg );
	
	msg->SetUint64( "xuid", m_xuidMachineId );

	if ( KeyValues *pMembers = m_pSettings ? m_pSettings->FindKey( "members" ) : NULL )
	{
		int numMachines = pMembers->GetInt( "numMachines" );
		for ( int i = 0; i < numMachines; ++ i )
		{
			XUID xuid = pMembers->GetUint64( CFmtStr( "machine%d/id", i ) );
			if ( g_pMatchVoice->IsMachineMuted( xuid ) )
				msg->SetUint64( CFmtStr( "Mutelist/%d", i ), xuid );
		}
	}

	// Find current mutelist
	KeyValues *pLocalMachine = NULL;
	SessionMembersFindPlayer( m_pSettings, m_xuidMachineId, &pLocalMachine );
	if ( pLocalMachine )
	{
		KeyValues *pCurMutelist = pLocalMachine->FindKey( "Mutelist" );
		KeyValues *pNewMutelist = msg->FindKey( "Mutelist" );

		if ( pCurMutelist && pNewMutelist )
		{
			for ( pCurMutelist = pCurMutelist->GetFirstValue(),
				  pNewMutelist = pNewMutelist->GetFirstValue();
				  pCurMutelist && pNewMutelist;
				  pCurMutelist = pCurMutelist->GetNextValue(),
				  pNewMutelist = pNewMutelist->GetNextValue() )
			{
				if ( pCurMutelist->GetUint64() != pNewMutelist->GetUint64() )
					break;
			}
		}
		if ( !pCurMutelist && !pNewMutelist )
			// current and new mutelists compared equal
			return;
	}

	SendMessage( msg );
}


void CSysSessionBase::OnPlayerLeave( XUID xuid )
{
}

bool CSysSessionBase::FindAndRemovePlayerFromMembers( XUID xuid )
{
	KeyValues *pMembers = m_pSettings->FindKey( "members" );
	Assert( pMembers );
	if ( !pMembers )
		return false;

	int numMachines = pMembers->GetInt( "numMachines", 0 );
	int numPlayers = pMembers->GetInt( "numPlayers", 0 );

	for ( int k = 0; k < numMachines; ++ k )
	{
		KeyValues *pMachine = pMembers->FindKey( CFmtStr( "machine%d", k ) );
		if ( !pMachine )
			continue;

		int numOtherPlayers = pMachine->GetInt( "numPlayers", 0 );
		for ( int j = 0; j < numOtherPlayers; ++ j )
		{
			XUID xuidOtherPlayer = pMachine->GetUint64( CFmtStr( "player%d/xuid", j ), 0ull );
			if ( xuidOtherPlayer == xuid )
			{

				// We also need to remove talkers
				Voice_ProcessTalkers( pMachine, false );

				// The entire machine will be effectively disconnected
				KeyValues::AutoDelete autodelete( pMachine );
				pMembers->RemoveSubKey( pMachine );

				for ( int kk = k + 1; kk < numMachines; ++ kk )
				{
					KeyValues *pNextMachine = pMembers->FindKey( CFmtStr( "machine%d", kk ) );
					if ( !pNextMachine )
						continue;

					pNextMachine->SetName( CFmtStr( "machine%d", kk - 1 ) );
				}

				// Update counts
				numMachines = MAX( 0, numMachines - 1 );
				pMembers->SetInt( "numMachines", numMachines );

				numPlayers = MAX( 0, numPlayers - numOtherPlayers );
				pMembers->SetInt( "numPlayers", numPlayers );

				// Now fire the events for the removed players
				for ( j = 0; j < numOtherPlayers; ++ j )
				{
					KeyValues *pPlayer = pMachine->FindKey( CFmtStr( "player%d", j ) );
					xuidOtherPlayer = pPlayer->GetUint64( "xuid", 0ull );

					KeyValues *kv = new KeyValues( "OnPlayerRemoved" );
					kv->SetUint64( "xuid", xuidOtherPlayer );
					if ( pPlayer )
					{
						KeyValues *pCopyPlayer = pPlayer->MakeCopy();
						pCopyPlayer->SetName( "player" );
						kv->AddSubKey( pCopyPlayer );
					}
					if ( pMachine )
					{
						KeyValues *pCopyMachine = pMachine->MakeCopy();
						pCopyMachine->SetName( "machine" );
						kv->AddSubKey( pCopyMachine );
					}
					OnSessionEvent( kv );
				}

#if !defined( NO_STEAM )
				if ( dynamic_cast< CSysSessionHost * >( this ) )
				{
					// Update members information
					LobbySetDataFromKeyValues( "members", m_pSettings->FindKey( "Members" ), false );
				}
#endif
				
				return true;
			}
		}
	}

	return false;
}

void CSysSessionBase::UpdateSessionProperties( KeyValues *kv, bool bHost )
{
#if !defined( NO_STEAM )
	SetupSteamRankingConfiguration();
#endif

	if ( !IsX360() && !bHost )
		// On X360 every client must set their contexts, on PC it's
		// all Steam-server-side and only host sets the metadata
		return;

	if ( IsX360() )
		return;

#if !defined( NO_STEAM )
	if ( !m_lobby.m_uiLobbyID )
		return;

	if ( kv )
	{
		LobbySetDataFromKeyValues( "system", kv->FindKey( "system" ) );
		LobbySetDataFromKeyValues( "game", kv->FindKey( "game" ) );
		LobbySetDataFromKeyValues( "options", kv->FindKey( "options" ) );
	}
#endif
}

void CSysSessionBase::SetSessionActiveGameplayState( bool bActive, char const *szSecureServerAddress )
{
#if !defined( NO_STEAM )
	switch ( m_lobby.m_eLobbyState )
	{
	case CSteamLobbyObject::STATE_DEFAULT:
		if ( bActive )
		{
			m_lobby.m_eLobbyState = CSteamLobbyObject::STATE_ACTIVE_GAME;
		}
		break;
	
	case CSteamLobbyObject::STATE_ACTIVE_GAME:
		if ( !bActive )
		{
			m_lobby.m_eLobbyState = CSteamLobbyObject::STATE_DEFAULT;
		}
		break;
	
	case CSteamLobbyObject::STATE_DISCONNECTED_FROM_STEAM:
		if ( !bActive )
		{
			// If active gameplay session has ended and we were disconnected from Steam
			// then go back to main menu
			m_lobby.m_eLobbyState = CSteamLobbyObject::STATE_DEFAULT;
			Steam_OnServersDisconnected( NULL );
		}
		break;
	}
#endif
}


void CSysSessionBase::UpdateTeamProperties( KeyValues *pTeamProperties )
{
#if !defined(NO_STEAM)

	if ( !m_lobby.m_uiLobbyID )
		return;

	LobbySetDataFromKeyValues( "members", pTeamProperties->FindKey( "members" ) );

#endif
}

void CSysSessionBase::UpdateServerInfo( KeyValues *pServerKey )
{
#if !defined(NO_STEAM)

	LobbySetDataFromKeyValues( "server", pServerKey->FindKey( "server" ) );

#endif
}

void CSysSessionBase::PrintValue( KeyValues *val, char *chBuffer, int numBytesBuffer )
{
	switch( val->GetDataType() )
	{
	case KeyValues::TYPE_INT:
		Q_snprintf( chBuffer, numBytesBuffer, "%d", val->GetInt() );
		break;
	case KeyValues::TYPE_UINT64:
		Q_snprintf( chBuffer, numBytesBuffer, "%llX", val->GetUint64() );
		break;
	case KeyValues::TYPE_FLOAT:
		Q_snprintf( chBuffer, numBytesBuffer, "%f", val->GetFloat() );
		break;
	case KeyValues::TYPE_STRING:
		Q_snprintf( chBuffer, numBytesBuffer, "%s", val->GetString() );
		break;
	default:
		Warning( "Unknown type in CSysSessionHost::PrintValue ( %s )\n", val->GetName() );
		break;
	}
}







CSysSessionHost::CSysSessionHost( KeyValues *pSettings ) :
	CSysSessionBase( pSettings ),
#if !defined( NO_STEAM )
	m_dblDormantMembersCheckTime( Plat_FloatTime() ),
	m_numDormantMembersDetected( 0 ),
#endif
	m_eState( STATE_INIT ),
	m_flTimeOperationStarted( 0.0f ),
	m_flInitializeTimestamp( 0.0f ),
	m_teamResKey( 0ull ),
	m_numRemainingTeamPlayers( 0 ),
	m_flTeamResStartTime( 0.0f ),
	m_ullCrypt( 0 )
{
}

CSysSessionHost::CSysSessionHost( CSysSessionClient *pClient, KeyValues *pSettings ) :
	CSysSessionBase( pSettings ),
#if !defined( NO_STEAM )
	m_dblDormantMembersCheckTime( Plat_FloatTime() ),
	m_numDormantMembersDetected( 0 ),
#endif
	m_eState( STATE_IDLE ),
	m_flTimeOperationStarted( 0.0f ),
	m_flInitializeTimestamp( 0.0f ),
	m_teamResKey( 0ull ),
	m_numRemainingTeamPlayers( 0 ),
	m_flTeamResStartTime( 0.0f ),
	m_ullCrypt( 0 )
{
	m_lobby = pClient->m_lobby;
	
	m_Voice_flLastHeadsetStatusCheck = pClient->m_Voice_flLastHeadsetStatusCheck;

#if !defined( NO_STEAM )
	// Install callback for messages
	m_CallbackOnLobbyChatMsg.Register( this, &CSysSessionBase::Steam_OnLobbyChatMsg );
	m_CallbackOnLobbyChatUpdate.Register( this, &CSysSessionBase::Steam_OnLobbyChatUpdate );

	// Set the migrated members information
	LobbySetDataFromKeyValues( "members", m_pSettings->FindKey( "members" ), false );
#endif

	// Send a notification
	KeyValues *kvEvent = new KeyValues( "OnPlayerLeaderChanged" );
	kvEvent->SetString( "state", "host" );
	kvEvent->SetUint64( "xuid", m_xuidMachineId );
	SendEventsNotification( kvEvent );
}


CSysSessionHost::~CSysSessionHost()
{
	;
}

bool CSysSessionHost::Update()
{
	if ( !CSysSessionBase::Update() )
		return false;

	switch( m_eState )
	{
	case STATE_INIT:
		if ( !m_flInitializeTimestamp )
		{
			m_flInitializeTimestamp = Plat_FloatTime();
#if !defined( NO_STEAM )
			SetupSteamRankingConfiguration();
#endif
		}
		if (
#if !defined( NO_STEAM )
			( IsSteamRankingConfigured() || ( Plat_FloatTime() >= m_flInitializeTimestamp + mm_session_sys_ranking_timeout.GetFloat() ) ) &&
#endif
			( Plat_FloatTime() >= m_flInitializeTimestamp + mm_session_sys_delay_create_host.GetFloat() ) &&
			SysSession_AllowCreate()
			)
		{
			UpdateStateInit();
		}
		break;

#if !defined( NO_STEAM )
	case STATE_IDLE:
		// Track players who are in the MMS session object, but haven't made KV request to join or failed KV request and didn't drop
		if ( m_lobby.m_uiLobbyID )
		{
			double dblTimeNow = Plat_FloatTime();
			if ( dblTimeNow - m_dblDormantMembersCheckTime >= 15.0 ) // check every 15 seconds
			{
				m_dblDormantMembersCheckTime = dblTimeNow;
				int numMembers = steamapicontext->SteamMatchmaking()->GetNumLobbyMembers( m_lobby.m_uiLobbyID );
				int numDormantMembers = 0;
				int nLimit = steamapicontext->SteamMatchmaking()->GetLobbyMemberLimit( m_lobby.m_uiLobbyID );
				for ( int k = 0; nLimit && ( k < numMembers ); ++k )
				{
					XUID xuidMember = steamapicontext->SteamMatchmaking()->GetLobbyMemberByIndex( m_lobby.m_uiLobbyID, k ).ConvertToUint64();
					if ( !xuidMember )
						continue;

					// Check if this player is in the KV session
					KeyValues *kvPlayer = SessionMembersFindPlayer( m_pSettings, xuidMember );
					if ( kvPlayer )
						continue; // member is properly authenticated, this is the common case

					++ numDormantMembers;
				}
				m_numDormantMembersDetected = numDormantMembers;
				int nNewLimit = m_numDormantMembersDetected + m_pSettings->GetInt( "members/numSlots", 1 );
				if ( nLimit && nNewLimit != nLimit )
				{
					steamapicontext->SteamMatchmaking()->SetLobbyMemberLimit( m_lobby.m_uiLobbyID, nNewLimit );
				}
			}
		}
		break;
#endif

	}

	// Update reservation status
	if ( m_teamResKey )
	{
		float time = Plat_FloatTime();
		if ( time >= m_flTeamResStartTime + mm_session_team_res_timeout.GetFloat() )
		{
			UnreserveTeamSession();
		}
	}

	return true;
}

void CSysSessionHost::Destroy()
{
	// If we are migrating, then don't let the
	// base class handle it because it will
	// post Quit messages and leave the lobby...
	if ( m_eState == STATE_MIGRATE )
	{
		delete this;
		return;
	}

	// Chain to base which will "delete this"
	CSysSessionBase::Destroy();
}

void CSysSessionHost::DebugPrint()
{
	DevMsg( "CSysSessionHost [ state=%d ]\n", m_eState );
	CSysSessionBase::DebugPrint();
}

XUID CSysSessionHost::GetHostXuid( XUID xuidValidResult )
{
#if !defined( NO_STEAM )
	return m_lobby.m_uiLobbyID ? steamapicontext->SteamMatchmaking()
		->GetLobbyOwner( m_lobby.m_uiLobbyID ).ConvertToUint64() : m_xuidMachineId;
#endif
	return 0ull;
}

void CSysSessionHost::KickPlayer( KeyValues *pCommand )
{
	XUID xuid = pCommand->GetUint64( "xuid", 0ull );

	// Locate the machine being kicked
	KeyValues *pMachine = NULL;
	SessionMembersFindPlayer( m_pSettings, xuid, &pMachine );
	if ( !pMachine )
		return;

	// Use machine primary xuid
	xuid = pMachine->GetUint64( "id" );
	if ( xuid == GetHostXuid() )
	{
		DevWarning( "CSysSessionHost::Kick unsupported for host xuid!\n" );
		return;
	}

	// Notify everybody that a machine is being kicked
	KeyValues *notify = new KeyValues( "SysSession::OnPlayerKicked" );
	KeyValues::AutoDelete autodelete_notify( notify );

	notify->SetUint64( "xuid", xuid );

	SendMessage( notify );

	// Remove player information from our records
	FindAndRemovePlayerFromMembers( xuid );

	// Remember the player in our kicked players map
	m_mapKickedPlayers.InsertOrReplace( xuid, Plat_FloatTime() );

#if !defined( NO_STEAM )
	// Forcefully kick the player from the lobby
	// steamapicontext->SteamMatchmaking()->???
	// On X360 we just forcefully shutdown that client's
	// network channel and all peers do the same, so the kicked
	// client is indeed kicked no matter how smartly he tries to stay.
#endif
}

void CSysSessionHost::OnUpdateSessionSettings( KeyValues *kv )
{
	KeyValues *kvPropertiesUpdated = kv->FindKey( "update", false );
	if ( !kvPropertiesUpdated )
		kvPropertiesUpdated = kv->FindKey( "delete", false );
	UpdateSessionProperties( kvPropertiesUpdated );

	// Now send the message to everybody about the session update
	KeyValues *notify = new KeyValues( "SysSession::OnUpdate" );
	KeyValues::AutoDelete autodelete_notify( notify );
	
	if ( KeyValues *kvUpdate = kv->FindKey( "update" ) )
		notify->AddSubKey( kvUpdate->MakeCopy() );
	if ( KeyValues *kvDelete = kv->FindKey( "delete" ) )
		notify->AddSubKey( kvDelete->MakeCopy() );

	SendMessage( notify );
}

void CSysSessionHost::OnPlayerUpdated( KeyValues *pPlayer )
{
	// Notify the framework about player updated
	KeyValues *kvEvent = new KeyValues( "OnPlayerUpdated" );
	kvEvent->SetUint64( "xuid", pPlayer->GetUint64( "xuid" ) );
	kvEvent->SetPtr( "player", pPlayer );
	OnSessionEvent( kvEvent );

	// Now send the message
	KeyValues *notify = new KeyValues( "SysSession::OnPlayerUpdated" );
	KeyValues::AutoDelete autodelete( notify );
	notify->MergeFrom( pPlayer, KeyValues::MERGE_KV_UPDATE );

	SendMessage( notify );
}

void CSysSessionHost::OnMachineUpdated( KeyValues *pMachine )
{
	// Send the message to peers
	KeyValues *notify = new KeyValues( "SysSession::OnMachineUpdated" );
	KeyValues::AutoDelete autodelete( notify );
	notify->MergeFrom( pMachine, KeyValues::MERGE_KV_UPDATE );

	SendMessage( notify );
}

void CSysSessionHost::UpdateStateInit()
{
#if !defined( NO_STEAM )
	ELobbyType eType = k_ELobbyTypeFriendsOnly;
	int numSlots = m_pSettings->GetInt( "members/numSlots", 1 );

	SteamAPICall_t hCall = steamapicontext->SteamMatchmaking()->CreateLobby( eType, numSlots );
	m_CallbackOnLobbyCreated.Set( hCall, this, &CSysSessionHost::Steam_OnLobbyCreated );
#endif

	m_eState = STATE_CREATING;
}

void CSysSessionHost::ReceiveMessage( KeyValues *msg, bool bValidatedLobbyMember, XUID xuidSrc )
{
	char const *szMsg = msg->GetName();

	// Parse the message depending on our state
	switch ( m_eState )
	{
	case STATE_IDLE:
		if ( !Q_stricmp( "SysSession::RequestJoinData", szMsg ) )
		{
			XUID machineid = msg->GetUint64( "id" );
			KeyValues *pSettings = msg->FindKey( "settings" );

			if ( machineid != xuidSrc )
				return;

			Process_RequestJoinData( xuidSrc, pSettings );
		}
		else if ( !bValidatedLobbyMember )
		{
			return;
		}
		else if ( !Q_stricmp( "SysSession::VoiceStatus", szMsg ) )
		{
			Process_VoiceStatus( msg, xuidSrc );
		}
		else
		{
			CSysSessionBase::ReceiveMessage( msg, bValidatedLobbyMember, xuidSrc );
		}
		return;
	
	}
}

void CSysSessionHost::Migrate( KeyValues *pCommand )
{
	if ( Q_stricmp( pCommand->GetString( "migrate" ), "host>client" ) )
	{
		Assert( 0 );
		return;
	}

#ifndef NO_STEAM
	Verify( steamapicontext->SteamMatchmaking()->SetLobbyOwner( m_lobby.m_uiLobbyID, pCommand->GetUint64( "xuid" ) ) );
	
	// Prepare the update notification
	KeyValues *kv = new KeyValues( "mmF->SysSessionUpdate" );
	kv->SetPtr( "syssession", this );
	m_eState = STATE_MIGRATE;
	kv->SetString( "action", "client" );
	// Inside this event broadcast our session will be deleted
	SendEventsNotification( kv );
#endif
}

void CSysSessionHost::OnPlayerLeave( XUID xuid )
{
	// We detected that the player dropped out of the lobby,
	// disconnect the player from the session

#if !defined( NO_STEAM )
	if ( !V_stricmp( m_pSettings->GetString( "system/netflag" ), "noleave" ) )
	{
		DevMsg( "CSysSessionHost::OnPlayerLeave(%llx) ignored in noleave mode.\n", xuid );
		return;
	}
#endif

	if ( FindAndRemovePlayerFromMembers( xuid ) )
	{
		// Now send the message to everybody about the session update
		KeyValues *reply = new KeyValues( "SysSession::OnPlayerRemoved" );
		KeyValues::AutoDelete autodelete_reply( reply );
		reply->SetUint64( "xuid", xuid );
		SendMessage( reply );

		// Fire the notification about a machine disconnected from the session
		if ( KeyValues *kvEvent = new KeyValues( "OnPlayerMachinesDisconnected" ) )
		{
			kvEvent->SetInt( "numMachines", 1 );
			kvEvent->SetUint64( "id", xuid );
			OnSessionEvent( kvEvent );
		}

	}
}



#if !defined( NO_STEAM )

void CSysSessionHost::Steam_OnLobbyCreated( LobbyCreated_t *pLobbyCreate, bool bError )
{
	if ( bError || pLobbyCreate->m_eResult != k_EResultOK )
	{
		Warning( "CSysSessionHost: CreateSession failed. Error %d\n", bError ? -1 : pLobbyCreate->m_eResult );
		
		m_eState = STATE_FAIL;

		KeyValues *kv = new KeyValues( "mmF->SysSessionUpdate" );
		kv->SetPtr( "syssession", this );
		kv->SetString( "error", "create" );
		OnSessionEvent( kv );
		return;
	}
	else
	{
		m_lobby.m_uiLobbyID = pLobbyCreate->m_ulSteamIDLobby;

		DevMsg( "Created lobby id: 0x%llx\n", m_lobby.m_uiLobbyID );

		// will get a subsequent callback at at Steam_OnLobbyEntered to indicate that we are a lobby member
		m_CallbackOnLobbyEntered.Register( this, &CSysSessionHost::Steam_OnLobbyEntered );
	}
}

void CSysSessionHost::Steam_OnLobbyEntered( LobbyEnter_t *pLobbyEnter )
{
	// Filter out notifications not from our lobby
	if ( pLobbyEnter->m_ulSteamIDLobby != m_lobby.m_uiLobbyID )
		return;

	m_CallbackOnLobbyEntered.Unregister();

	if ( pLobbyEnter->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess )
	{
		Warning( "Matchmaking: lobby response %d!\n", pLobbyEnter->m_EChatRoomEnterResponse );

		m_eState = STATE_FAIL;

		KeyValues *kv = new KeyValues( "mmF->SysSessionUpdate" );
		kv->SetPtr( "syssession", this );
		kv->SetString( "error", LobbyEnterErrorAsString( pLobbyEnter ) );
		OnSessionEvent( kv );
		return;
	}
	else
	{
		// Set all the properties of the new session
		InitSessionProperties();

		// Install callback for messages
		m_CallbackOnLobbyChatMsg.Register( this, &CSysSessionBase::Steam_OnLobbyChatMsg );
		m_CallbackOnLobbyChatUpdate.Register( this, &CSysSessionBase::Steam_OnLobbyChatUpdate );

		// Setup voice
		Voice_ProcessTalkers( NULL, true );

		m_eState = STATE_IDLE;

		KeyValues *kv = new KeyValues( "mmF->SysSessionUpdate" );
		kv->SetPtr( "syssession", this );
		OnSessionEvent( kv );
		return;
	}
}

bool CSysSessionHost::GetLobbyType( KeyValues *kv, ELobbyType *peType, bool *pbJoinable )
{
	if ( !peType || !pbJoinable )
		return false;

	char const *szLock = kv->GetString( "system/lock", NULL );
	char const *szAccess = kv->GetString( "system/access", NULL );
	if ( !szAccess && !szLock )
		return false;

	if ( !szLock )
		szLock = m_pSettings->GetString( "system/lock", "" );
	if ( !szAccess )
		szAccess = m_pSettings->GetString( "system/access", "public" );

	*pbJoinable = ( szLock[0] == 0 );	// non joinable if locked

	if ( !Q_stricmp( "public", szAccess ) )
		return *peType = k_ELobbyTypePublic, true;
	else if ( !Q_stricmp( "friends", szAccess ) )
		return *peType = k_ELobbyTypeFriendsOnly, true;
	else if ( !Q_stricmp( "private", szAccess ) )
		return *peType = k_ELobbyTypePrivate, true; // private
	else
		return false;
}

#endif

void CSysSessionHost::UpdateMembersInfo()
{
#if !defined( NO_STEAM )
	if ( m_lobby.m_uiLobbyID )
	{
		steamapicontext->SteamMatchmaking()->SetLobbyMemberLimit( m_lobby.m_uiLobbyID,
			m_pSettings->GetInt( "members/numSlots", 1 ) + m_numDormantMembersDetected );
	}

	// Set the initial members information
	LobbySetDataFromKeyValues( "members", m_pSettings->FindKey( "members" ), false );
#endif
}

void CSysSessionHost::InitSessionProperties()
{
	UpdateMembersInfo();
	UpdateSessionProperties( m_pSettings );
}

void CSysSessionHost::UpdateSessionProperties( KeyValues *kv )
{
	if ( !kv )
		return;

	CSysSessionBase::UpdateSessionProperties( kv, true );

	//
	// Set joinability and public/private slots distribution
	//

#if !defined( NO_STEAM )
	ELobbyType eType = k_ELobbyTypePublic;
	bool bJoinable = true;
	if ( GetLobbyType( kv, &eType, &bJoinable ) && m_lobby.m_uiLobbyID )
	{
		steamapicontext->SteamMatchmaking()->SetLobbyType( m_lobby.m_uiLobbyID, eType );
		steamapicontext->SteamMatchmaking()->SetLobbyJoinable( m_lobby.m_uiLobbyID, bJoinable );
	}
#endif

	//
	// Update QOS reply data of the session
	//
}

bool CSysSessionHost::Process_RequestJoinData( XUID xuidClient, KeyValues *pSettings )
{
	// Check if the request is a duplicate because of migration and client had
	// to re-request join authorization from the new host, but the old host
	// already included the new client into the session...
	if ( SessionMembersFindPlayer( m_pSettings, xuidClient ) )
		return true;

	// We should merge the client's information into our settings information
	int numUsersConnecting = pSettings->GetInt( "members/numPlayers", 0 );
	int numMachinesConnecting = pSettings->GetInt( "members/numMachines", 0 );

	int numSlotsTotal = m_pSettings->GetInt( "members/numSlots", 0 );
	int numUsersCurrent = m_pSettings->GetInt( "members/numPlayers", 0 );
	int numMachinesCurrent = m_pSettings->GetInt( "members/numMachines", 0 );

	// CS:GO 2017 - Validate a bunch of parameters to match the authenticated XUID:
	if ( numMachinesConnecting != 1 ) return false;
	if ( numUsersConnecting != 1 ) return false;
	if ( pSettings->GetInt( "members/machine0/numPlayers" ) != 1 ) return false;
	if ( pSettings->GetUint64( "members/machine0/id" ) != xuidClient ) return false;
	if ( pSettings->GetUint64( "members/machine0/player0/xuid" ) != xuidClient ) return false;

	// Check if there are more players on the server than in session
	INetSupport::ClientInfo_t nsci = {0};
	g_pMatchExtensions->GetINetSupport()->GetClientInfo( &nsci );
	
	// If we represent a team lobby, then only consider players on our team
	if ( nsci.m_numHumanPlayers &&
		!Q_stricmp( m_pSettings->GetString( "system/netflag", "" ), "teamlobby" ) )
	{
		// TODO: int nMatchTeam = m_pSettings->GetInt( "server/team", 1 );
		// for now just always go by Steam lobby slots
		nsci.m_numHumanPlayers = 0;
	}

	// Prepare the reply package
	KeyValues *reply = new KeyValues( "SysSession::ReplyJoinData" );
	KeyValues::AutoDelete autodelete( reply );

	reply->SetUint64( "id", xuidClient );

	// If we have a team reservation in place then reject clients that don't
	// provide the reservation key
	if ( m_teamResKey )
	{
		uint64 clientKey = pSettings->GetUint64( "teamResKey", 0 );
		if ( m_teamResKey != clientKey)
		{
			reply->SetString( "error", "TeamResFail" );
			SendMessage( reply );
			return false;
		}
		else
		{
			// One more PWF team client has joined the session
			m_numRemainingTeamPlayers--;
			if ( m_numRemainingTeamPlayers == 0 )
			{
				UnreserveTeamSession();
			}
		}
	}

	// If any of the data is malformed, early out
	if ( !numUsersConnecting || !numMachinesConnecting ||
		!numSlotsTotal || !numUsersCurrent || !numMachinesCurrent )
	{
		reply->SetString( "error", "n/a" );
		SendMessage( reply );
		return false;
	}

	// If the session is using a private key (tournament lobby), then keys must match
	char const *szRequestLockField = pSettings->GetString( "system/lock", "" );
	if ( mm_session_sys_pkey.GetString()[0] )
	{
		if ( V_strcmp( szRequestLockField, mm_session_sys_pkey.GetString() ) )
		{
			DevMsg( "LOBBY: blocking join request from %llu with invalid key: %s\n", xuidClient, szRequestLockField );
			reply->SetString( "error", "n/a" );
			SendMessage( reply );
			return false;
		}
	}

	// If the session is locked, prevent join
	char const *szSessionSystemSettingsLock = m_pSettings->GetString( "system/lock", "" );
	if ( szSessionSystemSettingsLock[0] )
	{
		char const *szReplyError = "lock";
		if ( !V_stricmp( "mmqueue", szSessionSystemSettingsLock ) )
		{
			szReplyError = "LockMmQueue";
		}
		reply->SetString( "error", szReplyError );
		SendMessage( reply );
		return false;
	}

	// If the request is a soft-join then check privacy and mmqueue
	if ( KeyValues *kvSoftChecks = pSettings->FindKey( "joincheck" ) )
	{
		FOR_EACH_SUBKEY( kvSoftChecks, kvSubCheck )
		{
			char const *szSoftValue = kvSubCheck->GetName();
			char const *szSoftKey = kvSubCheck->GetString();
			bool bSoftCheckOK = ( !V_strcmp( "#empty#", szSoftValue ) && !*m_pSettings->GetString( szSoftKey ) ) ||
				( ( *szSoftValue == '[' ) && V_strstr( szSoftValue, CFmtStr( "[%s]", m_pSettings->GetString( szSoftKey ) ) ) ) ||
				!V_stricmp( m_pSettings->GetString( szSoftKey ), szSoftValue );
			if ( !bSoftCheckOK )
			{
				DevMsg( "LOBBY: blocking join request from %llu with check: %s = '%s' (actual: '%s')\n", xuidClient,
					szSoftKey, szSoftValue, m_pSettings->GetString( szSoftKey ) );
				char const *szReplyError = "lock";
				reply->SetString( "error", szReplyError );
				SendMessage( reply );
				return false;
			}
		}
	}

	// If the user has previously been kicked then do not let them re-join
	int idxKicked = m_mapKickedPlayers.Find( xuidClient );
	if ( idxKicked != m_mapKickedPlayers.InvalidIndex() )
	{
		if ( Plat_FloatTime() - m_mapKickedPlayers.Element( idxKicked ) < mm_session_sys_kick_ban_duration.GetFloat() )
		{
			DevMsg( "LOBBY: blocking join request from %llu, still %.1f sec kick ban duration remaining, mm_session_sys_kick_ban_duration = %.1f.\n", xuidClient,
				m_mapKickedPlayers.Element( idxKicked ) + 1 + mm_session_sys_kick_ban_duration.GetFloat() - Plat_FloatTime(), mm_session_sys_kick_ban_duration.GetFloat() );
			reply->SetString( "error", "kicked" );
			SendMessage( reply );
			return false;
		}
	}

	// If the session is full, early out
	if ( MAX( numUsersCurrent, nsci.m_numHumanPlayers )
		+ numUsersConnecting > numSlotsTotal )
	{
		reply->SetString( "error", "full" );
		SendMessage( reply );
		return false;
	}

	// Get the members containers
	KeyValues *pMembers = m_pSettings->FindKey( "members" );
	Assert( pMembers );
	if ( !pMembers )
		return false;

	KeyValues *pMembersConnecting = pSettings->FindKey( "members" );
	Assert( pMembersConnecting );
	if ( !pMembersConnecting )
		return false;

	// Validate that the connecting machines have the required TU and DLC installed
	for ( int k = 0; k < numMachinesConnecting; ++ k )
	{
		KeyValues *kvConnecting = pMembersConnecting->FindKey( CFmtStr( "machine%d", k ) );
		Assert( kvConnecting );
		if ( !kvConnecting )
			continue;

		char const *szTuString = kvConnecting->GetString( "tuver" );
		uint64 uiDlcMask = kvConnecting->GetUint64( "dlcmask" );

		char const *szTuRequired = pMembers->GetString( "machine0/tuver" );
		uint64 uiDlcRequired = m_pSettings->GetUint64( "game/dlcrequired" );

		if ( Q_strcmp( szTuString, szTuRequired ) )
		{
			reply->SetString( "error", "turequired" );
			reply->SetString( "turequired", szTuRequired );
			SendMessage( reply );
			return false;
		}

		if ( ( uiDlcMask & uiDlcRequired ) != uiDlcRequired )
		{
			reply->SetString( "error", "dlcrequired" );
			reply->SetUint64( "dlcrequired", uiDlcRequired &~uiDlcMask );
			reply->SetUint64( "dlcmask", uiDlcRequired );
			SendMessage( reply );
			return false;
		}
	}

	// Merge the information about the connecting members
	for ( int k = 0; k < numMachinesConnecting; ++ k )
	{
		KeyValues *kvConnecting = pMembersConnecting->FindKey( CFmtStr( "machine%d", k ) );
		Assert( kvConnecting );
		if ( !kvConnecting )
			continue;

		KeyValues *kvNewMachine = kvConnecting->MakeCopy();
		kvNewMachine->SetName( CFmtStr( "machine%d", k + numMachinesCurrent ) );
		pMembers->AddSubKey( kvNewMachine );

		// Register talkers from that machine
		Voice_ProcessTalkers( kvNewMachine, true );
	}

	// Update the current count of machines and members
	pMembers->SetInt( "numMachines", numMachinesCurrent + numMachinesConnecting );
	pMembers->SetInt( "numPlayers", numUsersCurrent + numUsersConnecting );

	// Join flags
	pMembers->SetUint64( "joinflags", pMembersConnecting->GetUint64( "joinflags" ) );

	// Fire the notifications about a bunch of machines connected to the session
	if ( KeyValues *kvEvent = new KeyValues( "OnPlayerMachinesConnected" ) )
	{
		kvEvent->SetInt( "numMachines", numMachinesConnecting );
		kvEvent->SetUint64( "id", xuidClient );
		OnSessionEvent( kvEvent );
	}

	// Fire the notifications about the new players
	for ( int k = 0; k < numMachinesConnecting; ++ k )
	{
		KeyValues *kvConnecting = pMembersConnecting->FindKey( CFmtStr( "machine%d", k ) );
		if ( !kvConnecting )
			continue;

		int numPlayers = kvConnecting->GetInt( "numPlayers", 0 );
		for ( int j = 0; j < numPlayers; ++ j )
		{
			KeyValues *pPlayer = kvConnecting->FindKey( CFmtStr( "player%d", j ) );
			XUID xuid = pPlayer->GetUint64( "xuid", 0ull );
			if ( !xuid )
				continue;

			if ( KeyValues *kvEvent = new KeyValues( "OnPlayerUpdated" ) )
			{
				kvEvent->SetUint64( "xuid", xuid );
				kvEvent->SetString( "state", "joined" );
				kvEvent->SetPtr( "player", pPlayer );
				OnSessionEvent( kvEvent );
			}
		}
	}

	// Send the encryption key to the connected client too
	reply->SetUint64( "crypt", m_ullCrypt );

	// After everything settled with the new players on this machine,
	// notify everybody of the new state of the session
	reply->AddSubKey( m_pSettings->MakeCopy() );
	SendMessage( reply );

	Voice_UpdateMutelist();

#if !defined( NO_STEAM )
	// Update members information
	LobbySetDataFromKeyValues( "members", m_pSettings->FindKey( "members" ), false );
#endif

	return true;
}

void CSysSessionHost::Process_TeamReservation( XUID key, int teamSize )
{
	KeyValues *pMembers;
	int numPlayers;
	int numSlots;
	
	KeyValues *reply = new KeyValues( "SysSession::TeamReservationResult" );
	KeyValues::AutoDelete autodelete( reply );
		
	// Check we are not already reserved
	if ( m_teamResKey != 0 )
	{
		reply->SetString( "result", "alreadyReserved" );
		goto xit;
	}
	
	// Check if we have enough free slots
	pMembers = m_pSettings->FindKey( "members" );
	numPlayers = pMembers->GetInt( "numPlayers" );
	numSlots = pMembers->GetInt( "numSlots" );

	if ( numSlots < numPlayers + teamSize)
	{
		reply->SetString( "result", "notEnoughSlots" );
		goto xit;
	}

	reply->SetString( "result", "success" );
	ReserveTeamSession( key, teamSize );

xit:
	SendMessage( reply );
}

void CSysSessionHost::ReserveTeamSession( XUID key, int numPlayers )
{
#if !defined (NO_STEAM)
	DevMsg( "CSysSessionHost::ReserveTeamSession\n");
	m_teamResKey = key;
	m_numRemainingTeamPlayers = numPlayers;
	m_flTeamResStartTime = Plat_FloatTime();

	// Set lobby data
	KeyValues *settings = new KeyValues( "TeamReservation" );
	KeyValues::AutoDelete autodelete( settings );
	settings->SetInt( "TeamRes", 1 );
	LobbySetDataFromKeyValues( "TeamReservation", settings );
#endif
}

void CSysSessionHost::UnreserveTeamSession()
{
#if !defined (NO_STEAM)
	DevMsg( "CSysSessionHost::UnreserveTeamSession\n");
	m_teamResKey = 0;
	m_numRemainingTeamPlayers = 0;

	// Set lobby data
	KeyValues *settings = new KeyValues( "TeamReservation" );
	KeyValues::AutoDelete autodelete( settings );
	settings->SetInt( "TeamRes", 0 );
	LobbySetDataFromKeyValues( "TeamReservation", settings );
#endif
}

void CSysSessionHost::Process_VoiceStatus( KeyValues *msg, XUID xuidSrc )
{
	XUID xuid = msg->GetUint64( "xuid" );
	Assert( xuid == xuidSrc );
	if ( xuid != xuidSrc )
		return;

	KeyValues *pPlayer = SessionMembersFindPlayer( m_pSettings, xuid );
	if ( !pPlayer )
		return;

	char const *szValue = msg->GetString( "voice" );
	pPlayer->SetString( "voice", szValue );

	OnPlayerUpdated( pPlayer );
}

void CSysSessionHost::Process_VoiceMutelist( KeyValues *msg )
{
	XUID xuid = msg->GetUint64( "xuid" );
	
	KeyValues *pMachine = NULL;
	SessionMembersFindPlayer( m_pSettings, xuid, &pMachine );
	if ( !pMachine )
		return;

	// Remove old mutelist
	if ( KeyValues *pMutelist = pMachine->FindKey( "Mutelist" ) )
	{
		pMachine->RemoveSubKey( pMutelist );
		pMutelist->deleteThis();
	}

	// Update new mutelist
	if ( KeyValues *pMutelist = msg->FindKey( "Mutelist" ) )
	{
		pMachine->FindKey( "Mutelist", true )->MergeFrom( pMutelist, KeyValues::MERGE_KV_UPDATE );
	}

	OnMachineUpdated( pMachine );
}








//
// Client session implementation
//


CSysSessionClient::CSysSessionClient( KeyValues *pSettings ) :
	CSysSessionBase( pSettings ),
	m_eState( STATE_INIT ),
	m_flInitializeTimestamp( 0.0f )
{
}

CSysSessionClient::CSysSessionClient( CSysSessionHost *pHost, KeyValues *pSettings ) :
	CSysSessionBase( pSettings ),
#if !defined( NO_STEAM )
#endif
	m_eState( STATE_IDLE ),
	m_flInitializeTimestamp( 0.0f )
{
	m_lobby = pHost->m_lobby;

	m_Voice_flLastHeadsetStatusCheck = pHost->m_Voice_flLastHeadsetStatusCheck;

#if !defined( NO_STEAM )
	// Install callback for messages
	m_CallbackOnLobbyChatMsg.Register( this, &CSysSessionBase::Steam_OnLobbyChatMsg );
	m_CallbackOnLobbyChatUpdate.Register( this, &CSysSessionBase::Steam_OnLobbyChatUpdate );
#endif
}



CSysSessionClient::~CSysSessionClient()
{
	;
}

bool CSysSessionClient::Update()
{
	if ( !CSysSessionBase::Update() )
		return false;

	switch( m_eState )
	{
	case STATE_INIT:
		if ( !m_flInitializeTimestamp )
		{
			m_flInitializeTimestamp = Plat_FloatTime();
#if !defined( NO_STEAM )
			SetupSteamRankingConfiguration();
#endif
		}
		if (
#if !defined( NO_STEAM )
			( IsSteamRankingConfigured() || ( Plat_FloatTime() >= m_flInitializeTimestamp + mm_session_sys_ranking_timeout.GetFloat() ) ) &&
#endif
			SysSession_AllowCreate()
			)
			UpdateStateInit();
		break;

	case STATE_CREATING:
		break;

	case STATE_REQUESTING_JOIN_DATA:
		// Wait for 5 sec until data arrives
		if ( Plat_FloatTime() > m_RequestJoinDataInfo.m_fTimeSent + mm_session_sys_connect_timeout.GetFloat() )
		{
				m_eState = STATE_FAIL;

				Warning( "CSysSessionClient: Unable to get session information from host\n" );
				KeyValues *kv = new KeyValues( "mmF->SysSessionUpdate" );
				kv->SetPtr( "syssession", this );
				kv->SetString( "error", "n/a" );
				OnSessionEvent( kv );
			}
		break;

#if !defined( NO_STEAM )
	case STATE_JOIN_LOBBY:
		m_CallbackOnLobbyEntered.Register( this, &CSysSessionClient::Steam_OnLobbyEntered );
		steamapicontext->SteamMatchmaking()->JoinLobby( m_lobby.m_uiLobbyID );
		m_eState = STATE_CREATING;
		break;
#endif

	case STATE_IDLE:
		break;
	}

	return true;
}

void CSysSessionClient::Destroy()
{
	// If we are migrating, then don't let the
	// base class handle it because it will
	// post Quit messages and leave the lobby...
	if ( m_eState == STATE_MIGRATE )
	{
		delete this;
		return;
	}

	// Chain to base which will "delete this"
	CSysSessionBase::Destroy();
}

void CSysSessionClient::DebugPrint()
{
	DevMsg( "CSysSessionClient [ state=%d ]\n", m_eState );

	if ( m_eState == STATE_REQUESTING_JOIN_DATA )
	{
		DevMsg( "Requested join data %.3f sec ago from xuid = %llx\n",
			Plat_FloatTime() - m_RequestJoinDataInfo.m_fTimeSent, m_RequestJoinDataInfo.m_xuidLeader );
	}

	CSysSessionBase::DebugPrint();
}

XUID CSysSessionClient::GetHostXuid( XUID xuidValidResult )
{
#if !defined( NO_STEAM )
	return m_lobby.m_uiLobbyID ? steamapicontext->SteamMatchmaking()
		->GetLobbyOwner( m_lobby.m_uiLobbyID ).ConvertToUint64() : m_xuidMachineId;
#endif
	return 0ull;
}

void CSysSessionClient::UpdateStateInit()
{
#if !defined( NO_STEAM )

	m_lobby.m_uiLobbyID = m_pSettings->GetUint64( "options/sessionid", 0ull );
	m_eState = STATE_JOIN_LOBBY;

#endif
}

void CSysSessionClient::InitSessionProperties( KeyValues *pSettings )
{

	UpdateSessionProperties( pSettings );
}

void CSysSessionClient::UpdateSessionProperties( KeyValues *kv )
{
	if ( !kv )
		return;

	CSysSessionBase::UpdateSessionProperties( kv, false );

	//
	// Set joinability and public/private slots distribution
	//

}

void CSysSessionClient::ReceiveMessage( KeyValues *msg, bool bValidatedLobbyMember, XUID xuidSrc )
{
	char const *szMsg = msg->GetName();

	// Parse the message depending on our state
	switch ( m_eState )
	{
	case STATE_REQUESTING_JOIN_DATA:
		if ( !Q_stricmp( szMsg, "SysSession::ReplyJoinData" ) )
		{
			if ( ( msg->GetUint64( "id" ) == m_xuidMachineId )
				&& ( xuidSrc == GetHostXuid() ) )
			{
				Process_ReplyJoinData_Our( msg );
			}
		}
		break;

	case STATE_IDLE:
		if ( !bValidatedLobbyMember )
		{
			return;
		}
		else if ( !Q_stricmp( szMsg, "SysSession::ReplyJoinData" ) )
		{
			if ( GetHostXuid() == xuidSrc )
				Process_ReplyJoinData_Other( msg );
		}
		else if ( !Q_stricmp( szMsg, "SysSession::OnPlayerRemoved" ) )
		{
			if ( GetHostXuid() == xuidSrc )
				FindAndRemovePlayerFromMembers( msg->GetUint64( "xuid" ) );
		}
		else if ( !Q_stricmp( szMsg, "SysSession::OnPlayerKicked" ) )
		{
			if ( GetHostXuid() == xuidSrc )
			{
				XUID xuid = msg->GetUint64( "xuid" );
				if ( xuid == m_xuidMachineId )
					Process_Kicked( msg );
				else
					FindAndRemovePlayerFromMembers( xuid );
			}
		}
		else if ( !Q_stricmp( szMsg, "SysSession::OnPlayerUpdated" ) )
		{
			if ( GetHostXuid() == xuidSrc )
				Process_OnPlayerUpdated( msg );
		}
		else if ( !Q_stricmp( szMsg, "SysSession::OnMachineUpdated" ) )
		{
			if ( GetHostXuid() == xuidSrc )
				Process_OnMachineUpdated( msg );
		}
		else if ( !Q_stricmp( szMsg, "SysSession::OnUpdate" ) )
		{
			if ( GetHostXuid() == xuidSrc )
			{
				// Host is making changes to the session
				m_pSettings->MergeFrom( msg );

				// Take care of the updated properties on the client side
				UpdateSessionProperties( msg->FindKey( "update", false ) );

				// Broadcast the update to everybody interested
				MatchSession_BroadcastSessionSettingsUpdate( msg );
			}
		}
		else
		{
			CSysSessionBase::ReceiveMessage( msg, bValidatedLobbyMember, xuidSrc );
		}
		return;
	}
}



#if !defined( NO_STEAM )
void CSysSessionClient::Steam_OnLobbyEntered( LobbyEnter_t *pLobbyEnter )
{
	// Filter out notifications not from our lobby
	if ( pLobbyEnter->m_ulSteamIDLobby != m_lobby.m_uiLobbyID )
		return;

	m_CallbackOnLobbyEntered.Unregister();

	if ( pLobbyEnter->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess )
	{
		Warning( "CSysSessionClient: Cannot join lobby, response %d!\n", pLobbyEnter->m_EChatRoomEnterResponse );

		m_eState = STATE_FAIL;

		KeyValues *kv = new KeyValues( "mmF->SysSessionUpdate" );
		kv->SetPtr( "syssession", this );
		kv->SetString( "error", LobbyEnterErrorAsString( pLobbyEnter ) );
		OnSessionEvent( kv );
		return;
	}
	else
	{
		XUID xuidLeader = steamapicontext->SteamMatchmaking()->GetLobbyOwner( m_lobby.m_uiLobbyID ).ConvertToUint64();
		if ( m_xuidMachineId == xuidLeader )
		{
			Warning( "CSysSessionClient: Host left lobby, unable to migrate\n" );

			// We entered the lobby, but the host left
			KeyValues *kv = new KeyValues( "mmF->SysSessionUpdate" );
			kv->SetPtr( "syssession", this );
			kv->SetString( "error", "n/a" );
			OnSessionEvent( kv );
			return;
		}

		// DBUGG
		// What lobby have we just joined
		{
			DevMsg( "Joined lobby %llx\n", m_lobby.m_uiLobbyID );
			int i, dataCount;
			dataCount = steamapicontext->SteamMatchmaking()->GetLobbyDataCount( m_lobby.m_uiLobbyID );
			for ( i=0; i < dataCount; i++ )
			{
				char key[64];
				char val[64];
				steamapicontext->SteamMatchmaking()->GetLobbyDataByIndex( m_lobby.m_uiLobbyID,
					i, key, sizeof(key), val, sizeof( val ));
				DevMsg( "Lobby data: %s = %s\n", key, val );
			}
		}

		// In the case that the player is joining via direct connect we only want to proceed if
		//	1. The player has a friend in the lobby
		//	2. The player is not blocked by anyone in the lobby
		// If we have got this far (2) has already been taken care of by the frenemies code 
		// so we just need to take care of (1)
		// The "options/dcFriendsReqd" flag is set in the client-server protocol
		// For direct connects this handshake has already happened before the lobby is joined
		// For matchmaking this happens after the lobby is joined so is irrelevant
		KeyValuesDumpAsDevMsg( m_pSettings );
		bool bFriendsReqd = m_pSettings->GetBool( "options/dcFriendsRed", false );
		if ( bFriendsReqd )
		{
			// Check if the player is joining via direct connect (or invite)
			// if joining via invite the player will pass the test below
			const char *action = m_pSettings->GetString( "options/action", "" );
			if ( !Q_stricmp( action, "joinsession" )  )
			{
				// Walk lobby members
				int i, numLobbyMembers = steamapicontext->SteamMatchmaking()->GetNumLobbyMembers( m_lobby.m_uiLobbyID );
				CSteamID playerID = steamapicontext->SteamUser()->GetSteamID();

				for ( i = 0; i < numLobbyMembers; i++ )
				{
					CSteamID memberId = steamapicontext->SteamMatchmaking()->GetLobbyMemberByIndex( 
						m_lobby.m_uiLobbyID, i );

					if ( memberId == playerID )
					{					
						continue;
					}

					EFriendRelationship reln = steamapicontext->SteamFriends()->GetFriendRelationship( memberId );

					if ( reln == k_EFriendRelationshipFriend )
					{
						break;
					}
				}

				if ( i >= numLobbyMembers )
				{
					// No friends found - leave lobby
					steamapicontext->SteamMatchmaking()->LeaveLobby( m_lobby.m_uiLobbyID );

					Warning( "CSysSessionClient: No friends in lobby - cannot join\n");

					m_eState = STATE_FAIL;

					KeyValues *kv = new KeyValues( "mmF->SysSessionUpdate" );
					kv->SetPtr( "syssession", this );
					kv->SetString( "error", "Friend reqd to join lobby" );
					OnSessionEvent( kv );

					return;
				}
			}
		}
		
		// Request permission to join
		m_eState = STATE_REQUESTING_JOIN_DATA;
		m_RequestJoinDataInfo.m_fTimeSent = Plat_FloatTime();
		m_RequestJoinDataInfo.m_xuidLeader = xuidLeader;

		Send_RequestJoinData();
		return;
	}
}
#endif

void CSysSessionClient::Process_ReplyJoinData_Our( KeyValues *msg )
{
	DevMsg("CSysSessionClient::Process_ReplyJoinData_Our\n");
	KeyValuesDumpAsDevMsg( msg );

	KeyValues *pSettings = msg->FindKey( "settings" );
	char const *szError = msg->GetString( "error", NULL );
	if ( !pSettings || szError )
	{
		Warning( "CSysSessionClient: Received bad session data from host\n" );
		m_eState = STATE_FAIL;

		KeyValues *kv = msg->MakeCopy();
		kv->SetName( "mmF->SysSessionUpdate" );
		kv->SetPtr( "syssession", this );
		if ( !szError )
			kv->SetString( "error", "n/a" );
		OnSessionEvent( kv );
	}
	else
	{		
		m_eState = STATE_IDLE;

		// We have received an entirely new "settings" data, copy that to our "settings" data
		// after saving settings we need to restore
		int conteam = m_pSettings->GetInt( "conteam", -1 );
		
		m_pSettings->Clear();
		m_pSettings->SetName( pSettings->GetName() );
		m_pSettings->MergeFrom( pSettings, KeyValues::MERGE_KV_UPDATE );
		
		InitSessionProperties( m_pSettings );

		if ( conteam != -1 )
		{
			m_pSettings->SetInt("conteam", conteam );
		}

		// Setup voice engine
		Voice_ProcessTalkers( NULL, true );
		Voice_UpdateMutelist();

		KeyValues *kv = new KeyValues( "mmF->SysSessionUpdate" );
		kv->SetPtr( "syssession", this );
		if ( uint64 ullCrypt = msg->GetUint64( "crypt" ) )
			kv->SetUint64( "crypt", ullCrypt );

		OnSessionEvent( kv );
	}
}

void CSysSessionClient::Process_ReplyJoinData_Other( KeyValues *msg )
{
	// Somebody has joined the session
	char const *szError = msg->GetString( "error", NULL );
	KeyValues *pSettings = msg->FindKey( "settings" );
	if ( !pSettings || szError )
		// connection attempt was rejected
		return;

	KeyValues *pMembers = pSettings->FindKey( "members" );
	if ( !pMembers )
		return;

	// Now process all the new machines to notify the subscribers of
	// new players
	int numMachinesOld = m_pSettings->GetInt( "members/numMachines", 0 );

	// Use this opportunity to fully sync-up our client-side settings
	int conteam = m_pSettings->GetInt( "conteam", -1 );
	m_pSettings->Clear();
	m_pSettings->SetName( pSettings->GetName() );
	m_pSettings->MergeFrom( pSettings, KeyValues::MERGE_KV_UPDATE );
	if ( conteam != -1 )
	{
		m_pSettings->SetInt("conteam", conteam );
	}

	// Run over all new machines
	int numMachinesNew = m_pSettings->GetInt( "members/numMachines", 0 );
	Assert( numMachinesNew > numMachinesOld );
	for ( int k = numMachinesOld; k < numMachinesNew; ++ k )
	{
		KeyValues *kvMachine = pMembers->FindKey( CFmtStr( "machine%d", k ) );
		if ( !kvMachine )
			continue;

		// Register talkers
		Voice_ProcessTalkers( kvMachine, true );

		int numPlayers = kvMachine->GetInt( "numPlayers", 0 );
		for ( int j = 0; j < numPlayers; ++ j )
		{
			KeyValues *pPlayer = kvMachine->FindKey( CFmtStr( "player%d", j ) );
			XUID xuid = pPlayer->GetUint64( "xuid", 0ull );
			if ( !xuid )
				continue;

			KeyValues *kvEvent = new KeyValues( "OnPlayerUpdated" );
			kvEvent->SetString( "state", "joined" );
			kvEvent->SetUint64( "xuid", xuid );
			kvEvent->SetPtr( "player", pPlayer );
			OnSessionEvent( kvEvent );
		}
	}

	Voice_UpdateMutelist();
}

void CSysSessionClient::Process_OnPlayerUpdated( KeyValues *msg )
{
	KeyValues *pPlayer = SessionMembersFindPlayer( m_pSettings, msg->GetUint64( "xuid" ) );
	if ( !pPlayer )
		return;

	char chNameBuffer[64];
	Q_snprintf( chNameBuffer, ARRAYSIZE( chNameBuffer ), "%s", pPlayer->GetName() );

	pPlayer->Clear();
	pPlayer->SetName( chNameBuffer );
	pPlayer->MergeFrom( msg, KeyValues::MERGE_KV_UPDATE );

	// Notify the framework about player updated
	KeyValues *kvEvent = new KeyValues( "OnPlayerUpdated" );
	kvEvent->SetUint64( "xuid", pPlayer->GetUint64( "xuid" ) );
	kvEvent->SetPtr( "player", pPlayer );
	OnSessionEvent( kvEvent );
}

void CSysSessionClient::Process_OnMachineUpdated( KeyValues *msg )
{
	KeyValues *pMachine = NULL;
	SessionMembersFindPlayer( m_pSettings, msg->GetUint64( "id" ), &pMachine );
	if ( !pMachine )
		return;

	char chNameBuffer[64];
	Q_snprintf( chNameBuffer, ARRAYSIZE( chNameBuffer ), "%s", pMachine->GetName() );

	pMachine->Clear();
	pMachine->SetName( chNameBuffer );
	pMachine->MergeFrom( msg, KeyValues::MERGE_KV_UPDATE );
}

void CSysSessionClient::Process_Kicked( KeyValues *msg )
{
	m_eState = STATE_FAIL;

	// Prepare the update notification
	KeyValues *kv = new KeyValues( "mmF->SysSessionUpdate" );
	kv->SetPtr( "syssession", this );
	kv->SetString( "error", "kicked" );

	// Inside this event broadcast our session will be deleted
	SendEventsNotification( kv );
}

void CSysSessionClient::Send_RequestJoinData()
{
	KeyValues *msg = new KeyValues( "SysSession::RequestJoinData" );
	KeyValues::AutoDelete autodelete( msg );

	msg->SetUint64( "id", m_xuidMachineId );

	// msg->AddSubKey( m_pSettings->MakeCopy() ); << don't send all settings, there's some unnecessary data
	msg->FindKey( "settings", true )->AddSubKey(m_pSettings->FindKey( "members" )->MakeCopy() );

	KeyValues *pSystem = m_pSettings->FindKey( "options" );
	uint64 teamResKey = pSystem->GetUint64( "teamResKey", 0 );

	msg->FindKey( "settings" )->SetUint64( "teamResKey", teamResKey );

	if ( mm_session_sys_pkey.GetString()[0] )
	{
		msg->SetString( "settings/system/lock", mm_session_sys_pkey.GetString() );
	}

	// If client needs to do join checks then forward them to host
	if ( KeyValues *kvJoinCheck = m_pSettings->FindKey( "joincheck" ) )
		msg->FindKey( "settings" )->AddSubKey( kvJoinCheck->MakeCopy() );

	DevMsg( "Sending join session request...\n" );
	KeyValuesDumpAsDevMsg( msg, 1 );

#if !defined( NO_STEAM )

	// Install callback for messages
	m_CallbackOnLobbyChatMsg.Register( this, &CSysSessionBase::Steam_OnLobbyChatMsg );
	m_CallbackOnLobbyChatUpdate.Register( this, &CSysSessionBase::Steam_OnLobbyChatUpdate );
	SendMessage( msg );

#endif
}

void CSysSessionClient::Migrate( KeyValues *pCommand )
{
	if ( Q_stricmp( pCommand->GetString( "migrate" ), "client>host" ) )
	{
		Assert( 0 );
		return;
	}

#ifndef NO_STEAM
	uint64 uiNewLobbyOwner = steamapicontext->SteamMatchmaking()->GetLobbyOwner( m_lobby.m_uiLobbyID ).ConvertToUint64();
	Assert( uiNewLobbyOwner == m_xuidMachineId );
	uiNewLobbyOwner;
	
	// Prepare the update notification
	KeyValues *kv = new KeyValues( "mmF->SysSessionUpdate" );
	kv->SetPtr( "syssession", this );
	m_eState = STATE_MIGRATE;
	kv->SetString( "action", "host" );
	// Inside this event broadcast our session will be deleted
	SendEventsNotification( kv );
#endif
}

void CSysSessionClient::OnPlayerLeave( XUID xuid )
{
#if !defined( NO_STEAM )
	if ( !V_stricmp( m_pSettings->GetString( "system/netflag" ), "noleave" ) )
	{
		DevMsg( "CSysSessionClient::OnPlayerLeave(%llx) ignored in noleave mode.\n", xuid );
		return;
	}
#endif

	XUID xuidCurrentHost = GetHostXuid( xuid );	// Indicate the the leaving XUID could have been the host

	if ( m_eState == STATE_IDLE || m_eState == STATE_MIGRATE )
	{
		FindAndRemovePlayerFromMembers( xuid );
	}

	// We only care to handle this event further if we are becoming the new host
	char const *szForcedError = NULL;

#if !defined( NO_STEAM )

	XUID xuidNewHost = steamapicontext->SteamMatchmaking()->GetLobbyOwner( m_lobby.m_uiLobbyID ).ConvertToUint64();
	if ( xuidNewHost != m_xuidMachineId )
	{
		if ( m_eState == STATE_REQUESTING_JOIN_DATA &&
			 xuid == m_RequestJoinDataInfo.m_xuidLeader )
		{
			// Resend join request
			// m_RequestJoinDataInfo.m_fTimeSent = Plat_FloatTime();
			m_RequestJoinDataInfo.m_xuidLeader = xuidNewHost;

			Send_RequestJoinData();
		}
		else if ( xuidNewHost != xuidCurrentHost )
		{
			// Send a notification
			KeyValues *kvEvent = new KeyValues( "OnPlayerLeaderChanged" );
			kvEvent->SetString( "state", "client" );
			kvEvent->SetUint64( "xuid", xuidNewHost );
			SendEventsNotification( kvEvent );
		}

		return;
	}

	if ( m_eState != STATE_IDLE )
		szForcedError = "n/a";

#endif

	// Prepare the update notification
	KeyValues *kv = new KeyValues( "mmF->SysSessionUpdate" );
	kv->SetPtr( "syssession", this );

	// If migration is not possible at this stage,
	// then a forced error will send us into FAIL state
	if ( szForcedError )
	{
		m_eState = STATE_FAIL;

		kv->SetString( "error", szForcedError );
	}
	else if ( !Q_stricmp( "teamlink", m_pSettings->GetString( "system/netflag", "" ) ) )
	{
		// Team link sessions cannot migrate, we just trigger an error so that
		// entire client link session including network manager could destroy properly
		m_eState = STATE_FAIL;

		kv->SetString( "error", "migrate" );
	}
	else
	{
		// We are about to migrate and become the new host
		// See if the title settings mgr wants to chime in
		if ( KeyValues *pMigrationHdlr = g_pMMF->GetMatchTitleGameSettingsMgr()->PrepareClientLobbyForMigration( m_pSettings, NULL ) )
		{
			KeyValues::AutoDelete autodelete( pMigrationHdlr );
			if ( char const *szError = pMigrationHdlr->GetString( "error", NULL ) )
			{
				// Title forcing a migration error
				m_eState = STATE_FAIL;
				kv->SetString( "error", szError );

				// Enqueue disconnect command
				g_pMatchExtensions->GetIVEngineClient()->ClientCmd( "disconnect" );
			}
		}

		if ( m_eState != STATE_FAIL )
		{
			m_eState = STATE_MIGRATE;

			kv->SetString( "action", "host" );
		}
	}

	// Inside this event broadcast our session will be deleted
	SendEventsNotification( kv );
}


CSysSessionConTeamHost::CSysSessionConTeamHost( KeyValues *pSettings ) :
	CSysSessionBase( pSettings ),
	m_eState( STATE_INIT ),
	m_lastRequestSendTime( 0.0f )
{
#if !defined (NO_STEAM)
	m_CallbackOnLobbyChatMsg.Register( this, &CSysSessionBase::Steam_OnLobbyChatMsg );
#endif
}

CSysSessionConTeamHost::~CSysSessionConTeamHost()
{
#if !defined (NO_STEAM)
	m_CallbackOnLobbyChatMsg.Unregister();
#endif
}

bool CSysSessionConTeamHost::Update()
{
	if (!CSysSessionBase::Update())
		return false;

	switch ( m_eState )
	{
	case STATE_INIT:

#if !defined (NO_STEAM)

		// Join lobby
		m_lobby.m_uiLobbyID = m_pSettings->GetUint64( "options/sessionid", 0ull );		
		m_CallbackOnLobbyEntered.Register( this, &CSysSessionConTeamHost::Steam_OnLobbyEntered );
		steamapicontext->SteamMatchmaking()->JoinLobby( m_lobby.m_uiLobbyID );

#endif

		m_eState = STATE_WAITING_LOBBY_JOIN;
		break;

	case STATE_SEND_RESERVATION_REQUEST:
		SendReservationRequest();
		m_eState = STATE_WAITING_RESERVATION_REQUEST;
		break;		

	case STATE_WAITING_RESERVATION_REQUEST:

		if ( Plat_FloatTime() > m_lastRequestSendTime + mm_session_sys_connect_timeout.GetFloat() )
		{
			DevMsg( "CSysSessionConTeamHost: Timed out waiting for reservation reply\n");
			Failed();
		}
		break;
	}

	return true;
}

void CSysSessionConTeamHost::Destroy()
{

	// Chain to base which will "delete this"
	CSysSessionBase::Destroy();
}

bool CSysSessionConTeamHost::GetPlayerSidesAssignment( int *numPlayers, uint64 playerIDs[10], int side[10] )
{
#if !defined (NO_STEAM)
	if ( GetResult() != RESULT_SUCCESS )
	{
		return false;
	}

	const char* szNumTSlotsFree  = steamapicontext->SteamMatchmaking()->GetLobbyData( m_lobby.m_uiLobbyID, "members:numTSlotsFree" );
	const char* szNumCTSlotsFree  = steamapicontext->SteamMatchmaking()->GetLobbyData( m_lobby.m_uiLobbyID, "members:numCTSlotsFree" );
	int numTeamPlayers = m_pSettings->GetInt( "members/numPlayers" );

	DevMsg( "CSysSessionConTeamHost: numTSlotsFree = %s, numCTSlotsFree = %s\n", szNumTSlotsFree, szNumCTSlotsFree);

	// Choose a team to join
	int numSlotsFree[2];

	numSlotsFree[0] = atoi( szNumCTSlotsFree );
	numSlotsFree[1] = atoi( szNumTSlotsFree );

	// Choose a team at random
	int team = RandomInt(0, 1);
	int otherTeam = 1 - team;

	// If we chose a team with too few slots, but other team has enough
	// then swap
	if ( numSlotsFree[team] < numTeamPlayers )
	{
		if ( numSlotsFree[otherTeam] >= numTeamPlayers )
		{
			team = otherTeam;
			otherTeam = 1 - team;
		}
	}

	KeyValues *pMembers = m_pSettings->FindKey( "members" );

	// Assign players to different teams
	for ( int i = 0; i < numTeamPlayers; i++ )
	{
		const char *playerKey = CFmtStr( "machine%d/player0", i );
		KeyValues *pPlayer = pMembers->FindKey( playerKey );
		uint64 playerId = pPlayer->GetUint64( "xuid", 0 );

		playerIDs[i] = playerId;

		// In keyvalues, team CT = 1, team T = 2
		if ( numSlotsFree[team] )
		{
			side[i] = team + 1;
			numSlotsFree[team]--;
		}
		else
		{
			side[i] = otherTeam + 1;				
		}
	}
		
	*numPlayers = numTeamPlayers;

#endif

	return true;
}

void CSysSessionConTeamHost::ReceiveMessage( KeyValues *msg, bool bValidatedLobbyMember, XUID xuidSrc )
{
#if !defined (NO_STEAM)

	char const *szMsg = msg->GetName();
	bool bProcessed = false;

	switch ( m_eState )
	{
	case STATE_WAITING_RESERVATION_REQUEST:

		if ( !Q_stricmp( "SysSession::TeamReservationResult", szMsg ) )
		{
			bProcessed = true;

			const char *res = msg->GetString( "result");
			DevMsg( "CSysSessionConTeamHost: TeamReservationResult = %s\n", res );
			
			if ( !Q_stricmp( "success", res ) )
			{
				Succeeded();
			}
			else
			{
				Failed();
			}
		}

		break;
	}
			
	if ( !bProcessed )
	{
		CSysSessionBase::ReceiveMessage( msg, bValidatedLobbyMember, xuidSrc );
	}

#endif

}

void CSysSessionConTeamHost::SendReservationRequest()
{
	// Send reservation chat msg
	KeyValues *reservation = new KeyValues(	"SysSession::TeamReservation" );
	KeyValues::AutoDelete autodelete( reservation );

	int numTeamPlayers = m_pSettings->GetInt( "members/numPlayers" );
	reservation->SetInt( "numPlayers", numTeamPlayers );

	uint64 teamResKey = m_pSettings->GetUint64( "members/machine0/id", 0 );
	reservation->SetUint64( "teamResKey", teamResKey );

	DevMsg( "Sending res request with teamResKey == %llx\n ", teamResKey );

#if !defined (NO_STEAM)


	CUtlBuffer buf;
	buf.ActivateByteSwapping( !CByteswap::IsMachineBigEndian() );
	buf.PutInt( g_pMatchExtensions->GetINetSupport()->GetEngineBuildNumber() );
	reservation->WriteAsBinary( buf );

	steamapicontext->SteamMatchmaking()->SendLobbyChatMsg( m_lobby.m_uiLobbyID, buf.Base(), buf.TellMaxPut() );	

#endif

	m_lastRequestSendTime = Plat_FloatTime();
}


void CSysSessionConTeamHost::Succeeded()
{
	m_eState = STATE_DONE;
	m_result = RESULT_SUCCESS;
}

void CSysSessionConTeamHost::Failed()
{
	m_eState = STATE_DONE;
	m_result = RESULT_FAIL;
}

#if !defined( NO_STEAM )

void CSysSessionConTeamHost::Steam_OnLobbyEntered( LobbyEnter_t *pLobbyEnter )
{
	// Filter out notifications not from our lobby
	if ( pLobbyEnter->m_ulSteamIDLobby != m_lobby.m_uiLobbyID )
		return;

	m_CallbackOnLobbyEntered.Unregister();

	if ( pLobbyEnter->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess )
	{
		Warning( "CSysSessionConTeamHost: Could not join lobby\n" );
		Failed();
	}
	else
	{
		m_eState = STATE_SEND_RESERVATION_REQUEST;
	}	
}
#endif

XUID CSysSessionConTeamHost::GetHostXuid( XUID xuidValidResult )
{
	return 0ull;
}
