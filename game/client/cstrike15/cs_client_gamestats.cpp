//-------------------------------------------------------------
// File:		cs_client_gamestats.cpp
// Desc: 		Manages client side stat storage, accumulation, and access
// Author: 		Peter Freese <peter@hiddenpath.com>
// Date: 		2009/09/11
// Copyright:	� 2009 Hidden Path Entertainment
//
// Keywords: 	
//-------------------------------------------------------------

#include "cbase.h"
#include "cs_client_gamestats.h"
#include "achievementmgr.h"
#include "usermessages.h"
#include "c_cs_player.h"
#include "achievements_cs.h"
#include "vgui/ILocalize.h"
#include "c_team.h"
#include "engineinterface.h"
#include "matchmaking/mm_helpers.h"
#include "cstrikeloadout.h"
#include "gametypes.h"
#include "cs_gamerules.h"
#include "matchmaking/iplayerrankingdata.h"
#include "inputsystem/iinputsystem.h"
#include "platforminputdevice.h"
#include "cs_player_rank_mgr.h"
#include "hltvreplaysystem.h"

// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"

// Added to facilitate data collection below. Add and remove to match current experimental column data

extern ConVar cl_crosshairstyle;
extern ConVar cl_hud_color;
extern ConVar cl_hud_healthammo_style;
extern ConVar cl_hud_bomb_under_radar;
extern ConVar cl_hud_playercount_pos;
extern ConVar cl_hud_playercount_showcount;
extern ConVar cl_radar_rotate;


CCSClientGameStats g_CSClientGameStats;

bool MsgFunc_PlayerStatsUpdate( const CCSUsrMsg_PlayerStatsUpdate &msg )
{
	return g_CSClientGameStats.MsgFunc_PlayerStatsUpdate(msg);
}


struct MapName_LBStatID
{
	const char	*szMapName;
	DWORD		mapLeaderboardStat;
};


// [jason] Map name -> Leaderboard property mapping.  Should be kept in sync with maplist.txt
const MapName_LBStatID MapName_LBStatId_Table[] = 
{
	{"cs_italy",		PROPERTY_CSS_LB_GP_TIME_MAP_ITALY		},
	{"cs_office",		PROPERTY_CSS_LB_GP_TIME_MAP_OFFICE		},
	{"de_aztec",		PROPERTY_CSS_LB_GP_TIME_MAP_AZTEC		},
	{"de_dust",			PROPERTY_CSS_LB_GP_TIME_MAP_DUST		},
	{"de_dust2",		PROPERTY_CSS_LB_GP_TIME_MAP_DUST2		},
	{"de_inferno",		PROPERTY_CSS_LB_GP_TIME_MAP_INFERNO		},
	{"de_nuke",			PROPERTY_CSS_LB_GP_TIME_MAP_NUKE		},
	{"de_shorttrain",	PROPERTY_CSS_LB_GP_TIME_MAP_SHORTTRAIN	},
	{"ar_baggage",		PROPERTY_CSS_LB_GP_TIME_MAP_BAGGAGE		},
	{"ar_shoots",		PROPERTY_CSS_LB_GP_TIME_MAP_SHOOTS		},
	{"de_bank",			PROPERTY_CSS_LB_GP_TIME_MAP_BANK		},
	{"de_lake",			PROPERTY_CSS_LB_GP_TIME_MAP_LAKE		},
	{"de_safehouse",	PROPERTY_CSS_LB_GP_TIME_MAP_SAFEHOUSE	},
	{"de_sugarcane",	PROPERTY_CSS_LB_GP_TIME_MAP_SUGARCANE	},
	{"de_stmarc",		PROPERTY_CSS_LB_GP_TIME_MAP_STMARC		},
	{"de_train",		PROPERTY_CSS_LB_GP_TIME_MAP_TRAIN		},
	{"training1",		PROPERTY_CSS_LB_GP_TIME_MAP_TRAINING	},		
	{"",				DWORD(-1)								},
};

const int kNumMapLeaderboardEntries = sizeof(MapName_LBStatId_Table)/sizeof(MapName_LBStatId_Table[0]);

struct LeaderboardMap_t
{
	DWORD winsId;
	const char* winsName;
	DWORD csId;
    const char* csName;
	DWORD kdId;
    const char* kdName;
	DWORD starsId;
    const char* starsName;
	DWORD gpId;
    const char* gpName;
};

// Mapping of game mode/type to leaderboard id
LeaderboardMap_t g_LeaderboardIDMap[] =
{
	{ STATS_VIEW_WINS_ONLINE_CASUAL,		"WINS_ONLINE_CASUAL",		STATS_VIEW_CS_ONLINE_CASUAL,		"CS_ONLINE_CASUAL",			STATS_VIEW_KD_ONLINE_CASUAL,			"KD_ONLINE_CASUAL",			STATS_VIEW_STARS_ONLINE_CASUAL,			"STARS_ONLINE_CASUAL",			STATS_VIEW_GP_ONLINE_CASUAL,		"GP_ONLINE_CASUAL"			},
	{ STATS_VIEW_WINS_ONLINE_COMPETITIVE,	"WINS_ONLINE_COMPETITIVE",	STATS_VIEW_CS_ONLINE_COMPETITIVE,	"CS_ONLINE_COMPETITIVE",	STATS_VIEW_KD_ONLINE_COMPETITIVE,		"KD_ONLINE_COMPETITIVE",	STATS_VIEW_STARS_ONLINE_COMPETITIVE,	"STARS_ONLINE_COMPETITIVE",		STATS_VIEW_GP_ONLINE_COMPETITIVE,	"GP_ONLINE_COMPETITIVE"		},	
	{ STATS_VIEW_WINS_ONLINE_GG_PROG,		"WINS_ONLINE_GG_PROG",		STATS_VIEW_CS_ONLINE_GG_PROG,		"CS_ONLINE_GG_PROG",		STATS_VIEW_KD_ONLINE_GG_PROG,			"KD_ONLINE_GG_PROG",		STATS_VIEW_STARS_ONLINE_GG_PROG,		"STARS_ONLINE_GG_PROG",			STATS_VIEW_GP_ONLINE_GG_PROG,		"GP_ONLINE_GG_PROG"			},
	{ STATS_VIEW_WINS_ONLINE_GG_BOMB,		"WINS_ONLINE_GG_BOMB",		STATS_VIEW_CS_ONLINE_GG_BOMB,		"CS_ONLINE_GG_BOMB",		STATS_VIEW_KD_ONLINE_GG_BOMB,			"KD_ONLINE_GG_BOMB",		STATS_VIEW_STARS_ONLINE_GG_BOMB,		"STARS_ONLINE_GG_BOMB",			STATS_VIEW_GP_ONLINE_GG_BOMB,		"GP_ONLINE_GG_BOMB"			},
};

const int kNumLeaderboardIDs = sizeof(g_LeaderboardIDMap)/sizeof(g_LeaderboardIDMap[0]);

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CCSClientGameStats::CCSClientGameStats() 
{
	m_bSteamStatsDownload = false;
}

