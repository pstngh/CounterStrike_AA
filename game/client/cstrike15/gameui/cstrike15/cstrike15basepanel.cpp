//===== Copyright  1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Main panel for CS:GO UI
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "cstrike15basepanel.h"

#include "engineinterface.h"
#include "steam/steam_api.h"
#include "vguisystemmoduleloader.h"

#include "gameui_interface.h"
#include "uigamedata.h"
#include "cdll_client_int.h"

#include "vgui/ILocalize.h"


#include "cdll_util.h"
#include "c_baseplayer.h"
#include "c_cs_player.h"
#include "inputsystem/iinputsystem.h"

#include "cstrike15_gcmessages.pb.h"

#include "cs_gamerules.h"
#include "clientmode_csnormal.h"
#include "c_cs_playerresource.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static CCStrike15BasePanel *g_pCStrike15BasePanel = NULL;

// [jason] For tracking the last team played by the main user (TEAM_CT by default)
ConVar player_teamplayedlast( "player_teamplayedlast", "3", FCVAR_ARCHIVE | FCVAR_ARCHIVE_GAMECONSOLE | FCVAR_SS );
// for tracking whether the player dismissed the community server warning message
ConVar player_nevershow_communityservermessage( "player_nevershow_communityservermessage", "0", FCVAR_ARCHIVE | FCVAR_ARCHIVE_GAMECONSOLE | FCVAR_SS );

static bool s_bSteamOverlayPositionNeedsToBeSet = true;
static void FnSteamOverlayChangeCallback( IConVar *var, const char *pOldValue, float flOldValue )
{
	s_bSteamOverlayPositionNeedsToBeSet = true;
}
ConVar ui_steam_overlay_notification_position( "ui_steam_overlay_notification_position", "topleft", FCVAR_ARCHIVE, "Steam overlay notification position", FnSteamOverlayChangeCallback );

float g_flReadyToCheckForPCBootInvite = 0;
static ConVar connect_lobby( "connect_lobby", "", FCVAR_HIDDEN, "Sets the lobby ID to connect to on start." );


//-----------------------------------------------------------------------------
// Purpose: singleton accessor
//-----------------------------------------------------------------------------
CBaseModPanel *BasePanel()
{
	return g_pCStrike15BasePanel;
}

