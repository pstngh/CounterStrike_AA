//===== Copyright © 1996-2009, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#include "mm_framework.h"

#include "fmtstr.h"
#include "vstdlib/random.h"

#include "protocol.h"
#include "proto_oob.h"
#include "bitbuf.h"
#include "checksum_crc.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar mm_dedicated_allow( "mm_dedicated_allow", "1", FCVAR_DEVELOPMENTONLY, "1 = allow searches for dedicated servers" );
ConVar mm_dedicated_fake( "mm_dedicated_fake", "0", FCVAR_DEVELOPMENTONLY, "1 = pretend like search is going, but abort after some time" );
ConVar mm_dedicated_force_servers( "mm_dedicated_force_servers", "", FCVAR_RELEASE,
								   "Comma delimited list of ip:port of servers used to search for dedicated servers instead of searching for public servers.\n"
								   "Use syntax `publicip1:port|privateip1:port,publicip2:port|privateip2:port` if your server is behind NAT.\n"
								   "If the server is behind NAT, you can specify `0.0.0.0|privateip:port` and if server port is in the list of `mm_server_search_lan_ports` its public address should be automatically detected." );
ConVar mm_dedicated_ip( "mm_dedicated_ip", "", FCVAR_DEVELOPMENTONLY, "IP address of dedicated servers to consider available" );
ConVar mm_dedicated_timeout_request( "mm_dedicated_timeout_request", "20", FCVAR_DEVELOPMENTONLY );
ConVar mm_dedicated_search_maxping( "mm_dedicated_search_maxping", IsX360() ? "200" : "150", FCVAR_RELEASE | FCVAR_ARCHIVE, "Longest preferred ping to dedicated servers for games", true, 25.f, true, 350.f );
ConVar mm_dedicated_search_maxresults( "mm_dedicated_search_maxresults", "75", FCVAR_DEVELOPMENTONLY );

extern ConVar mm_dedicated_xlsp_timeout;

CDsSearcher::CDsSearcher( KeyValues *pSettings, uint64 uiReserveCookie, IMatchSession *pMatchSession, uint64 ullCrypt ) :
	m_pSettings( pSettings ),
	m_uiReserveCookie( uiReserveCookie ),
	m_pReserveSettings( g_pMatchFramework->GetMatchNetworkMsgController()->PackageGameDetailsForReservation( m_pSettings ) ),
	m_autodelete_pReserveSettings( m_pReserveSettings ),
#if !defined( NO_STEAM )
	m_pServerListListener( NULL ),
	m_nSearchPass( 0 ),
#endif
	m_eState( STATE_INIT ),
	m_flTimeout( 0.0f ),
	m_pAsyncOperation( NULL ),
	m_pMatchSession( pMatchSession ),
	m_ullCrypt( ullCrypt )
{

	DevMsg( "Created DS searcher\n" );
	KeyValuesDumpAsDevMsg( m_pSettings );

	memset( &m_Result, 0, sizeof( m_Result ) );

	// Build the reservation settings

	// Load test
	m_bLoadTest = (m_pSettings->GetInt( "options/sv_load_test", 0) == 1);
}

CDsSearcher::~CDsSearcher()
{
	;
}

void CDsSearcher::Update()
{
	switch ( m_eState )
	{
	case STATE_INIT:
		{
			char const *szNetwork = m_pSettings->GetString( "system/network", "" );
			char const *szServer = m_pSettings->GetString( "options/server", "listen" );
			if ( m_pSettings->GetString( "server/server", NULL ) )
			{
				InitWithKnownServer();
			}
			else if ( mm_dedicated_allow.GetBool() &&
				!Q_stricmp( "LIVE", szNetwork ) &&
				Q_stricmp( "listen", szServer ) &&
				!( g_pMatchFramework->GetMatchTitle()->GetTitleSettingsFlags() & MATCHTITLE_SETTING_NODEDICATED ) )
			{
				InitDedicatedSearch();
			}
			else
			{
				m_eState = STATE_FINISHED;
			}
		}
		break;

	case STATE_WAITING:
		{
			if ( Plat_FloatTime() > m_flTimeout )
				m_eState = STATE_FINISHED;
		}
		break;

#if !defined( NO_STEAM )

	case STATE_STEAM_REQUESTING_SERVERS:
		if ( Plat_FloatTime() > m_flTimeout )
		{
			DevWarning( "Steam search for dedicated servers timed out.\n" );
			Steam_OnDedicatedServerListFetched();
		}
		break;

	case STATE_STEAM_NEXT_SEARCH_PASS:
		Steam_SearchPass();
		break;

#endif
	}
}

