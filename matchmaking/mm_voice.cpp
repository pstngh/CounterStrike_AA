//===== Copyright � 1996-2009, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#include "mm_voice.h"

#include "fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define MMVOICEMSG(...) ((void)0)
#define MMVOICEMSG2(...) ((void)0)

#if !defined(NO_STEAM) && !defined( SWDS )
static inline bool FriendRelationshipMute( int iRelationship )
{
	switch ( iRelationship )
	{
	case k_EFriendRelationshipBlocked:
	case k_EFriendFlagIgnored:
	case k_EFriendFlagIgnoredFriend:
		return true;
	default:
		return false;
	}
}
#endif

//
// Construction/destruction
//

CMatchVoice::CMatchVoice()
{
	;
}

CMatchVoice::~CMatchVoice()
{
	;
}

static CMatchVoice g_MatchVoice;
CMatchVoice *g_pMatchVoice = &g_MatchVoice;

//
// Implementation
//

// Whether remote player talking can be visualized / audible
bool CMatchVoice::CanPlaybackTalker( XUID xuidTalker )
{
	if ( IsMachineMutingLocalTalkers( xuidTalker ) )
	{
		MMVOICEMSG2( "CanPlaybackTalker(0x%llX)=false(IsMachineMutingLocalTalkers)\n", xuidTalker );
		return false;
	}

	if ( IsMachineMuted( xuidTalker ) )
	{
		MMVOICEMSG2( "CanPlaybackTalker(0x%llX)=false(IsMachineMuted)\n", xuidTalker );
		return false;
	}

	return true;
}

// Whether we are explicitly muting a remote player
bool CMatchVoice::IsTalkerMuted( XUID xuidTalker )
{

#if !defined(NO_STEAM) && !defined( SWDS )
	if ( FriendRelationshipMute( steamapicontext->SteamFriends()->GetFriendRelationship( xuidTalker ) ) )
	{
		MMVOICEMSG( "IsTalkerMuted(0x%llX)=true(GetFriendRelationship=0x%X)\n", xuidTalker, steamapicontext->SteamFriends()->GetFriendRelationship( xuidTalker ) );
		return true;
	}

	if ( m_arrMutedTalkers.Find( xuidTalker ) != m_arrMutedTalkers.InvalidIndex() )
	{
		MMVOICEMSG( "IsTalkerMuted(0x%llX)=true(locallist)\n", xuidTalker );
		return true;
	}
#endif

	xuidTalker = RemapTalkerXuid( xuidTalker );

#if !defined(NO_STEAM) && !defined( SWDS )
	if ( FriendRelationshipMute( steamapicontext->SteamFriends()->GetFriendRelationship( xuidTalker ) ) )
	{
		MMVOICEMSG( "IsTalkerMuted(0x%llX/0x%llX)=true(GetFriendRelationship=0x%X)\n", xuidTalker, xuidOriginal, steamapicontext->SteamFriends()->GetFriendRelationship( xuidTalker ) );
		return true;
	}
#endif

	if ( m_arrMutedTalkers.Find( xuidTalker ) != m_arrMutedTalkers.InvalidIndex() )
	{
		MMVOICEMSG( "IsTalkerMuted(0x%llX/0x%llX)=true(locallist)\n", xuidTalker, xuidOriginal );
		return true;
	}

	return false;
}

// Whether we are muting any player on the player's machine
bool CMatchVoice::IsMachineMuted( XUID xuidPlayer )
{
	return IsTalkerMuted( xuidPlayer );
}

// X360: Remap XUID of a player to a valid LIVE-enabled XUID
// PS3: Remap SteamID of a player to a PSN ID
XUID CMatchVoice::RemapTalkerXuid( XUID xuidTalker )
{
	if ( !IsGameConsole() )
		return xuidTalker;

	// Find the session and the talker within session members
	IMatchSession *pMatchSession = g_pMatchFramework->GetMatchSession();
	if ( !pMatchSession )
		return xuidTalker;

	KeyValues *pSettings = pMatchSession->GetSessionSettings();

	KeyValues *pMachine = NULL;
	KeyValues *pTalker = SessionMembersFindPlayer( pSettings, xuidTalker, &pMachine );
	if ( !pTalker || !pMachine )
		return xuidTalker;

	// Check this user name if he is a guest
	char const *szTalkerName = pTalker->GetString( "name" );
	char const *pchr = strchr( szTalkerName, '(' );
	if ( !pchr )
		return xuidTalker;	// user is not a guest

	// Find another user from the same machine
	int numPlayers = pMachine->GetInt( "numPlayers" );
	for ( int k = 0; k < numPlayers; ++ k )
	{
		KeyValues *pOtherPlayer = pMachine->FindKey( CFmtStr( "player%d", k ) );
		if ( !pOtherPlayer )
			continue;

		char const *szOtherName = pOtherPlayer->GetString( "name" );
		if ( strchr( szOtherName, '(' ) )
			continue;

		XUID xuidOther = pOtherPlayer->GetUint64( "xuid" );
		if ( xuidOther )
			return xuidOther;
	}

	// No remapping
	return xuidTalker;
}

