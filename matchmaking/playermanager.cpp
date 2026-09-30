//========= Copyright � 1996-2009, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#include "xbox/xboxstubs.h"

#include "mm_framework.h"

#include "fmtstr.h"

// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"

#ifndef SWDS

#pragma warning (disable : 4355 )

static ConVar mm_player_search_requests_limit( "mm_player_search_requests_limit", "-1", FCVAR_DEVELOPMENTONLY, "How many friend requests are displayed." );
static ConVar mm_player_search_update_interval( "mm_player_search_update_interval", "10", FCVAR_DEVELOPMENTONLY, "Interval between players searches." );
static ConVar mm_player_search_lan_ping_interval( "mm_player_search_lan_ping_interval", "0.2", FCVAR_DEVELOPMENTONLY, "Interval between LAN discovery pings." );
static ConVar mm_player_search_lan_ping_duration( "mm_player_search_lan_ping_duration", "0.6", FCVAR_DEVELOPMENTONLY, "Duration of LAN discovery ping phase." );

PlayerManager::PlayerManager() :
	m_bUpdateEnabled( true ),
	m_flNextUpdateTime( 0.0f ),
	m_searchesPending( 0 ),
	m_bRequestStoreStats( false )
{
	memset( mLocalPlayer, 0, sizeof( mLocalPlayer ) );
	memset( m_searchData, 0, sizeof( m_searchData ) );

}

PlayerManager::~PlayerManager()
{

	memset( mLocalPlayer, 0, sizeof( mLocalPlayer ) );
	memset( m_searchData, 0, sizeof( m_searchData ) );

	m_bUpdateEnabled = false;
	m_searchesPending = 0;

	// We are leaking player objects here, but it's during destruction of a global (app shutdown).
	// We don't want to Destroy() because doing so may call into Xbox libs that have already shutdown.
	mFriendsList.Purge();
}

static PlayerManager g_PlayerManager;
PlayerManager *g_pPlayerManager = &g_PlayerManager;

IPlayerLocal * PlayerManager::GetLocalPlayer(int playerIndex)
{
	if( ( playerIndex >= 0 ) && ( playerIndex < ARRAYSIZE(mLocalPlayer) ) && mLocalPlayer[ playerIndex ] )
	{
		return mLocalPlayer[ playerIndex ];
	}
	
	return NULL;
}

int PlayerManager::GetNumFriends()
{
	return mFriendsList.Count();
}

IPlayerFriend * PlayerManager::GetFriendByIndex( int index )
{
	return mFriendsList.IsValidIndex( index ) ? mFriendsList[ index ] : NULL;
}

IPlayerFriend * PlayerManager::GetFriendByXUID( XUID xuid )
{
	return FindPlayerFriend( xuid );
}

IPlayer * PlayerManager::FindPlayer( XUID xuid )
{
	if ( IPlayer *player = FindPlayerLocal( xuid ) )
		return player;

	if ( IPlayer *player = FindPlayerFriend( xuid ) )
		return player;

	return NULL;
}

PlayerFriend * PlayerManager::FindPlayerFriend( XUID xuid )
{
	for ( int iIndex = 0; iIndex < mFriendsList.Count(); ++ iIndex )
	{
		PlayerFriend *player = mFriendsList[iIndex];
		if ( player && player->GetXUID() == xuid )
			return player;
	}

	return NULL;
}

PlayerLocal * PlayerManager::FindPlayerLocal( XUID xuid )
{
	for ( int iIndex = 0; iIndex < ARRAYSIZE( mLocalPlayer ); ++ iIndex )
	{
		PlayerLocal *player = mLocalPlayer[iIndex];
		if ( player && player->GetXUID() == xuid )
			return player;
	}

	return NULL;
}

void PlayerManager::MarkOldFriends()
{
	for ( int iIndex = 0; iIndex < mFriendsList.Count(); iIndex++ )
	{
		PlayerFriend &player = * mFriendsList[iIndex];
		player.SetIsStale( true );
		player.SetFriendMark( 0 );
	}
}