void CDsSearcher::OnEvent( KeyValues *pEvent )
{
	char const *szEvent = pEvent->GetName();

#if !defined( NO_STEAM )
	szEvent;
#endif
}

void CDsSearcher::Destroy()
{
	if ( m_pAsyncOperation )
	{
		m_pAsyncOperation->Release();
		m_pAsyncOperation = NULL;
	}

#if !defined( NO_STEAM )
	if ( m_pServerListListener )
	{
		m_pServerListListener->Destroy();
		m_pServerListListener = NULL;
	}
#endif

	delete this;
}

bool CDsSearcher::IsFinished()
{
	return m_eState == STATE_FINISHED;
}

CDsSearcher::DsResult_t const & CDsSearcher::GetResult()
{
	return m_Result;
}

void CDsSearcher::DsResult_t::CopyToServerKey( KeyValues *pKvServer, uint64 ullCrypt ) const
{
	Assert( m_bDedicated );
	pKvServer->SetString( "server", "dedicated" );

#if !defined( NO_STEAM )
	if ( char const *szEncrypted = MatchSession_EncryptAddressString( m_szPublicConnectionString, ullCrypt ) )
		pKvServer->SetString( "adronline", szEncrypted );
	else
		pKvServer->SetString( "adronline", m_szPublicConnectionString );
	
	if ( m_szPrivateConnectionString[0] )
	{
		if ( char const *szEncrypted = MatchSession_EncryptAddressString( m_szPrivateConnectionString, ullCrypt ) )
			pKvServer->SetString( "adrlocal", szEncrypted );
		else
			pKvServer->SetString( "adrlocal", m_szPrivateConnectionString );
	}
#endif
}

//
// Implementation
//

void CDsSearcher::InitDedicatedSearch()
{
	if ( mm_dedicated_fake.GetBool() )
	{
		// Special fake of the search - it just spins for some time and
		// pretends like it was aborted
		m_flTimeout = Plat_FloatTime() + mm_dedicated_timeout_request.GetFloat();
		m_eState = STATE_WAITING;
		m_Result.m_bAborted = true;
		return;
	}

#if !defined( NO_STEAM )
	m_flTimeout = Plat_FloatTime() + mm_dedicated_timeout_request.GetFloat();
	Steam_SearchPass();
#endif
}

void CDsSearcher::InitWithKnownServer()
{
#if !defined( NO_STEAM )
	if ( m_pSettings->GetInt( "server/reserved" ) )
	{
		m_Result.m_bDedicated = true;
		
		char const *szAdrOnline = m_pSettings->GetString( "server/adronline", "" );
		if ( char const *szDecrypted = MatchSession_DecryptAddressString( szAdrOnline, m_ullCrypt ) )
			szAdrOnline = szDecrypted;
		Q_strncpy( m_Result.m_szConnectionString, szAdrOnline, ARRAYSIZE( m_Result.m_szConnectionString ) );
		Q_strncpy( m_Result.m_szPublicConnectionString, szAdrOnline, ARRAYSIZE( m_Result.m_szPublicConnectionString ) );

		char const *szAdrLocal = m_pSettings->GetString( "server/adrlocal", "" );
		if ( char const *szDecrypted = MatchSession_DecryptAddressString( szAdrLocal, m_ullCrypt ) )
			szAdrLocal = szDecrypted;
		Q_strncpy( m_Result.m_szPrivateConnectionString, szAdrLocal, ARRAYSIZE( m_Result.m_szPrivateConnectionString ) );

		m_eState = STATE_FINISHED;
	}
	else
	{
		DsServer_t dsResult(
			m_pSettings->GetString( "server/adronline", "" ),
			m_pSettings->GetString( "server/adrlocal", "" ),
			0
			);
		if ( char const *szDecrypted = MatchSession_DecryptAddressString( dsResult.m_szConnectionString, m_ullCrypt ) )
			Q_strncpy( dsResult.m_szConnectionString, szDecrypted, ARRAYSIZE( dsResult.m_szConnectionString ) );
		if ( char const *szDecrypted = MatchSession_DecryptAddressString( dsResult.m_szPrivateConnectionString, m_ullCrypt ) )
			Q_strncpy( dsResult.m_szPrivateConnectionString, szDecrypted, ARRAYSIZE( dsResult.m_szPrivateConnectionString ) );

		m_arrServerList.AddToTail( dsResult );

		ReserveNextServer();
	}
#endif
}