// Check player-player voice privileges for machine blocking purposes
bool CMatchVoice::IsTalkerMutedWithPrivileges( int dwCtrlr, XUID xuidTalker )
{

	if ( m_arrMutedTalkers.Find( xuidTalker ) != m_arrMutedTalkers.InvalidIndex() )
	{
		MMVOICEMSG( "IsTalkerMutedWithPrivileges(%d/0x%llX)=true(locallist)\n", dwCtrlr, xuidTalker );
		return true;
	}

	return false;
}

// Check if player machine is muting any of local players
bool CMatchVoice::IsMachineMutingLocalTalkers( XUID xuidPlayer )
{
	// Find the session and the talker within session members
	IMatchSession *pMatchSession = g_pMatchFramework->GetMatchSession();
	if ( !pMatchSession )
		return false;

	KeyValues *pSettings = pMatchSession->GetSessionSettings();

	KeyValues *pMachine = NULL;
	SessionMembersFindPlayer( pSettings, xuidPlayer, &pMachine );
	if ( !pMachine )
		return false;

	// Find the local player record in the session
	XUID xuidLocalId = g_pPlayerManager->GetLocalPlayer( XBX_GetPrimaryUserId() )->GetXUID();
	KeyValues *pLocalMachine = NULL;
	SessionMembersFindPlayer( pSettings, xuidLocalId, &pLocalMachine );
	if ( !pLocalMachine || pLocalMachine == pMachine )
		return false;
	int numLocalPlayers = pLocalMachine->GetInt( "numPlayers" );

	// Check the mutelist on the machine
	if ( KeyValues *pMutelist = pMachine->FindKey( "Mutelist" ) )
	{
		for ( KeyValues *val = pMutelist->GetFirstValue(); val; val = val->GetNextValue() )
		{
			XUID xuidMuted = val->GetUint64();
			if ( !xuidMuted )
				continue;

			for ( int iLocal = 0; iLocal < numLocalPlayers; ++ iLocal )
			{
				XUID xuidLocal = pLocalMachine->GetUint64( CFmtStr( "player%d/xuid", iLocal ) );
				if ( xuidMuted == xuidLocal )
				{
					MMVOICEMSG2( "IsMachineMutingLocalTalkers(0x%llX/0x%llX)=true(mutelist)\n", xuidPlayer, xuidLocal );
					return true;
				}
			}
		}
	}

	return false;
}

// Whether voice recording mode is currently active
bool CMatchVoice::IsVoiceRecording()
{
#if !defined(NO_STEAM) && !defined( SWDS )

	EVoiceResult res = steamapicontext->SteamUser()->GetAvailableVoice( NULL, NULL, 0 );

	switch ( res )
	{
	case k_EVoiceResultOK:
	case k_EVoiceResultNoData:
		return true;
	default:
		return false;
	}
#endif

	return false;
}

// Enable or disable voice recording
void CMatchVoice::SetVoiceRecording( bool bRecordingEnabled )
{
#if !defined(NO_STEAM) && !defined( SWDS )
	if ( bRecordingEnabled )
		steamapicontext->SteamUser()->StartVoiceRecording();
	else
		steamapicontext->SteamUser()->StopVoiceRecording();
#endif
}

// Enable or disable voice mute for a given talker
void CMatchVoice::MuteTalker( XUID xuidTalker, bool bMute )
{
#if !defined(NO_STEAM) && !defined( SWDS )
	if ( !xuidTalker )
	{
		if ( !bMute )
			m_arrMutedTalkers.Purge();
	}
	else
	{
		m_arrMutedTalkers.FindAndFastRemove( xuidTalker );
		if ( bMute )
		{
			m_arrMutedTalkers.AddToTail( xuidTalker );
		}
	}
	
	g_pMatchFramework->GetEventsSubscription()->BroadcastEvent( new KeyValues( "OnSysMuteListChanged" ) );
#endif
}

