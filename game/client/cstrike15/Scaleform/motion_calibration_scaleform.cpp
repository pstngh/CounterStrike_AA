//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#if defined( INCLUDE_SCALEFORM )
#include "basepanel.h"
#include "gameui_util.h"
#include "motion_calibration_scaleform.h"
#include "inputsystem/iinputsystem.h"
#include "../gameui/cstrike15/cstrike15basepanel.h"
#include "vgui/ISurface.h"
#include "vgui_controls/Controls.h"
#include "gameui_interface.h"


// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>


ConVar cl_test_calibration( "cl_test_calibration", "0", FCVAR_CLIENTDLL | FCVAR_DEVELOPMENTONLY );


CMotionCalibrationScaleform* CMotionCalibrationScaleform::m_pInstance = NULL;

SFUI_BEGIN_GAME_API_DEF
	SFUI_DECL_METHOD( OnCancel ),
	SFUI_DECL_METHOD( OnAccept ),
	SFUI_DECL_METHOD( TimerCallback ),
SFUI_END_GAME_API_DEF( CMotionCalibrationScaleform, MotionCalibration );

CMotionCalibrationScaleform::CMotionCalibrationScaleform() :
	m_bVisible ( false ),
	m_bLoading ( false ),
	m_pInfoText( NULL ),
	m_pNavText( NULL ),
	m_pTargetTopLeft( NULL ),
	m_pTargetBottomLeft( NULL ),
	m_pTargetBottomRight( NULL ),
	m_pTargetTopRight( NULL ),
	m_pSensitivity( NULL ),
	m_nSceneLevel( SCENE_START ),
	m_bCursorVisible( false )
{	
	m_iSplitScreenSlot = GET_ACTIVE_SPLITSCREEN_SLOT();
}



CMotionCalibrationScaleform::~CMotionCalibrationScaleform()
{
}

void CMotionCalibrationScaleform::LoadDialog( void )
{
	if ( !m_pInstance )
	{
		m_pInstance = new CMotionCalibrationScaleform();
		m_pInstance->m_bLoading = true;

		SFUI_REQUEST_ELEMENT( SF_FULL_SCREEN_SLOT, g_pScaleformUI, CMotionCalibrationScaleform, m_pInstance, MotionCalibration );
	}
}

void CMotionCalibrationScaleform::UnloadDialog( void )
{
}





void CMotionCalibrationScaleform::FlashLoaded( void )
{
}


void CMotionCalibrationScaleform::FlashReady( void )
{
	if ( m_FlashAPI && m_pScaleformUI )
	{
		SF_FORCE_SPLITSCREEN_PLAYER_GUARD( m_iSplitScreenSlot );
		m_bLoading = false;

		LockInputToSlot( m_iSplitScreenSlot );


		SFVALUE panelTop = m_pScaleformUI->Value_GetMember( m_FlashAPI, "PanelTop" );

		if ( panelTop )
		{
			SFVALUE panel = m_pScaleformUI->Value_GetMember( panelTop, "Panel" );

			if ( panel )
			{
				m_pInfoText = m_pScaleformUI->TextObject_MakeTextObjectFromMember( panel, "InfoText" );

				m_pSensitivity = m_pScaleformUI->Value_GetMember( panel, "Sensitivity" );
				m_pScaleformUI->Value_SetVisible( m_pSensitivity, false );

				SFVALUE navBar = m_pScaleformUI->Value_GetMember( panel, "NavigationMaster" );
				if ( navBar )
				{
					m_pNavText = m_pScaleformUI->TextObject_MakeTextObjectFromMember( navBar, "ControllerNavl" );
					m_pScaleformUI->ReleaseValue( navBar );
				}

				m_pScaleformUI->ReleaseValue( panel );
			}

			
			m_pScaleformUI->ReleaseValue( panelTop );
		}

		m_pTargetTopLeft = m_pScaleformUI->Value_GetMember( m_FlashAPI, "TargetUpperLeft" );

		m_pScaleformUI->Value_SetVisible( m_pTargetTopLeft, false );

		m_pTargetBottomLeft = m_pScaleformUI->Value_GetMember( m_FlashAPI, "TargetLowerLeft" );

		m_pScaleformUI->Value_SetVisible( m_pTargetBottomLeft, false );

		m_pTargetBottomRight = m_pScaleformUI->Value_GetMember( m_FlashAPI, "TargetLowerRight" );

		m_pScaleformUI->Value_SetVisible( m_pTargetBottomRight, false );

		m_pTargetTopRight = m_pScaleformUI->Value_GetMember( m_FlashAPI, "TargetUpperRight" );

		m_pScaleformUI->Value_SetVisible( m_pTargetTopRight, false );

		// advance through scenes until we require player input
		while ( TryAdvance( false, false ) );

		Show();
	}
}


