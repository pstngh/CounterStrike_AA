//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Prints chat, server text messages and kills to the console, which
//          is the only UI in this build.
//
//=============================================================================//

#include "cbase.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "hud_basechat.h"
#include "text_message.h"
#include "c_playerresource.h"
#include "c_cs_playerresource.h"
#include "voice_status.h"
#include "vgui/ILocalize.h"
#include "engine/IEngineSound.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CHudConsoleMessages : public CHudElement
{
public:
	explicit CHudConsoleMessages( const char *pElementName );

	virtual void Init( void ) OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;

	bool MsgFunc_SayText( const CCSUsrMsg_SayText &msg );
	bool MsgFunc_SayText2( const CCSUsrMsg_SayText2 &msg );
	bool MsgFunc_TextMsg( const CCSUsrMsg_TextMsg &msg );

	CUserMessageBinder m_UMCMsgSayText;
	CUserMessageBinder m_UMCMsgSayText2;
	CUserMessageBinder m_UMCMsgTextMsg;
};

DECLARE_HUDELEMENT( CHudConsoleMessages );
DECLARE_HUD_MESSAGE( CHudConsoleMessages, SayText );
DECLARE_HUD_MESSAGE( CHudConsoleMessages, SayText2 );
DECLARE_HUD_MESSAGE( CHudConsoleMessages, TextMsg );

CHudConsoleMessages::CHudConsoleMessages( const char *pElementName ) : CHudElement( pElementName )
{
}

void CHudConsoleMessages::Init( void )
{
	HOOK_HUD_MESSAGE( CHudConsoleMessages, SayText );
	HOOK_HUD_MESSAGE( CHudConsoleMessages, SayText2 );
	HOOK_HUD_MESSAGE( CHudConsoleMessages, TextMsg );

	ListenForGameEvent( "player_death" );
}

static void PlayChatSound()
{
	CLocalPlayerFilter filter;
	C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, "HudChat.Message" );
}

// Text sent by the server itself, such as `say` typed in a server console.
bool CHudConsoleMessages::MsgFunc_SayText( const CCSUsrMsg_SayText &msg )
{
	char szText[512];
	V_strcpy_safe( szText, msg.text().c_str() );
	Msg( "%s\n", RemoveColorMarkup( ConvertCRtoNL( szText ) ) );

	if ( msg.chat() )
		PlayChatSound();
	return true;
}

// Player chat. Formatted as CHudChat formats it.
bool CHudConsoleMessages::MsgFunc_SayText2( const CCSUsrMsg_SayText2 &msg )
{
	// Messages that arrive while connecting have no player names yet.
	if ( !g_PR )
		return true;

	int client = msg.ent_idx();
	if ( client > 0 && ( GetClientVoiceMgr()->IsPlayerBlocked( client ) || GetClientVoiceMgr()->ShouldHideCommunicationFromPlayer( client ) ) &&
		V_strcmp( msg.params( 3 ).c_str(), "auto" ) != 0 )
	{
		return true;
	}

	wchar_t szBuf[6][256];
	wchar_t *pFormat = ReadLocalizedString( msg.msg_name().c_str(), szBuf[0], sizeof( szBuf[0] ), false );
	ReadChatTextString( msg.params( 0 ).c_str(), szBuf[1], sizeof( szBuf[1] ) );		// player name
	ReadChatTextString( msg.params( 1 ).c_str(), szBuf[2], sizeof( szBuf[2] ), true );	// chat text
	ReadLocalizedString( msg.params( 2 ).c_str(), szBuf[3], sizeof( szBuf[3] ), true );	// location
	ReadLocalizedString( msg.params( 3 ).c_str(), szBuf[4], sizeof( szBuf[4] ), true );
	g_pVGuiLocalize->ConstructString( szBuf[5], sizeof( szBuf[5] ), pFormat, 4, szBuf[1], szBuf[2], szBuf[3], szBuf[4] );

	char szText[512];
	g_pVGuiLocalize->ConvertUnicodeToANSI( ConvertCRtoNL( szBuf[5] ), szText, sizeof( szText ) );
	Msg( "%s\n", RemoveColorMarkup( szText ) );

	if ( msg.chat() )
		PlayChatSound();
	return true;
}

