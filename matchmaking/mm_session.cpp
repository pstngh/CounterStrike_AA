//===== Copyright © 1996-2009, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#include "mm_framework.h"
#include "filesystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


void MatchSession_BroadcastSessionSettingsUpdate( KeyValues *pUpdateDeletePackage )
{
	KeyValues *notify = new KeyValues( "OnMatchSessionUpdate" );
	notify->SetString( "state", "updated" );

	if ( KeyValues *kvUpdate = pUpdateDeletePackage->FindKey( "update" ) )
		notify->AddSubKey( kvUpdate->MakeCopy() );
	if ( KeyValues *kvDelete = pUpdateDeletePackage->FindKey( "delete" ) )
		notify->AddSubKey( kvDelete->MakeCopy() );

	g_pMatchEventsSubscription->BroadcastEvent( notify );
}


ConVar cl_session( "cl_session", "", FCVAR_USERINFO | FCVAR_HIDDEN | FCVAR_SERVER_CAN_EXECUTE | FCVAR_DEVELOPMENTONLY );

void MatchSession_PrepareClientForConnect( KeyValues *pSettings, uint64 uiReservationCookieOverride )
{
	char chSession[64];
	sprintf( chSession, "$%llx", uiReservationCookieOverride ? uiReservationCookieOverride :
		g_pMatchFramework->GetMatchSession()->GetSessionSystemData()->
		GetUint64( "xuidReserve", 0ull ) );
	cl_session.SetValue( chSession );

	g_pMatchFramework->GetMatchTitle()->PrepareClientForConnect( pSettings );
}

static bool MatchSession_ResolveServerInfo_Helper_DsResult( KeyValues *pSettings, CSysSessionBase *pSysSession,
	MatchSessionServerInfo_t &info, uint uiResolveFlags, uint64 ullCrypt )
{
#if !defined( NO_STEAM )

	char const *szAddress = pSettings->GetString( "server/adronline", "0.0.0.0" );
	if ( char const *szDecrypted = MatchSession_DecryptAddressString( szAddress, ullCrypt ) )
		szAddress = szDecrypted;
	Q_strncpy( info.m_dsResult.m_szPublicConnectionString, szAddress,
		ARRAYSIZE( info.m_dsResult.m_szPublicConnectionString ) );

	szAddress = pSettings->GetString( "server/adrlocal", "0.0.0.0" );
	if ( char const *szDecrypted = MatchSession_DecryptAddressString( szAddress, ullCrypt ) )
		szAddress = szDecrypted;
	Q_strncpy( info.m_dsResult.m_szPrivateConnectionString, szAddress,
		ARRAYSIZE( info.m_dsResult.m_szPrivateConnectionString ) );
#endif

	return true;
}

static bool MatchSession_ResolveServerInfo_Helper_ConnectString( KeyValues *pSettings, CSysSessionBase *pSysSession, MatchSessionServerInfo_t &info, uint uiResolveFlags )
{
	//
	// Prepare the connect command
	//
#if !defined( NO_STEAM )
	Q_snprintf( info.m_szConnectCmd, sizeof( info.m_szConnectCmd ),
		"connect %s %s\n",
		info.m_dsResult.m_szPublicConnectionString,
		info.m_dsResult.m_szPrivateConnectionString );
#endif

	info.m_xuidJingle = pSettings->GetUint64( "server/xuid", 0ull );

	if ( uint64 uiReservationCookieOverride = pSettings->GetUint64( "server/reservationid", 0ull ) )
		info.m_uiReservationCookie = uiReservationCookieOverride;
	else if ( pSysSession )
		info.m_uiReservationCookie = pSysSession->GetReservationCookie();
	else
		info.m_uiReservationCookie = 0ull;

	return true;
}

bool MatchSession_ResolveServerInfo( KeyValues *pSettings, CSysSessionBase *pSysSession, MatchSessionServerInfo_t &info, uint uiResolveFlags, uint64 ullCrypt )
{
	if ( ( uiResolveFlags & ( info.RESOLVE_DSRESULT | info.RESOLVE_QOS_RATE_PROBE ) ) &&
		 !MatchSession_ResolveServerInfo_Helper_DsResult( pSettings, pSysSession, info, uiResolveFlags, ullCrypt ) )
		return false;

	if ( ( uiResolveFlags & info.RESOLVE_CONNECTSTRING ) &&
		!MatchSession_ResolveServerInfo_Helper_ConnectString( pSettings, pSysSession, info, uiResolveFlags ) )
		return false;

	return true;
}

ConVar mm_tu_string( "mm_tu_string", "00000000" );

uint64 MatchSession_GetMachineFlags()
{
	uint64 uiFlags = 0;
	if ( IsPS3() )
		uiFlags |= MACHINE_PLATFORM_PS3;
	return uiFlags;
}

char const * MatchSession_GetTuInstalledString()
{
	return mm_tu_string.GetString();
}

char const * MatchSession_EncryptAddressString( char const *szAddress, uint64 ullCrypt )
{
	if ( !szAddress || !*szAddress )
		return NULL;
	if ( !ullCrypt )
		return NULL;
	if ( szAddress[0] == ':' )
		return NULL;
	if ( szAddress[ 0 ] == '$' )
		return NULL;
	
	static unsigned char s_chData[256];
	int nLen = Q_strlen( szAddress );
	if ( nLen >= ARRAYSIZE( s_chData )/2 - 1 )
		return NULL;
	
	// Copy the address
	s_chData[0] = '$';
	for ( int j = 0; j < nLen; ++ j )
	{
		uint8 uiVal = uint8( szAddress[j] ) ^ uint8( reinterpret_cast< uint8 * >(&ullCrypt)[ j % sizeof( uint64 ) ] );
		Q_snprintf( (char*)( s_chData + 1 + 2*j ), 3, "%02X", ( uint32 ) uiVal );
	}
	return (char*) s_chData;
}

char const * MatchSession_DecryptAddressString( char const *szAddress, uint64 ullCrypt )
{
	if ( !szAddress || !*szAddress )
		return NULL;
	if ( !ullCrypt )
		return NULL;
	if ( szAddress[ 0 ] != '$' )
		return NULL;

	static unsigned char s_chData[ 256 ];
	int nLen = Q_strlen( szAddress );
	if ( nLen*2 + 2 >= ARRAYSIZE( s_chData ) )
		return NULL;

	// Copy the address
	for ( int j = 0; j < nLen/2; ++j )
	{
		uint32 uiVal;
		if ( !sscanf( szAddress + 1 + 2*j, "%02X", &uiVal ) )
			return NULL;
		if ( uiVal > 0xFF )
			return NULL;
		uiVal = uint8( uiVal ) ^ uint8( reinterpret_cast< uint8 * >(&ullCrypt)[ j % sizeof( uint64 ) ] );
		if ( !uiVal )
			return NULL;
		s_chData[j] = uiVal;
	}
	s_chData[nLen/2] = 0;
	return (char*) s_chData;
}


CON_COMMAND( mm_debugprint, "Show debug information about current matchmaking session" )
{
	if ( IMatchSession *pIMatchSession = g_pMMF->GetMatchSession() )
	{
		( ( IMatchSessionInternal * ) pIMatchSession )->DebugPrint();
	}
	else
	{
		DevMsg( "No match session.\n" );
	}
}