void CMotionCalibrationScaleform::Show( void )
{
	if ( FlashAPIIsValid() && !m_bVisible )
	{
		WITH_SLOT_LOCKED
		{
			g_pScaleformUI->Value_InvokeWithoutReturn( m_FlashAPI, "ShowPanel", 0, NULL );
		}

		m_bVisible = true;
	}
}


void CMotionCalibrationScaleform::Hide( void )
{

	if ( FlashAPIIsValid() && m_bVisible )
	{
		WITH_SLOT_LOCKED
		{
			g_pScaleformUI->Value_InvokeWithoutReturn( m_FlashAPI, "HidePanel", 0, NULL );
		}

		m_bVisible = false;

		RemoveFlashElement();
	}
}

bool CMotionCalibrationScaleform::PreUnloadFlash( void )
{
	SafeReleaseSFTextObject( m_pInfoText );
	SafeReleaseSFTextObject( m_pNavText );
	SafeReleaseSFVALUE( m_pTargetTopLeft);
	SafeReleaseSFVALUE( m_pTargetBottomLeft);
	SafeReleaseSFVALUE( m_pTargetBottomRight );
	SafeReleaseSFVALUE( m_pTargetTopRight );
	SafeReleaseSFVALUE( m_pSensitivity );

	UnlockInput();

	return ScaleformFlashInterface::PreUnloadFlash();	
}


void CMotionCalibrationScaleform::PostUnloadFlash( void )
{
	m_pInstance = NULL;
	delete this;
}

void CMotionCalibrationScaleform::OnCancel( SCALEFORM_CALLBACK_ARGS_DECL )
{
	if ( m_nSceneLevel == SCENE_ADJUST_SENSITIVITY ||
		 m_nSceneLevel == SCENE_CALIBRATE_CONTROLLER_RESULT )
	{
		TryAdvance( false, true );
	}
	else
	{
		Hide();

		if ( !GameUI().IsInLevel() )
		{
			g_pInputSystem->ResetCurrentInputDevice();
			g_pInputSystem->SampleInputToFindCurrentDevice( false );

			IGameEvent * event = gameeventmanager->CreateEvent( "mb_input_lock_cancel" );
			if ( event )
			{
				gameeventmanager->FireEventClientSide( event );
			}
		}
	}
}

void CMotionCalibrationScaleform::OnAccept( SCALEFORM_CALLBACK_ARGS_DECL )
{
	TryAdvance( true, false );
}