void PlayerManager::RemoveOldFriends()
{
#if !defined( NO_STEAM )
	static bool bPerfectWorld = !!CommandLine()->FindParm( "-perfectworld" );
	CUtlMap< int, PlayerFriend*, int, CDefLess< int > > mapFriendRequests;
#endif
	for ( int iIndex = 0; iIndex < mFriendsList.Count(); iIndex++ )
	{
		PlayerFriend &player = * mFriendsList[iIndex];
		if ( player.GetIsStale() || !player.GetFriendMark() )
		{
			mFriendsList.FastRemove( iIndex -- );
			player.Destroy();
		}
		else if ( !bPerfectWorld && ( player.GetTitleID() == uint64( -3 ) || player.GetTitleID() == uint64( -2 ) ) )
		{
			int nLevel = steamapicontext->SteamFriends()->GetFriendSteamLevel( player.GetXUID() );
			mapFriendRequests.Insert( nLevel, &player );
#if !defined( NO_STEAM )
			if ( !nLevel ) // force the information to be downloaded
				steamapicontext->SteamFriends()->RequestUserInformation( player.GetXUID(), false );
#endif
		}
	}
#if !defined( NO_STEAM )
	int nLimit = mm_player_search_requests_limit.GetInt();
	if ( !bPerfectWorld && ( nLimit >= 0 ) )
	{
		while ( mapFriendRequests.Count() > nLimit )
		{
			int iMap = mapFriendRequests.FirstInorder();
			PlayerFriend *pCullFriendRequest = mapFriendRequests.Element( iMap );
			mapFriendRequests.RemoveAt( iMap );

			if ( mFriendsList.FindAndFastRemove( pCullFriendRequest ) )
				pCullFriendRequest->Destroy();
		}
	}
#endif
}

void PlayerManager::OnLocalPlayerDisconnectedFromLive( int iCtrlr )
{
	for ( int iIndex = 0; iIndex < mFriendsList.Count(); iIndex++ )
	{
		PlayerFriend &player = * mFriendsList[iIndex];
		
		uint uiMask = player.GetFriendMark();
		uiMask &=~ (1 << iCtrlr );

		if ( !uiMask )
		{
			mFriendsList.FastRemove( iIndex -- );
			player.Destroy();
		}
		else
		{
			player.SetFriendMark( uiMask );
		}
	}
}

