//========= Copyright  1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef CSTRIKE15BASEPANEL_H
#define CSTRIKE15BASEPANEL_H

#ifdef _WIN32
#pragma once
#endif

#include "basepanel.h"
#include "matchmaking/imatchevents.h"
#include "GameEventListener.h"

class SplitScreenSignonWidget;
//-----------------------------------------------------------------------------
// Purpose: This is the panel at the top of the panel hierarchy for GameUI
//			It handles all the menus, background images, and loading dialogs
//-----------------------------------------------------------------------------
class CCStrike15BasePanel: public CBaseModPanel, public IMatchEventsSink, public CGameEventListener
{
	DECLARE_CLASS_SIMPLE( CCStrike15BasePanel, CBaseModPanel );

public:
	CCStrike15BasePanel();
	virtual ~CCStrike15BasePanel();

	virtual void OnEvent( KeyValues *pEvent );

	virtual void FireGameEvent( IGameEvent *event );

protected:
	enum CCSOnClosedCommand
	{
		ON_CLOSED_NULL,
		ON_CLOSED_DISCONNECT,
		ON_CLOSED_QUIT,
		ON_CLOSED_RESTORE_PAUSE_MENU,
		ON_CLOSED_RESTORE_MAIN_MENU,
		ON_CLOSED_DISCONNECT_TO_MP_GAME_MENU, // quit from a game and return to the create game menu instead of main menu
	};

	CCSOnClosedCommand m_OnClosedCommand;

	bool	m_bMigratingActive;

	bool	m_bShowRequiredGameVoiceChannelUI;
	CountdownTimer m_GameVoiceChannelRecheckTimer;

    bool m_bNeedToStartIntroMovie;
    bool m_bTestedStaticIntroMovieDependencies;


private:

	SplitScreenSignonWidget* m_pSplitScreenSignon;
	bool	m_bStartLogoIsShowing;
	bool m_bServerBrowserWarningRaised;
	bool m_bCommunityQuickPlayWarningRaised;
	bool m_bCommunityServerWarningIssued;
	bool m_bGameIsShuttingDown;
};

inline void GameStats_UserStartedPlaying( float flTime ) {}
inline void GameStats_ReportAction( char const *szReportAction ) {}

#endif // CSTRIKE15BASEPANEL_H

