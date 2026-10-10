//================ Copyright (c) Valve Corporation. All Rights Reserved. =================
//
// mtlconnect.mm
//
// ToMetal's connection to the rest of the engine, and its exported helpers.
//
//==================================================================================================
#include "mtlinternal.h"
#include "tier1/tier1.h"
#include "tier2/tier2.h"
#include "mathlib/mathlib.h"
#include "appframework/ilaunchermgr.h"

#include "tier0/memdbgon.h"

ILauncherMgr *g_pLauncherMgr = NULL;

std::atomic<uint64> g_nMtlTotalDraws;
std::atomic<uint64> g_nMtlTotalPrimitives;

TOMETAL_INTERFACE void ToMetalConnectLibraries( CreateInterfaceFn factory )
{
	ConnectTier1Libraries( &factory, 1 );
	ConVar_Register();
	ConnectTier2Libraries( &factory, 1 );

	if ( !g_pFullFileSystem )
	{
		Warning( "ToMetal was unable to access the required interfaces!\n" );
	}

	// As ToGL does
	MathLib_Init( 2.2f, 2.2f, 0.0f, 2.0f );

	g_pLauncherMgr = (ILauncherMgr *)factory( SDLMGR_INTERFACE_VERSION, NULL );
}

TOMETAL_INTERFACE void ToMetalDisconnectLibraries()
{
	DisconnectTier2Libraries();
	ConVar_Unregister();
	DisconnectTier1Libraries();
}

void tometalGetDrawTotals( uint64 *pDraws, uint64 *pPrimitives )
{
	*pDraws = g_nMtlTotalDraws.load();
	*pPrimitives = g_nMtlTotalPrimitives.load();
}

void toglGetClientRect( VD3DHWND hWnd, RECT *destRect )
{
	// The useful answer is the size of the canvas: the backbuffer's
	uint width = 0, height = 0;
	if ( g_pLauncherMgr )
	{
		g_pLauncherMgr->RenderedSize( width, height, false );
	}
	destRect->left = 0;
	destRect->top = 0;
	destRect->right = width;
	destRect->bottom = height;
}
