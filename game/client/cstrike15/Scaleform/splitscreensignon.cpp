//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: [jpaquin] The "Player Two press start" widget
//
//=============================================================================//
#include "cbase.h"

#if defined( INCLUDE_SCALEFORM )

#include "basepanel.h"
#include "splitscreensignon.h"
#include "../gameui/cstrike15/cstrike15basepanel.h"
#include "../engine/filesystem_engine.h"

#include "xbox/xboxstubs.h"

#include "engineinterface.h"
#include "modinfo.h"
#include "gameui_interface.h"

#include "tier1/utlbuffer.h"
#include "filesystem.h"
#include <vgui/ILocalize.h>
#include "inputsystem/iinputsystem.h"


using namespace vgui;

// for SRC
#include <vstdlib/random.h>

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>


SFUI_BEGIN_GAME_API_DEF
SFUI_END_GAME_API_DEF( SplitScreenSignonWidget, SplitScreenSignon );

SplitScreenSignonWidget::SplitScreenSignonWidget() : 
	m_bVisible( false ),
	m_bConditionsAreValid( false ),
	m_bLoading( false ),
	m_bWantShown( false ),
	m_pPlayer2Name( NULL ),
	m_bWaitingForSignon( false ),
	m_iSecondPlayerId( -1 ),
	m_iControllerThatPressedStart( -1 ),
	m_bCurrentlyProcessingSignin( false ),
	m_bDropSecondPlayer( false )
{
	ListenForGameEvent( "sfuievent" );
}

void SplitScreenSignonWidget::FlashReady( void )
{
	m_bLoading = false;
	// Setup subscription so we are notified when the user signs in
	g_pMatchFramework->GetEventsSubscription()->Subscribe( this );

	( m_bVisible ) ? OnShow() : OnHide();
}

bool SplitScreenSignonWidget::PreUnloadFlash( void )
{
	// Remember to unsubscribe so we don't crash later!
	StopListeningForAllEvents();
	g_pMatchFramework->GetEventsSubscription()->Unsubscribe( this );

	return true;
}
	

void SplitScreenSignonWidget::OnShow( void )
{
	if ( FlashAPIIsValid() )
	{
		WITH_SLOT_LOCKED
		{
			ScaleformUI()->Value_InvokeWithoutReturn( m_FlashAPI, "showPanel", 0, NULL );
		}
	}
	else if ( !m_bLoading )
	{
		m_bLoading = true;
		SFUI_REQUEST_ELEMENT( SF_FULL_SCREEN_SLOT, g_pScaleformUI, SplitScreenSignonWidget, this, SplitScreenSignon );
	}

	m_bVisible = true;
}

void SplitScreenSignonWidget::OnHide( void )
{
	if ( FlashAPIIsValid() )
	{
		WITH_SLOT_LOCKED
		{
			ScaleformUI()->Value_InvokeWithoutReturn( m_FlashAPI, "hidePanel", 0, NULL );
		}
	}

	m_bVisible = false;

}

void SplitScreenSignonWidget::UpdateState( void )
{
	bool showit = ( m_bConditionsAreValid && m_bWantShown );

	if ( showit != m_bVisible )
		showit ? OnShow() : OnHide();
}

void SplitScreenSignonWidget::Show( bool showit )
{
	if ( showit != m_bWantShown )
	{
		m_bWantShown = showit;
		UpdateState();
	}
}

void SplitScreenSignonWidget::SplitScreenConditionsAreValid( bool value )
{
	if ( value != m_bConditionsAreValid )
	{
		m_bConditionsAreValid = value;
		UpdateState();
	}
}

void SplitScreenSignonWidget::Update( void )
{

}

void SplitScreenSignonWidget::SetPlayerSignedIn( void )
{
	if ( m_iControllerThatPressedStart == -1 )
	{
		return;
	}

	m_iSecondPlayerId = m_iControllerThatPressedStart;
	m_iControllerThatPressedStart = -1;

	g_pMatchFramework->GetEventsSubscription()->BroadcastEvent( new KeyValues( "OnProfilesChanged", "numProfiles", ( int ) XBX_GetNumGameUsers() ) );

	BasePanel()->UpdateRichPresenceInfo();

	ConVarRef ss_enable( "ss_enable" );
	ss_enable.SetValue( 1 );
	ConVarRef ss_pipsplit( "ss_pipsplit" );
	ss_pipsplit.SetValue( 0 );

}

void SplitScreenSignonWidget::SetPlayer2Name( const char* name )
{
}

void SplitScreenSignonWidget::DropSecondPlayer( void )
{
}


void SplitScreenSignonWidget::RevertUIToOnePlayerMode( void )
{
	if ( FlashAPIIsValid() )
		SetPlayer2Name( NULL );
	m_iSecondPlayerId = -1;
	m_bWaitingForSignon = false;
	ConVarRef ss_enable( "ss_enable" );
	ss_enable.SetValue( 0 );
	ConVarRef ss_pipsplit( "ss_pipsplit" );
	ss_pipsplit.SetValue( 0 );

}



void SplitScreenSignonWidget::FireGameEvent( IGameEvent* pEvent )
{
	char const *szName = pEvent->GetName();

	// Notify that sign-in has completed
	if ( !V_stricmp( szName, "sfuievent" ) )
	{
		const char* action = pEvent->GetString( "action" );
		const char* data = pEvent->GetString( "data" );

		if ( action && *action )
		{
			if ( data && *data )
			{
				if ( !V_stricmp( data, "mainmenu" ) || !V_stricmp( data, "creategamedialog" ) )
				{
					if ( !V_stricmp( action, "show" ) )
					{
						Show( true );
					}
					else 
					{
						Show( false );
					}
				}
			}
		}

	}

}

void SplitScreenSignonWidget::OnEvent( KeyValues *pEvent )
{

}

#endif
