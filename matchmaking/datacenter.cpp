//========= Copyright © 1996-2009, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#include "xbox/xboxstubs.h"

#include "mm_framework.h"

#include "proto_oob.h"
#include "fmtstr.h"
#include "vstdlib/random.h"
#include "mathlib/IceKey.H"
#include "filesystem.h"

#if !defined( NO_STEAM )
#include "steam_datacenterjobs.h"
#endif

// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"


static ConVar mm_datacenter_update_interval( "mm_datacenter_update_interval", "3600", FCVAR_DEVELOPMENTONLY, "Interval between datacenter stats updates." );
static ConVar mm_datacenter_retry_interval( "mm_datacenter_retry_interval", "75", FCVAR_DEVELOPMENTONLY, "Interval between datacenter stats retries." );
static ConVar mm_datacenter_retry_infochunks_attempts( "mm_datacenter_retry_infochunks_attempts", "3", FCVAR_DEVELOPMENTONLY, "How many times can we retry retrieving each info chunk before failing." );
static ConVar mm_datacenter_query_delay( "mm_datacenter_query_delay", "2", FCVAR_DEVELOPMENTONLY, "Delay after datacenter update is enabled before data is actually queried." );
static ConVar mm_datacenter_report_version( "mm_datacenter_report_version", "5", FCVAR_DEVELOPMENTONLY, "Data version to report to DC." );
static ConVar mm_datacenter_delay_mount_frames( "mm_datacenter_delay_mount_frames", "6", FCVAR_DEVELOPMENTONLY, "How many frames to delay before attempting to mount the xlsp patch." );


static CDatacenter g_Datacenter;
CDatacenter *g_pDatacenter = &g_Datacenter;

CON_COMMAND( mm_datacenter_debugprint, "Shows information retrieved from data center" )
{
	KeyValuesDumpAsDevMsg( g_pDatacenter->GetDataInfo(), 1 );
	KeyValuesDumpAsDevMsg( g_pDatacenter->GetStats(), 1 );
}

//
// Buffer encryption/decryption
//


//
// Datacenter implementation
//


CDatacenter::CDatacenter() :
	m_pInfoChunks( NULL ),
	m_pDataInfo( NULL ),
#if !defined( NO_STEAM ) && !defined( NO_STEAM_GAMECOORDINATOR ) && !defined( SWDS )
	m_JobIDDataRequest( k_GIDNil ),
#endif
	m_flNextSearchTime( 0.0f ),
	m_bCanReachDatacenter( true ),
	m_eState( STATE_IDLE )
{
}

CDatacenter::~CDatacenter()
{
	if ( m_pInfoChunks )
		m_pInfoChunks->deleteThis();
	m_pInfoChunks = NULL;

	if ( m_pDataInfo )
		m_pDataInfo->deleteThis();
	m_pDataInfo = NULL;

#if !defined( NO_STEAM ) && !defined( NO_STEAM_GAMECOORDINATOR ) && !defined( SWDS )
	if ( GGCClient() )
	{
		GCSDK::CJob *pJob = GGCClient()->GetJobMgr().GetPJob( m_JobIDDataRequest );
		delete pJob;
	}
#endif
}

void CDatacenter::PushAwayNextUpdate()
{
	// Push away the next update to prevent start/stop updates
	float flNextUpdateTime = Plat_FloatTime() + mm_datacenter_query_delay.GetFloat();
	if ( flNextUpdateTime > m_flNextSearchTime )
		m_flNextSearchTime = flNextUpdateTime;
}

void CDatacenter::EnableUpdate( bool bEnable )
{
	DevMsg( "Datacenter::EnableUpdate( %d ), current state = %d\n", bEnable, m_eState );

	if ( bEnable && m_eState == STATE_PAUSED )
	{
		m_eState = STATE_IDLE;

		PushAwayNextUpdate();
	}

	if ( !bEnable )
	{
		RequestStop();
		m_eState = STATE_PAUSED;
	}
}

KeyValues * CDatacenter::GetDataInfo()
{
	return m_pInfoChunks;
}

KeyValues * CDatacenter::GetStats()
{
	return m_pInfoChunks ? m_pInfoChunks->FindKey( "stat" ) : NULL;
}

//
// CreateCmdBatch
//	creates a new instance of cmd batch to communicate
//	with datacenter backend
//
IDatacenterCmdBatch * CDatacenter::CreateCmdBatch( bool bMustSupportPII )
{
	CDatacenterCmdBatchImpl *pBatch = new CDatacenterCmdBatchImpl( this, bMustSupportPII );
	m_arrCmdBatchObjects.AddToTail( pBatch );
	return pBatch;
}

//
// CanReachDatacenter
//  returns true if we were able to establish a connection with the
//  datacenter backend regardless if it returned valid data or not.
bool CDatacenter::CanReachDatacenter()
{
	return m_bCanReachDatacenter;
}

