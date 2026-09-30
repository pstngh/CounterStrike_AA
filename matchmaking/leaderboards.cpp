//========= Copyright © 1996-2009, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#include "mm_framework.h"

#include "leaderboards.h"

#include "fmtstr.h"

// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"

//
// Definition of leaderboard request queue class
//

class CLeaderboardRequestQueue : public ILeaderboardRequestQueue
{
public:
	CLeaderboardRequestQueue();
	~CLeaderboardRequestQueue();

	// ILeaderboardRequestQueue
public:
	virtual void Request( KeyValues *pRequest );
	virtual void Update();

public:
	KeyValues * GetFinishedRequest();

protected:
	CUtlVector< KeyValues * > m_arrRequests;
	bool m_bQueryRunning;
	KeyValues *m_pFinishedRequest;

	void OnStartNewQuery();
	void OnSubmitQuery();
	void OnQueryFinished();
	void Cleanup();

protected:
#if !defined( NO_STEAM )

	KeyValues *m_pViewDescription;

	CCallResult< CLeaderboardRequestQueue, LeaderboardFindResult_t > m_CallbackOnLeaderboardFindResult;
	void Steam_OnLeaderboardFindResult( LeaderboardFindResult_t *p, bool bError );

	CCallResult< CLeaderboardRequestQueue, LeaderboardScoresDownloaded_t > m_CallbackOnLeaderboardScoresDownloaded;
	void Steam_OnLeaderboardScoresDownloaded( LeaderboardScoresDownloaded_t *p, bool bError );

	void ProcessResults( LeaderboardEntry_t const &lbe );

#endif
};
CLeaderboardRequestQueue g_LeaderboardRequestQueue;
ILeaderboardRequestQueue *g_pLeaderboardRequestQueue = &g_LeaderboardRequestQueue;




//
// Implementation of leaderboard request queue class
//

CLeaderboardRequestQueue::CLeaderboardRequestQueue() :
	m_bQueryRunning( false ),
#if !defined( NO_STEAM )
	m_pViewDescription( NULL ),
#endif
	m_pFinishedRequest( NULL )
{
}

CLeaderboardRequestQueue::~CLeaderboardRequestQueue()
{
	Cleanup();
}

void CLeaderboardRequestQueue::Request( KeyValues *pRequest )
{
	if ( !pRequest )
		return;

	DevMsg( "CLeaderboardRequestQueue::Request\n" );
	KeyValuesDumpAsDevMsg( pRequest, 1 );

	m_arrRequests.AddToTail( pRequest->MakeCopy() );
}

void CLeaderboardRequestQueue::Update()
{
	if ( m_bQueryRunning )
	{
	}
	else if ( m_arrRequests.Count() )
	{
		OnStartNewQuery();
	}
	else if ( m_arrRequests.NumAllocated() )
	{
		Cleanup();
	}
}

KeyValues * CLeaderboardRequestQueue::GetFinishedRequest()
{
	return m_pFinishedRequest;
}

void CLeaderboardRequestQueue::OnQueryFinished()
{

	// Query is no longer running
	m_bQueryRunning = false;

	DevMsg( "CLeaderboardRequestQueue::OnQueryFinished\n" );
	KeyValuesDumpAsDevMsg( m_pFinishedRequest, 1 );

	// Stuff the data into the players
	int iCtrlr = XBX_GetPrimaryUserId();
	{
		XUID xuid = g_pPlayerManager->GetLocalPlayer( iCtrlr )->GetXUID();

		KeyValues *pUserViews = NULL;
		if ( xuid )
			pUserViews = m_pFinishedRequest->FindKey( CFmtStr( "%llx", xuid ) );

		if ( pUserViews )
		{
			IPlayerLocal *pPlayer = g_pPlayerManager->GetLocalPlayer( iCtrlr );
			if ( pPlayer )
				(( PlayerLocal * ) pPlayer)->OnLeaderboardRequestFinished( pUserViews );
		}

	}
}

void CLeaderboardRequestQueue::Cleanup()
{
#if !defined( NO_STEAM )
	if ( m_pViewDescription )
		m_pViewDescription->deleteThis();
	m_pViewDescription = NULL;
#endif

	// Clear requests
	while ( m_arrRequests.Count() )
	{
		m_arrRequests.Head()->deleteThis();
		m_arrRequests.FastRemove( 0 );
	}

	m_arrRequests.Purge();

	// Clear finished result
	if ( m_pFinishedRequest )
		m_pFinishedRequest->deleteThis();
	m_pFinishedRequest = NULL;
}