#if !defined( NO_STEAM )

void CDsSearcher::Steam_SearchPass()
{
	if ( mm_dedicated_force_servers.GetString()[0] )
	{
		// if convar is on to force dedicated server choices, pretend we got search results of just those servers
		CSplitString serverList( mm_dedicated_force_servers.GetString(), "," );

		for ( int i = 0; i < serverList.Count(); i++ )
		{
			// Check if the specification has a private IP address
			char const * adrsStrings[2] = { serverList[i], "" };
			if ( char *pchDelim = strchr( serverList[i], '|' ) )
			{
				*( pchDelim ++ ) = 0;
				adrsStrings[1] = pchDelim;
			}

			netadr_t adrsForced[2];
			adrsForced[0].SetFromString( adrsStrings[0] );
			adrsForced[1].SetFromString( adrsStrings[1] );

			// Check if a locally discovered server is known with
			// either public or private address that is being forced
			int numServers = g_pServerManager->GetNumServers();
			for ( int iServer = 0; iServer < numServers; ++ iServer )
			{
				IMatchServer *pMatchServer = g_pServerManager->GetServerByIndex( iServer );
				if ( !pMatchServer )
					continue;

				KeyValues *pServerDetails = pMatchServer->GetGameDetails();
				netadr_t adrsKnown[2];
				char const * adrsStringsKnown[2] = { pServerDetails->GetString( "server/adronline" ), pServerDetails->GetString( "server/adrlocal" ) };
				adrsKnown[0].SetFromString( adrsStringsKnown[0] );
				adrsKnown[1].SetFromString( adrsStringsKnown[1] );

				for ( int iAdrForced = 0; iAdrForced < ARRAYSIZE( adrsForced ); ++ iAdrForced )
				{
					for ( int iAdrKnown = 0; iAdrKnown < ARRAYSIZE( adrsKnown ); ++ iAdrKnown )
					{
						if ( adrsForced[iAdrForced].GetIPHostByteOrder() && adrsKnown[iAdrKnown].GetIPHostByteOrder() &&
							 adrsForced[iAdrForced].CompareAdr( adrsKnown[iAdrKnown] ) )
						{
							if ( !adrsForced[!iAdrForced].GetIPHostByteOrder() )	// user not forcing other address, but we know it
								adrsStrings[!iAdrForced] = adrsStringsKnown[!iAdrKnown];
							goto finished_server_lookup;
						}
					}
				}
			}
			finished_server_lookup:

			m_arrServerList.AddToTail( CDsSearcher::DsServer_t( 
				adrsStrings[0], adrsStrings[1],
				0 ) ); // you get no accurate ping to forced servers
		}

		Steam_OnDedicatedServerListFetched();
		return;
	}

	CUtlVector< MatchMakingKeyValuePair_t > filters;
	filters.EnsureCapacity( 10 );

	// filter by game and require empty server
	filters.AddToTail( MatchMakingKeyValuePair_t( "gamedir", COM_GetModDirectory() ) );
	filters.AddToTail( MatchMakingKeyValuePair_t( "noplayers", "1" ) );

	// Official servers

	bool bNeedOfficialServer = false;
	char const *szServerType = m_pSettings->GetString( "options/server", NULL );
	
	if ( szServerType && !Q_stricmp( szServerType, "official" ) )
	{
		bNeedOfficialServer = true;
		filters.AddToTail( MatchMakingKeyValuePair_t( "white", "1" ) );
	}

	// Allow the game to extend the filters
	if ( KeyValues *pExtra = g_pMMF->GetMatchTitleGameSettingsMgr()->DefineDedicatedSearchKeys( m_pSettings, bNeedOfficialServer, m_nSearchPass ) )
	{
		if ( !mm_dedicated_ip.GetString()[0] )
		{
			for ( KeyValues *val = pExtra->GetFirstValue(); val; val = val->GetNextValue() )
			{
				char const *szValue = val->GetString();
				if ( !szValue || !*szValue )
					continue;
				filters.AddToTail( MatchMakingKeyValuePair_t( val->GetName(), szValue ) );
			}
		}
		pExtra->deleteThis();
	}

	// Load test
	if (m_bLoadTest && m_nSearchPass == 0)
	{
		// Add to the "gametype" filter
		for ( int i=0; i < filters.Count(); i++ )
		{
			MatchMakingKeyValuePair_t *pKV = &(filters[i]);
			if ( !Q_stricmp( pKV->m_szKey, "gametype" ) )
			{
				Q_strncat( pKV->m_szValue, ",sv_load_test", sizeof(pKV->m_szValue) );
				break;
			}
		}
	}

	// request the server list.  We will get called back at ServerResponded, ServerFailedToRespond, and RefreshComplete
	m_eState = STATE_STEAM_REQUESTING_SERVERS;
	m_pServerListListener = new CServerListListener( this, filters );
	++m_nSearchPass;
}