CON_COMMAND( voice_reset_mutelist, "Reset all mute information for all players who were ever muted." )
{
	g_pMatchVoice->MuteTalker( 0ull, false );
	Msg( "Mute list cleared.\n" );
}

#if !defined( NO_STEAM )
CON_COMMAND( voice_mute, "Mute a specific Steam user" )
{
	if ( args.ArgC() != 2 )
	{
		goto usage;
	}
	else
	{
		int iUserId = Q_atoi( args.Arg( 1 ) );
		player_info_t pi;
		if ( !g_pMatchExtensions->GetIVEngineClient()->GetPlayerInfo( iUserId, &pi ) || !pi.xuid )
		{
			Msg( "Player# is invalid or refers to a bot, please use \"voice_show_mute\" command.\n" );
			goto usage;
		}

		g_pMatchVoice->MuteTalker( pi.xuid, true );
		if ( !g_pMatchExtensions->GetIVEngineClient()->GetDemoPlaybackParameters() )
		{
			Msg( "%s is now muted.\n", pi.name );
		}
		return;
	}

usage:
	Msg( "Example usage: voice_mute player#   -   where player# is a number that you can find with \"voice_show_mute\" command.\n" );
}

CON_COMMAND( voice_unmute, "Unmute a specific Steam user, or `all` to unmute all connected players." )
{
	if ( args.ArgC() != 2 )
	{
		goto usage;
	}
	else
	{
		if ( !Q_stricmp( "all", args.Arg(1) ) )
		{
			XUID xuidLocal = g_pPlayerManager->GetLocalPlayer( XBX_GetPrimaryUserId() )->GetXUID();
			int maxClients = g_pMatchExtensions->GetIVEngineClient()->GetMaxClients();
			for ( int i = 1; i <= maxClients; ++ i )
			{
				// Get the player info from the engine	
				player_info_t pi;
				if ( !g_pMatchExtensions->GetIVEngineClient()->GetPlayerInfo( i, &pi ) )
					continue;
				if ( !pi.xuid )
					continue;
				if ( pi.xuid == xuidLocal )
					continue;

				g_pMatchVoice->MuteTalker( pi.xuid, false );
			}
			Msg( "All connected players have been unmuted.\n" );
			return;
		}

		int iUserId = Q_atoi( args.Arg( 1 ) );
		player_info_t pi;
		if ( !g_pMatchExtensions->GetIVEngineClient()->GetPlayerInfo( iUserId, &pi ) || !pi.xuid )
		{
			Msg( "Player# is invalid or refers to a bot, please use \"voice_show_mute\" command.\n" );
			goto usage;
		}

		g_pMatchVoice->MuteTalker( pi.xuid, false );
		if ( !g_pMatchExtensions->GetIVEngineClient()->GetDemoPlaybackParameters() )
		{
			Msg( "%s is now unmuted.\n", pi.name );
		}
		return;
	}

usage:
	Msg( "Example usage: voice_unmute {player#|all}   -   where player# is a number that you can find with \"voice_show_mute\" command, or all to unmute all connected players.\n" );
}

CON_COMMAND( voice_show_mute, "Show whether current players are muted." )
{
	if ( g_pMatchExtensions->GetIVEngineClient()->GetDemoPlaybackParameters() )
		return;

	bool bPrinted = false;
	XUID xuidLocal = g_pPlayerManager->GetLocalPlayer( XBX_GetPrimaryUserId() )->GetXUID();
	int maxClients = g_pMatchExtensions->GetIVEngineClient()->GetMaxClients();
	for ( int i = 1; i <= maxClients; ++ i )
	{
		// Get the player info from the engine	
		player_info_t pi;
		if ( !g_pMatchExtensions->GetIVEngineClient()->GetPlayerInfo( i, &pi ) )
			continue;
		if ( !pi.xuid )
			continue;
		if ( pi.xuid == xuidLocal )
			continue;

		if ( !bPrinted )
		{
			bPrinted = true;
			Msg( "Player#     Player Name\n" );
			Msg( "-------     ----------------\n" );
		}

		Msg( " % 2d %s %s\n", i, g_pMatchVoice->IsTalkerMuted( pi.xuid ) ? "(muted)" : "       ", pi.name );
	}
	
	if ( bPrinted )
	{
		Msg( "-------     ----------------\n" );
	}
	else
	{
		Msg( "No players currently connected who can be muted.\n" );
	}
}
#endif
