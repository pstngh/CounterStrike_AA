//===== Copyright � 1996-2009, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#include "mm_framework.h"

#include "vstdlib/random.h"
#include "fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar mm_session_search_num_results( "mm_session_search_num_results", "10", FCVAR_DEVELOPMENTONLY );
static ConVar mm_session_search_distance( "mm_session_search_distance", "1", FCVAR_DEVELOPMENTONLY );
ConVar mm_session_search_qos_timeout( "mm_session_search_qos_timeout", "15.0", FCVAR_RELEASE );
// static ConVar mm_session_search_ping_limit( "mm_session_search_ping_limit", "200", FCVAR_RELEASE ); -- removed, using mm_dedicated_search_maxping instead
static ConVar mm_session_search_ping_buckets( "mm_session_search_ping_buckets", "4", FCVAR_RELEASE );

CMatchSearcher::CMatchSearcher( KeyValues *pSettings ) :
	m_pSettings( pSettings ), // takes ownership
	m_autodelete_pSettings( m_pSettings ),
	m_pSessionSearchTree( NULL ),
	m_autodelete_pSessionSearchTree( m_pSessionSearchTree ),
	m_pSearchPass( NULL ),
	m_eState( STATE_INIT )
{

	DevMsg( "Created CMatchSearcher:\n" );
	KeyValuesDumpAsDevMsg( m_pSettings, 1 );

	InitializeSettings();
}

CMatchSearcher::~CMatchSearcher()
{
	DevMsg( "Destroying CMatchSearcher:\n" );
	KeyValuesDumpAsDevMsg( m_pSettings, 1 );

	// Free all retrieved game details
	CUtlVector< SearchResult_t > *arrResults[] = { &m_arrSearchResults, &m_arrSearchResultsAggregate };
	for ( int ia = 0; ia < ARRAYSIZE( arrResults ); ++ ia )
	{
		for ( int k = 0; k < arrResults[ia]->Count(); ++ k )
		{
			SearchResult_t &sr = arrResults[ia]->Element( k );
			if ( sr.m_pGameDetails )
			{
				sr.m_pGameDetails->deleteThis();
				sr.m_pGameDetails = NULL;
			}
		}
	}
}

void CMatchSearcher::InitializeSettings()
{
	// Initialize only the settings required to connect...

	if ( KeyValues *kv = m_pSettings->FindKey( "system", true ) )
	{
		KeyValuesAddDefaultString( kv, "network", "LIVE" );
		KeyValuesAddDefaultString( kv, "access", "public" );
	}

	if ( KeyValues *pMembers = m_pSettings->FindKey( "members", true ) )
	{
        int numMachines = pMembers->GetInt( "numMachines", -1 );
		if ( numMachines == -1 )
		{
			numMachines = 1;
			pMembers->SetInt( "numMachines", numMachines );
		}

		int numPlayers = pMembers->GetInt( "numPlayers", -1 );
		if ( numPlayers == -1 )
		{
			numPlayers = 1;		
			pMembers->SetInt( "numPlayers", numPlayers );
		}

		pMembers->SetInt( "numSlots", numPlayers );

		KeyValues *pMachine = pMembers->FindKey( "machine0");

		if ( !pMachine )
		{
			pMachine = pMembers->CreateNewKey();
			pMachine->SetName( "machine0" );

			XUID machineid = g_pPlayerManager->GetLocalPlayer( XBX_GetPrimaryUserId() )->GetXUID();

			pMachine->SetUint64( "id", machineid );
			pMachine->SetUint64( "flags", MatchSession_GetMachineFlags() );
			pMachine->SetInt( "numPlayers", numPlayers );
			pMachine->SetUint64( "dlcmask", g_pMatchFramework->GetMatchSystem()->GetDlcManager()->GetDataInfo()->GetUint64( "@info/installed" ) );
			pMachine->SetString( "tuver", MatchSession_GetTuInstalledString() );
			pMachine->SetInt( "ping", 0 );

			for ( int k = 0; k < numPlayers; ++ k )
			{
				if ( KeyValues *pPlayer = pMachine->FindKey( CFmtStr( "player%d", k ), true ) )
				{
					int iController = 0;
					IPlayerLocal *player = g_pPlayerManager->GetLocalPlayer( iController );

					pPlayer->SetUint64( "xuid", player->GetXUID() );
					pPlayer->SetString( "name", player->GetName() );
				}
			}
		}
	}

	DevMsg( "CMatchSearcher::InitializeGameSettings adjusted settings:\n" );
	KeyValuesDumpAsDevMsg( m_pSettings, 1 );
}