CDsSearcher::CServerListListener::CServerListListener( CDsSearcher *pDsSearcher, CUtlVector< MatchMakingKeyValuePair_t > &filters ) :
	m_pOuter( pDsSearcher ),
	m_hRequest( NULL )
{
	MatchMakingKeyValuePair_t *pFilter = filters.Base();
	DevMsg( 1, "Requesting dedicated server list...\n" );
	for (int i = 0; i < filters.Count(); i++)
	{
		DevMsg("Filter %d: %s=%s\n", i, filters.Element(i).m_szKey, filters.Element(i).m_szValue);
	}
	m_hRequest = steamapicontext->SteamMatchmakingServers()->RequestInternetServerList(
		( AppId_t ) g_pMatchFramework->GetMatchTitle()->GetTitleID(), &pFilter,
		filters.Count(), this );
}

void CDsSearcher::CServerListListener::Destroy()
{
	m_pOuter = NULL;
	
	if ( m_hRequest )
		steamapicontext->SteamMatchmakingServers()->ReleaseRequest( m_hRequest );
	m_hRequest = NULL;

	delete this;
}

void CDsSearcher::CServerListListener::ServerResponded( HServerListRequest hReq, int iServer )
{
	gameserveritem_t *pServer = steamapicontext->SteamMatchmakingServers()
		->GetServerDetails( hReq, iServer );

	Msg( "[MM] Server responded '%s', dist %d\n",
		pServer->m_NetAdr.GetConnectionAddressString(), pServer->m_nPing );

	// Check if the server IP address matches
	char const *szDedicatedIp = mm_dedicated_ip.GetString();
	if ( szDedicatedIp && szDedicatedIp[0] )
	{
		char const *szServerConnString = pServer->m_NetAdr.GetConnectionAddressString();
		szServerConnString = StringAfterPrefix( szServerConnString, szDedicatedIp );
		if ( !szServerConnString ||
			( szServerConnString[0] &&
			  szServerConnString[0] != ':' ) )
		{
			DevMsg( "    rejected dedicated server '%s' due to ip filter '%s'\n",
				pServer->m_NetAdr.GetConnectionAddressString(), szDedicatedIp );
			return;
		}
	}

	// Register the dedicated server as acceptable
	// netadr_t adrPublic, adrPrivate;
	// adrPublic.SetFromString( pServer->m_NetAdr.GetConnectionAddressString() );
	// TODO: bool bHasPrivate = FindLANServerPrivateIPByPublicIP( adrPublic, adrPrivate );
	
	// Don't reserve servers with human players playing
	int nHumanPlayers = pServer->m_nPlayers - pServer->m_nBotPlayers;
	if ( nHumanPlayers > 0 )
		return;

	// Don't reserve servers with too high ping
	if ( ( mm_dedicated_search_maxping.GetInt() > 0 ) && ( pServer->m_nPing > mm_dedicated_search_maxping.GetInt() ) )
		return;

	// Register the result
	if ( m_pOuter )
	{
		// See if maybe we know about a private address for this server
		char const *szPrivateAddress = "";
		netadr_t adrPublic;
		adrPublic.SetFromString( pServer->m_NetAdr.GetConnectionAddressString() );

		XUID xuidOnline = uint64( adrPublic.GetIPNetworkByteOrder() ) | ( uint64( adrPublic.GetPort() ) << 32ull );
		if ( IMatchServer *pMatchServer = g_pServerManager->GetServerByOnlineId( xuidOnline ) )
		{
			szPrivateAddress = pMatchServer->GetGameDetails()->GetString( "server/adrlocal" );
		}

			m_pOuter->m_arrServerList.AddToTail( CDsSearcher::DsServer_t(
				pServer->m_NetAdr.GetConnectionAddressString(),
				szPrivateAddress,
				pServer->m_nPing ) );

			if ( m_pOuter->m_arrServerList.Count() > mm_dedicated_search_maxresults.GetInt() ||
				 Plat_FloatTime() > m_pOuter->m_flTimeout )
			{
				steamapicontext->SteamMatchmakingServers()->CancelQuery( hReq );
			}
		}
	}

