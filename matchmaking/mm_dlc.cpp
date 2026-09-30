//========= Copyright © 1996-2009, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#include "xbox/xboxstubs.h"

#include "mm_framework.h"
#include "filesystem.h"

// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"



static CDlcManager g_DlcManager;
CDlcManager *g_pDlcManager = &g_DlcManager;

CON_COMMAND( mm_dlc_debugprint, "Shows information about dlc" )
{
	KeyValuesDumpAsDevMsg( g_pDlcManager->GetDataInfo(), 1 );
}

//////////////////////////////////////////////////////////////////////////

CDlcManager::CDlcManager() :
	m_pDataInfo( NULL ),
	m_eState( STATE_IDLE ),
	m_flTimestamp( 0.0f ),
	m_bNeedToDiscoverAllDlcs( true ),
	m_bNeedToUpdateFileSystem( false )
{
}

CDlcManager::~CDlcManager()
{
	if ( m_pDataInfo )
		m_pDataInfo->deleteThis();
	m_pDataInfo = NULL;
}

void CDlcManager::Update()
{

	// Once we are idle check if we need to update the file systems list of DLC
	if ( m_eState == STATE_IDLE )
	{
		if ( m_bNeedToUpdateFileSystem )
		{
			m_bNeedToUpdateFileSystem = false;
			g_pFullFileSystem->DiscoverDLC( XBX_GetPrimaryUserId() );
		}
	}
}

void CDlcManager::RequestDlcUpdate()
{
	if ( m_eState > STATE_IDLE )
		return;

	if ( m_eState == STATE_IDLE && !m_bNeedToDiscoverAllDlcs )
	{
		Msg( "DLCMANAGER: RequestDlcUpdate has no new content.\n" );
		return;
	}
		
	// If we specified dlc via the command line we can skip the actual enumeration
	const char *pCmdLine = CommandLine()->GetCmdLine();
	if ( V_stristr( pCmdLine, "-dlc" ) )
	{
		m_eState = STATE_IDLE;
		m_bNeedToUpdateFileSystem = true;
		m_bNeedToDiscoverAllDlcs = false;
		return;
	}

#if !defined( NO_STEAM ) && !defined( SWDS )
	// Client is requesting a DLC update
	m_CallbackOnDLCInstalled.Register( this, &CDlcManager::Steam_OnDLCInstalled );
	Steam_OnDLCInstalled( NULL );
#endif

}

bool CDlcManager::IsDlcUpdateFinished( bool bWaitForFinish )
{
	if ( m_eState == STATE_IDLE )
		return true;
	if ( !bWaitForFinish )
		return false;

	float flTimestamp = Plat_FloatTime();
	while ( m_eState != STATE_IDLE )
	{
		Update();
		ThreadSleep( 1 );
	}
	float flEndTimestamp = Plat_FloatTime();

	Warning( "DLCMANAGER: Forcing wait for update to finish stalled for %.3f sec\n", flEndTimestamp - flTimestamp );
	return true;
}

KeyValues * CDlcManager::GetDataInfo()
{
	return m_pDataInfo;
}

void CDlcManager::OnEvent( KeyValues *kvEvent )
{
}

#if !defined( NO_STEAM ) && !defined( SWDS )
void CDlcManager::Steam_OnDLCInstalled( DlcInstalled_t *pParam )
{
	m_bNeedToDiscoverAllDlcs = false;

	TitleDlcDescription_t const *dlcs = g_pMatchFramework->GetMatchTitle()->DescribeTitleDlcs();
	if ( !dlcs )
		return;

	TitleDataFieldsDescription_t const *fields = g_pMatchFramework->GetMatchTitle()->DescribeTitleDataStorage();

	uint64 uiOldDlcMask = m_pDataInfo->GetUint64( "@info/installed" );
	if ( !m_pDataInfo )
	{
		m_pDataInfo = new KeyValues( "DlcManager" );
		m_pDataInfo->SetUint64( "@info/installed", 0 );
	}
	IPlayerLocal *pPlayerLocal = g_pMatchFramework->GetMatchSystem()->GetPlayerManager()->GetLocalPlayer( XBX_GetPrimaryUserId() );
	for ( ; dlcs->m_uiLicenseMaskId; ++ dlcs )
	{
		// Check if DLC already detected
		if ( ( uiOldDlcMask & dlcs->m_uiLicenseMaskId ) == dlcs->m_uiLicenseMaskId )
			continue;

		// Check player profile first
		TitleDataFieldsDescription_t const *pDlcField = dlcs->m_szTitleDataBitfieldStatName ?
			TitleDataFieldsDescriptionFindByString( fields, dlcs->m_szTitleDataBitfieldStatName ) : NULL;
		if ( pDlcField && pPlayerLocal &&
			TitleDataFieldsDescriptionGetBit( pDlcField, pPlayerLocal ) )
		{
			m_pDataInfo->SetUint64( "@info/installed", m_pDataInfo->GetUint64( "@info/installed" ) | dlcs->m_uiLicenseMaskId );
			continue;
		}

		// Check Steam subscription
		if ( steamapicontext->SteamApps()->BIsSubscribedApp( dlcs->m_idDlcAppId ) )
		{
			m_pDataInfo->SetUint64( "@info/installed", m_pDataInfo->GetUint64( "@info/installed" ) | dlcs->m_uiLicenseMaskId );

			// Set player profile bit
			if ( pDlcField && pPlayerLocal )
				TitleDataFieldsDescriptionSetBit( pDlcField, pPlayerLocal, true );
		}
	}

	// Send the event in case detected DLC installed changes
	uint64 uiNewDlcMask = m_pDataInfo->GetUint64( "@info/installed" );
	if ( uiNewDlcMask != uiOldDlcMask )
	{
		KeyValues *kvEvent = new KeyValues( "OnDowloadableContentInstalled" );
		kvEvent->SetUint64( "installed", uiNewDlcMask );
		g_pMatchFramework->GetEventsSubscription()->BroadcastEvent( kvEvent );
		m_bNeedToUpdateFileSystem = true;
	}
}
#endif