KeyValues * CMatchSearcher::GetSearchSettings()
{
	return m_pSettings;
}

void CMatchSearcher::Destroy()
{
	// Stop the search
	if ( m_eState == STATE_SEARCHING )
	{
	}

#if !defined( NO_STEAM )
	while ( m_arrOutstandingAsyncOperation.Count() > 0 )
	{
		IMatchAsyncOperation *pAsyncOperation = m_arrOutstandingAsyncOperation.Tail();
		m_arrOutstandingAsyncOperation.RemoveMultipleFromTail( 1 );
		if ( pAsyncOperation )
			pAsyncOperation->Release();
	}
#endif

	delete this;
}

void CMatchSearcher::OnSearchEvent( KeyValues *pNotify )
{
	g_pMatchEventsSubscription->BroadcastEvent( pNotify );
}

void CMatchSearcher::Update()
{
	switch ( m_eState )
	{
	case STATE_INIT:
		m_eState = STATE_SEARCHING;

		// Session is searching
		OnSearchEvent( new KeyValues(
			"OnMatchSessionUpdate",
				"state", "progress",
				"progress", "searching"
			) );

		// Kick off the search
		StartSearch();
		break;

	case STATE_SEARCHING:
		// Waiting for session search to complete
		break;


#if !defined (NO_STEAM)
	case STATE_WAITING_LOBBY_DATA_AND_PING:
		{
			bool bWaitLonger = ( ( Plat_MSTime() - m_uiQosTimeoutStartMS ) < ( mm_session_search_qos_timeout.GetFloat() * 1000.f ) );
			if ( bWaitLonger )
			{
				bool bHasPendingLobbyData = false;
				for ( int j = 0; j < m_arrSearchResults.Count(); ++ j )
				{
					SearchResult_t &sr = m_arrSearchResults[j];
					if ( !sr.m_numPlayers )
					{	// didn't even receive lobby data from Steam yet
						bHasPendingLobbyData = true;
					}
					else if ( sr.m_svAdr.GetIPHostByteOrder() && ( sr.m_svPing <= 0 ) )
					{	// received valid IP for lobby, still pinging
						bHasPendingLobbyData = true;
					}
				}
				if ( !bHasPendingLobbyData )
					bWaitLonger = false;
			}
			if ( !bWaitLonger )
			{
				m_CallbackOnLobbyDataReceived.Unregister();

				// Go ahead and start joining the results
				AggregateSearchPassResults();
				OnSearchPassDone( m_pSearchPass );
			}
		}
		break;
#endif
	}
}

