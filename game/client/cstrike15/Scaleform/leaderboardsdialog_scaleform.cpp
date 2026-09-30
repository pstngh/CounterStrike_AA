//==== Copyright © 1996-2005, Valve Corporation, All rights reserved. =========//
//
// Purpose:
//
//=============================================================================//

#include "cbase.h"
#include "fmtstr.h"

#if defined( INCLUDE_SCALEFORM )

#include "basepanel.h"
#include "leaderboardsdialog_scaleform.h"
#include <vgui/ILocalize.h>


#include "keyvalues.h"
#include "engineinterface.h"
#include "modinfo.h"
#include "gameui_interface.h"

#include "tier1/utlbuffer.h"
#include "filesystem.h"

using namespace vgui;

// for SRC
#include <vstdlib/random.h>

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

#define LEADERBOARD_SHOW_RANK_UNDER		1000

CCreateLeaderboardsDialogScaleform* CCreateLeaderboardsDialogScaleform::m_pInstance = NULL;

SFUI_BEGIN_GAME_API_DEF
	SFUI_DECL_METHOD( OnOk ),
	SFUI_DECL_METHOD( SetQuery ),
	SFUI_DECL_METHOD( Query_NumResults ),
	SFUI_DECL_METHOD( Query_GetCurrentPlayerRow ),
	SFUI_DECL_METHOD( QueryRow_GamerTag ),
	SFUI_DECL_METHOD( QueryRow_ColumnValue ),
	SFUI_DECL_METHOD( QueryRow_ColumnRatio ),
	SFUI_DECL_METHOD( DisplayUserInfo ),
	SFUI_END_GAME_API_DEF( CCreateLeaderboardsDialogScaleform, LeaderBoards )
	;

// For tracking the last panel the user viewed on this screen
ConVar player_last_leaderboards_panel( "player_last_leaderboards_panel", "0",  FCVAR_ARCHIVE | FCVAR_ARCHIVE_GAMECONSOLE | FCVAR_SS, "Last opened panel in the Leaderboards screen" );
ConVar player_last_leaderboards_mode( "player_last_leaderboards_mode", "0",  FCVAR_ARCHIVE | FCVAR_ARCHIVE_GAMECONSOLE | FCVAR_SS, "Last mode setting in the Leaderboards screen" );
ConVar player_last_leaderboards_filter( "player_last_leaderboards_filter", "0",  FCVAR_ARCHIVE | FCVAR_ARCHIVE_GAMECONSOLE | FCVAR_SS, "Last mode setting in the Leaderboards screen" );

const float QUERY_DELAY_TIME = 0.5f;
const float QUERY_SHORT_DELAY_TIME = 0.35f;

// Enable this define for asynch debug output
// #define DIAGNOSE_ASYNCH_PROBLEMS 1

void CCreateLeaderboardsDialogScaleform::LoadDialog( void )
{
	if ( !m_pInstance )
	{
		STEAMWORKS_TESTSECRETALWAYS();

		m_pInstance = new CCreateLeaderboardsDialogScaleform( );
		SFUI_REQUEST_ELEMENT( SF_FULL_SCREEN_SLOT, g_pScaleformUI, CCreateLeaderboardsDialogScaleform, m_pInstance, LeaderBoards );
	}
}

void CCreateLeaderboardsDialogScaleform::UnloadDialog( void )
{
	if ( m_pInstance )
	{
		m_pInstance->RemoveFlashElement();
	}
}

void CCreateLeaderboardsDialogScaleform::UpdateDialog( void )
{
	if ( m_pInstance )
	{
		m_pInstance->Tick();
	}
}