//-----------------------------------------------------------------------------
// Purpose: called at init time after all systems are init'd.  We have to
//			do this in PostInit because the Steam app ID is not available earlier
//-----------------------------------------------------------------------------
void CCSClientGameStats::PostInit()
{
	ACTIVE_SPLITSCREEN_PLAYER_GUARD( 0 );

	// listen for events
	ListenForGameEvent( "player_stats_updated" );
	ListenForGameEvent( "user_data_downloaded" );
	ListenForGameEvent( "round_end_upload_stats" );
	ListenForGameEvent( "round_end" );
	ListenForGameEvent( "cs_game_disconnected" );
	ListenForGameEvent( "read_game_titledata" );
	ListenForGameEvent( "write_game_titledata" );
	ListenForGameEvent( "reset_game_titledata" );
	ListenForGameEvent( "update_matchmaking_stats" );
	ListenForGameEvent( "begin_new_match" );
	ListenForGameEvent( "bomb_planted" );
	ListenForGameEvent( "hostage_follows" ); 
	
	// Client info messages
	for ( int hh = 0; hh < MAX_SPLITSCREEN_PLAYERS; ++hh )
	{
		ACTIVE_SPLITSCREEN_PLAYER_GUARD( hh );
		m_UMCMsgPlayerStatsUpdate.Bind< CS_UM_PlayerStatsUpdate, CCSUsrMsg_PlayerStatsUpdate>( UtlMakeDelegate( ::MsgFunc_PlayerStatsUpdate ));
	}

	m_RoundEndReason = Invalid_Round_End_Reason;
	m_bObjectiveAttempted = false;
}

void CCSClientGameStats::LevelInitPostEntity()
{
	// Need this for players who join mid-match to have a client session 
	if ( CSGameRules()->HasMatchStarted() )
	{
		GetSteamWorksGameStatsClient().StartSession();
	}
}

//-----------------------------------------------------------------------------
// Purpose: called at level shutdown
//-----------------------------------------------------------------------------
void CCSClientGameStats::LevelShutdownPreEntity()
{
	UploadRoundStats();
	GetSteamWorksGameStatsClient().EndSession();

	// This is a good opportunity to update our last match stats
	UpdateLastMatchStats();

	// upload user stats to Steam on every map change
	UpdateSteamStats();
}

static inline int GetNumPlayers( C_Team *pTeam )
{
	return pTeam ? pTeam->Get_Number_Players() : 0;
}

//-----------------------------------------------------------------------------
// Purpose: called when the stats have changed in-game
//-----------------------------------------------------------------------------
CEG_NOINLINE void CCSClientGameStats::FireGameEvent( IGameEvent *event )
{
	const char *pEventName = event->GetName();
	if ( 0 == Q_strcmp( pEventName, "player_stats_updated" ) )
	{
		UpdateSteamStats();
	}
	else if ( 0 == Q_strcmp( pEventName, "user_data_downloaded" ) )
	{
		RetrieveSteamStats();
	}
	else if ( 0 == Q_strcmp( pEventName, "read_game_titledata" ) )
	{
		SyncCSStatsToTitleData( event->GetInt( "controllerId" ), CSSTAT_READ_STAT );
		SyncCSLoadoutsToTitleData( event->GetInt( "controllerId" ), CSSTAT_READ_STAT );
		SyncCSMatchmakingDataToTitleData( event->GetInt( "controllerId" ), CSSTAT_READ_STAT );
		SyncCSRankingDataToTitleData( event->GetInt( "controllerId" ), CSSTAT_READ_STAT );
	}
	else if ( 0 == Q_strcmp( pEventName, "write_game_titledata" ) )
	{
		SyncCSStatsToTitleData( event->GetInt( "controllerId" ), CSSTAT_WRITE_STAT );
		SyncCSLoadoutsToTitleData( event->GetInt( "controllerId" ), CSSTAT_WRITE_STAT );	
		SyncCSMatchmakingDataToTitleData( event->GetInt( "controllerId" ), CSSTAT_WRITE_STAT );
		SyncCSRankingDataToTitleData( event->GetInt( "controllerId" ), CSSTAT_WRITE_STAT );
	}
	else if ( 0 == Q_strcmp( pEventName, "reset_game_titledata" ) )
	{
		// $TODO(hpe) need to get controllerID and use it when stats handle splitscreen and loadouts handle splitscreen
		ResetMatchmakingData( MMDATA_SCOPE_LIFETIME );
		ResetMatchmakingData( MMDATA_SCOPE_ROUND );

		// clear stats
		int userSlot = XBX_GetSlotByUserId( event->GetInt( "controllerId" ) );
		if ( userSlot < 0 || userSlot >= MAX_SPLITSCREEN_PLAYERS )
		{
			AssertMsg( false, "CCSClientGameStats::FireGameEvent:: reset_game_titledata  invalid userSlot\n" );
			userSlot = STEAM_PLAYER_SLOT;
		}
		g_CSClientGameStats.ResetAllStats( userSlot );

	}
	else if ( Q_strcmp( pEventName, "update_matchmaking_stats" ) == 0 )
	{
		UpdateMatchmakingData();
	}
	else if ( Q_strcmp( pEventName, "round_end_upload_stats" ) == 0 )
	{
		// [jhail] Write leaderboard stats at pre-start, before our stats collection gets reset
		WriteLeaderboardStats();

		UploadRoundStats();
	}
	else if ( Q_strcmp( pEventName, "round_end" ) == 0 )
	{

		m_RoundEndReason = event->GetInt( "reason", Invalid_Round_End_Reason );
		int iCurrentPlayerCount = event->GetInt( "player_count", 0 );
#ifdef DBGFLAG_ASSERT
		int nPlayerCountOnClient = GetNumPlayers( GetGlobalTeam( TEAM_CT ) ) + GetNumPlayers( GetGlobalTeam( TEAM_TERRORIST ) );
		// don't collect stats at the wrong point in time if round_end is passed through during replay
		if ( g_HltvReplaySystem.GetHltvReplayDelay() )
			Assert( iCurrentPlayerCount <= nPlayerCountOnClient ); // the number of players at round end can shrink, but cannot grow comparing to the replayed state
		else
			Assert( nPlayerCountOnClient == 0 || iCurrentPlayerCount == nPlayerCountOnClient ); // if we are not replaying, the number of player must be the same on server and client 
#endif
		m_matchMaxPlayerCount = Max( m_matchMaxPlayerCount, iCurrentPlayerCount );
	}
	else if ( 0 == Q_strcmp( pEventName, "cs_game_disconnected" ) )
	{
		UploadRoundStats();
	}
	else if ( 0 == Q_strcmp( pEventName, "begin_new_match" ) )
	{
		GetSteamWorksGameStatsClient().EndSession();
		GetSteamWorksGameStatsClient().StartSession();
	}
	else if ( 0 == Q_strcmp( pEventName, "bomb_planted" ) || 0 == Q_strcmp( pEventName, "hostage_follows" ) )
	{
	   //ignore events after round end (planting for cash after CT elimination, picking up a hostage after T elimination
		if ( m_RoundEndReason == Invalid_Round_End_Reason || m_RoundEndReason == Game_Commencing )
			{
				 m_bObjectiveAttempted = true;
		    }
	}
}