#if !defined (NO_STEAM)
void CMatchSearcher::Steam_OnLobbyDataReceived( LobbyDataUpdate_t *pLobbyDataUpdate )
{
	int iResultIndex = 0;
	for ( ; iResultIndex < m_arrSearchResults.Count(); ++ iResultIndex )
	{
		if ( m_arrSearchResults[ iResultIndex ].m_uiLobbyId == pLobbyDataUpdate->m_ulSteamIDLobby )
			break;
	}
	if ( !m_arrSearchResults.IsValidIndex( iResultIndex ) )
		return;

	SearchResult_t *pRes = &m_arrSearchResults[ iResultIndex ];

	if ( !pLobbyDataUpdate->m_bSuccess )
	{
		DevMsg( "[MM] Could not get lobby data for lobby %llu (%llx)\n", 
			pRes->m_uiLobbyId, pRes->m_uiLobbyId );
		pRes->m_numPlayers = -1; // set numPlayers to negative number to indicate a failure here
	}
	else
	{
		// Get num players
		const char *numPlayers = steamapicontext->SteamMatchmaking()->GetLobbyData( 
			pRes->m_uiLobbyId, "members:numPlayers" );
		if ( !numPlayers )
		{
			DevMsg( "[MM] Unable to get num players for lobby (%llx)\n", 
				pRes->m_uiLobbyId );
			pRes->m_numPlayers = -1; // set numPlayers to negative number to indicate a failure here
		}
		else
		{
			pRes->m_numPlayers = V_atoi( numPlayers );
			if ( !pRes->m_numPlayers )
				pRes->m_numPlayers = -1; // set numPlayers to negative number to indicate a failure here
		}

		// Get the address of the server
		const char* pServerAdr = steamapicontext->SteamMatchmaking()->GetLobbyData( 
			pRes->m_uiLobbyId, "server:adronline" );

		if ( !pServerAdr )
		{
// 			DevMsg( "[MM] Unable to get server address from lobby (%llx)\n", 
// 				pRes->m_uiLobbyId );
		}
		else
		{
			pRes->m_svAdr.SetFromString( pServerAdr );
			
			DevMsg ( "[MM] Lobby %d: id %llu (%llx), num Players %d, sv ip %s, pinging dist %d\n", 
				iResultIndex, pRes->m_uiLobbyId, pRes->m_uiLobbyId, pRes->m_numPlayers,
				pRes->m_svAdr.ToString(), pRes->m_svPing );
			
			// Ping server
			IMatchAsyncOperation *pAsyncOperationPing = NULL;
			g_pMatchExtensions->GetINetSupport()->ServerPing( pRes->m_svAdr, this, &pAsyncOperationPing );
			m_arrOutstandingAsyncOperation.AddToTail( pAsyncOperationPing );
			pRes->m_pAsyncOperationPingWeakRef = pAsyncOperationPing;
		}
	}
}

// Callback for server reservation check
void CMatchSearcher::OnOperationFinished( IMatchAsyncOperation *pOperation )
{
	if ( !pOperation )
		return;

	if ( m_eState != STATE_WAITING_LOBBY_DATA_AND_PING )
		return;

	for ( int i = 0; i < m_arrSearchResults.Count(); ++ i )
	{
		if ( m_arrSearchResults[i].m_pAsyncOperationPingWeakRef != pOperation )
			continue;

		int result = pOperation->GetResult();
		bool failed = ( pOperation->GetState() == AOS_FAILED );
		SearchResult_t *pRes = &m_arrSearchResults[i];

		pRes->m_svPing = ( failed ? -1 : ( result ? result : -1 ) );
		if ( pRes->m_svPing < 0 )
		{
			DevMsg( "[MM] Failed pinging server %s for lobby#%d %llu (%llx)\n", 
				pRes->m_svAdr.ToString(), i, pRes->m_uiLobbyId, pRes->m_uiLobbyId );
		}
		else
		{
			DevMsg( "[MM] Successfully pinged server %s for lobby#%d %llu (%llx) = %d ms\n", 
				pRes->m_svAdr.ToString(), i, pRes->m_uiLobbyId, pRes->m_uiLobbyId, pRes->m_svPing );
		}

		m_arrSearchResults[i].m_pAsyncOperationPingWeakRef = NULL;
	}
}
#endif

void CMatchSearcher::OnSearchDone()
{
	m_eState = STATE_DONE;
}

void CMatchSearcher::AggregateSearchPassResults()
{
#ifndef NO_STEAM

	extern ConVar mm_dedicated_search_maxping;
	int nPingLimitMax = mm_dedicated_search_maxping.GetInt();
	if ( nPingLimitMax <= 0 )
		nPingLimitMax = 5000;
	for ( int k = 0; k < mm_session_search_ping_buckets.GetInt(); ++ k )
	{
		int iPingLow = ( nPingLimitMax * k ) / mm_session_search_ping_buckets.GetInt();
		int iPingHigh = ( nPingLimitMax * ( k + 1 ) ) / mm_session_search_ping_buckets.GetInt();
		for ( int iResult = 0; iResult < m_arrSearchResults.Count(); ++ iResult )
		{
			SearchResult_t *pRes = &m_arrSearchResults[iResult];
			if ( ( pRes->m_svPing > iPingLow ) && ( pRes->m_svPing <= iPingHigh ) )
			{
				m_arrSearchResultsAggregate.AddToTail( *pRes );

				DevMsg ( "[MM] Search aggregated result%d / %d: id %llu (%llx), num Players %d, sv ip %s, dist %d\n", 
					m_arrSearchResultsAggregate.Count(), iResult, pRes->m_uiLobbyId, pRes->m_uiLobbyId, pRes->m_numPlayers,
					pRes->m_svAdr.ToString(), pRes->m_svPing );
			}
		}
	}
	DevMsg ( "[MM] Search aggregated %d results\n", m_arrSearchResultsAggregate.Count() );

#endif

	m_arrSearchResults.Purge();

}