bool CMotionCalibrationScaleform::TryAdvance( bool bAccept, bool bCancel )
{
	if ( !FlashAPIIsValid() )
	{
		return false;
	}

	bool bResult = false;

	switch ( m_nSceneLevel )
	{
	case SCENE_START:
		bResult = true;
		inputsystem->SetMotionControllerCalibrationInvalid();
		break;

	case SCENE_CONNECT_EYE:
		{
			bResult = ( inputsystem->GetMotionControllerDeviceStatus() > INPUT_DEVICE_MC_STATE_CAMERA_NOT_CONNECTED || cl_test_calibration.GetBool() );
		}
		break;

	case SCENE_CONNECT_CONTROLLER:
		{
			bool moveConnected = g_pInputSystem->IsInputDeviceConnected( INPUT_DEVICE_PLAYSTATION_MOVE );
			bool sharpshooterConnected = g_pInputSystem->IsInputDeviceConnected( INPUT_DEVICE_SHARPSHOOTER );

			bResult = ( moveConnected || sharpshooterConnected || cl_test_calibration.GetBool() );
		}
		break;

	case SCENE_CALIBRATE_CONTROLLER:
		bResult = ( inputsystem->GetMotionControllerDeviceStatus() == INPUT_DEVICE_MC_STATE_OK ||
					inputsystem->GetMotionControllerDeviceStatus() == INPUT_DEVICE_MC_STATE_CONTROLLER_ERROR ||
					cl_test_calibration.GetBool() );
		break;

	case SCENE_CALIBRATE_CONTROLLER_RESULT:
		if ( inputsystem->GetMotionControllerDeviceStatus() != INPUT_DEVICE_MC_STATE_CONTROLLER_ERROR )
		{
			bool bOkToProceed = true;

			bResult = bAccept || bOkToProceed ;
		}

		if ( bCancel )
		{
			m_nSceneLevel = SCENE_START;
		}

		break;

	case SCENE_TARGET_TOP_LEFT:
		if ( bAccept )
		{
			inputsystem->StepMotionControllerCalibration();
			bResult = true;
		}
		break;

	case SCENE_TARGET_BOTTOM_RIGHT:
		if ( bAccept )
		{
			inputsystem->StepMotionControllerCalibration();
			bResult = true;
		}
		break;

	case SCENE_TARGET_BOTTOM_LEFT:
		if ( bAccept )
		{
			inputsystem->StepMotionControllerCalibration();
			bResult = true;
		}
		break;

	case SCENE_TARGET_TOP_RIGHT:
		if ( bAccept )
		{
			inputsystem->StepMotionControllerCalibration();
			bResult = true;
		}
		break;

	case SCENE_ADJUST_SENSITIVITY:
		if ( bCancel )
		{
			inputsystem->ResetMotionControllerScreenCalibration();
			m_nSceneLevel = SCENE_TARGET_TOP_LEFT;
		}
		
		bResult = bAccept;

		break;

	case SCENE_END:
		Hide();
		IGameEvent * event = gameeventmanager->CreateEvent( "mb_input_lock_success" );
		if ( event )
		{
			gameeventmanager->FireEventClientSide( event );
		}
		return true;
		break;
	}

	if ( bResult )
	{
		SceneAdvance();
	}

	SceneDraw( m_nSceneLevel );

	return bResult;
}

void CMotionCalibrationScaleform::SceneAdvance( void )
{
	if ( !FlashAPIIsValid() )
	{
		return;
	}

	//WITH_SLOT_LOCKED
	//{
	//	switch ( m_nSceneLevel )
	//	{
	//	case SCENE_START:
	//	case SCENE_CONNECT_EYE:
	//	case SCENE_CONNECT_CONTROLLER:
	//	case SCENE_CALIBRATE_CONTROLLER:
	//	case SCENE_CALIBRATE_CONTROLLER_RESULT:
	//	case SCENE_TARGET_TOP_LEFT:
	//	case SCENE_TARGET_BOTTOM_RIGHT:
	//	case SCENE_TARGET_BOTTOM_LEFT:
	//	case SCENE_TARGET_TOP_RIGHT:
	//	case SCENE_END:
	//		break;
	//	}
	//}

	m_nSceneLevel++;
}