void CCSClientGameStats::RetrieveSteamStats()
{
	Assert( steamapicontext->SteamUserStats() );
	if ( !steamapicontext->SteamUserStats() )
		return;

	// we shouldn't be downloading stats more than once
	Assert(m_bSteamStatsDownload == false);
	if (m_bSteamStatsDownload)
		return;

	int nStatFailCount = 0;
	for ( int i = 0; i < CSSTAT_MAX; ++i )
	{
		if ( CSStatProperty_Table[i].szSteamName == NULL )
			continue;

		int iData;
		if ( steamapicontext->SteamUserStats()->GetStat( CSStatProperty_Table[i].szSteamName, &iData ) )
		{	
			m_lifetimeStats[STEAM_PLAYER_SLOT][i] = iData;

			// Init our 'last upload' values to those we got from steam.
			m_lifetimeStatsLastUpload[STEAM_PLAYER_SLOT][i] = iData;
		}
		else
		{
			++nStatFailCount;
		}
	}

	if ( nStatFailCount > 0 )
	{
		Msg("RetrieveSteamStats: failed to get %i stats\n", nStatFailCount);
		return;
	}

	IGameEvent * event = gameeventmanager->CreateEvent( "player_stats_updated" );
	if ( event )
	{
		gameeventmanager->FireEventClientSide( event );
	}

	m_bSteamStatsDownload = true;
}

//-----------------------------------------------------------------------------
// Purpose: Uploads stats for current Steam user to Steam
//-----------------------------------------------------------------------------
void CCSClientGameStats::UpdateSteamStats()
{
	// only upload if Steam is running
	if ( !steamapicontext->SteamUserStats() )
		return; 

	// don't upload any stats if we haven't successfully download stats yet
	if ( !m_bSteamStatsDownload )
	{
		// this used to request stats periodically which is now handled in stats request heartbeat in PlayerLocal::Update

		return;
	}

	for ( int i = 0; i < CSSTAT_MAX; ++i )
	{
		if ( CSStatProperty_Table[i].szSteamName == NULL )
			continue;

		if ( m_lifetimeStatsLastUpload[ STEAM_PLAYER_SLOT ][ i ] != m_lifetimeStats[ STEAM_PLAYER_SLOT ][ i ] )
		{
			// set the stats locally in Steam client
			steamapicontext->SteamUserStats()->SetStat( CSStatProperty_Table[ i ].szSteamName, m_lifetimeStats[ STEAM_PLAYER_SLOT ][ i ] );
			m_lifetimeStatsLastUpload[ STEAM_PLAYER_SLOT ][ i ] = m_lifetimeStats[ STEAM_PLAYER_SLOT ][ i ];
		}

	}

	// let the achievement manager know the stats have changed
	g_AchievementMgrCS.SetDirty( true, STEAM_PLAYER_SLOT );
}


int CCSClientGameStats::GetStatCount()
{
	return CSSTAT_MAX;
}

PlayerStatData_t CCSClientGameStats::GetStatById( int id, int nUserSlot )
{
	Assert(id >= 0 && id < CSSTAT_MAX);
	if ( id >= 0 && id < CSSTAT_MAX)
	{
		PlayerStatData_t statData;

		statData.iStatId = id;
		statData.iStatValue = m_lifetimeStats[nUserSlot][statData.iStatId];

		// we can make this more efficient by caching the localized names
		statData.pStatDisplayName = g_pVGuiLocalize->Find( CSStatProperty_Table[id].szLocalizationToken );

		return statData;
	}
	else
	{
		PlayerStatData_t dummy;
		dummy.pStatDisplayName = NULL;
		dummy.iStatId = CSSTAT_UNDEFINED;
		dummy.iStatValue = 0;
		return dummy;
	}
}

const StatsCollection_t& CCSClientGameStats::GetLifetimeStats( int nUserSlot ) 
{ 
	if ( nUserSlot < 0 || nUserSlot >= MAX_SPLITSCREEN_PLAYERS )
	{
		AssertMsg( false, "CCSClientGameStats::GetLifetimeStats nUserSlot out of range; using 0\n" );
		return m_lifetimeStats[STEAM_PLAYER_SLOT]; 
	}
	return m_lifetimeStats[nUserSlot]; 
}

const StatsCollection_t& CCSClientGameStats::GetMatchStats( int nUserSlot ) 
{ 
	if ( nUserSlot < 0 || nUserSlot >= MAX_SPLITSCREEN_PLAYERS )
	{
		AssertMsg( false, "CCSClientGameStats::GetMatchStats nUserSlot out of range; using 0\n" );
		return m_matchStats[STEAM_PLAYER_SLOT]; 
	}
	return m_matchStats[nUserSlot]; 
}

const StatsCollection_t& CCSClientGameStats::GetRoundStats( int nUserSlot ) 
{ 
	if ( nUserSlot < 0 || nUserSlot >= MAX_SPLITSCREEN_PLAYERS )
	{
		AssertMsg( false, "CCSClientGameStats::GetRoundStats nUserSlot out of range; using 0\n" );
		return m_roundStats[STEAM_PLAYER_SLOT]; 
	}
	return m_roundStats[nUserSlot]; 
}

void CCSClientGameStats::UpdateStats( const StatsCollection_t &stats, int nUserSlot )
{
	C_CSPlayer *pPlayer = C_CSPlayer::GetLocalCSPlayer();
	if ( !pPlayer )
		return;

	// don't count stats if cheats on, commentary mode, etc
	if ( !g_AchievementMgrCS.CheckAchievementsEnabled() )
		return;

	// Update matchmaking related stats.
	IncrementMatchmakingData( stats );

	// Update the accumulated stats
	// We don't aggregate stats in Offline Games with "Dumb" or No Bots
	if ( CSGameRules() && CSGameRules()->IsAwardsProgressAllowedForBotDifficulty() )
	{
		m_lifetimeStats[nUserSlot].Aggregate(stats);
		m_matchStats[nUserSlot].Aggregate(stats);
		m_roundStats[nUserSlot].Aggregate(stats);
	}
	
	// $TODO: hpe: sb: pass along the userSlot in the player_stats_updated message
	IGameEvent * event = gameeventmanager->CreateEvent( "player_stats_updated" );
	if ( event )
	{
		gameeventmanager->FireEventClientSide( event );
	}
}

void CCSClientGameStats::ResetAllStats( int nUSerSlot )
{
	ISteamUserStats* pSteamUserStats = steamapicontext->SteamUserStats();
	if ( pSteamUserStats )
	{
		pSteamUserStats->ResetAllStats(false);
	}
	else
	{
		// need to pass along user slot reset into the player_stats_updated message
		int userSlot = nUSerSlot;
		if ( userSlot < 0 || userSlot >= MAX_SPLITSCREEN_PLAYERS )
		{
			AssertMsg( false, "CCSClientGameStats::ResetAllStats invalid userSlot\n");
			userSlot = STEAM_PLAYER_SLOT;
		}
		m_lifetimeStats[userSlot].Reset();
		m_matchStats[userSlot].Reset();

		m_roundStats[userSlot].Reset();

		UpdateSteamStats();

		IGameEvent * event = gameeventmanager->CreateEvent( "player_stats_updated" );
		if ( event )
		{
			gameeventmanager->FireEventClientSide( event );
		}
	}
}

void CCSClientGameStats::ResetAllStatsAndAchievements( )
{
	ISteamUserStats* pSteamUserStats = steamapicontext->SteamUserStats();
	if ( pSteamUserStats )
	{
		pSteamUserStats->ResetAllStats(true);
	}
}

void CRC32Helper_ProcessInt16( CRC32_t &crc, int16 n )
{
	int16 plat_n = LittleShort( n );
	CRC32_ProcessBuffer( &crc, &plat_n, sizeof(plat_n) );
}


void CRC32Helper_ProcessInt32( CRC32_t &crc, int32 n )
{
	int32 plat_n = LittleDWord( n );
	CRC32_ProcessBuffer( &crc, &plat_n, sizeof(plat_n) );
}


void CRC32Helper_ProcessUInt32( CRC32_t &crc, uint32 n )
{
	uint32 plat_n = LittleDWord( n );
	CRC32_ProcessBuffer( &crc, &plat_n, sizeof(plat_n) );
}