CCreateLeaderboardsDialogScaleform::CCreateLeaderboardsDialogScaleform( void ) :
	m_hAsyncQuery(0),
	m_currentFilterType(eLBFilter_Overall),
	m_startingRowIndex(-1),
	m_rowsPerPage(0),
	m_bCheckForQueryResults(false),
	m_iNumFriends(0),
	m_iNextFriend(-1),
	m_bEnumeratingFriends(false),
	m_bResultsValid(false),
	m_iTotalViewRows(0)
{
	ACTIVE_SPLITSCREEN_PLAYER_GUARD( GET_ACTIVE_SPLITSCREEN_SLOT() );
	m_iPlayerSlot = XBX_GetActiveUserId();

	IPlayerLocal *pProfile = g_pMatchFramework->GetMatchSystem()->GetPlayerManager()->GetLocalPlayer( m_iPlayerSlot );
	if ( pProfile )
		m_PlayerXUID = pProfile->GetXUID();

#if !defined( NO_STEAM )
	m_currentLeaderboardHandle = (SteamLeaderboard_t)0;
	m_currentLeaderboardName = NULL;
	V_memset( &m_cachedLeaderboardScores, 0, sizeof(LeaderboardScoresDownloaded_t) );
	m_pLeaderboardDescription = NULL;
	V_memset( m_payloadSizes, 0, sizeof(int)*kMaxPayloadEntries );

	SetDefLessFunc( m_LeaderboardHandles );
#endif // !NO_STEAM
}

void CCreateLeaderboardsDialogScaleform::OnOk( SCALEFORM_CALLBACK_ARGS_DECL )
{
	CCreateLeaderboardsDialogScaleform::UnloadDialog();
}

void CCreateLeaderboardsDialogScaleform::FlashLoaded( void )
{
	if ( FlashAPIIsValid() )
	{
		m_pScaleformUI->Value_InvokeWithoutReturn( m_FlashAPI, "InitDialogData", NULL, 0 );
	}
}

void CCreateLeaderboardsDialogScaleform::FlashReady( void )
{
	SFVALUE PanelHandle = m_pScaleformUI->Value_GetMember( m_FlashAPI, "ScoreBoard" );

	if ( PanelHandle )
	{
		SFVALUE AnimatedPanelHandle = m_pScaleformUI->Value_GetMember( PanelHandle, "Panel" );

		if ( AnimatedPanelHandle )
		{
			// Grab any references you need now...

			m_pScaleformUI->ReleaseValue( AnimatedPanelHandle );
		}

		m_pScaleformUI->ReleaseValue( PanelHandle );
	}

	m_bCheckForQueryResults = false;
	m_bResultsValid = false;
	m_iNumFriends = 0;
	m_iNextFriend = -1;

	Show();
}

bool CCreateLeaderboardsDialogScaleform::PreUnloadFlash( void )
{
	return true;
}

void CCreateLeaderboardsDialogScaleform::PostUnloadFlash( void )
{
	
#if !defined( NO_STEAM )
	if ( !m_pLeaderboardDescription )
		m_pLeaderboardDescription->deleteThis();
#endif // !NO_STEAM

	if ( GameUI().IsInLevel() )
	{
		BasePanel()->RestorePauseMenu();
	}
	else
	{
		BasePanel()->RestoreMainMenuScreen();
	}

	m_pInstance = NULL;
	delete this;
}

void CCreateLeaderboardsDialogScaleform::Show( void )
{
	if ( FlashAPIIsValid() )
	{
		WITH_SLOT_LOCKED
		{
			ScaleformUI()->Value_InvokeWithoutReturn( m_FlashAPI, "showPanel", 0, NULL );
		}
	}
}

void CCreateLeaderboardsDialogScaleform::Hide( void )
{
	if ( FlashAPIIsValid() )
	{
		WITH_SLOT_LOCKED
		{
			ScaleformUI()->Value_InvokeWithoutReturn( m_FlashAPI, "hidePanel", 0, NULL );
		}
	}
}

