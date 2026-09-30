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
#if defined( INCLUDE_SCALEFORM )
#include "messagebox_scaleform.h"
#endif
#include "GameEventListener.h"
#include "splitscreensignon.h"

class SplitScreenSignonWidget;
//-----------------------------------------------------------------------------
// Purpose: This is the panel at the top of the panel hierarchy for GameUI
//			It handles all the menus, background images, and loading dialogs
//-----------------------------------------------------------------------------
#if defined( INCLUDE_SCALEFORM )
class CCStrike15BasePanel: public CBaseModPanel, public IMessageBoxEventCallback, public IMatchEventsSink, public CGameEventListener
#else
class CCStrike15BasePanel: public CBaseModPanel, public IMatchEventsSink, public CGameEventListener
#endif
{
	DECLARE_CLASS_SIMPLE( CCStrike15BasePanel, CBaseModPanel );

public:
	CCStrike15BasePanel();
	virtual ~CCStrike15BasePanel();

	virtual void OnEvent( KeyValues *pEvent );

	virtual void FireGameEvent( IGameEvent *event );

#if defined(INCLUDE_SCALEFORM)
	virtual void OnOpenCreateStartScreen( void ); // [jason] provides the "Press Start" screen interface
	virtual void DismissStartScreen( void );
	virtual bool IsStartScreenActive( void );

	virtual void OnOpenCreateMainMenuScreen( void ); 
	virtual void DismissMainMenuScreen( void );
	virtual void RestoreMainMenuScreen( void );
	virtual void DismissAllMainMenuScreens( bool bHideMainMenuOnly = false );

	void RestoreMPGameMenu( void );

	virtual void ShowScaleformMainMenu( bool bShow );
	virtual bool IsScaleformMainMenuActive( void );

	virtual void OnOpenCreateSingleplayerGameDialog( bool bMatchmakingFilter );
	virtual void OnOpenCreateMultiplayerGameDialog( void );
	virtual void OnOpenCreateMultiplayerGameCommunity( void );
	virtual void OnOpenDisconnectConfirmationDialog( void );
	virtual void OnOpenQuitConfirmationDialog( bool bForceToDesktop = false );

	virtual	void OnOpenServerBrowser();
	virtual void OnOpenCreateLobbyScreen( bool bIsHost = false );
	virtual void OnOpenLobbyBrowserScreen( bool bIsHost = false );
	virtual void UpdateLobbyScreen( void );
	virtual void UpdateMainMenuScreen();
	virtual void UpdateLobbyBrowser( void );

	virtual void OnOpenMessageBox( char const *pszTitle, char const *pszMessage, char const *pszButtonLegend, DWORD dwFlags, IMessageBoxEventCallback *pEventCallback = NULL, CMessageBoxScaleform** ppInstance = NULL, wchar_t const *pszWideMessage = NULL );
	virtual void OnOpenMessageBoxInSlot( int slot, char const *pszTitle, char const *pszMessage, char const *pszButtonLegend, DWORD dwFlags, IMessageBoxEventCallback *pEventCallback = NULL, CMessageBoxScaleform** ppInstance = NULL );
	virtual void OnOpenMessageBoxThreeway( char const *pszTitle, char const *pszMessage, char const *pszButtonLegend, char const *pszThirdButtonLabel, DWORD dwFlags, IMessageBoxEventCallback *pEventCallback = NULL, CMessageBoxScaleform** ppInstance = NULL );

	virtual void CreateCommandMsgBox( const char* pszTitle, const char* pszMessage, bool showOk = true, bool showCancel = false, const char* okCommand = NULL, const char* cancelCommand = NULL, const char* closedCommand = NULL, const char* pszLegend = NULL );
	virtual void CreateCommandMsgBoxInSlot( ECommandMsgBoxSlot slot, const char* pszTitle, const char* pszMessage, bool showOk = true, bool showCancel = false, const char* okCommand = NULL, const char* cancelCommand = NULL, const char* closedCommand = NULL, const char* pszLegend = NULL );

	virtual void ShowMatchmakingStatus( void );

	// returns true if message box is displayed successfully
	virtual bool ShowLockInput(  void );

	virtual void OnOpenPauseMenu( void );
	virtual void DismissPauseMenu( void );
	virtual void RestorePauseMenu( void );
	virtual void OnOpenControllerDialog( void );
	virtual void OnOpenSettingsDialog( void );
	virtual void OnOpenMouseDialog();
	virtual void OnOpenKeyboardDialog();
	virtual void OnOpenMotionControllerMoveDialog();
	virtual void OnOpenMotionControllerSharpshooterDialog();
	virtual void OnOpenMotionControllerDialog();
	virtual void OnOpenMotionCalibrationDialog();
	virtual void OnOpenVideoSettingsDialog();
	virtual void OnOpenOptionsQueued();
	virtual void OnOpenAudioSettingsDialog();

	virtual void OnOpenUpsellDialog( void );

	virtual void OnOpenHowToPlayDialog( void );	
	

	virtual void ShowScaleformPauseMenu( bool bShow );
	virtual bool IsScaleformPauseMenuActive( void );
	virtual bool IsScaleformPauseMenuVisible( void );

	virtual bool OnMessageBoxEvent( MessageBoxFlags_t buttonPressed );

	virtual void OnOpenMedalsDialog();
	virtual void OnOpenStatsDialog();
	virtual void CloseMedalsStatsDialog();


	virtual void OnOpenLeaderboardsDialog();
	virtual void OnOpenCallVoteDialog();
	virtual void OnOpenMarketplace();
	virtual void UpdateLeaderboardsDialog();
	virtual void CloseLeaderboardsDialog();
	virtual void StartExitingProcess( void );

	virtual void RunFrame( void );

	void DoCommunityQuickPlay( void );

protected:
	virtual void LockInput( void );
	virtual void UnlockInput( void );

    virtual bool IsScaleformIntroMovieEnabled( void );
    virtual void CreateScaleformIntroMovie( void );
    virtual void DismissScaleformIntroMovie( void );
   	virtual void OnPlayCreditsVideo( void );

    void CheckIntroMovieStaticDependencies( void );
#elif defined(INCLUDE_ROCKETUI)
    virtual void OnOpenCreateStartScreen( void ); // [jason] provides the "Press Start" screen interface
	virtual void DismissStartScreen( void );
	virtual bool IsStartScreenActive( void );

	virtual void OnOpenCreateMainMenuScreen( void );
	virtual void DismissMainMenuScreen( void );
	virtual void RestoreMainMenuScreen( void );
	virtual void DismissAllMainMenuScreens( bool bHideMainMenuOnly = false );

	void RestoreMPGameMenu( void );

    virtual void ShowRocketMainMenu( bool bShow );
	virtual bool IsRocketMainMenuActive( void );

    virtual void ShowRocketPauseMenu( bool bShow );
    virtual bool IsRocketPauseMenuActive( void );
    virtual bool IsRocketPauseMenuVisible( void );

	virtual void OnOpenCreateSingleplayerGameDialog( bool bMatchmakingFilter );
	virtual void OnOpenCreateMultiplayerGameDialog( void );
	virtual void OnOpenCreateMultiplayerGameCommunity( void );
	virtual void OnOpenDisconnectConfirmationDialog( void );
	virtual void OnOpenQuitConfirmationDialog( bool bForceToDesktop = false );

	virtual	void OnOpenServerBrowser();
	virtual void OnOpenCreateLobbyScreen( bool bIsHost = false );
	virtual void OnOpenLobbyBrowserScreen( bool bIsHost = false );
	virtual void UpdateLobbyScreen( void );
	virtual void UpdateMainMenuScreen();
	virtual void UpdateLobbyBrowser( void );

	virtual void ShowMatchmakingStatus( void );

	virtual void OnOpenPauseMenu( void );
	virtual void DismissPauseMenu( void );
	virtual void RestorePauseMenu( void );
	virtual void OnOpenControllerDialog( void );
	virtual void OnOpenSettingsDialog( void );
	virtual void OnOpenMouseDialog();
	virtual void OnOpenKeyboardDialog();
	virtual void OnOpenMotionControllerMoveDialog();
	virtual void OnOpenMotionControllerSharpshooterDialog();
	virtual void OnOpenMotionControllerDialog();
	virtual void OnOpenMotionCalibrationDialog();
	virtual void OnOpenVideoSettingsDialog();
	virtual void OnOpenOptionsQueued();
	virtual void OnOpenAudioSettingsDialog();

	virtual void OnOpenUpsellDialog( void );

	virtual void OnOpenHowToPlayDialog( void );

	virtual void OnOpenMedalsDialog();
	virtual void OnOpenStatsDialog();
	virtual void CloseMedalsStatsDialog();


	virtual void OnOpenLeaderboardsDialog();
	virtual void OnOpenCallVoteDialog();
	virtual void OnOpenMarketplace();
	virtual void UpdateLeaderboardsDialog();
	virtual void CloseLeaderboardsDialog();
	virtual void StartExitingProcess( void );

	virtual void RunFrame( void );

	void DoCommunityQuickPlay( void );

protected:
	virtual void LockInput( void );
	virtual void UnlockInput( void );
#endif// Scaleform/RocketUI

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