void CLeaderboardRequestQueue::OnStartNewQuery()
{
	// When we are starting a new query we need to get rid of the old finished result
	if ( m_pFinishedRequest )
		m_pFinishedRequest->deleteThis();
	m_pFinishedRequest = NULL;

#if !defined( NO_STEAM ) && !defined( SWDS )
	extern CInterlockedInt g_numSteamLeaderboardWriters;
	if ( g_numSteamLeaderboardWriters )
		return; // yield to writers that can alter the leaderboard
#endif

	DevMsg( "CLeaderboardRequestQueue::OnStartNewQuery preparing request...\n" );

#if !defined( NO_STEAM )
	// Clear view descriptions
	if ( m_pViewDescription )
		m_pViewDescription->deleteThis();
	m_pViewDescription = NULL;

	for ( int q = 0; q < m_arrRequests.Count(); ++ q )
	{
		KeyValues *pRequest = m_arrRequests[q];
		KeyValues::AutoDelete autodelete_pRequest( pRequest );
		m_arrRequests.Remove( q -- );

		char const *szViewName = pRequest->GetName();

		m_pViewDescription = g_pMMF->GetMatchTitle()->DescribeTitleLeaderboard( szViewName );
		if ( !m_pViewDescription )
		{
			DevWarning( "   View %s failed to allocate description!\n", szViewName );
			continue;
		}

		m_pViewDescription->SetString( ":name", szViewName );

		SteamAPICall_t hCall = steamapicontext->SteamUserStats()->FindLeaderboard( szViewName );
		m_CallbackOnLeaderboardFindResult.Set( hCall, this, &CLeaderboardRequestQueue::Steam_OnLeaderboardFindResult );
		m_bQueryRunning = true;
		break;
	}
#endif

	// Clean up all the requests in the queue
	DevMsg( "CLeaderboardRequestQueue::OnStartNewQuery - request prepared.\n" );

	// Run the query
	OnSubmitQuery();
}

void CLeaderboardRequestQueue::OnSubmitQuery()
{
}

#if !defined( NO_STEAM )
void CLeaderboardRequestQueue::ProcessResults( LeaderboardEntry_t const &lbe )
{
	/*
	901D41D61DC61					// XUID %llx
	{
		survival_c5m2_park
		{
			:rank		=	923		// uint64
			:rows		=	999		// uint64
			:rating		=	600		// uint64
			besttime	=	600		// uint64
		}
		... more views ...
	}
	... more users ...
	*/
	KeyValues *pViewDesc = m_pViewDescription;
	Assert( pViewDesc );
	if ( !pViewDesc )
	{
		Warning( "LeaderboardRequestQueue: ProcessResults has no view description for view!\n" );
		return;
	}

	char const *szViewName = pViewDesc->GetString( ":name" );
	DevMsg( "    Processing view %s\n", szViewName );

	DevMsg( "        Gamer data loaded: rank=%d, score=%d\n",
		lbe.m_nGlobalRank, lbe.m_nScore );

	// Gamer is present in the leaderboard and should be included in the results
	if ( !m_pFinishedRequest )
		m_pFinishedRequest = new KeyValues( "Leaderboard" );

	// Find or create the view for this gamer
	KeyValues *pUserInfo = m_pFinishedRequest->FindKey( CFmtStr( "%llx/%s",
		g_pPlayerManager->GetLocalPlayer( XBX_GetPrimaryUserId() )->GetXUID(), szViewName ), true );

	// Set user score
	pUserInfo->SetUint64( pViewDesc->GetString( ":score" ), lbe.m_nScore );

	DevMsg( "LeaderboardRequestQueue: ProcessResults finished.\n" );
}
#endif

#if !defined( NO_STEAM )

void CLeaderboardRequestQueue::Steam_OnLeaderboardFindResult( LeaderboardFindResult_t *p, bool bError )
{
	if ( bError || !p->m_bLeaderboardFound )
	{
		DevMsg( "Steam leaderboard was not found.\n" );
		OnQueryFinished();
		return;
	}

	// Download the data
	SteamAPICall_t hCall = steamapicontext->SteamUserStats()->DownloadLeaderboardEntries( p->m_hSteamLeaderboard,
		k_ELeaderboardDataRequestGlobalAroundUser, 0, 0 );
	m_CallbackOnLeaderboardScoresDownloaded.Set( hCall, this, &CLeaderboardRequestQueue::Steam_OnLeaderboardScoresDownloaded );
}

void CLeaderboardRequestQueue::Steam_OnLeaderboardScoresDownloaded( LeaderboardScoresDownloaded_t *p, bool bError )
{
	// Fetch the data if found and no error
	LeaderboardEntry_t lbe;
	if ( !bError &&
		p->m_cEntryCount == 1 &&
		steamapicontext->SteamUserStats()->GetDownloadedLeaderboardEntry( p->m_hSteamLeaderboardEntries, 0, &lbe, NULL, 0 ) )
	{
		ProcessResults( lbe );
	}

	OnQueryFinished();
}

#endif