bool CCSClientGameStats::MsgFunc_PlayerStatsUpdate( const CCSUsrMsg_PlayerStatsUpdate &msg )
{
	// Note: if any check fails while decoding this message, bail out and disregard this data to avoid 
	// potentially polluting player stats 

	StatsCollection_t deltaStats;

	CRC32_t crc;
	CRC32_Init( &crc );

	const uint32 key = 0x82DA9F4C;	// this key should match the key in cs_gamestats.cpp
	
	CRC32Helper_ProcessUInt32( crc, key );

	const byte version = 0x03;
	CRC32_ProcessBuffer( &crc, &version, sizeof(version));

	if (msg.version() != version)
	{
		Warning("PlayerStatsUpdate message: ignoring unsupported version\n");
		return true;
	}

	short iStatsToRead = msg.stats_size();
	CRC32Helper_ProcessInt16( crc, iStatsToRead );

	for ( int i = 0; i < iStatsToRead; ++i)
	{
		const CCSUsrMsg_PlayerStatsUpdate::Stat &stat = msg.stats(i);

		short iStat = stat.idx();
		CRC32Helper_ProcessInt16( crc, iStat );

		if (iStat >= CSSTAT_MAX)
		{
			Warning("PlayerStatsUpdate: invalid statId encountered; ignoring stats update\n");
			return true;
		}
		short delta = stat.delta();
		deltaStats[iStat] = delta;
		CRC32Helper_ProcessInt16( crc, delta );
	}

	int userID = msg.user_id();
	CRC32Helper_ProcessInt32( crc, userID );
	
	CRC32_Final( &crc );
	CRC32_t readCRC = msg.crc();

	if ( readCRC != crc )
	{
		Warning("PlayerStatsUpdate message from server is corrupt; ignoring\n");
		return true;
	}

	// do one additional pass for out of band values
	for ( int iStat = CSSTAT_FIRST; iStat < CSSTAT_MAX; ++iStat )
	{
		if (deltaStats[iStat] < 0 || deltaStats[iStat] >= 0x4000)
		{
			Warning("PlayerStatsUpdate message from server has out of band values; ignoring\n");
			return true;
		}
	}

	// everything looks okay at this point; add these stats for the player's round, match, and lifetime stats
	int userSlot = STEAM_PLAYER_SLOT;

	UpdateStats(deltaStats, userSlot );

	return true;
}

void CCSClientGameStats::ResetMatchStats()
{
	for ( int userSlot = 0; userSlot < MAX_SPLITSCREEN_PLAYERS; ++userSlot)
	{
		m_matchStats[userSlot].Reset();
	}
	m_matchMaxPlayerCount = 0;
}

void CCSClientGameStats::ResetRoundStats( void )
{
	for ( int userSlot = 0; userSlot < MAX_SPLITSCREEN_PLAYERS; ++userSlot)
	{
		m_roundStats[userSlot].Reset();
	}
}

// note, since we now reset the stats after we update them each time, this can be called multiple times without overwriting match stats
void CCSClientGameStats::UpdateLastMatchStats( void )
{
	for ( int userSlot = 0; userSlot < MAX_SPLITSCREEN_PLAYERS; ++userSlot )
	{
		// only update that last match if we actually have valid data
		if ( m_matchStats[userSlot][CSSTAT_ROUNDS_PLAYED] == 0 )
			return;

		// check to see if the player materially participate; they could have been spectating or joined just in time for the ending.
		int s = 0;
		s += m_matchStats[userSlot][CSSTAT_ROUNDS_WON];
		s += m_matchStats[userSlot][CSSTAT_KILLS];
		s += m_matchStats[userSlot][CSSTAT_DEATHS];
		s += m_matchStats[userSlot][CSSTAT_MVPS];
		s += m_matchStats[userSlot][CSSTAT_DAMAGE];
		s += m_matchStats[userSlot][CSSTAT_MONEY_SPENT];

		if( s != 0 && CSGameRules() && CSGameRules()->IsAwardsProgressAllowedForBotDifficulty() )
		{
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_CONTRIBUTION_SCORE]	= m_matchStats[userSlot][CSSTAT_CONTRIBUTION_SCORE];
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_GG_PROGRESSIVE_CONTRIBUTION_SCORE] = m_matchStats[userSlot][CSSTAT_GG_PROGRESSIVE_CONTRIBUTION_SCORE];
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_T_ROUNDS_WON]	= m_matchStats[userSlot][CSSTAT_T_ROUNDS_WON];
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_CT_ROUNDS_WON]	= m_matchStats[userSlot][CSSTAT_CT_ROUNDS_WON];
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_ROUNDS_WON]	= m_matchStats[userSlot][CSSTAT_ROUNDS_WON];
			m_lifetimeStats[userSlot][CSTAT_LASTMATCH_ROUNDS_PLAYED]	= m_matchStats[userSlot][CSSTAT_ROUNDS_PLAYED];
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_KILLS]			= m_matchStats[userSlot][CSSTAT_KILLS];
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_DEATHS]		= m_matchStats[userSlot][CSSTAT_DEATHS];
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_MVPS]			= m_matchStats[userSlot][CSSTAT_MVPS];
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_DAMAGE]		= m_matchStats[userSlot][CSSTAT_DAMAGE];
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_MONEYSPENT]	= m_matchStats[userSlot][CSSTAT_MONEY_SPENT];
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_DOMINATIONS]	= m_matchStats[userSlot][CSSTAT_DOMINATIONS];
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_REVENGES]		= m_matchStats[userSlot][CSSTAT_REVENGES];
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_MAX_PLAYERS]	= m_matchMaxPlayerCount;
			CalculateMatchFavoriteWeapons();
		}
	}
	ResetMatchStats();
}

//-----------------------------------------------------------------------------
// Purpose: Calculate and store the match favorite weapon for each player as only deltaStats for that weapon are stored on Steam
//-----------------------------------------------------------------------------
void CCSClientGameStats::CalculateMatchFavoriteWeapons()
{
	for ( int userSlot = 0; userSlot < MAX_SPLITSCREEN_PLAYERS; ++userSlot )
	{
		int maxKills = 0, maxKillId = -1;

		for( int j = CSSTAT_KILLS_DEAGLE; j <= CSSTAT_KILLS_M249; ++j )
		{
			if ( m_matchStats[userSlot][j] > maxKills )
			{
				maxKills = m_matchStats[userSlot][j];
				maxKillId = j;
			}
		}
		if ( maxKillId == -1 )
		{
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_FAVWEAPON_ID] = WEAPON_NONE;
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_FAVWEAPON_SHOTS] = 0;
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_FAVWEAPON_HITS] = 0;
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_FAVWEAPON_KILLS] = 0;
		}
		else
		{
			int statTableID = -1;
			for (int j = 0; WeaponName_StatId_Table[j].killStatId != CSSTAT_UNDEFINED; ++j)
			{
				if ( WeaponName_StatId_Table[j].killStatId == maxKillId )
				{
					statTableID = j;
					break;
				}
			}
			Assert( statTableID != -1 );

			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_FAVWEAPON_ID] = WeaponName_StatId_Table[statTableID].weaponId;
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_FAVWEAPON_SHOTS] = m_matchStats[userSlot][WeaponName_StatId_Table[statTableID].shotStatId];
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_FAVWEAPON_HITS] = m_matchStats[userSlot][WeaponName_StatId_Table[statTableID].hitStatId];
			m_lifetimeStats[userSlot][CSSTAT_LASTMATCH_FAVWEAPON_KILLS] = m_matchStats[userSlot][WeaponName_StatId_Table[statTableID].killStatId];
		}
	}
}