void CCreateLeaderboardsDialogScaleform::Tick( void )
{
	if ( FlashAPIIsValid() )
	{
		QueryUpdate();
		CheckForQueryResults();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called from scaleform whenever any of our search parameters change:
//		game mode & type, filter (me/friends/overall), position in board
//-----------------------------------------------------------------------------
void CCreateLeaderboardsDialogScaleform::SetQuery( SCALEFORM_CALLBACK_ARGS_DECL )
{

#if !defined( NO_STEAM )

	// As soon as we change any parameters, cancel any previous queries	
	m_bCheckForQueryResults = false;

	int nIdx = 0;

	int filterType = (int)( pui->Params_GetArgAsNumber( obj, nIdx++ ) );
	m_currentFilterType = (eLeaderboardFiltersType) clamp( filterType, (int)eLBFilter_Overall, (int)eLBFilter_Friends );
	
	m_startingRowIndex = (int)( pui->Params_GetArgAsNumber( obj, nIdx++ ) );
	m_rowsPerPage = (int)( pui->Params_GetArgAsNumber( obj, nIdx++ ) );	

	m_currentLeaderboardName = ( pui->Params_GetArgAsString( obj, nIdx++ ) );

	// DevMsg("  Querying Steam board \"%s\" starting at row %d, %d total rows..\n\n", m_currentLeaderboardName, m_startingRowIndex, m_rowsPerPage );

	// Wait a second before we handle this query, in case user is flicking through options
	m_fQueryDelayTime = Plat_FloatTime() + QUERY_DELAY_TIME;


#endif
}

//-----------------------------------------------------------------------------
// Purpose: Determine how many results we have obtained from our previous query
//		Returns -1 while the query is still pending.
//-----------------------------------------------------------------------------
void CCreateLeaderboardsDialogScaleform::Query_NumResults( SCALEFORM_CALLBACK_ARGS_DECL )
{
	int numResults = 0;

#if !defined( NO_STEAM )
	if ( m_fQueryDelayTime > 0.f )
		numResults = -1; // Query is still in progress; display the "Searching..." msg
	else if ( m_bCheckForQueryResults )
		numResults = ( m_cachedLeaderboardScores.m_cEntryCount );
#endif

	m_pScaleformUI->Params_SetResult( obj, numResults );
}


//-----------------------------------------------------------------------------
// Purpose: Retrieve the row that contains information about the current player
//-----------------------------------------------------------------------------
void CCreateLeaderboardsDialogScaleform::Query_GetCurrentPlayerRow( SCALEFORM_CALLBACK_ARGS_DECL )
{
	int playerRow = 0;

#if !defined (NO_STEAM)
	
	if ( m_currentLeaderboardHandle != 0 && m_cachedLeaderboardScores.m_hSteamLeaderboardEntries != 0 )
	{
		for ( int Idx = 0; Idx < m_cachedLeaderboardScores.m_cEntryCount; Idx++ )
		{
			LeaderboardEntry_t leaderboardEntry;
			if ( steamapicontext->SteamUserStats()->GetDownloadedLeaderboardEntry( m_cachedLeaderboardScores.m_hSteamLeaderboardEntries, Idx, &leaderboardEntry, NULL, 0 ) )
			{
				if ( leaderboardEntry.m_steamIDUser.ConvertToUint64() == m_PlayerXUID )
				{
					playerRow = Idx;
					break;
				}		
			}
		}
	}

#endif  // !NO_STEAM

	m_pScaleformUI->Params_SetResult( obj, playerRow );
}

//-----------------------------------------------------------------------------
// Purpose: Get the name of the user in our results list at the row specified
//-----------------------------------------------------------------------------
void CCreateLeaderboardsDialogScaleform::QueryRow_GamerTag( SCALEFORM_CALLBACK_ARGS_DECL )
{
	int rowNumber = (int)( pui->Params_GetArgAsNumber( obj, 0 ) );

	// We retrieve all friend info as one big list, so offset into that list by the current row we're filling in Flash
	if ( m_currentFilterType == eLBFilter_Friends )
		rowNumber += m_startingRowIndex;

	const char *gamerTag = NULL;

#if !defined( NO_STEAM )

	LeaderboardEntry_t leaderboardEntry;

	if ( steamapicontext->SteamUserStats()->GetDownloadedLeaderboardEntry( m_cachedLeaderboardScores.m_hSteamLeaderboardEntries, rowNumber, &leaderboardEntry, NULL, 0 ) )
	{
		gamerTag = steamapicontext->SteamFriends()->GetFriendPersonaName( leaderboardEntry.m_steamIDUser );
	}

#endif // !NO_STEAM

	if ( !gamerTag )
		gamerTag = "ERROR";

	wchar_t unicodeName[MAX_NETWORKID_LENGTH];
	g_pVGuiLocalize->ConvertANSIToUnicode( gamerTag, unicodeName, sizeof(unicodeName) );

	wchar_t safe_wide_name[MAX_NETWORKID_LENGTH * 10 ]; // add enough room to safely escape all 64 name characters
	safe_wide_name[0] = L'\0';			
	g_pScaleformUI->MakeStringSafe( unicodeName, safe_wide_name, sizeof( safe_wide_name ) );

	SFVALUE sfGamertag = CreateFlashString( safe_wide_name );

	m_pScaleformUI->Params_SetResult( obj, sfGamertag );

	SafeReleaseSFVALUE( sfGamertag );
}

//-----------------------------------------------------------------------------
// Purpose: Get the column value in our results list at the row specified
//-----------------------------------------------------------------------------
void CCreateLeaderboardsDialogScaleform::QueryRow_ColumnValue( SCALEFORM_CALLBACK_ARGS_DECL )
{
	int numArgs = pui->Params_GetNumArgs( obj );

	if ( numArgs < 2 )
	{
		Warning( "QueryRow_ColumnValue: Invalid number of arguments in call to function: %d\n", numArgs );
		return;
	}

	int rowNumber = (int)( pui->Params_GetArgAsNumber( obj, 0 ) );
	
	// We retrieve all friend info as one big list, so offset into that list by the current row we're filling in Flash
	if ( m_currentFilterType == eLBFilter_Friends )
		rowNumber += m_startingRowIndex;

	int columnId = (int)( pui->Params_GetArgAsNumber( obj, 1 ) );

	int columnValue = -1;	
	char largeNumberString[64] = {0}; // if the field is a 64-bit value, we have to retrieve it as a string

#if !defined( NO_STEAM )

	LeaderboardEntry_t leaderboardEntry;

	int lbDetails[kMaxPayloadEntries] = { 0 };
	int numDetails = kMaxPayloadEntries;

	if ( columnId == -3 )
	{
		// -3 retrieves the total number of users ranked on this view
		columnValue = steamapicontext->SteamUserStats()->GetLeaderboardEntryCount( m_currentLeaderboardHandle );
	}
	else if ( columnId == -4 )
	{
		columnValue = ( steamapicontext->SteamUser()->BLoggedOn() ? 1 : 0 );
	}
	else
	if ( m_currentLeaderboardHandle != 0 && 
		 m_cachedLeaderboardScores.m_hSteamLeaderboardEntries != 0 &&
		 steamapicontext->SteamUserStats()->GetDownloadedLeaderboardEntry( m_cachedLeaderboardScores.m_hSteamLeaderboardEntries, rowNumber, &leaderboardEntry, lbDetails, numDetails ) )
	{
		// DevMsg( "Reading from row %d columnId: %d, extra details count = %d.\n", rowNumber, columnId, numDetails );

		if ( columnId == 0 )
		{
			int totalViewRows = steamapicontext->SteamUserStats()->GetLeaderboardEntryCount( m_currentLeaderboardHandle );

			if ( totalViewRows > 0 )
			{
				if ( leaderboardEntry.m_nGlobalRank <= LEADERBOARD_SHOW_RANK_UNDER )
					V_snprintf( largeNumberString, sizeof(largeNumberString), "%d", leaderboardEntry.m_nGlobalRank );
				else
					V_snprintf( largeNumberString, sizeof(largeNumberString), "%.1f%%", MAX( 0.1f, ((float)leaderboardEntry.m_nGlobalRank / (float)totalViewRows) * 100 ));
			}
			else
			{
				// Couldn't locate valid totalViewRows, so just return this row's rank
				if ( leaderboardEntry.m_nGlobalRank <= LEADERBOARD_SHOW_RANK_UNDER )
					columnValue = leaderboardEntry.m_nGlobalRank;
				else
					V_snprintf( largeNumberString, sizeof(largeNumberString), "%s", "--" );
			}
		}
		else if ( columnId == -1 )
		{
			columnValue = leaderboardEntry.m_nScore;
		}
		else if ( columnId == -2 )
		{
			// -2 gets the row's overall board "Ranking / TotalRankedUsers"
			int totalViewRows = steamapicontext->SteamUserStats()->GetLeaderboardEntryCount( m_currentLeaderboardHandle );

			if ( totalViewRows > 0 )
			{
				if ( leaderboardEntry.m_nGlobalRank <= LEADERBOARD_SHOW_RANK_UNDER )
					V_snprintf( largeNumberString, sizeof(largeNumberString), "%d", leaderboardEntry.m_nGlobalRank );
				else
					V_snprintf( largeNumberString, sizeof(largeNumberString), "%.1f%%", MAX( 0.1f, ((float)leaderboardEntry.m_nGlobalRank / (float)totalViewRows) * 100 ));
			}
			else
			{
				// Couldn't locate valid totalViewRows, so just return this row's rank
				if ( leaderboardEntry.m_nGlobalRank <= LEADERBOARD_SHOW_RANK_UNDER )
					columnValue = leaderboardEntry.m_nGlobalRank;
				else
					V_snprintf( largeNumberString, sizeof(largeNumberString), "%s", "--" );
			}
		}
		else if ( columnId == -3 )
		{
			// -3 retrieves the total number of users ranked on this view
			columnValue = steamapicontext->SteamUserStats()->GetLeaderboardEntryCount( m_currentLeaderboardHandle );
		}
		else
		{
			V_snprintf( largeNumberString, sizeof(largeNumberString), "%lld", ExtractPayloadDataByColumnID( lbDetails, columnId-1 ) );
		}
	}

#endif // !NO_STEAM

	if ( largeNumberString[0] != 0 )
	{
		SFVALUE sfNumberString = CreateFlashString( largeNumberString );

		m_pScaleformUI->Params_SetResult( obj, sfNumberString );

		SafeReleaseSFVALUE( sfNumberString );
	}
	else
	{
		m_pScaleformUI->Params_SetResult( obj, columnValue );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Get the ratio of 2 column values in our results list at the row specified
//		(for example, to retrieve the ratio of Kills to Deaths by querying
//		those two columns in unison and returning their ratio).
//-----------------------------------------------------------------------------
void CCreateLeaderboardsDialogScaleform::QueryRow_ColumnRatio( SCALEFORM_CALLBACK_ARGS_DECL )
{
	int numArgs = pui->Params_GetNumArgs( obj );

	if ( numArgs < 3 )
	{
		Warning( "QueryRow_ColumnRatio: Invalid number of arguments in call to function: %d\n", numArgs );
		return;
	}

	int rowNumber = (int)( pui->Params_GetArgAsNumber( obj, 0 ) );
	
	// We retrieve all friend info as one big list, so offset into that list by the current row we're filling in Flash
	if ( m_currentFilterType == eLBFilter_Friends )
		rowNumber += m_startingRowIndex;

	int columnIdA = (int)( pui->Params_GetArgAsNumber( obj, 1 ) );
	int columnIdB = (int)( pui->Params_GetArgAsNumber( obj, 2 ) );
	
	// Optional param: specify formatting string for result
	const char *formatStr = "%3.3f";
	if ( numArgs > 3 )
	{
		formatStr = pui->Params_GetArgAsString( obj, 3 );
	}

	// Optional param: If true, return the value scaled up by 100 to be a percentage
	bool bPercentage = false;
	if ( numArgs > 4 )
	{
		bPercentage = pui->Params_GetArgAsBool( obj, 4 );
	}

	// Optional param: If true, we want the ratio of columnA / (columnA+columnB) - useful for calculating win percentage
	bool bRatioSumAB = false;
	if ( numArgs > 5 )
	{
		bRatioSumAB = pui->Params_GetArgAsBool( obj, 5 );
	}

#if !defined( NO_STEAM )

	// Our resulting ratio will be "columnA / columnB" or "column A / (columnA + columnB)"
	uint64 columnValueA = 0;
	uint64 columnValueB = 0;

	LeaderboardEntry_t leaderboardEntry;

	int lbDetails[kMaxPayloadEntries] = { 0 };
	int numDetails = kMaxPayloadEntries;

	if ( m_currentLeaderboardHandle != 0 && 
		m_cachedLeaderboardScores.m_hSteamLeaderboardEntries != 0 &&
		steamapicontext->SteamUserStats()->GetDownloadedLeaderboardEntry( m_cachedLeaderboardScores.m_hSteamLeaderboardEntries, rowNumber, &leaderboardEntry, lbDetails, numDetails ) )
	{
		if ( columnIdA == -1 )
		{
			columnValueA = leaderboardEntry.m_nScore;
		}
		if ( columnIdB == -1 )
		{
			columnValueB = leaderboardEntry.m_nScore;
		}
		if ( columnIdA == -2 )
		{
			columnValueA = leaderboardEntry.m_nGlobalRank;
		}
		if ( columnIdB == -2 )
		{
			columnValueB = leaderboardEntry.m_nGlobalRank;
		}
		if ( columnIdA > 0 )
		{
			// 1+ are the details of the entry, subtract 1 to lookup in the array			
			columnValueA = ExtractPayloadDataByColumnID( lbDetails, columnIdA-1 );
		}
		if ( columnIdB > 0 )
		{
			// 1+ are the details of the entry, subtract 1 to lookup in the array			
			columnValueB = ExtractPayloadDataByColumnID( lbDetails, columnIdB-1 );
		}
	}

#endif // !NO_STEAM

#if !defined( NO_STEAM ) 
	float fResult = (float)columnValueA;
	float fDivisor = (float)columnValueB;

	if ( bRatioSumAB )
	{
		// When this flag is set, we want to divide by the sum of the two columns, so we return the ratio of A to the sum of A and B
		fDivisor = (float)columnValueA + (float)columnValueB;
	}

	if ( fDivisor != 0 )
	{
		fResult = (float)columnValueA / fDivisor;
	}

	if ( bPercentage )
	{
		fResult *= 100.f;
	}

	char ratioString[64];
	V_snprintf( ratioString, sizeof(ratioString), formatStr, fResult );

	SFVALUE sfRatioString = CreateFlashString( ratioString );

	m_pScaleformUI->Params_SetResult( obj, sfRatioString );

	SafeReleaseSFVALUE( sfRatioString );
#endif  // !defined( NO_STEAM ) || defined( _X360 ) 
}

//-----------------------------------------------------------------------------
// Purpose: Trigger the display of additional info about a user in the specified
//		row of our query results.  i.e. On XBox this brings up the Gamer Card
//-----------------------------------------------------------------------------
void CCreateLeaderboardsDialogScaleform::DisplayUserInfo( SCALEFORM_CALLBACK_ARGS_DECL )
{
	int rowNumber = (int)( pui->Params_GetArgAsNumber( obj, 0 ) );
	
	// We retrieve all friend info as one big list, so offset into that list by the current row we're filling in Flash
	if ( m_currentFilterType == eLBFilter_Friends )
		rowNumber += m_startingRowIndex;

// Steam overlay not available on console (PS3)
#if !defined( NO_STEAM ) 
	// Show the steam user id in the overlay
	LeaderboardEntry_t leaderboardEntry;
	if ( m_currentLeaderboardHandle != 0 && 
		m_cachedLeaderboardScores.m_hSteamLeaderboardEntries != 0 &&
		steamapicontext->SteamUserStats()->GetDownloadedLeaderboardEntry( m_cachedLeaderboardScores.m_hSteamLeaderboardEntries, rowNumber, &leaderboardEntry, NULL, 0 ) )
	{
		steamapicontext->SteamFriends()->ActivateGameOverlayToUser( "steamid", leaderboardEntry.m_steamIDUser );
	}
#endif // !_GAMECONSOLE && !NO_STEAM

}

//-----------------------------------------------------------------------------
// Purpose: Cancels any existing queries and issues a new query.
//-----------------------------------------------------------------------------
void CCreateLeaderboardsDialogScaleform::QueryUpdate( void )
{
	// If we have no new query, early out
	if ( m_fQueryDelayTime <= 0.f )
		return;

	// Don't actually update the query until our delay has passed
	if ( Plat_FloatTime() < m_fQueryDelayTime )
		return;

#if !defined( NO_STEAM )

	if ( !steamapicontext || !steamapicontext->SteamUserStats() )
		return;

	// Invalidate any results
	m_bResultsValid = false;
	V_memset( &m_cachedLeaderboardScores, 0, sizeof(LeaderboardScoresDownloaded_t) );

	m_fQueryDelayTime = 0.f;

	// Read out the leaderboard format description from matchmaking
	if ( !m_pLeaderboardDescription )
		m_pLeaderboardDescription->deleteThis();

	m_pLeaderboardDescription = g_pMatchFramework->GetMatchTitle()->DescribeTitleLeaderboard( m_currentLeaderboardName );

	// Extract payload data size info
	V_memset( m_payloadSizes, 0, sizeof(int)*kMaxPayloadEntries );

	KeyValues *pPayloadFormat = m_pLeaderboardDescription ? m_pLeaderboardDescription->FindKey( ":payloadformat" ) : NULL;

	if ( pPayloadFormat )
	{
		for ( int payloadIndex=0; payloadIndex < kMaxPayloadEntries; ++payloadIndex )
		{
			KeyValues *pPayload = pPayloadFormat->FindKey( CFmtStr( "payload%d", payloadIndex ) );
			if ( !pPayload )
			{
				// No more payload entries specified.
				break;
			}

			const char* pszFormat = pPayload->GetString( ":format", NULL );
			if ( V_stricmp( pszFormat, "int" ) == 0 )
			{
				m_payloadSizes[payloadIndex] += sizeof( uint32 );
			}
			else if ( V_stricmp( pszFormat, "uint64" ) == 0 )
			{
				m_payloadSizes[payloadIndex] += sizeof( uint64 );
			}
			else
			{
				Warning( " LEADERBOARDS: WARNING! Unsupported data type in leaderboard payload data - assuming 32 bit\n\n" );
				m_payloadSizes[payloadIndex] += sizeof( uint32 );
			}
		}
	}

	m_currentLeaderboardHandle = GetLeaderboardHandle( m_currentLeaderboardName );

	// If we haven't loaded this leaderboard yet, look up its handle first
	if ( m_currentLeaderboardHandle == 0 )
	{
		SteamAPICall_t hSteamAPICall = steamapicontext->SteamUserStats()->FindLeaderboard( m_currentLeaderboardName );
		if ( hSteamAPICall != 0 )
		{
			m_SteamCallResultFindLeaderboard.Set( hSteamAPICall, this, &CCreateLeaderboardsDialogScaleform::Steam_OnFindLeaderboard );
		}
	}
	else
	{
		// We can perform our query for this leaderboard immediately
		QueryLeaderboard( );
	}	

#endif // !NO_STEAM
}

#if !defined ( NO_STEAM )

uint64	CCreateLeaderboardsDialogScaleform::ExtractPayloadDataByColumnID( int *pData, int columnId )
{
	char* pPtr = (char*)pData;
	int curColumnId = 0;

	if ( !pPtr )
		return 0ull;

	while ( curColumnId < columnId && curColumnId < kMaxPayloadEntries )
	{
		pPtr += m_payloadSizes[curColumnId++];
	}

	if ( curColumnId >= kMaxPayloadEntries )
		return 0ull;

#if defined( PLAT_BIG_ENDIAN )
	// On big-endian platforms, we need to byteswap the values written to Steam since we always write them little-endian 
	//		and Steam doesn't swap them for us
	return ( m_payloadSizes[curColumnId] == sizeof(uint32) ? DWordSwap(*(uint32*)pPtr) : QWordSwap(*(uint64*)pPtr) );
#else
	return ( m_payloadSizes[curColumnId] == sizeof(uint32) ? (*(uint32*)pPtr) : (*(uint64*)pPtr) );
#endif
}


SteamLeaderboard_t CCreateLeaderboardsDialogScaleform::GetLeaderboardHandle( const char* szLeaderboardName )
{
	// make a mapping of map contexts to leaderboard handles
	unsigned short index = m_LeaderboardHandles.Find( szLeaderboardName );

	if ( index != m_LeaderboardHandles.InvalidIndex() )
	{
		return m_LeaderboardHandles.Element(index);
	}

	return (SteamLeaderboard_t)0;
}

void CCreateLeaderboardsDialogScaleform::SetLeaderboardHandle( const char* szLeaderboardName, SteamLeaderboard_t hLeaderboard )
{
	m_LeaderboardHandles.InsertOrReplace( szLeaderboardName, hLeaderboard );
}

//=============================================================================
// Callback for FindLeaderboard
//=============================================================================
void CCreateLeaderboardsDialogScaleform::Steam_OnFindLeaderboard( LeaderboardFindResult_t *pFindLeaderboardResult, bool bIOFailure )
{
	// see if we encountered an error during the call
	if ( !pFindLeaderboardResult->m_bLeaderboardFound || bIOFailure )
	{
		Warning( " Leaderboards: Steam error trying to retrieve leaderboard \"%s\"\n\n", m_currentLeaderboardName );
		
		// Report the error now
		m_bCheckForQueryResults = true;

		return;
	}

	// check to see which leaderboard handle we just retrieved
	const char *pszReturnedName = steamapicontext->SteamUserStats()->GetLeaderboardName( pFindLeaderboardResult->m_hSteamLeaderboard );
	Assert( StringHasPrefix( pszReturnedName, m_currentLeaderboardName ) );

	SetLeaderboardHandle( pszReturnedName, pFindLeaderboardResult->m_hSteamLeaderboard );

	m_currentLeaderboardHandle = pFindLeaderboardResult->m_hSteamLeaderboard;

	// Now we can perform our query for this leaderboard
	QueryLeaderboard( );

}

//=============================================================================
// Callback for DownloadLeaderboardEntries
//=============================================================================
void CCreateLeaderboardsDialogScaleform::Steam_OnLeaderboardScoresDownloaded( LeaderboardScoresDownloaded_t *p, bool bError )
{
	if ( bError )
		Warning( " Leaderboard \"%s\": Steam_OnLeaderboardScoresDownloaded received an error.\n", m_currentLeaderboardName );

	// Fetch the data if found and no error	
	if ( !bError )
	{
		// DevMsg( " Leaderboard \"%s\": %d Entries found.\n", m_currentLeaderboardName, p->m_cEntryCount );

		V_memcpy( &m_cachedLeaderboardScores, p, sizeof( LeaderboardScoresDownloaded_t ) );
	}

	m_bCheckForQueryResults = true;
}


void CCreateLeaderboardsDialogScaleform::QueryLeaderboard( void )
{
	SteamAPICall_t hSteamAPICall = (SteamAPICall_t)0;

	if ( m_currentLeaderboardHandle == 0 )
	{
		Warning( " CCreateLeaderboardsDialogScaleform::QueryLeaderboard - leaderboard not found: \"%s\"\n", m_currentLeaderboardName );
		// Report the error now
		m_bCheckForQueryResults = true;
		return;
	}

	switch ( m_currentFilterType )
	{
	case eLBFilter_Me:
		{
			hSteamAPICall = steamapicontext->SteamUserStats()->DownloadLeaderboardEntries( m_currentLeaderboardHandle, k_ELeaderboardDataRequestGlobalAroundUser, -(m_rowsPerPage/2), (m_rowsPerPage/2) );
		}
		break;

	case eLBFilter_Overall:
		{
			hSteamAPICall = steamapicontext->SteamUserStats()->DownloadLeaderboardEntries( m_currentLeaderboardHandle, k_ELeaderboardDataRequestGlobal, m_startingRowIndex, m_startingRowIndex + m_rowsPerPage - 1 );
		}
		break;

	case eLBFilter_Friends:
		{
			hSteamAPICall = steamapicontext->SteamUserStats()->DownloadLeaderboardEntries( m_currentLeaderboardHandle, k_ELeaderboardDataRequestFriends, 0, 0 );
		}
		break;
	}	

	if ( hSteamAPICall )
	{
		// Register for the async callback
		m_SteamCallbackOnLeaderboardScoresDownloaded.Set( hSteamAPICall, this, &CCreateLeaderboardsDialogScaleform::Steam_OnLeaderboardScoresDownloaded );
	}
	else
	{
		// Report the error now
		m_bCheckForQueryResults = true;
	}
}

#endif // !NO_STEAM

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCreateLeaderboardsDialogScaleform::CheckForQueryResults( void )
{

#if !defined( NO_STEAM )
	// No new query to check on
	if ( !m_bCheckForQueryResults )
		return;

	// Signal Flash that we are ready to show results
	WITH_SLOT_LOCKED
	{
		ScaleformUI()->Value_InvokeWithoutReturn( m_FlashAPI, "NotifyResults", 0, NULL );
	}

	m_bCheckForQueryResults = false;
#endif // !NO_STEAM
}

#endif // INCLUDE_SCALEFORM
