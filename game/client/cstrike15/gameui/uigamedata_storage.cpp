//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#include "uigamedata.h"
#include "engineinterface.h"
#include "vgui/ILocalize.h"
#include "matchmaking/imatchframework.h"
#include "filesystem.h"
#include "fmtstr.h"
#ifndef NO_STEAM
#include "steam/steam_api.h"
#endif
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


using namespace BaseModUI;
using namespace vgui;

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0
#endif

#ifndef ERROR_IO_INCOMPLETE
#define ERROR_IO_INCOMPLETE 996L
#endif




void CUIGameData::RunFrame_Storage()
{

#ifdef _PS3
	GetPs3SaveSteamInfoProvider()->RunFrame();
#endif

}









#ifdef _PS3

//////////////////////////////////////////////////////////////////////////
//
// Steam info provider implementation
//
//////////////////////////////////////////////////////////////////////////

class CPS3SaveSteamInfoProvider : public IPS3SaveSteamInfoProviderUiGameData
{
public:
	virtual CUtlBuffer * GetInitialLoadBuffer()
	{
		GetBufferForSaveUtil().EnsureCapacity( 512*1024 - 128 );
		return &GetBufferForSaveUtil();
	}
	virtual CUtlBuffer * GetSaveBufferForCommit()
	{
		// called inside the saveutil callback (cannot cause allocations)
		return &GetBufferForSaveUtil();
	}
	virtual CUtlBuffer * PrepareSaveBufferForCommit()
	{
		// called on the main thread
		if ( !FillSteamBuffer() )
		{
			m_uiState &=~ ( STATE_DIRTY | STATE_INITIATED | STATE_COMMITTING );
			return NULL;
		}
		m_uiState |= STATE_COMMITTING; // flipping the committing flag early
		++ m_idxSaveUtilOwned;	// now saveutil owns the buffer we were writing into
		m_uiState &=~ STATE_DIRTY; // mark the steam data buffer as not yet dirty
		return &GetBufferForSaveUtil();
	}

	virtual void RunFrame();
	virtual void WriteSteamStats();

protected:
	CUtlBuffer m_arrBuffers[2];
	int m_idxSaveUtilOwned;
	inline CUtlBuffer& GetBufferForSaveUtil() { return m_arrBuffers[ m_idxSaveUtilOwned%2 ]; }
	inline CUtlBuffer& GetBufferForSteamData() { return m_arrBuffers[ !(m_idxSaveUtilOwned%2) ]; }
	bool FillSteamBuffer();

	CPS3SaveRestoreAsyncStatus m_ps3AsyncSaveStatus;
	enum AsyncSaveState_t
	{
		STATE_DEFAULT		= 0x00,		// default state, no save data ready, no save pending
		STATE_DIRTY			= 0x01,		// save data ready, will trigger saveutil when possible
		STATE_INITIATED		= 0x10,		// saveutil triggered, can still slipstream updated data
		STATE_COMMITTING	= 0x20,		// saveutil committing data, new updates must wait
	};
	uint32 m_uiState;
	uint32 m_numDirtyFrames;
}
g_ps3saveSteamInfoProvider;

IPS3SaveSteamInfoProviderUiGameData * GetPs3SaveSteamInfoProvider()
{
	return &g_ps3saveSteamInfoProvider;
}

bool CPS3SaveSteamInfoProvider::FillSteamBuffer()
{
	// Write the data into save buffer
	CUtlBuffer &buf = GetBufferForSteamData();
#ifndef NO_STEAM
	uint32 uiSizeRequired = buf.Size();
	bool bResult = steamapicontext->SteamUserStats()->GetUserStatsData( buf.Base(), buf.Size(), &uiSizeRequired );
	if ( !bResult && uiSizeRequired > buf.Size() )
	{
		buf.EnsureCapacity( uiSizeRequired );
		bResult = steamapicontext->SteamUserStats()->GetUserStatsData( buf.Base(), buf.Size(), &uiSizeRequired );
	}
	if ( !bResult )
	{
		buf.Purge();
		return false;
	}
	else
	{
		buf.SeekPut( CUtlBuffer::SEEK_HEAD, uiSizeRequired );
		return true;
	}
#else
	buf.Purge();
	return true;
#endif
}

void CPS3SaveSteamInfoProvider::RunFrame()
{
	// if save has been initiated, see if it has completed already
	if ( m_uiState & STATE_INITIATED )
	{
		if ( m_ps3AsyncSaveStatus.JobDone() )
		{
			m_uiState &=~( STATE_INITIATED | STATE_COMMITTING );
			Msg( "%.3f  CPS3SaveSteamInfoProvider::WriteSteamStats completed!\n", Plat_FloatTime() );
			
			GetBufferForSaveUtil().Purge();
		}
	}

	// if we have some new dirty data, then see if we can kick off a save
	if ( ( m_uiState & STATE_DIRTY ) && !( m_uiState & STATE_INITIATED ) )
	{
		if ( m_numDirtyFrames )
			-- m_numDirtyFrames;
		if ( ps3saveuiapi && !ps3saveuiapi->IsSaveUtilBusy() &&
			!m_numDirtyFrames

			/*
			// Not needed for CSGO
			&&
			( !CBaseModPanel::GetSingleton().GetWindow( WT_ATTRACTSCREEN ) ||
			( ( CAttractScreen * ) CBaseModPanel::GetSingleton().GetWindow( WT_ATTRACTSCREEN ) )->IsGameBootReady() ) 
			*/

			)
		{
			m_ps3AsyncSaveStatus.m_nCurrentOperationTag = kSAVE_TAG_WRITE_STEAMINFO;
			ps3saveuiapi->WriteSteamInfo( &m_ps3AsyncSaveStatus );

			Msg( "%.3f  CPS3SaveSteamInfoProvider::WriteSteamStats kicked off saveutil work\n", Plat_FloatTime() );
			m_uiState |= STATE_INITIATED; // we kicked off a save operation successfully
		}
	}
}

void CPS3SaveSteamInfoProvider::WriteSteamStats()
{
	Msg( "%.3f  CPS3SaveSteamInfoProvider::WriteSteamStats prepared data\n", Plat_FloatTime() );

	m_uiState |= STATE_DIRTY;
	m_numDirtyFrames = 3;
}

#endif