void CDsSearcher::CServerListListener::RefreshComplete( HServerListRequest hReq, EMatchMakingServerResponse response )
{
	if ( m_pOuter )
	{
		m_pOuter->Steam_OnDedicatedServerListFetched();
	}
}

void CDsSearcher::Steam_OnDedicatedServerListFetched()
{
	if ( m_bLoadTest && m_nSearchPass < 2 )
	{
		m_eState = STATE_INIT;
	}
	else
	{
		if ( m_pServerListListener )
		{
			m_pServerListListener->Destroy();
			m_pServerListListener = NULL;
		}

		DevMsg( 1, "Dedicated server list fetched %d servers.\n", m_arrServerList.Count() );

		ReserveNextServer();
	}
}

#endif


void CDsSearcher::ReserveNextServer()
{
	m_eState = STATE_RESERVING;

#if !defined( NO_STEAM )

	if ( !m_arrServerList.Count() )
	{
		if ( Plat_FloatTime() < m_flTimeout )
		{
			m_eState = STATE_STEAM_NEXT_SEARCH_PASS;
			return;
		}
		else
		{
			m_eState = STATE_FINISHED;
			return;
		}
	}

	DsServer_t dss = m_arrServerList.Head();
	m_arrServerList.RemoveMultipleFromHead( 1 );

	Q_strncpy( m_Result.m_szPublicConnectionString, dss.m_szConnectionString, ARRAYSIZE( m_Result.m_szPublicConnectionString ) );
	Q_strncpy( m_Result.m_szPrivateConnectionString, dss.m_szPrivateConnectionString, ARRAYSIZE( m_Result.m_szPrivateConnectionString ) );

	netadr_t addrPublic, addrPrivate;
	
	addrPublic.SetFromString( dss.m_szConnectionString );
	
	if ( dss.m_szPrivateConnectionString[0] )
		addrPrivate.SetFromString( dss.m_szPrivateConnectionString );
	else
		addrPrivate.SetType( NA_NULL );

#else

	netadr_t addrPublic, addrPrivate;
	
#endif

	g_pMatchExtensions->GetINetSupport()->ReserveServer( addrPublic, addrPrivate,
		m_uiReserveCookie, m_pReserveSettings,
		this, &m_pAsyncOperation );
}

void CDsSearcher::OnOperationFinished( IMatchAsyncOperation *pOperation )
{
	Assert( pOperation == m_pAsyncOperation );
	
	uint64 uiResult = m_pAsyncOperation->GetResult();
	if ( m_pAsyncOperation->GetState() == AOS_FAILED || !uiResult )
	{
		ReserveNextServer();
	}
	else
	{
		m_Result.m_bDedicated = true;
		char const *szReservedAddr = ( char const * )( int )uiResult;
		Q_strncpy( m_Result.m_szConnectionString, szReservedAddr, ARRAYSIZE( m_Result.m_szConnectionString ) );

		// If this server reservation reported number of game slots then
		// force that setting into session data
		uint32 numGameSlots = uint32( pOperation->GetResultExtraInfo() & 0xFF );
		if ( m_pMatchSession && ( numGameSlots > 0 ) )
		{
			KeyValues *kvUpdate = new KeyValues( "update" );
			KeyValues::AutoDelete autodelete( kvUpdate );
			kvUpdate->SetInt( "update/members/numSlots", numGameSlots );

			m_pMatchSession->UpdateSessionSettings( kvUpdate );
		}
	
		m_eState = STATE_FINISHED;
	}
}