void PlayerManager::Update()
{
	if ( m_searchesPending )
	{
		for ( int i = 0; i < XUSER_MAX_COUNT; ++i )
		{
			SFriendSearchData &data = m_searchData[ i ];
	
			if( data.mSearchInProgress )
			{
#if !defined( NO_STEAM )
				if ( 1 ) // XHasOverlappedIoCompleted
				{
					if ( 1 ) // XUserGetSigninState
					{
						uint64 ullTitleFlags = g_pMatchFramework->GetMatchTitle()->GetTitleSettingsFlags();
						bool bFetchAllFriends =  !!( ullTitleFlags & MATCHTITLE_PLAYERMGR_ALLFRIENDS );
						bool bManageFriendRequests = !!( ullTitleFlags & MATCHTITLE_PLAYERMGR_FRIENDREQS );
						int nSteamFriendsQueryMask = k_EFriendFlagImmediate;
						if ( bManageFriendRequests )
							nSteamFriendsQueryMask |= ( k_EFriendFlagFriendshipRequested | k_EFriendFlagRequestingFriendship );
						int iCtrlr = 0;
						int numFriends = steamapicontext->SteamFriends() ? steamapicontext->SteamFriends()->GetFriendCount( nSteamFriendsQueryMask ) : 0;
						uint64 uiAppID = steamapicontext->SteamUtils()->GetAppID();
						for ( int index = 0; index < numFriends; ++ index )
						{
							CSteamID steamIDFriend = steamapicontext->SteamFriends()->GetFriendByIndex( index, nSteamFriendsQueryMask );
							XUID xuidFriend = steamIDFriend.ConvertToUint64();
							FriendGameInfo_t fgi;
							bool bInGame = steamapicontext->SteamFriends()->GetFriendGamePlayed( xuidFriend, &fgi );
							EFriendRelationship eRelationship = bManageFriendRequests ? steamapicontext->SteamFriends()->GetFriendRelationship( xuidFriend ) : k_EFriendRelationshipFriend;
							EPersonaState ePersonaState = steamapicontext->SteamFriends()->GetFriendPersonaState( steamIDFriend );

							static bool bPerfectWorld = !!CommandLine()->FindParm( "-perfectworld" );
							if ( ( bInGame && fgi.m_gameID.AppID() == uiAppID ) ||
								( eRelationship == k_EFriendRelationshipRequestRecipient ) || ( eRelationship == k_EFriendRelationshipRequestInitiator ) ||
								( bFetchAllFriends && ( ( ePersonaState != k_EPersonaStateOffline ) || bPerfectWorld ) ) )

#else

				if ( 1 ) // XHasOverlappedIoCompleted
				{
					if ( 1 ) // XUserGetSigninState
					{
						int iCtrlr = 0;
						int numFriends = 0;
						uint64 uiAppID = 0;
						for ( int index = 0; index < numFriends; ++ index )
						{
							XUID xuidFriend = 0ull;
							bool bInGame = false;
							if ( bInGame )
#endif
							{
								PlayerFriend * player = FindPlayerFriend( xuidFriend );
								if( ! player )
								{
									player = new PlayerFriend( xuidFriend );
									mFriendsList.AddToTail( player );
								}
								player->SetIsStale( false );

								PlayerFriend::FriendInfo_t fi = {0};
#if !defined( NO_STEAM )
								uint64 uiLobbyIdFriend = fgi.m_steamIDLobby.ConvertToUint64();

								fi.m_uiTitleID = bInGame ? fgi.m_gameID.AppID() : 0;
								if ( bInGame && fgi.m_gameID.AppID() == uiAppID )
								{
									fi.m_xSessionID = ( const XNKID & ) uiLobbyIdFriend;
									fi.m_uiGameServerIP = fgi.m_unGameIP;
								}

								fi.m_szName = steamapicontext->SteamFriends()->GetFriendPersonaName( xuidFriend );
								fi.m_wszRichPresence = L"";
								switch ( ePersonaState )
								{
								case k_EPersonaStateOffline:		fi.m_wszRichPresence = L"Offline"; fi.m_uiTitleID = uint64( -1 );	break;
								case k_EPersonaStateOnline:			fi.m_wszRichPresence = L"Online";	break;
								case k_EPersonaStateBusy:			fi.m_wszRichPresence = L"Busy";	break;
								case k_EPersonaStateAway:			fi.m_wszRichPresence = L"Away";	break;
								case k_EPersonaStateSnooze:			fi.m_wszRichPresence = L"Snooze";	break;
								case k_EPersonaStateLookingToTrade:	fi.m_wszRichPresence = L"LookingToTrade";	break;
								case k_EPersonaStateLookingToPlay:	fi.m_wszRichPresence = L"LookingToPlay";	break;
								}

								if ( bManageFriendRequests )
								{	// When trying to manage friend requests, pass the status via rich presence
									if ( eRelationship == k_EFriendRelationshipRequestInitiator )
									{
										fi.m_wszRichPresence = L"AwaitingRemoteAccept";
										fi.m_uiTitleID = uint64( -2 );
									}
									else if ( eRelationship == k_EFriendRelationshipRequestRecipient )
									{
										fi.m_wszRichPresence = L"AwaitingLocalAccept";
										fi.m_uiTitleID = uint64( -3 );
									}
								}
#else
								uint64 uiLobbyIdFriend = 0ull;

								fi.m_szName = "";
								fi.m_wszRichPresence = L"";
								fi.m_xSessionID = ( const XNKID & ) uiLobbyIdFriend;
#endif
								player->UpdateFriendInfo( &fi );

								unsigned uiMask = player->GetFriendMark();
								uiMask |= ( 1 << iCtrlr );
								player->SetFriendMark( uiMask );
							}
						}
					}

					// This search has completed
					--m_searchesPending;
					data.mSearchInProgress = false;

				}
			}
		}

		UpdateLanSearch();
	
		if ( !m_searchesPending ) // Have all searches completed?
		{
			//we are done searching for friends, remove any that are still marked as old
			RemoveOldFriends();

			// Signal that we are finished with a search
			MEM_ALLOC_CREDIT();
			g_pMatchFramework->GetEventsSubscription()->BroadcastEvent( new KeyValues(
				"OnMatchPlayerMgrUpdate", "update", "searchfinished" ) );

			// If nobody request an immediate update, then nudge the next update time
			if ( m_flNextUpdateTime )
			{
				m_flNextUpdateTime = Plat_FloatTime() + mm_player_search_update_interval.GetFloat();
			}
		}
	}
	else if( m_bUpdateEnabled && Plat_FloatTime() > m_flNextUpdateTime &&
#ifndef NO_STEAM
		steamapicontext->SteamFriends() &&
#endif
		!IsLocalClientConnectedToServer() )
	{
		MarkOldFriends();

		CreateFriendEnumeration( 0 );
		CreateLanSearch();

		// Signal that we are starting a search
		MEM_ALLOC_CREDIT();
		g_pMatchFramework->GetEventsSubscription()->BroadcastEvent( new KeyValues(
			"OnMatchPlayerMgrUpdate", "update", "searchstarted" ) );
		
		// Nudge the next update time to indicate that update has started
		m_flNextUpdateTime = Plat_FloatTime() + mm_player_search_update_interval.GetFloat();
	}


	//
	// Let all the player classes run the update loop
	//

	for ( int iIndex = 0; iIndex < ARRAYSIZE( mLocalPlayer ); ++ iIndex )
	{
		PlayerLocal *player = mLocalPlayer[iIndex];
		if ( player )
			player->Update();
	}

	for ( int iIndex = 0; iIndex < mFriendsList.Count(); ++ iIndex )
	{
		PlayerFriend *player = mFriendsList[iIndex];
		if ( player )
			player->Update();
	}

	ExecuteStoreStatsRequest();
}

void PlayerManager::UpdateLanSearch()
{
    return; //lwss - hack, this function crashes.
    if ( !m_lanSearchData.m_bSearchInProgress )
		return;

	if ( m_lanSearchData.m_flStartTime && m_lanSearchData.m_flLastBroadcastTime )
	{
		if ( Plat_FloatTime() > m_lanSearchData.m_flStartTime + mm_player_search_lan_ping_duration.GetFloat() )
		{
			m_lanSearchData.m_bSearchInProgress = false;
			-- m_searchesPending;
			return;
		}

		if ( Plat_FloatTime() < m_lanSearchData.m_flLastBroadcastTime + mm_player_search_lan_ping_interval.GetFloat() )
		{
			// waiting out interval between pings
			return;
		}
	}
	else
	{
		// Initialize the start time of the lan broadcast
		m_lanSearchData.m_flStartTime = Plat_FloatTime();
	}

	//
	// Send the packet
	//
	m_lanSearchData.m_flLastBroadcastTime = Plat_FloatTime();
	MEM_ALLOC_CREDIT();
	g_pConnectionlessLanMgr->SendPacket( KeyValues::AutoDeleteInline( new KeyValues(
		"LanSearch"
		) ) );
}

enum SyncKeyValueDirection_t
{
	KVSTAT_WRITE_STAT,
	KVSTAT_READ_STAT
};
static void SyncKeyValueWithStatField( KeyValues *kvValue, IPlayerLocal *pPlayerLocal, TitleDataFieldsDescription_t const *pField, SyncKeyValueDirection_t eOp )
{
	switch( pField->m_eDataType )
	{
	case TitleDataFieldsDescription_t::DT_BITFIELD:
		if ( eOp == KVSTAT_WRITE_STAT )
			TitleDataFieldsDescriptionSetBit( pField, pPlayerLocal, !!kvValue->GetInt( "" ) );
		else
			kvValue->SetInt( "", TitleDataFieldsDescriptionGetBit( pField, pPlayerLocal ) ? 1 : 0 );
		break;
	case TitleDataFieldsDescription_t::DT_uint8:
		if ( eOp == KVSTAT_WRITE_STAT )
			TitleDataFieldsDescriptionSetValue<uint8>( pField, pPlayerLocal, (uint8)kvValue->GetInt( "" ) );
		else
			kvValue->SetInt( "", TitleDataFieldsDescriptionGetValue<uint8>( pField, pPlayerLocal ) );
		break;
	case TitleDataFieldsDescription_t::DT_uint16:
		if ( eOp == KVSTAT_WRITE_STAT )
			TitleDataFieldsDescriptionSetValue<uint16>( pField, pPlayerLocal, (uint16)kvValue->GetInt( "" ) );
		else
			kvValue->SetInt( "", TitleDataFieldsDescriptionGetValue<uint16>( pField, pPlayerLocal ) );
		break;
	case TitleDataFieldsDescription_t::DT_uint32:
		if ( eOp == KVSTAT_WRITE_STAT )
			TitleDataFieldsDescriptionSetValue<uint32>( pField, pPlayerLocal, (uint32)kvValue->GetInt( "" ) );
		else
			kvValue->SetInt( "", TitleDataFieldsDescriptionGetValue<uint32>( pField, pPlayerLocal ) );
		break;
	case TitleDataFieldsDescription_t::DT_float:
		if ( eOp == KVSTAT_WRITE_STAT )
			TitleDataFieldsDescriptionSetValue<float>( pField, pPlayerLocal, (float)kvValue->GetFloat( "" ) );
		else
			kvValue->SetInt( "", TitleDataFieldsDescriptionGetValue<float>( pField, pPlayerLocal ) );
		break;
	case TitleDataFieldsDescription_t::DT_uint64:
		if ( eOp == KVSTAT_WRITE_STAT )
			TitleDataFieldsDescriptionSetValue<uint64>( pField, pPlayerLocal, (uint64)kvValue->GetUint64( "" ) );
		else
			kvValue->SetUint64( "", TitleDataFieldsDescriptionGetValue<uint64>( pField, pPlayerLocal ) );
		break;
	}
}

void PlayerManager::OnEvent( KeyValues *pEvent )
{
	char const *szName = pEvent->GetName();

	if ( !Q_stricmp( szName, "OnNetLanConnectionlessPacket" ) )
	{
		if ( IsPC() && !m_lanSearchData.m_bSearchInProgress )
			return;

		if ( IsLocalClientConnectedToServer() )
			return;

		if ( KeyValues *pFriendGame = pEvent->FindKey( "GameDetailsPlayer" ) )
		{
			// Incoming data:
			//
			//	Options
			//		sessioninfo
			//	Player
			//		xuid
			//		xuidonline
			//		name
			//	binary
			//		ptr -> QOS block

			XUID xuid = pFriendGame->GetUint64( "player/xuidOnline", 0ull );
			if ( !xuid )
				xuid = pFriendGame->GetUint64( "player/xuid", 0ull );
			if ( !xuid )
				return;
			
			// Check if this is not our local client
			for ( int k = 0; k < ARRAYSIZE( mLocalPlayer ); ++ k )
			{
				if ( !mLocalPlayer[k] )
					continue;
				XUID xuidLocal = mLocalPlayer[k]->GetXUID();
				if ( xuidLocal == xuid )
					return;
			}

			// Unpack the QOS data block
			MM_GameDetails_QOS_t gd = {
				pFriendGame->GetPtr( "binary/ptr" ),
				pFriendGame->GetInt( "binary/size" ),
				0 };
			KeyValues *pGameDetails = g_pMatchFramework->GetMatchNetworkMsgController()->UnpackGameDetailsFromQOS( &gd );
			KeyValues::AutoDelete autodelete( pGameDetails );

			// On X360 do NOT let through unsolicited packets unless they are system link info
			if ( IsX360() && !m_lanSearchData.m_bSearchInProgress && Q_stricmp( "lan", pGameDetails->GetString( "system/network" ) ) )
				return;

			// Find or create the player friend that these game details belong to
			PlayerFriend *player = FindPlayerFriend( xuid );
			if ( !player )
			{
				player = new PlayerFriend( xuid );
				mFriendsList.AddToTail( player );
			}
			player->SetIsStale( false );
			player->SetFriendMark( ~0u );

			if ( pGameDetails )
			{
				// Append "player" and "options" subkeys
				if ( KeyValues *kvSubkey = pFriendGame->FindKey( "options" ) )
					pGameDetails->FindKey( "options", true )->MergeFrom( kvSubkey, KeyValues::MERGE_KV_UPDATE );
				if ( KeyValues *kvSubkey = pFriendGame->FindKey( "player" ) )
					pGameDetails->FindKey( "player", true )->MergeFrom( kvSubkey, KeyValues::MERGE_KV_UPDATE );
			}

			//
			// Set friend data
			//
			PlayerFriend::FriendInfo_t fi = {0};
			fi.m_szName = pFriendGame->GetString( "player/name", "" );
			fi.m_pGameDetails = pGameDetails;
			fi.m_uiTitleID = g_pMatchFramework->GetMatchTitle()->GetTitleID();
			fi.m_uiGameServerIP = ~0u;
			player->UpdateFriendInfo( &fi );
		}
	}
	else if( !Q_stricmp( szName, "OnSysSigninChange" ) )
	{
		OnSigninChange( pEvent );
	}
	else if ( !Q_stricmp( szName, "OnProfilesChanged" ) )
	{
		OnGameUsersChanged();
	}
	else if ( !Q_stricmp( szName, "OnUnlockArcadeTitle" ) )
	{
	}
	else if ( !Q_stricmp( szName, "OnSysProfileSettingsChanged" ) )
	{
		for ( int k = 0; k < ARRAYSIZE( mLocalPlayer ); ++ k )
		{
			if ( mLocalPlayer[k] && pEvent->GetInt( CFmtStr( "user%d", k ) ) )
			{
				DevMsg( "Reloading player profile data for ctrlr%d (%s)\n", k, mLocalPlayer[k]->GetName() );
				mLocalPlayer[k]->LoadPlayerProfileData();
			}
		}
	}
	else if ( !Q_stricmp( szName, "OnProfilesWriteOpportunity" ) )
	{
		char const *szReason = pEvent->GetString( "reason" );
		MM_XWriteOpportunity mmxwo = MMXWO_NONE;
		if ( !Q_stricmp( "checkpoint", szReason ) )
			mmxwo = MMXWO_CHECKPOINT;
		else if ( !Q_stricmp( "sessionstart", szReason ) )
			mmxwo = MMXWO_SESSION_STARTED;
		else if ( !Q_stricmp( "sessionend", szReason ) )
			mmxwo = MMXWO_SESSION_FINISHED;
		else if ( !Q_stricmp( "settings", szReason ) )
			mmxwo = MMXWO_SETTINGS;
		else if ( !Q_stricmp( "deactivation", szReason ) )
		{
			// The controllers are about to be deactivated, but
			// the actual signed in users at the controllers indices
			// are not changing.
			// Use this opportunity to write profile data if
			// XWriteOpportunity is allowing to do so.
			// This is the last chance to use currently signed in
			// players before they will be deactivated and destroyed.
			for ( int k = 0; k < ARRAYSIZE( mLocalPlayer ); ++ k )
			{
				if ( mLocalPlayer[k] )
					mLocalPlayer[k]->WriteTitleData();
			}
			ExecuteStoreStatsRequest();
			return;
		}
		else
			return;
		
		// Signal a write opportunity
		SignalXWriteOpportunity( mmxwo );
	}
	else if ( !Q_stricmp( szName, "Client::CmdKeyValues" ) )
	{
		KeyValues *pCmd = pEvent->GetFirstTrueSubKey();
		if ( !pCmd )
			return;

		int nSlot = pEvent->GetInt( "slot" );
		int iCtrlr = XBX_GetUserId( nSlot );
		IPlayerLocal *pPlayerLocal = GetLocalPlayer( iCtrlr );

		char const *szCmd = pCmd->GetName();
		if ( !Q_stricmp( "write_awards", szCmd ) )
		{
			if ( pPlayerLocal )
			{
				pPlayerLocal->UpdateAwardsData( pCmd );
			}
			else
			{
				DevWarning( "pPlayerLocal(#%d)->write_awards UNKNOWN SLOT!\n", nSlot );
			}
		}
		else if ( !Q_stricmp( "read_awards", szCmd ) )
		{
			KeyValues *kvReply = pCmd->MakeCopy();
			if ( pPlayerLocal )
			{
				pPlayerLocal->GetAwardsData( kvReply );
			}
			else
			{
				DevWarning( "pPlayerLocal(#%d)->read_awards UNKNOWN SLOT!\n", nSlot );
			}

			// Send the reply to server
			int nActiveSlot = g_pMatchExtensions->GetIVEngineClient()->GetActiveSplitScreenPlayerSlot();
			g_pMatchExtensions->GetIVEngineClient()->SetActiveSplitScreenPlayerSlot( nSlot );
			g_pMatchExtensions->GetIVEngineClient()->ServerCmdKeyValues( kvReply );
			g_pMatchExtensions->GetIVEngineClient()->SetActiveSplitScreenPlayerSlot( nActiveSlot );
		}
		else if ( !Q_stricmp( "write_stats", szCmd ) )
		{
			if ( pPlayerLocal )
			{
				TitleDataFieldsDescription_t const *pFields = g_pMatchFramework->GetMatchTitle()->DescribeTitleDataStorage();
				for ( KeyValues *kvValue = pCmd->GetFirstValue(); kvValue; kvValue = kvValue->GetNextValue() )
				{
					char const *szStatName = kvValue->GetName();
					// Try to find the stat to write
					if ( TitleDataFieldsDescription_t const *pField = TitleDataFieldsDescriptionFindByString( pFields, szStatName ) )
					{
						// Found the stat to write
						DevMsg( "pPlayerLocal(%s)->write_stat(%s)\n", pPlayerLocal->GetName(), pField->m_szFieldName );
						SyncKeyValueWithStatField( kvValue, pPlayerLocal, pField, KVSTAT_WRITE_STAT );
						szStatName = NULL;
					}
					if ( szStatName )
					{
						DevWarning( "pPlayerLocal(%s)->write_stat(%s) UNKNOWN STAT!\n", pPlayerLocal->GetName(), szStatName );
					}
				}
			}
			else
			{
				DevWarning( "pPlayerLocal(#%d)->write_stat UNKNOWN SLOT!\n", nSlot );
			}
		}
		else if ( !Q_stricmp( "read_stats", szCmd ) )
		{
			KeyValues *kvReply = pCmd->MakeCopy();
			if ( pPlayerLocal )
			{
				TitleDataFieldsDescription_t const *pFields = g_pMatchFramework->GetMatchTitle()->DescribeTitleDataStorage();
				for ( KeyValues *kvValue = kvReply->GetFirstValue(); kvValue; kvValue = kvValue->GetNextValue() )
				{
					char const *szStatName = kvValue->GetName();
					// Try to find the stat to read
					if ( TitleDataFieldsDescription_t const *pField = TitleDataFieldsDescriptionFindByString( pFields, szStatName ) )
					{
						// Found the stat to read
						SyncKeyValueWithStatField( kvValue, pPlayerLocal, pField, KVSTAT_READ_STAT );
						szStatName = NULL;
					}
					if ( szStatName )
					{
						DevWarning( "pPlayerLocal(%s)->read_stat(%s) UNKNOWN STAT!\n", pPlayerLocal->GetName(), szStatName );
					}
				}
			}
			else
			{
				DevWarning( "pPlayerLocal(#%d)->read_stats UNKNOWN SLOT!\n", nSlot );
			}

			// Send the reply to server
			int nActiveSlot = g_pMatchExtensions->GetIVEngineClient()->GetActiveSplitScreenPlayerSlot();
			g_pMatchExtensions->GetIVEngineClient()->SetActiveSplitScreenPlayerSlot( nSlot );
			g_pMatchExtensions->GetIVEngineClient()->ServerCmdKeyValues( kvReply );
			g_pMatchExtensions->GetIVEngineClient()->SetActiveSplitScreenPlayerSlot( nActiveSlot );
		}
		else if ( !Q_stricmp( "write_leaderboard", szCmd ) )
		{
			if ( pPlayerLocal )
			{
				pPlayerLocal->UpdateLeaderboardData( pCmd );
			}
			else
			{
				DevWarning( "pPlayerLocal(#%d)->write_leaderboard UNKNOWN SLOT!\n", nSlot );
			}
		}
		else if ( !Q_stricmp( "read_leaderboard", szCmd ) )
		{
			KeyValues *kvReply = pCmd->MakeCopy();
			if ( pPlayerLocal )
			{
				pPlayerLocal->GetLeaderboardData( kvReply );
			}
			else
			{
				DevWarning( "pPlayerLocal(#%d)->read_leaderboard UNKNOWN SLOT!\n", nSlot );
			}
			
			// Send the reply to server
			int nActiveSlot = g_pMatchExtensions->GetIVEngineClient()->GetActiveSplitScreenPlayerSlot();
			g_pMatchExtensions->GetIVEngineClient()->SetActiveSplitScreenPlayerSlot( nSlot );
			g_pMatchExtensions->GetIVEngineClient()->ServerCmdKeyValues( kvReply );
			g_pMatchExtensions->GetIVEngineClient()->SetActiveSplitScreenPlayerSlot( nActiveSlot );
		}
	}
	else if ( !Q_stricmp( szName, "OnProfileLeaderboardData" ) )
	{
		if ( !g_pMatchExtensions->GetIVEngineClient()->IsConnected() )
			return;

		int iPlayerSlot = 0;

		// Send the leaderboard data to server
		int nActiveSlot = g_pMatchExtensions->GetIVEngineClient()->GetActiveSplitScreenPlayerSlot();
		g_pMatchExtensions->GetIVEngineClient()->SetActiveSplitScreenPlayerSlot( iPlayerSlot );
		KeyValues *kvForServer = pEvent->MakeCopy();
		kvForServer->SetName( "read_leaderboard" );
		g_pMatchExtensions->GetIVEngineClient()->ServerCmdKeyValues( kvForServer );
		g_pMatchExtensions->GetIVEngineClient()->SetActiveSplitScreenPlayerSlot( nActiveSlot );
	}
}

bool IsUserSignedInProperly( int iCtrlr )
{
	return true;
}

void PlayerManager::OnSigninChange( KeyValues *pEvent )
{
}

void PlayerManager::OnLostConnectionToConsoleNetwork()
{
	EnableFriendsUpdate( m_bUpdateEnabled );

	if ( IMatchSession *pIMatchSession = g_pMatchFramework->GetMatchSession() )
	{
		char const *szNetwork = pIMatchSession->GetSessionSettings()->GetString( "system/network", "LIVE" );
		if ( !Q_stricmp( szNetwork, "LIVE" ) )
		{
			// There is an active LIVE session
			g_pMatchFramework->CloseSession();
			g_pMatchEventsSubscription->BroadcastEvent( new KeyValues( "OnEngineDisconnectReason", "reason", "Lost connection to LIVE" ) );
		}
	}
}

void PlayerManager::OnGameUsersChanged()
{
	DevMsg( "PlayerManager::OnGameUsersChanged\n" );

	//
	// Cleanup all players currently created
	//
	for ( int k = 0; k < mFriendsList.Count(); ++ k )
	{
		PlayerFriend *&player = mFriendsList[ k ];
		if ( player )
			player->Destroy();
		player = NULL;
	}
	mFriendsList.RemoveAll();
	for ( int k = 0; k < ARRAYSIZE( mLocalPlayer ); ++ k )
	{
		PlayerLocal *&player = mLocalPlayer[k];
		if ( player )
			player->Destroy();
		player = NULL;
	}

	#if !defined( NO_STEAM )
	if ( !steamapicontext->SteamUser() )
		return;
	#endif

	PlayerLocal * player = new PlayerLocal( 0 );
	mLocalPlayer[0] = player;

	// Start a search when the sign-on changes
	EnableFriendsUpdate( true );

#if !defined( NO_STEAM )
	Update(); // Update immediately to start friends search
	Update(); // Update one more time to actually pick up friends
#endif
}

void PlayerManager::RecomputePlayerXUIDs( char const *szNetwork )
{
	for ( int k = 0; k < ARRAYSIZE( mLocalPlayer ); ++ k )
	{
		PlayerLocal *player = mLocalPlayer[k];
		if ( player )
		{
			player->RecomputeXUID( szNetwork );
		}
	}
}

void PlayerManager::RequestStoreStats()
{
	m_bRequestStoreStats = true;
}

void PlayerManager::ExecuteStoreStatsRequest()
{
	if ( !m_bRequestStoreStats )
		return;

	m_bRequestStoreStats = false;

#ifndef NO_STEAM
	if ( steamapicontext->SteamUserStats() )
	{
		steamapicontext->SteamUserStats()->StoreStats();
	}
#endif
}

void PlayerManager::EnableFriendsUpdate( bool bEnable )
{
	if ( bEnable &&
		!IsX360() && ( g_pMatchFramework->GetMatchTitle()->GetTitleSettingsFlags() & MATCHTITLE_PLAYERMGR_DISABLED ) ) // On X360 system link games still must use lan probes
		bEnable = false;

	m_bUpdateEnabled = bEnable;
	m_flNextUpdateTime = 0.0f;

	m_lanSearchData.m_flStartTime = 0.0f;
	m_lanSearchData.m_flLastBroadcastTime = 0.0f;

	// If enabled the search, then we'll pick it up next frame
	if ( bEnable )
		return;

	// Otherwise searches are disabled, cancel everything
	// TODO: cancel
}

void PlayerManager::CreateLanSearch()
{
	if ( !IsX360() && ( g_pMatchFramework->GetMatchTitle()->GetTitleSettingsFlags() & MATCHTITLE_PLAYERMGR_DISABLED ) )
		return;

	if ( !m_lanSearchData.m_bSearchInProgress )
	{
		m_lanSearchData.m_bSearchInProgress = true;
		++ m_searchesPending;
	}

	m_lanSearchData.m_flStartTime = 0.0f;
	m_lanSearchData.m_flLastBroadcastTime = 0.0f;
}

void PlayerManager::CreateFriendEnumeration( int iCtrlr )
{
	SFriendSearchData &data = m_searchData[ iCtrlr ];

	if ( g_pMatchFramework->GetMatchTitle()->GetTitleSettingsFlags() & MATCHTITLE_PLAYERMGR_DISABLED )
		return;

	if ( data.mSearchInProgress )
		return;

	// We need to look at all friends
	#if !defined( NO_STEAM )
	if ( !steamapicontext->SteamFriends() )
		return;
	#endif

	data.mSearchInProgress = true;
	++ m_searchesPending;

}

#else // SWDS

class PlayerManager *g_pPlayerManager = NULL;

#endif