bool CCSClientGameStats::ValidateTitleBlockVersion( TitleDataFieldsDescription_t const *pFields, IPlayerLocal *pPlayerLocal, CSSyncStatValueDirection_t eOp, int titleBlockNo )
{
	return false;		// no title data for non Xbox systems

}


//-----------------------------------------------------------------------------
// Purpose: Serialize lifetime stats to the user profile title data
//-----------------------------------------------------------------------------
bool CCSClientGameStats::SyncCSStatsToTitleData( int iController, CSSyncStatValueDirection_t eOp )
{

	return true;

}


bool CCSClientGameStats::SyncCSLoadoutsToTitleData( int iController, CSSyncStatValueDirection_t eOp )
{
	// get the local player
	IPlayerLocal *pPlayerLocal = g_pMatchFramework->GetMatchSystem()->GetPlayerManager()->GetLocalPlayer( iController );
	if ( !pPlayerLocal )
		return false;

	return true;
}


// Purpose: resets the server-side leaderboards for testing purposes
void CCSClientGameStats::ResetLeaderboardStats( void )
{
}

CEG_NOINLINE void CCSClientGameStats::WriteLeaderboardStats( void )
{
	return;	// disabling client-writing leaderboards for now

	for ( int userSlot = 0; userSlot < MAX_SPLITSCREEN_PLAYERS; ++userSlot )
	{
		ACTIVE_SPLITSCREEN_PLAYER_GUARD( userSlot );
		int userID = XBX_GetUserId( userSlot );

		// Skip writing if we haven't completed a round
		if ( m_roundStats[userSlot][CSSTAT_ROUNDS_PLAYED] == 0 )
			continue;

		IPlayerLocal *pProfile = g_pMatchFramework->GetMatchSystem()->GetPlayerManager()->GetLocalPlayer( userID );

		if ( !pProfile )
		{
			Warning( "CCSClientGameStats::WriteLeaderboardStats failed to find the PlayerLocal Profile for the Active User!\n" );
			return;
		}

		// Retrieve online status from our matchmaking framework.
		bool bMultiplayerGame = false;
		bool bPublicGame = false;

		// Check if this is already a public game
		IMatchSession *pMatchSession = g_pMatchFramework ? g_pMatchFramework->GetMatchSession() : NULL;
		if ( pMatchSession ) 
		{
			KeyValues *pSystemData = pMatchSession->GetSessionSystemData();
			if ( pSystemData )
			{
				KeyValues *kv = pMatchSession->GetSessionSettings();
				if ( kv )
				{
					char const *szOnline = kv->GetString( "system/network", NULL );
					if ( szOnline &&
						 !V_stricmp( "LIVE", szOnline ) )
					{
						bMultiplayerGame = true;
					}

					char const *szAccess = kv->GetString( "system/access", NULL );
					if ( szAccess &&
						 !V_stricmp( "public", szAccess ) )
					{
						bPublicGame = true;
					}

					char const *szQueue = kv->GetString( "game/mmqueue", NULL );
					if ( szQueue && *szQueue )
					{	// Queue games are always public
						bPublicGame = true;
					}
				}
			}
		}

		// We don't write to leaderboards for Offline or Online Private games
		if ( !bMultiplayerGame || !bPublicGame )
			return;

		// Online write to leaderboard if we played as CT/T during this round
		C_CSPlayer *pPlayer = (C_CSPlayer*)C_BasePlayer::GetLocalPlayer( userSlot );
		bool bIsCT = true;

		if ( pPlayer && pPlayer->GetTeamNumber() == TEAM_CT )
		{
			bIsCT = true;
		}
		else if ( pPlayer && pPlayer->GetTeamNumber() == TEAM_TERRORIST )
		{
			bIsCT = false;
		}
		else
		{
			return;
		}

		// Calculate which set of leaderboards in the g_LeaderboardIDMap we write to, based on the current game mode/type
		int boardSetIndex = -1;

		switch ( g_pGameTypes->GetCurrentGameType() )
		{
		case CS_GameType_Classic:
			{
				if ( bMultiplayerGame && bPublicGame )
				{
					switch ( g_pGameTypes->GetCurrentGameMode() ) 
					{
					case CS_GameMode::Classic_Casual:
						boardSetIndex = 0; // ONLINE_CASUAL
						break;
					case CS_GameMode::Classic_Competitive:
						boardSetIndex = 1; // ONLINE_COMPETITIVE
						break;
					default:
						Warning( "Leaderboard Write Error: Unknown CurrentGameMode value: %d!\n", g_pGameTypes->GetCurrentGameMode() );
						break;
					}
				}
			}
			break;

		case CS_GameType_GunGame:
			{
				if ( bMultiplayerGame && bPublicGame )
				{
					switch ( g_pGameTypes->GetCurrentGameMode() ) 
					{
					case CS_GameMode::GunGame_Progressive:
						boardSetIndex = 2; // ONLINE_GG_PROG
						break;

					case CS_GameMode::GunGame_Bomb:
						boardSetIndex = 3; // ONLINE_GG_BOMB
						break;

					default: // Unsupported game type
						Warning( "Leaderboard Write Error: Unknown CurrentGameMode value: %d!\n", g_pGameTypes->GetCurrentGameMode() );
						break;
					}
				}
			}
			break;
		
		default:
			{
				Warning( "Leaderboard Write Error: Unknown CurrentGameType value: %d!\n", g_pGameTypes->GetCurrentGameType() );
			}
			break;
		}

		// Sanity check the board type we selected:
		if ( boardSetIndex < 0 || boardSetIndex >= kNumLeaderboardIDs )
		{
			Warning( "Leaderboard Write Error: Current Game type/mode does not have a valid Leaderboard associated with it!\n" );
			Warning( " Game Setup: type = %d, mode = %d, isMultiplayer? %d, isPublic? %d [ expected boardSetIdx = %d ]\n", 
						g_pGameTypes->GetCurrentGameType(),
						g_pGameTypes->GetCurrentGameMode(),
						bMultiplayerGame, bPublicGame, boardSetIndex );
			return;
		}

		// Construct keyvalues that set the values we want to write to the leaderboard.
		KeyValues *pLeaderboardInfo = new KeyValues( "leaderboardinfo" );
		KeyValues::AutoDelete autoDelete( pLeaderboardInfo );

		CEG_PROTECT_MEMBER_FUNCTION( CCSClientGameStats_WriteLeaderboardStats );

		//
		// Write out: Contribution Score
		char csBoardName[256] = {0};
		
		// Write to the appropriate board based on input type for all online boards
		InputDevice_t inputDevice = g_pInputSystem->GetCurrentInputDevice();
		// If we somehow don't have a device set yet, assume we're using the default device for the platform
		if ( inputDevice == INPUT_DEVICE_NONE )
		{
			inputDevice = PlatformInputDevice::GetDefaultInputDeviceForPlatform();
		}

		const char* pDeviceName = PlatformInputDevice::GetInputDeviceNameInternal( inputDevice );

		if ( pDeviceName == NULL )
		{
			Warning( "Leaderboard Write Error: Invalid input device (InputType_t = %d)- cannot write to ELO leaderboard!\n", inputDevice );
		}
		else
		{
			V_snprintf( csBoardName, ARRAYSIZE(csBoardName), "%s_%s", g_LeaderboardIDMap[boardSetIndex].csName, pDeviceName );
		}
		
		KeyValues *pkv = NULL;

		if ( csBoardName[0] != 0 )
		{
			pkv = pLeaderboardInfo->FindKey( csBoardName, true );
			if ( pkv )
			{
				pkv->SetInt( "average_contribution", 0 );
				pkv->SetInt( "mvp_awards", m_roundStats[userSlot][CSSTAT_MVPS] );
				pkv->SetInt( "rounds_played", m_roundStats[userSlot][CSSTAT_ROUNDS_PLAYED] );
				pkv->SetInt( "kills", m_roundStats[userSlot][CSSTAT_KILLS] );
				pkv->SetInt( "deaths", m_roundStats[userSlot][CSSTAT_DEATHS] );
				pkv->SetInt( "damage", m_roundStats[userSlot][CSSTAT_DAMAGE] );
				pkv->SetInt( "total_contribution", m_roundStats[userSlot][CSSTAT_CONTRIBUTION_SCORE] );
			}
		}

		//
		// Write out: Kill / Death Ratio
		pkv = pLeaderboardInfo->FindKey( g_LeaderboardIDMap[boardSetIndex].kdName, true );
		if ( pkv )
		{
			pkv->SetInt( "kd_ratio", 0 );
			pkv->SetInt( "kills", m_roundStats[userSlot][CSSTAT_KILLS] );
			pkv->SetInt( "shots_fired", m_roundStats[userSlot][CSSTAT_SHOTS_FIRED] );
			pkv->SetInt( "head_shots", m_roundStats[userSlot][CSSTAT_KILLS_HEADSHOT] );
			pkv->SetInt( "deaths", m_roundStats[userSlot][CSSTAT_DEATHS] );
			pkv->SetInt( "shots_hit", m_roundStats[userSlot][CSSTAT_SHOTS_HIT] );
			pkv->SetInt( "rounds_played", m_roundStats[userSlot][CSSTAT_ROUNDS_PLAYED] );
		}

		//
		// Write out: Wins
		pkv = pLeaderboardInfo->FindKey( g_LeaderboardIDMap[boardSetIndex].winsName, true );
		if ( pkv )
		{
			int winsAsCT	=  bIsCT ? m_roundStats[userSlot][CSSTAT_CT_ROUNDS_WON]	: 0;
			int winsAsT	= !bIsCT ? m_roundStats[userSlot][CSSTAT_T_ROUNDS_WON]	: 0;
			int totalWins	=  winsAsCT + winsAsT;
			int totalPlayed = m_roundStats[userSlot][CSSTAT_ROUNDS_PLAYED];
			int totalLosses = clamp( totalPlayed - totalWins, 0, totalPlayed );

			int lossesAsCT	=  bIsCT ? (totalPlayed - m_roundStats[userSlot][CSSTAT_CT_ROUNDS_WON]) : 0;
			int lossesAsT	= !bIsCT ? (totalPlayed - m_roundStats[userSlot][CSSTAT_T_ROUNDS_WON]) : 0;

			pkv->SetInt( "wins_ratio", 0 );
			pkv->SetInt( "total_wins", totalWins );
			pkv->SetInt( "total_losses", totalLosses );
			pkv->SetInt( "win_as_ct", winsAsCT );
			pkv->SetInt( "win_as_t", winsAsT );
			pkv->SetInt( "loss_as_ct", lossesAsCT );
			pkv->SetInt( "loss_as_t", lossesAsT );
		}

		//
		// Write out: Stars
		pkv = pLeaderboardInfo->FindKey( g_LeaderboardIDMap[boardSetIndex].starsName, true );
		if ( pkv )
		{
			// Number of detonations is the number of "completed objectives" if you're on the Terrorist team
			int totalDetonations = 0;
			if ( !bIsCT )
			{
				totalDetonations = m_roundStats[userSlot][CSSTAT_OBJECTIVES_COMPLETED];
			}

			pkv->SetInt( "numstars", m_roundStats[userSlot][CSSTAT_MVPS] );
			pkv->SetInt( "bombs_planted", m_roundStats[userSlot][CSSTAT_NUM_BOMBS_PLANTED] );
			pkv->SetInt( "bombs_detonated", totalDetonations );
			pkv->SetInt( "bombs_defused", m_roundStats[userSlot][CSSTAT_NUM_BOMBS_DEFUSED] );
			pkv->SetInt( "hostages_rescued", m_roundStats[userSlot][CSSTAT_NUM_HOSTAGES_RESCUED] );
		}

		//
		// Write out: Games played
		pkv = pLeaderboardInfo->FindKey( g_LeaderboardIDMap[boardSetIndex].gpName, true );
		if ( pkv )
		{
			// Run through all medals to determine how many have been unlocked
			CUtlMap<int, CBaseAchievement *> &achievements = g_AchievementMgrCS.GetAchievements( userID );
			DWORD nMedalCount = 0;
			for ( int i=achievements.FirstInorder(); i!=achievements.InvalidIndex(); i=achievements.NextInorder(i) )
			{
				if ( achievements[i]->IsAchieved() )
					++nMedalCount;
			}

			// $TODO: This credits the entire round to having played as CT or T - is there any info about actual playtime?
			int playTimeTotal = m_roundStats[userSlot][CSSTAT_PLAYTIME];
			int ctTime =  bIsCT ? playTimeTotal : 0;
			int tTime	= !bIsCT ? playTimeTotal : 0;

			pkv->SetInt( "num_rounds", m_roundStats[userSlot][CSSTAT_ROUNDS_PLAYED] );
			pkv->SetInt( "time_played", playTimeTotal );
			pkv->SetInt( "time_played_ct", ctTime );
			pkv->SetInt( "time_played_t", tTime );
			pkv->SetInt( "total_medals", nMedalCount );
		}

		DevMsg( "Updating leaderboards with:\n" );
		KeyValuesDumpAsDevMsg( pLeaderboardInfo, 1 );

		pProfile->UpdateLeaderboardData( pLeaderboardInfo );
	}

}