void CMatchSearcher::OnSearchPassDone( KeyValues *pSearchPass )
{
	// If we have enough results, then call it done
	if ( m_arrSearchResultsAggregate.Count() >= mm_session_search_num_results.GetInt() )
	{
		OnSearchDone();
		return;
	}

	// Evaluate if there is a nextpass condition
	char const *szNextPassKeyName = "nextpass";
	if ( KeyValues *pConditions = pSearchPass->FindKey( "nextpass?" ) )
	{
		// Inspect conditions and select which next pass will happen
	}

	if ( KeyValues *pNextPass = pSearchPass->FindKey( szNextPassKeyName ) )
	{
		StartSearchPass( pNextPass );
	}
	else
	{
		OnSearchDone();
	}
}



#if !defined( NO_STEAM )

void CMatchSearcher::Steam_OnLobbyMatchListReceived( LobbyMatchList_t *pLobbyMatchList, bool bError )
{
	Msg( "[MM] Received %d search results.\n", bError ? 0 : pLobbyMatchList->m_nLobbiesMatching );

	m_CallbackOnLobbyDataReceived.Register( this, &CMatchSearcher::Steam_OnLobbyDataReceived );

	// Walk through search results and request lobby data
	for ( int iLobby = 0; iLobby < (int) ( bError ? 0 : pLobbyMatchList->m_nLobbiesMatching ); ++ iLobby )
	{		
		uint64 uiLobbyId = steamapicontext->SteamMatchmaking()->GetLobbyByIndex( iLobby ).ConvertToUint64();

		SearchResult_t sr = { uiLobbyId };
		sr.m_pGameDetails = NULL;
		sr.m_svAdr.SetFromString( "0.0.0.0" );
		sr.m_svPing = 0;
		sr.m_numPlayers = 0;
		sr.m_pAsyncOperationPingWeakRef = NULL;
		m_arrSearchResults.AddToTail( sr );

		steamapicontext->SteamMatchmaking()->RequestLobbyData( sr.m_uiLobbyId );
	}

	m_eState = STATE_WAITING_LOBBY_DATA_AND_PING;		// Waiting for lobby data
	m_uiQosPingLastMS = m_uiQosTimeoutStartMS = Plat_MSTime();
}

KeyValues * CMatchSearcher::SearchResult_t::GetGameDetails() const
{
	if ( !m_pGameDetails )
	{
		m_pGameDetails = g_pMatchFramework->GetMatchNetworkMsgController()->UnpackGameDetailsFromSteamLobby( m_uiLobbyId );
	}
	
	return m_pGameDetails;
}

#endif

void CMatchSearcher::StartSearch()
{
	Assert( !m_pSessionSearchTree );
	m_pSessionSearchTree = g_pMMF->GetMatchTitleGameSettingsMgr()->DefineSessionSearchKeys( m_pSettings );
	m_autodelete_pSessionSearchTree.Assign( m_pSessionSearchTree );

	Assert( m_pSessionSearchTree );
	if ( !m_pSessionSearchTree )
	{
		DevWarning( "OnlineSearch::StartSearch failed to build filter list!\n" );
		OnSearchDone();
	}
	else
	{
		StartSearchPass( m_pSessionSearchTree );
	}
}