void CMotionCalibrationScaleform::SceneDraw( int nSceneLevel )
{
	if ( FlashAPIIsValid() )
	{
		WITH_SLOT_LOCKED
		{
			m_pScaleformUI->Value_SetVisible( m_pSensitivity, false );
			m_pScaleformUI->Value_SetVisible( m_pTargetTopLeft, false );
			m_pScaleformUI->Value_SetVisible( m_pTargetBottomLeft, false );
			m_pScaleformUI->Value_SetVisible( m_pTargetBottomRight, false );
			m_pScaleformUI->Value_SetVisible( m_pTargetTopRight, false );

			m_pNavText->SetTextHTML( g_pScaleformUI->Translate( "#SFUI_Calibrate_Nav_Cancel", NULL ) );

			switch ( nSceneLevel )
			{
			case SCENE_START:
				break;

			case SCENE_CONNECT_EYE:
				m_pInfoText->SetTextHTML( g_pScaleformUI->Translate( "#SFUI_Calibrate_Eye_Disconnected", NULL ) );
				break;

			case SCENE_CONNECT_CONTROLLER:
				m_pInfoText->SetTextHTML( g_pScaleformUI->Translate( "#SFUI_Calibrate_Activate_Move", NULL ) );
				break;

			case SCENE_CALIBRATE_CONTROLLER:
				if ( inputsystem->GetMotionControllerDeviceStatus() == INPUT_DEVICE_MC_STATE_CONTROLLER_CALIBRATING )
				{
					m_pInfoText->SetTextHTML( g_pScaleformUI->Translate( "#SFUI_Calibrate_Calibrating", NULL ) );
				}
				else
				{
					m_pInfoText->SetTextHTML( g_pScaleformUI->Translate( "#SFUI_Calibrate_Hold_the_Motion", NULL ) );
				}

				break;

			case SCENE_CALIBRATE_CONTROLLER_RESULT:
				{
				}
				break;

			case SCENE_TARGET_TOP_LEFT:
				m_pInfoText->SetTextHTML( g_pScaleformUI->Translate( "#SFUI_Calibrate_Aim_at_icon", NULL ) );
				m_pScaleformUI->Value_SetVisible( m_pTargetTopLeft, true );
				break;

			case SCENE_TARGET_BOTTOM_RIGHT:
				m_pInfoText->SetTextHTML( g_pScaleformUI->Translate( "#SFUI_Calibrate_Aim_at_icon", NULL ) );
				m_pScaleformUI->Value_SetVisible( m_pTargetBottomRight, true );
				break;

			case SCENE_TARGET_BOTTOM_LEFT:
				m_pInfoText->SetTextHTML( g_pScaleformUI->Translate( "#SFUI_Calibrate_Aim_at_icon", NULL ) );
				m_pScaleformUI->Value_SetVisible( m_pTargetBottomLeft, true );
				break;

			case SCENE_TARGET_TOP_RIGHT:
				m_pInfoText->SetTextHTML( g_pScaleformUI->Translate( "#SFUI_Calibrate_Aim_at_icon", NULL ) );
				m_pScaleformUI->Value_SetVisible( m_pTargetTopRight, true );
				break;

			case SCENE_ADJUST_SENSITIVITY:
				if ( !m_bCursorVisible )
				{
					g_pScaleformUI->ShowCursor();
					m_bCursorVisible = true;
				}

				m_pInfoText->SetVisible( false );
				m_pScaleformUI->Value_SetVisible( m_pSensitivity, false );
				m_pNavText->SetTextHTML( g_pScaleformUI->Translate( "#SFUI_Calibrate_Nav_Accept", NULL ) );
				break;
			}
		}
	}
}




void CMotionCalibrationScaleform::TimerCallback( SCALEFORM_CALLBACK_ARGS_DECL )
{
	if ( FlashAPIIsValid() )
	{
		SceneThink();
	}
}

void CMotionCalibrationScaleform::SceneThink( void )
{
	int nSceneStart = m_nSceneLevel;
	if ( !cl_test_calibration.GetBool() )
	{
		switch( inputsystem->GetMotionControllerDeviceStatus() )
		{
		case INPUT_DEVICE_MC_STATE_CAMERA_NOT_CONNECTED:
			m_nSceneLevel = SCENE_CONNECT_EYE;
			break;

		case INPUT_DEVICE_MC_STATE_CONTROLLER_NOT_CONNECTED:
			m_nSceneLevel = SCENE_CONNECT_CONTROLLER;
			break;

		case INPUT_DEVICE_MC_STATE_CONTROLLER_NOT_CALIBRATED:
		case INPUT_DEVICE_MC_STATE_CONTROLLER_CALIBRATING:
			m_nSceneLevel = SCENE_CALIBRATE_CONTROLLER;
			break;
		}
	}

	if ( nSceneStart != m_nSceneLevel )
	{
		SceneDraw( m_nSceneLevel );
	}
	
	TryAdvance( false, false );

	if ( m_nSceneLevel != SCENE_ADJUST_SENSITIVITY )
	{
		if ( m_bCursorVisible )
		{
			g_pScaleformUI->HideCursor();
			m_bCursorVisible = false;

		}
	}
}




#endif // INCLUDE_SCALEFORM