bool CCSClientGameStats::SyncCSMatchmakingDataToTitleData( int iController, CSSyncStatValueDirection_t eOp )
{
	// Get the local player.
	IPlayerLocal *pPlayerLocal = g_pMatchFramework->GetMatchSystem()->GetPlayerManager()->GetLocalPlayer( iController );
	if ( !pPlayerLocal )
		return false;

	return true;
}



bool CCSClientGameStats::SyncCSRankingDataToTitleData( int iController, CSSyncStatValueDirection_t eOp )
{
	// Get the local player.
	IPlayerLocal *pPlayerLocal = g_pMatchFramework->GetMatchSystem()->GetPlayerManager()->GetLocalPlayer( iController );
	if ( !pPlayerLocal )
		return false;

	return true;
}

// Increment the current round matchmaking data by the data in the given stats structure.
// This method may be called multiple times per round so we have to deal with rounds played
// differently.  Each call to the method is the delta from the previous call.
void CCSClientGameStats::IncrementMatchmakingData( const StatsCollection_t &stats )
{
}

// Get the matchmaking data for current primary user and compute the new rolling average based
// on the data accumulated so far.
void CCSClientGameStats::UpdateMatchmakingData( void )
{
}

// Reset the matchmaking data for the current primary user for the given scope.
void CCSClientGameStats::ResetMatchmakingData( MatchmakingDataScope mmDataScope )
{
}

