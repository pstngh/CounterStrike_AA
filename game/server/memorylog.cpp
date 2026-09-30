//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose:	See memorylog.h
//
//=============================================================================//

#include "cbase.h"
#include "memorylog.h"
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


#if !defined( _CERT )

// Memory log auto game system instantiation
CMemoryLog g_MemoryLog;

const char *GetMapName( void )
{
	static char mapName[32];
	mapName[0] = 0;
	if ( gpGlobals->mapname.ToCStr() )
		V_strncpy( mapName, gpGlobals->mapname.ToCStr(), sizeof( mapName ) );
	if ( !mapName[ 0 ] )
		V_strncpy( mapName, "none", sizeof( mapName ) );
	return mapName;
}

void CMemoryLog::LevelInitPostEntity( void )
{
//#include "entitylist.h"
}

#endif // !defined( _CERT )