//-----------------------------------------------------------------------------
// Purpose: singleton accessor (constructs)
//-----------------------------------------------------------------------------
CBaseModPanel *BasePanelSingleton()
{
	if ( !g_pCStrike15BasePanel )
	{
		g_pCStrike15BasePanel = new CCStrike15BasePanel();
	}
	return g_pCStrike15BasePanel;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CCStrike15BasePanel::CCStrike15BasePanel() :
	BaseClass( "CStrike15BasePanel" ),
	m_pSplitScreenSignon( NULL ),
	m_OnClosedCommand( ON_CLOSED_NULL ),
	m_bMigratingActive( false ),
	m_bShowRequiredGameVoiceChannelUI( false ),
    m_bNeedToStartIntroMovie( true ),
    m_bTestedStaticIntroMovieDependencies( false ),
	m_bStartLogoIsShowing( false ),
	m_bServerBrowserWarningRaised( false ),
	m_bCommunityQuickPlayWarningRaised( false ),
	m_bCommunityServerWarningIssued( false ),
	m_bGameIsShuttingDown( false )
{
	g_pMatchFramework->GetEventsSubscription()->Subscribe( this );

}

//-----------------------------------------------------------------------------
CCStrike15BasePanel::~CCStrike15BasePanel()
{
	m_bGameIsShuttingDown = true;

	StopListeningForAllEvents();

	// [jason] Release any screens that may still be active on shutdown so we don't leak memory
}

void CCStrike15BasePanel::OnEvent( KeyValues *pEvent )
{
	const char *pEventName = pEvent->GetName();

	if ( !Q_strcmp( pEventName, "OnSysSigninChange" ) )
	{
		if ( !Q_stricmp( "signout", pEvent->GetString( "action", "" ) ) )
		{
			int primaryID = pEvent->GetInt( "user0", -1 );

			ACTIVE_SPLITSCREEN_PLAYER_GUARD( GET_ACTIVE_SPLITSCREEN_SLOT() );
			if ( XBX_GetSlotByUserId( primaryID ) != -1 )
			{
				// go to the start screen
				// $TODO ?? display message stating you got booted back to start cuz you signed out
				if ( GameUI().IsInLevel() ) 
				{
					m_bForceStartScreen = true;
					engine->ClientCmd_Unrestricted( "disconnect" );
				}
				else
				{
					HandleOpenCreateStartScreen();			
				}
			}
		}
		if ( !Q_stricmp( "signin", pEvent->GetString( "action", "" ) ) )
		{
		}

		UpdateRichPresenceInfo();
	}
	else if ( !Q_stricmp( pEventName, "OnDemoFileEndReached" ) )
	{
		if ( CDemoPlaybackParameters_t const *pParameters = engine->GetDemoPlaybackParameters() )
		{
			if ( pParameters->m_bAnonymousPlayerIdentity &&
				pParameters->m_uiLockFirstPersonAccountID &&
				pParameters->m_uiCaseID )
			{
			}
		}
		engine->ClientCmd_Unrestricted( "disconnect" );
	}
}

void CCStrike15BasePanel::FireGameEvent( IGameEvent *event )
{
	const char *type = event->GetName();
	C_CSPlayer *pLocalPlayer = C_CSPlayer::GetLocalCSPlayer();

	if ( StringHasPrefix( type, "player_team" ) )
	{
		if ( pLocalPlayer )
		{
			int newTeam = event->GetInt( "team" );
			int userId = event->GetInt( "userid" );

			if ( pLocalPlayer->GetUserID() == userId )
			{
				if ( newTeam == TEAM_CT || newTeam == TEAM_TERRORIST )
					player_teamplayedlast.SetValue( newTeam );
			}
		}
	}
	else if ( StringHasPrefix( type, "cs_game_disconnected" ) )
	{
		if ( GameUI().IsInLevel() )
		{
			// Ensure we remove any pending dialogs as soon as we receive notification that the client is disconnecting
			//	(fixes issue with the quit dialog staying up when you "disconnect" via console window)
			//  Passing in false to indicate we do not wish to dismiss CCommandMsgBoxes, which indicate error codes/kick reasons/etc
		}
	}
}

//////////////////////////////////////////////////////////////////////////
//
// Avatars conversion to rgb
//
#include "imageutils.h"
CON_COMMAND_F( cl_avatar_convert_rgb, "Converts all png avatars in the avatars directory to rgb", FCVAR_RELEASE | FCVAR_CHEAT )
{
	FileFindHandle_t hFind = NULL;
	for ( char const *szFileName = g_pFullFileSystem->FindFirst( "avatars/*.png", &hFind );
		szFileName && *szFileName; szFileName = g_pFullFileSystem->FindNext( hFind ) )
	{
		CFmtStr sFile( "avatars/%s", szFileName );
		char chRgbFile[MAX_PATH] = {};
		V_strcpy_safe( chRgbFile, sFile );
		V_SetExtension( chRgbFile, ".rgb", MAX_PATH );

		int width = 0, height = 0;
		ConversionErrorType cet = CE_ERROR_WRITING_OUTPUT_FILE;
		unsigned char *pbImgData = ImgUtl_ReadPNGAsRGBA( sFile, width, height, cet );

		if ( ( cet == CE_SUCCESS ) && ( width == 64 ) && ( height == 64 ) && pbImgData )
		{
			// trim alpha for size
			for ( int y = 0; y < 64; ++y ) for ( int x = 0; x < 64; ++x )
			{
				V_memmove( pbImgData + y * 64 * 3 + x * 3, pbImgData + y * 64 * 4 + x * 4, 3 );
			}
			CUtlBuffer bufWriteExternal( pbImgData, 64*64*3, CUtlBuffer::READ_ONLY );
			bufWriteExternal.SeekPut( CUtlBuffer::SEEK_HEAD, 64*64*3 );
			if ( g_pFullFileSystem->WriteFile( chRgbFile, "MOD", bufWriteExternal ) )
			{
				Msg( "Converted rgb '%s'->'%s'.\n", sFile.Access(), chRgbFile );
			}
			else
			{
				Warning( "Failed to save converted rgb '%s'->'%s'.\n", sFile.Access(), chRgbFile );
			}
		}
		else
		{
			Warning( "Invalid conversion source '%s' (%d/%p; %dx%d), expecting 64x64 PNG.\n", sFile.Access(), cet, pbImgData, width, height );
		}
		
		if ( pbImgData )
		{
			free( pbImgData );
		}
	}
	g_pFullFileSystem->FindClose( hFind );
}

// lwss- Overrides for the basepanel if either of these systems are enabled.