void CMatchSearcher::StartSearchPass( KeyValues *pSearchPass )
{
	KeyValues *pSearchParams = pSearchPass;
	m_pSearchPass = pSearchPass;

	// Make sure we have fresh buffer for search results
	m_arrSearchResults.Purge();
	m_eState = STATE_SEARCHING;

	DevMsg( "OnlineSearch::StartSearchPass:\n" );
	KeyValuesDumpAsDevMsg( pSearchParams, 1 );

#if !defined( NO_STEAM )

	ISteamMatchmaking *mm = steamapicontext->SteamMatchmaking();

	// Configure filters
	DWORD dwNumSlotsRequired = pSearchParams->GetInt( "numPlayers" );
	mm->AddRequestLobbyListFilterSlotsAvailable( dwNumSlotsRequired );

	// Set filters
	char const * arrKeys[] = { "Filter<", "Filter<=", "Filter=", "Filter<>", "Filter>=", "Filter>" };
	ELobbyComparison nValueCmp[] = { k_ELobbyComparisonLessThan, k_ELobbyComparisonEqualToOrLessThan, k_ELobbyComparisonEqual, k_ELobbyComparisonNotEqual, k_ELobbyComparisonEqualToOrGreaterThan, k_ELobbyComparisonGreaterThan };
	for ( int k = 0; k < ARRAYSIZE( arrKeys ); ++ k )
	{
		if ( KeyValues *kv = pSearchParams->FindKey( arrKeys[k] ) )
		{
			for ( KeyValues *val = kv->GetFirstValue(); val; val = val->GetNextValue() )
			{
				if ( val->GetDataType() == KeyValues::TYPE_STRING )
				{
					mm->AddRequestLobbyListStringFilter( val->GetName(), val->GetString(), nValueCmp[k] );
				}
				else if ( val->GetDataType() == KeyValues::TYPE_INT )
				{
					mm->AddRequestLobbyListNumericalFilter( val->GetName(), val->GetInt(), nValueCmp[k] );
				}
			}
		}
	}

	// Set ordering near values
	if ( KeyValues *kv = pSearchParams->FindKey( "Near" ) )
	{
		for ( KeyValues *val = kv->GetFirstValue(); val; val = val->GetNextValue() )
		{
			if ( val->GetDataType() == KeyValues::TYPE_INT )
			{
				mm->AddRequestLobbyListNearValueFilter( val->GetName(), val->GetInt() );
			}
			else
			{
				// TODO: need to set near value filter for strings
			}
		}
	}

	ELobbyDistanceFilter eFilter = k_ELobbyDistanceFilterDefault;
	switch ( mm_session_search_distance.GetInt() )
	{
	case k_ELobbyDistanceFilterClose:
	case k_ELobbyDistanceFilterDefault:
	case k_ELobbyDistanceFilterFar:
	case k_ELobbyDistanceFilterWorldwide:
		eFilter = ( ELobbyDistanceFilter ) mm_session_search_distance.GetInt();
		break;
	}
	// Set distance filter to Near
	mm->AddRequestLobbyListDistanceFilter( eFilter );

	// Set dependent lobby
	if ( uint64 uiDependentLobbyId = pSearchParams->GetUint64( "DependentLobby", 0ull ) )
	{
		mm->AddRequestLobbyListCompatibleMembersFilter( uiDependentLobbyId );
	}

	// RequestLobbyList - will get called back at Steam_OnLobbyMatchListReceived
	DevMsg( "Searching...\n" );
	
	SteamAPICall_t hCall = mm->RequestLobbyList();

	m_CallbackOnLobbyMatchListReceived.Set( hCall, this, &CMatchSearcher::Steam_OnLobbyMatchListReceived );

#endif
}


//
// Results retrieval overrides
//

bool CMatchSearcher::IsSearchFinished() const
{
	return m_eState == STATE_DONE;
}

int CMatchSearcher::GetNumSearchResults() const
{
	return IsSearchFinished() ? m_arrSearchResultsAggregate.Count() : 0;
}

CMatchSearcher::SearchResult_t const & CMatchSearcher::GetSearchResult( int idx ) const
{
	if ( !IsSearchFinished() || !m_arrSearchResultsAggregate.IsValidIndex( idx ) )
	{
		Assert( false );
		static SearchResult_t s_empty;
		return s_empty;
	}
	else
	{
		return m_arrSearchResultsAggregate[ idx ];
	}
}