// OGS data and functions

// WARNING: must be in sync with the CSClientCsgoGameEventType_t in .h file
char const *g_CSClientCsgoGameEventTypeNames[] = {
	"Undefined",
	"Spray",
	"ConnectionProblem",
	"ConnectionLoss",
	"ConnectionChoke",
};
// WARNING: must be in sync with the CSClientCsgoGameEventType_t in .h file

void CCSClientGameStats::AddClientCSGOGameEvent( CSClientCsgoGameEventType_t eEvent, Vector const &pos, QAngle const &ang, uint64 ullData /* = 0ull */, char const *szMapName /* = NULL */, int16 nRound /* = CSCLIENTCSGOGAMEEVENTTYPE_AUTODETECT_INT16 */, int16 nRoundSecondsElapsed /* = CSCLIENTCSGOGAMEEVENTTYPE_AUTODETECT_INT16 */ )
{
	CsgoGameEvent_t &cge = m_arrClientCsgoGameEvents[ m_arrClientCsgoGameEvents.AddToTail() ];
	cge.m_eEvent = eEvent;
	cge.m_pos = pos;
	cge.m_ang = ang;
	cge.m_ullData = ullData;
	cge.m_symMap = CUtlSymbol( ( szMapName && *szMapName ) ? szMapName : engine->GetLevelNameShort() );
	cge.m_nRound = nRound;
	if ( nRound == CSCLIENTCSGOGAMEEVENTTYPE_AUTODETECT_INT16 )
	{
		if ( !CSGameRules() || CSGameRules()->IsWarmupPeriod() )
		{
			cge.m_nRound = 0;
		}
		else
		{
			cge.m_nRound = CSGameRules()->GetTotalRoundsPlayed();
			if ( !CSGameRules()->IsRoundOver() )
				++ cge.m_nRound;
		}
	}
	cge.m_numRoundSeconds = nRoundSecondsElapsed;
	if ( nRoundSecondsElapsed == CSCLIENTCSGOGAMEEVENTTYPE_AUTODETECT_INT16 )
	{
		cge.m_numRoundSeconds = gpGlobals->curtime - CSGameRules()->GetRoundStartTime();
	}
	
	switch ( eEvent )
	{
	case k_CSClientCsgoGameEventType_SprayApplication:
		cge.m_bRequireMoreReliableUpload = true;
		break;
	default:
		cge.m_bRequireMoreReliableUpload = false;
		break;
	}
}

ConVar cl_debug_round_stat_submission( "cl_debug_round_stat_submission", "0", FCVAR_DEVELOPMENTONLY );

CCSClientGameStats::StatContainerList_t* CCSClientGameStats::s_StatLists = new CCSClientGameStats::StatContainerList_t();