// Localized server messages ("#Cstrike_..." tokens with up to four parameters).
bool CHudConsoleMessages::MsgFunc_TextMsg( const CCSUsrMsg_TextMsg &msg )
{
	if ( !cl_showtextmsg.GetBool() )
		return true;

	int dest = msg.msg_dst();
#if defined( INCLUDE_ROCKETUI )
	// RocketUI's chat already prints console and notify messages.
	if ( dest == HUD_PRINTCONSOLE || dest == HUD_PRINTNOTIFY )
		return true;
#endif

	wchar_t szBuf[5][256] = {};
	for ( int i = 0; i < 5 && i < msg.params_size(); i++ )
	{
		const char *pParam = msg.params( i ).c_str();

		// "#ENTNAME[<index>]<fallback>" names a player.
		if ( const char *pEntIndex = StringAfterPrefix( pParam, "#ENTNAME[" ) )
		{
			int iEntIndex = V_atoi( pEntIndex );
			if ( C_CS_PlayerResource *pCSPR = ( C_CS_PlayerResource * )GameResources() )
			{
				pCSPR->GetDecoratedPlayerName( iEntIndex, szBuf[i], sizeof( szBuf[i] ),
					( EDecoratedPlayerNameFlag_t )( k_EDecoratedPlayerNameFlag_DontUseNameOfControllingPlayer | k_EDecoratedPlayerNameFlag_DontUseAssassinationTargetName ) );
			}
			if ( szBuf[i][0] )
				continue;
			const char *pEndBracket = V_strnchr( pEntIndex, ']', 64 );
			pParam = pEndBracket ? pEndBracket + 1 : pParam;
		}

		char szParam[1024];
		V_strcpy_safe( szParam, hudtextmessage->LookupString( pParam, i == 0 ? &dest : NULL ) );
		if ( szParam[0] == '#' )
		{
			if ( const wchar_t *pLocalized = g_pVGuiLocalize->Find( szParam ) )
			{
				V_wcsncpy( szBuf[i], pLocalized, sizeof( szBuf[i] ) );
				continue;
			}
		}

		// Parameters are substituted into the first string; drop their newline.
		int nLen = V_strlen( szParam );
		if ( i > 0 && nLen > 0 && ( szParam[nLen - 1] == '\n' || szParam[nLen - 1] == '\r' ) )
			szParam[nLen - 1] = 0;
		g_pVGuiLocalize->ConvertANSIToUnicode( szParam, szBuf[i], sizeof( szBuf[i] ) );
	}

	wchar_t szOutput[512];
	g_pVGuiLocalize->ConstructString( szOutput, sizeof( szOutput ), szBuf[0], 4, szBuf[1], szBuf[2], szBuf[3], szBuf[4] );

	char szText[1024];
	g_pVGuiLocalize->ConvertUnicodeToANSI( ConvertCRtoNL( szOutput ), szText, sizeof( szText ) );
	int nLen = V_strlen( szText );
	if ( nLen == 0 )
		return true;
	Msg( "%s%s", RemoveColorMarkup( szText ), szText[nLen - 1] == '\n' ? "" : "\n" );
	return true;
}

// Kill feed: "Attacker killed Victim with weapon (headshot, through a wall)".
void CHudConsoleMessages::FireGameEvent( IGameEvent *event )
{
	if ( !g_PR || V_strcmp( event->GetName(), "player_death" ) )
		return;

	int nVictim = engine->GetPlayerForUserID( event->GetInt( "userid" ) );
	int nAttacker = engine->GetPlayerForUserID( event->GetInt( "attacker" ) );
	if ( nVictim <= 0 )
		return;

	const char *pVictim = g_PR->GetPlayerName( nVictim );
	if ( nAttacker <= 0 || nAttacker == nVictim )
	{
		Msg( "%s died\n", pVictim );
		return;
	}

	bool bHeadshot = event->GetInt( "headshot" ) > 0;
	bool bWallbang = event->GetInt( "penetrated" ) > 0;
	const char *pDetails = bHeadshot && bWallbang ? " (headshot, through a wall)" :
		bHeadshot ? " (headshot)" :
		bWallbang ? " (through a wall)" : "";
	Msg( "%s killed %s with %s%s\n", g_PR->GetPlayerName( nAttacker ), pVictim, event->GetString( "weapon" ), pDetails );
}