void CDatacenter::OnCmdBatchReleased( CDatacenterCmdBatchImpl *pCmdBatch )
{
	m_arrCmdBatchObjects.FindAndRemove( pCmdBatch );
}

void CDatacenter::OnEvent( KeyValues *pEvent )
{
	char const *szEvent = pEvent->GetName();

	if ( !V_stricmp( szEvent, "OnProfileStorageAvailable" ) )
	{
		OnStorageDeviceAvailable( pEvent->GetInt( "iController" ) );
	}
}

void CDatacenter::OnStorageDeviceAvailable( int iCtrlr )
{
}

void CDatacenter::StorageDeviceWriteInfo( int iCtrlr )
{
}

void CDatacenter::TrySaveInfoToUserStorage()
{
}

void CDatacenter::Update()
{
#if !defined( NO_STEAM ) && !defined( NO_STEAM_GAMECOORDINATOR ) && !defined( SWDS )
	// Give a time-slice to the GCClient, which is used by Steam to communicate with the datacenter
	if ( GGCClient() && !IsLocalClientConnectedToServer() )
	{
		GGCClient()->BMainLoop( k_nThousand );
	}
#endif

	switch ( m_eState )
	{
	case STATE_IDLE:
		if ( Plat_FloatTime() > m_flNextSearchTime &&
			!IsLocalClientConnectedToServer() )
			RequestStart();
		else
		{
			TrySaveInfoToUserStorage();

		}
		break;

	case STATE_REQUESTING_DATA:
	case STATE_REQUESTING_CHUNKS:
		RequestUpdate();
		break;

	case STATE_PAUSED:
		// paused
		break;
	}

	// Update all the contained cmd batches
	for ( int k = 0; k < m_arrCmdBatchObjects.Count(); ++ k )
	{
		m_arrCmdBatchObjects[k]->Update();
	}
}

void CDatacenter::RequestStart()
{
#if !defined( NO_STEAM ) && !defined( NO_STEAM_GAMECOORDINATOR ) && !defined( SWDS )
	if ( !GGCClient() )
		return;

	// Avoid stacking requests
	if ( GGCClient()->GetJobMgr().BJobExists( m_JobIDDataRequest ) )
		return;

	GCSDK::CJob *pJob = new CGCClientJobDataRequest();
	m_JobIDDataRequest = pJob->GetJobID();
	pJob->StartJob( NULL );
#else
#endif

#if !defined( NO_STEAM_GAMECOORDINATOR )
	DevMsg( "Datacenter::RequestStart, time %.2f\n", Plat_FloatTime() );
#endif
	m_eState = STATE_REQUESTING_DATA;
}

void CDatacenter::RequestStop()
{
#if !defined( NO_STEAM_GAMECOORDINATOR )
	DevMsg( "Datacenter::RequestStop, time %.2f, state %d\n", Plat_FloatTime(), m_eState );
#endif

	bool bWasRequestingData = false;

#if !defined( NO_STEAM ) && !defined( NO_STEAM_GAMECOORDINATOR ) && !defined( SWDS )
	if ( GGCClient() )
	{
		bWasRequestingData = GGCClient()->GetJobMgr().BJobExists( m_JobIDDataRequest );
	}
	m_JobIDDataRequest = k_GIDNil;
#endif

	if ( bWasRequestingData )
		m_flNextSearchTime = Plat_FloatTime() + mm_datacenter_retry_interval.GetFloat();

	m_eState = STATE_IDLE;
}

void CDatacenter::RequestUpdate()
{
	bool bSuccessfulUpdate = false;

#if !defined( NO_STEAM ) && !defined( NO_STEAM_GAMECOORDINATOR ) && !defined( SWDS )
	if ( GGCClient() )
	{
		CGCClientJobDataRequest *pJob = (CGCClientJobDataRequest *)GGCClient()->GetJobMgr().GetPJob( m_JobIDDataRequest );
		if ( pJob )
		{
			if ( !pJob->BComplete() )
				return;

			bSuccessfulUpdate = pJob->BSuccess();
			if ( bSuccessfulUpdate )
			{
				if ( m_pDataInfo )
					m_pDataInfo->deleteThis();
				if ( m_pInfoChunks )
					m_pInfoChunks->deleteThis();

				m_pDataInfo = pJob->GetResults()->MakeCopy();
				m_pInfoChunks = pJob->GetResults()->MakeCopy();
			}

			pJob->Finish();
		}
	}
#else
#endif

	RequestStop();

#if !defined( NO_STEAM_GAMECOORDINATOR )
	DevMsg( "Datacenter::RequestUpdate %s\n", bSuccessfulUpdate ? "successful" : "failed" );
#endif

	m_bCanReachDatacenter = bSuccessfulUpdate;

	if ( bSuccessfulUpdate )
	{
		m_flNextSearchTime = Plat_FloatTime() + mm_datacenter_update_interval.GetFloat();
		
		OnDatacenterInfoUpdated();
	}
}