void CCSClientGameStats::UploadRoundStats()
{
	// Upload all client game events, and remove the ones that we don't need to reupload
	FOR_EACH_VEC( m_arrClientCsgoGameEvents, i )
	{
		CsgoGameEvent_t const &cge = m_arrClientCsgoGameEvents[i];
		char const *szEvent = g_CSClientCsgoGameEventTypeNames[0];
		Assert( cge.m_eEvent > 0 && cge.m_eEvent < Q_ARRAYSIZE( g_CSClientCsgoGameEventTypeNames ) );
		if ( ( cge.m_eEvent > 0 ) && ( cge.m_eEvent < Q_ARRAYSIZE( g_CSClientCsgoGameEventTypeNames ) ) )
			szEvent = g_CSClientCsgoGameEventTypeNames[cge.m_eEvent];

		if ( GetSteamWorksGameStatsClient().AddCsgoGameEventStat( cge.m_symMap.String(), szEvent, cge.m_pos, cge.m_ang, cge.m_ullData, cge.m_nRound, cge.m_numRoundSeconds )
			|| !cge.m_bRequireMoreReliableUpload )
			m_arrClientCsgoGameEvents.Remove( i -- );
	}

	C_CSPlayer *pPlayer = ToCSPlayer( C_BasePlayer::GetLocalPlayer() );

	if ( cl_debug_round_stat_submission.GetBool() )
	{
		Msg( "Attempting to submit round stats... ");
	}

	// Need to have played more than 10 seconds. If you haven't, then that means it's a nop round when first joining a server and having it restart
	// due to having players on it. Also need to ensure that rounds played is greater than 0 since we'll be subtracting 1 to make it 0 based.
	bool bIsValidTimedMatch = ( m_roundStats[0][CSSTAT_PLAYTIME] > 10 && m_matchStats[0][CSSTAT_ROUNDS_PLAYED] > 0 );
	bool bIsValidArmsRaceMatch = CSGameRules()->IsPlayingGunGameProgressive() && ( m_RoundEndReason == CTs_Win || m_RoundEndReason == Terrorists_Win );

	if ( pPlayer && ( bIsValidTimedMatch || bIsValidArmsRaceMatch ) )
	{
		SRoundData roundData( &m_roundStats[0] );

		if ( cl_debug_round_stat_submission.GetBool() )
		{
			Msg( "Client session ID %llu Server Session ID %llu \n", GetSteamWorksGameStatsClient().GetSessionID(), GetSteamWorksGameStatsClient().GetServerSessionID() );
		}
		// Use servers count of rounds this match, not rounds played by this player.
		//pRoundData->nRound = m_matchStats[0][CSSTAT_ROUNDS_PLAYED] - 1;
		roundData.nRound = CSGameRules( )->GetTotalRoundsPlayed( );

		if ( cl_debug_round_stat_submission.GetBool() )
		{
			Msg( "Submitting session id %llu round %d\n", GetSteamWorksGameStatsClient().GetSessionID(), roundData.nRound );
		}

		static int sLastRoundSubmitted = -1;
		static uint64 sLastSessionIDSubmitted = 0;

		if ( sLastRoundSubmitted >= 0 && sLastSessionIDSubmitted != 0 )
		{
			// HACK: We've got so many primary key violations we're effecting OGS perf.
			// We currently think there are community servers running mods/rule changes
			// that are responsible for much of this so we can't fix all of it. This
			// horrible hack will throw out problem submits.
			if ( roundData.nRound <= sLastRoundSubmitted && sLastSessionIDSubmitted == GetSteamWorksGameStatsClient( ).GetSessionID( ) )
			{
				Warning( "OGS PK VIOLATION: Dropping round data for round %d session %llu because we've already submitted it.\n", roundData.nRound, GetSteamWorksGameStatsClient().GetSessionID() );
				m_roundStats[0].Reset();
				return;
			}
		}

		roundData.nReason = m_RoundEndReason;

		// HACK: Adding to the 16th bit of pRoundData->nRoundTime
		// This lets us keep track of whether a player has:
		//	attempted to rescue a hostage.
		roundData.nRoundTime |= ((uint32)m_bObjectiveAttempted) << 16;

															  
		// EXPERIMENTAL COLUMN IN OGS
		//
		// This column is general-purpose, intended to collect interesting stats on a round-by-round granularity
		// RoundData was selected because coarse player attributes (e.g., their current server's tick rate, certain convars)
		// seem unlikely to change at a faster rate. In many cases these values will be repeated across rounds, so most
		// SQL aggregations involving the column will be AVG. 
		//
		// We expect that the specific attributes stored below will change, possibly frequently. In some cases the attributes will
		// be stale. The primary value of this column is to exist as a rapid option for sampling new player stats that are unlikely
		// to be relevant in the distant future.
		//
		// An obvious problem with this implementation is loss of history, so any changes to the experimental column should be
		// listed here, with a timeline. List previously-recorded attributes in little endian order, with #bits 
		// 

		//	Experiment 5:
		//
		//					Primary Weapon Def Index;
		//					Primary Weapon Ammo Count at death;
		//					Secondary Weapon Def Index;
		//					Secondary Weapon Ammo Count at death;
		//
		//					Master Music Volume
		//					Main Menu Volume
		//					Round Start Volume
		//					Round End Volume
		//					Map Objective Volume
		//					Ten Second Warning Volume
		//					Death Cam Volume

		// 		// ConVarRef required 				
		static ConVarRef snd_musicvolume( "snd_musicvolume" );
 		static ConVarRef snd_menumusic_volume( "snd_menumusic_volume" );
		static ConVarRef snd_roundstart_volume( "snd_roundstart_volume" );
		static ConVarRef snd_roundend_volume( "snd_roundend_volume" );
		static ConVarRef snd_mapobjective_volume( "snd_mapobjective_volume" );
		static ConVarRef snd_tensecondwarning_volume( "snd_tensecondwarning_volume" );
		static ConVarRef snd_deathcamera_volume( "snd_deathcamera_volume" );
		static ConVarRef voice_scale("voice_scale");
		
		uint8 *pData = ( uint8* )&roundData.llExperimental;

		//  Ammo count at death OGS data
		//
		*( pData ) = ( uint8 )pPlayer->m_roundEndAmmoCount.nPrimaryWeaponDefIndex;
		*( ++pData ) = ( uint8 )pPlayer->m_roundEndAmmoCount.nPrimaryWeaponAmmoCount;
		*( ++pData ) = ( uint8 )pPlayer->m_roundEndAmmoCount.nSecondaryWeaponDefIndex;
		*( ++pData ) = ( uint8 )pPlayer->m_roundEndAmmoCount.nSecondaryWeaponAmmoCount;
		//
		// end of ammo count at death OGS data

		// This code allowed us to measure discrepency between client and server bullet hits.
		// It became obsolete when we started using a separate seed for client and server
		// to eliminate 'rage' hacks.
		//
		*( ++pData ) = ( uint8 )pPlayer->m_ui8ClientServerHitDifference;

		// Experiment 6:
		// 	   Replay utilization
		// 			EE_REPLAY_OFFERED = 1,
		// 			EE_REPLAY_REQUESTED = 2,
		// 			EE_REPLAY_STARTED = 4,
		// 			EE_REPLAY_CANCELLED = 8,
		// 			EE_REPLAY_AUTOMATIC = 16
		*( ++pData ) = ( uint8 )g_HltvReplaySystem.GetExperimentalEvents();

//		float tickrate = 1.0 / gpGlobals->interval_per_tick;
//		*( ++pData ) = ( uint8 )tickrate;

// 		// Sound and Music
// 		uint8 nMusicVolumeAsPct = (uint8)( 100.0f * snd_musicvolume.GetFloat() );
// 		uint8 nMenuMusicVolumeAsPct = (uint8)( 100.0f * snd_menumusic_volume.GetFloat() );
// 		uint8 nRoundStartVolumeAsPct = (uint8)( 100.0f * snd_roundstart_volume.GetFloat() );
// 		uint8 nRoundEndVolumeAsPct = (uint8)( 100.0f * snd_roundend_volume.GetFloat() );
// 		uint8 nMapObjectiveVolumeAsPct = (uint8)( 100.0f * snd_mapobjective_volume.GetFloat() );
// 		uint8 nTenSecondWarningVolumeAsPct = (uint8)( 100.0f * snd_tensecondwarning_volume.GetFloat() );
// 		uint8 nDeathCameraVolumeAsPct = (uint8)( 100.0f * snd_deathcamera_volume.GetFloat() );
// 
// 		// Pack Reasonable Volume Duplets to fit 64 bits - set each to 0-10 range, pack into left and right 4 bits.
// 		uint8 nMusicAndMenuMusicVolume =  (uint8)( ( nMusicVolumeAsPct / 10 ) | ( nMenuMusicVolumeAsPct / 10 ) << 4 );
// 		uint8 nRoundStartAndEndVolume =  (uint8)( ( nRoundStartVolumeAsPct / 10 ) | ( nRoundEndVolumeAsPct / 10 ) << 4 );
// 		uint8 nMapObjectiveAndWarning =  (uint8)( ( nMapObjectiveVolumeAsPct / 10 ) | ( nTenSecondWarningVolumeAsPct / 10 ) << 4 );
// 
// 		*( ++pData ) = (uint8)nMusicAndMenuMusicVolume;
// 		*( ++pData ) = (uint8)nRoundStartAndEndVolume;
// 		*( ++pData ) = (uint8)nMapObjectiveAndWarning;
// 		*( ++pData ) = (uint8)nDeathCameraVolumeAsPct;		


		// END EXPERIMENTAL


		// Our current money + what we spent is what we started with at the beginning of round
		roundData.nStartingMoney = pPlayer->m_iStartAccount & 0x0000FFFF;
		//NOTHER TEMP HACK: Store round start player net worth in the top 16 bits of starting money
		roundData.nStartingMoney |= ((uint32)pPlayer->GetRoundStartEquipmentValue( )) << 16;
		roundData.nTeamID = pPlayer->m_iTeamNum;

		if ( cl_debug_round_stat_submission.GetBool() )
			Msg( "Setting team num %d", roundData.nTeamID );

		if( CSGameRules()->IsPlayingGunGameProgressive() )
		{
			roundData.nRoundScore = m_roundStats[ 0 ][ CSSTAT_GG_PROGRESSIVE_CONTRIBUTION_SCORE ];
		}
		else
		{
			roundData.nRoundScore = m_roundStats[ 0 ][ CSSTAT_CONTRIBUTION_SCORE ];
		}

		// Send off all OGS stats at level shutdown
		KeyValues *pKV = new KeyValues( "basedata" );
		if ( !pKV )
			return;

		char szMapNameBuffer[MAX_PATH];
		V_strcpy_safe( szMapNameBuffer, engine->GetLevelName() );
		V_FixSlashes( szMapNameBuffer, '/' ); // use consistent slashes so we don't get double entries for different platforms
		V_StripExtension(szMapNameBuffer, szMapNameBuffer, sizeof( szMapNameBuffer ) );

		int nLen = V_strlen( "maps/" );
		if ( StringHasPrefix( szMapNameBuffer, "maps/" ) && *( szMapNameBuffer + nLen ) )
		{
			// skip maps dir
			pKV->SetString( "MapID", szMapNameBuffer + nLen );
		}
		else
		{
			pKV->SetString( "MapID", szMapNameBuffer );
		}

		SubmitStat( &roundData );

		// Perform the actual submission
		SubmitGameStats( pKV );

		sLastRoundSubmitted = CSGameRules()->GetTotalRoundsPlayed();
		sLastSessionIDSubmitted = GetSteamWorksGameStatsClient().GetSessionID();

		pKV->deleteThis();

		m_RoundEndReason = Invalid_Round_End_Reason;
		m_bObjectiveAttempted = false;
	}
	else if ( cl_debug_round_stat_submission.GetBool() )
	{
		Msg( "Skipping -- Client thinks round time is %d and num matches is %d\n",  m_roundStats[0][CSSTAT_PLAYTIME],  m_matchStats[0][CSSTAT_ROUNDS_PLAYED] );
	}

	m_roundStats[0].Reset();
}