static void UnpackPatchBinary( KeyValues *pPatch, CUtlBuffer &buf )
{
	int nSize = pPatch->GetInt( "size" );
	if ( !nSize )
	{
		buf.Purge();
		return;
	}

	buf.EnsureCapacity( nSize + sizeof( uint64 ) ); // include extra room for alignments
	buf.SeekPut( buf.SEEK_HEAD, nSize );			// set the data size
	
	unsigned char *pchBuffer = ( unsigned char * ) buf.Base();
	memset( pchBuffer, 0, nSize );

	for ( KeyValues *sub = pPatch->GetFirstTrueSubKey(); sub; sub = sub->GetNextTrueSubKey() )
	{
		for( KeyValues *val = sub->GetFirstValue(); val; val = val->GetNextValue() )
		{
			if ( !val->GetName()[0] && val->GetDataType() == KeyValues::TYPE_UINT64 )
			{
				if ( !nSize )
				{
					nSize -= sizeof( uint64 );
					goto unpack_error;
				}

				uint64 ui = val->GetUint64();

				for ( int k = 0; k < MIN( nSize, sizeof( ui ) ); ++ k )
				{
					pchBuffer[k] = ( unsigned char )( ( ui >> ( 8 * k ) ) & 0xFF );
				}
				
				nSize -= MIN( nSize, sizeof( ui ) );
				pchBuffer += MIN( nSize, sizeof( ui ) );
			}
		}
	}

	// If all the bytes were correctly written to buffer, then the unpack succeeded
	if ( !nSize )
		return;

unpack_error:
	// Transmitted patch indicated a size different than transmitted data!
	DevWarning( "UnpackPatchBinary error: %d size indicated, but %d bytes failed to unpack!\n", buf.TellPut(), nSize );
	buf.Purge();
	return;
}

void CDatacenter::OnDatacenterInfoUpdated()
{

	// Downloaded update version
	int nUpdateVersion = m_pInfoChunks->GetInt( "version", 0 );

	bool bXlspPatchFileMounted = false;

	// Signal all other subscribers about the update
	if ( KeyValues *newEvent = new KeyValues( "OnDatacenterUpdate", "version", nUpdateVersion ) )
	{
		if ( bXlspPatchFileMounted )
			newEvent->SetInt( "filesystempatch", 1 );

		g_pMatchFramework->GetEventsSubscription()->BroadcastEvent( newEvent );
	}
}





//////////////////////////////////////////////////////////////////////////
//
// CDatacenterCmdBatchImpl
//

CDatacenterCmdBatchImpl::CDatacenterCmdBatchImpl( CDatacenter *pParent, bool bMustSupportPII ) :
	m_pParent( pParent ),
	m_arrCommands(),
	m_numRetriesAllowedPerCmd( 0 ),
	m_flRetryCmdTimeout( 0 ),
	m_bDestroyWhenFinished( true ),
	m_bMustSupportPII( bMustSupportPII )
{
}

void CDatacenterCmdBatchImpl::AddCommand( KeyValues *pCommand )
{
	if ( !pCommand )
		return;

	m_arrCommands.AddToTail( pCommand->MakeCopy() );
}

bool CDatacenterCmdBatchImpl::IsFinished()
{
	return true;
}

int CDatacenterCmdBatchImpl::GetNumResults()
{
	return 0;
}

KeyValues * CDatacenterCmdBatchImpl::GetResult( int idx )
{
	return NULL;
}

void CDatacenterCmdBatchImpl::Destroy()
{
	if ( m_pParent )
		m_pParent->OnCmdBatchReleased( this );

	for ( int k = 0; k < m_arrCommands.Count(); ++ k )
	{
		m_arrCommands[k]->deleteThis();
	}
	m_arrCommands.Purge();

	delete this;
}

void CDatacenterCmdBatchImpl::SetDestroyWhenFinished( bool bDestroyWhenFinished )
{
	m_bDestroyWhenFinished = bDestroyWhenFinished;
}

void CDatacenterCmdBatchImpl::SetNumRetriesAllowedPerCmd( int numRetriesAllowed )
{
	m_numRetriesAllowedPerCmd = numRetriesAllowed;
}

void CDatacenterCmdBatchImpl::SetRetryCmdTimeout( float flRetryCmdTimeout )
{
	m_flRetryCmdTimeout = flRetryCmdTimeout;
}

void CDatacenterCmdBatchImpl::Update()
{
	// Destroy ourselves since we cannot do any work anyway
	if ( m_bDestroyWhenFinished )
		this->Destroy();
}




