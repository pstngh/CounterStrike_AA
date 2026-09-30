//========= CounterStrike_AA ==================================================//
//
// Purpose: Performance log; see perflog.h.
//
// perf_log.txt is plain text for a person to read or paste into a chat. Each
// session starts with a header describing the build, the machine and the
// display. Frames are then grouped into segments; a segment ends when the map
// or any logged setting changes, so settings changed from the console can be
// compared within one session. Each segment lists its settings, a sample line
// every perf_log_interval seconds and a summary.
//
//=============================================================================//

#include "tier0/platform.h"
#include "perflog.h"
#include "host.h"
#include "client.h"
#include "server.h"
#include "convar.h"
#include "filesystem_engine.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/materialsystem_config.h"
#include "tier0/icommandline.h"
#include "tier0/perfstats.h"
#include "tier0/vprof.h"
#include "tier1/fmtstr.h"
#include "tier1/strtools.h"
#include "tier1/utlvector.h"
#include "vstdlib/jobthread.h"
#include "perflog_version.h"
#include <time.h>
#if defined( OSX )
#include <sys/sysctl.h>
#elif defined( LINUX )
#include <sys/utsname.h>
#include <unistd.h>
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifndef DEDICATED

#if defined( DX_TO_GL_ABSTRACTION )
// togl's running totals of draw calls and primitives. The profiler's draw counters are
// unreliable here: a counter first reached on the render thread counts into a dummy.
extern "C" void toglGetDrawTotals( uint64 *pDraws, uint64 *pPrimitives );
#endif

static ConVar perf_log( "perf_log", "0", FCVAR_RELEASE, "Write frame timing and the settings that affect it to perf_log.txt in the game directory." );
static ConVar perf_log_interval( "perf_log_interval", "5", FCVAR_RELEASE, "Seconds between perf_log sample lines.", true, 1.0f, true, 600.0f );
static ConVar perf_log_profile( "perf_log_profile", "1", FCVAR_RELEASE, "Add a VProf breakdown of main-thread time to each perf_log summary. Profiling costs a little frame time." );

// Settings that change frame time. A change to any of them starts a new segment.
// Settings a build does not have are left out.
static const char *const s_pszLoggedSettings[] =
{
	"fps_max", "mat_vsync", "mat_queue_mode", "r_frameratesmoothing", "gl_swap_limit",
	"cpu_level", "gpu_level", "gpu_mem_level", "mem_level",
	"cl_csm_enabled", "csm_quality_level", "r_shadows", "r_shadowrendertotexture", "r_flashlightdepthtexture",
	"mat_picmip", "mat_forceaniso", "mat_trilinear", "mat_antialias", "mat_reducefillrate",
	"mat_postprocess_enable", "mat_motion_blur_enabled", "cl_detaildist", "r_drawdetailprops",
	"cl_threaded_bone_setup", "snd_mix_async", "r_threaded_shadow_clip", "r_threaded_particles", "r_threaded_renderables",
	"rocket_enable", "perf_log_profile", "bot_quota", "bot_difficulty", "host_timescale",
};

// Frames right after joining a map include loading work, so they are skipped.
#define PERF_LOG_SETTLE_SECONDS			3.0
#define PERF_LOG_SETTINGS_CHECK_SECONDS	0.25

// Frame time histogram for the percentiles: 0.1 ms buckets up to 200 ms.
#define PERF_LOG_BUCKET_MS				0.1
#define PERF_LOG_BUCKETS				2000

struct PerfTotals_t
{
	void Reset() { V_memset( this, 0, sizeof( *this ) ); }

	int		m_nFrames;
	int		m_nQueuedFrames;		// frames rendered on a separate render thread
	double	m_flFrameMS;
	double	m_flFrameMaxMS;
	double	m_flInputMS;
	double	m_flClientMS;
	double	m_flServerMS;
	double	m_flRenderMS;			// main thread; includes m_flRenderWaitMS
	double	m_flRenderWaitMS;		// main thread waiting for the render thread
	double	m_flSoundMS;
	double	m_flClientDLLMS;
	double	m_flCmdExecuteMS;
	double	m_flRenderThreadMS;
	double	m_flDraws;
	double	m_flPrimitives;
};

static FileHandle_t s_hLog = FILESYSTEM_INVALID_HANDLE;
static double s_flLastFrameTime;
static double s_flJoinTime;				// when the client last became active; 0 while not in game
static double s_flLastSettingsCheck;
static char s_szSettings[2048];			// settings of the current segment
static char s_szMap[64];
static bool s_bSegmentOpen;
static int s_nSegment;
static double s_flSegmentStart;
static double s_flSampleStart;
static PerfTotals_t s_Segment;
static PerfTotals_t s_Sample;
static int s_nHistogram[PERF_LOG_BUCKETS];
static bool s_bProfiling;
static uint64 s_nLastDraws;
static uint64 s_nLastPrimitives;

//-----------------------------------------------------------------------------
// Output
//-----------------------------------------------------------------------------
static void LogPrintf( const char *pFormat, ... ) FMTFUNCTION( 1, 2 );
static void LogPrintf( const char *pFormat, ... )
{
	if ( s_hLog == FILESYSTEM_INVALID_HANDLE )
		return;

	char szLine[4096];
	va_list args;
	va_start( args, pFormat );
	V_vsnprintf( szLine, sizeof( szLine ), pFormat, args );
	va_end( args );
	g_pFileSystem->Write( szLine, V_strlen( szLine ), s_hLog );
}

static void LogFlush()
{
	if ( s_hLog != FILESYSTEM_INVALID_HANDLE )
	{
		g_pFileSystem->Flush( s_hLog );
	}
}

//-----------------------------------------------------------------------------
// Session header
//-----------------------------------------------------------------------------
static void DescribeSystem( char *pOut, int nOutSize )
{
	const CPUInformation &cpu = GetCPUInformation();
	char szOS[128] = "unknown OS";
	char szCores[128];
	V_snprintf( szCores, sizeof( szCores ), "%d cores, %d logical", cpu.m_nPhysicalProcessors, cpu.m_nLogicalProcessors );
	double flMemoryGB = 0.0;

#if defined( OSX )
	char szVersion[64] = "";
	size_t nSize = sizeof( szVersion );
	if ( sysctlbyname( "kern.osproductversion", szVersion, &nSize, NULL, 0 ) == 0 )
	{
		V_snprintf( szOS, sizeof( szOS ), "macOS %s", szVersion );
	}

	// Apple Silicon reports performance cores as perflevel0 and efficiency cores as perflevel1.
	int nPerformanceCores = 0, nEfficiencyCores = 0;
	nSize = sizeof( nPerformanceCores );
	if ( sysctlbyname( "hw.perflevel0.physicalcpu", &nPerformanceCores, &nSize, NULL, 0 ) == 0 )
	{
		nSize = sizeof( nEfficiencyCores );
		sysctlbyname( "hw.perflevel1.physicalcpu", &nEfficiencyCores, &nSize, NULL, 0 );
		V_snprintf( szCores, sizeof( szCores ), "%d performance + %d efficiency cores",
			nPerformanceCores, nEfficiencyCores );
	}

	uint64 nMemory = 0;
	nSize = sizeof( nMemory );
	if ( sysctlbyname( "hw.memsize", &nMemory, &nSize, NULL, 0 ) == 0 )
	{
		flMemoryGB = nMemory / ( 1024.0 * 1024.0 * 1024.0 );
	}
#elif defined( LINUX )
	struct utsname name;
	if ( uname( &name ) == 0 )
	{
		V_snprintf( szOS, sizeof( szOS ), "%s %s", name.sysname, name.release );
	}
	flMemoryGB = (double)sysconf( _SC_PHYS_PAGES ) * sysconf( _SC_PAGESIZE ) / ( 1024.0 * 1024.0 * 1024.0 );
#endif

	V_snprintf( pOut, nOutSize, "%s | %s | %s | %.0f GB",
		szOS, cpu.m_szProcessorBrand ? cpu.m_szProcessorBrand : "unknown CPU", szCores, flMemoryGB );
}

static void WriteSessionHeader()
{
	char szTime[64];
	time_t now = time( NULL );
	strftime( szTime, sizeof( szTime ), "%Y-%m-%d %H:%M:%S", localtime( &now ) );

	char szSystem[512];
	DescribeSystem( szSystem, sizeof( szSystem ) );

	MaterialAdapterInfo_t adapter;
	V_memset( &adapter, 0, sizeof( adapter ) );
	materials->GetDisplayAdapterInfo( materials->GetCurrentAdapter(), adapter );

	LogPrintf( "\n==== CounterStrike_AA performance log, %s ====\n", szTime );
	LogPrintf( "build: %s (engine compiled %s %s)\n", KISAK_GIT_COMMIT, __DATE__, __TIME__ );
	LogPrintf( "system: %s\n", szSystem );
	LogPrintf( "gpu: %s (vendor 0x%04x, device 0x%04x)\n", adapter.m_pDriverName, adapter.m_VendorID, adapter.m_DeviceID );
	LogPrintf( "worker threads: %d\n", g_pThreadPool ? g_pThreadPool->NumThreads() : 0 );
	LogPrintf( "command line: %s\n", CommandLine()->GetCmdLine() );
	LogPrintf( "Columns of the sample lines, times in ms per frame unless noted:\n"
		"  time_s: seconds into the segment. fps and frame: average over the sample; max: longest frame.\n"
		"  Main thread: server (game and bot simulation), client (entity updates), render (building the\n"
		"  frame, including wait), wait (waiting for the render thread), other (input, sound, commands),\n"
		"  idle (frame time outside those, mostly the fps_max limit).\n"
		"  rthread: render thread (OpenGL calls, plus any wait for the GPU). draws: draw calls per frame.\n" );
	LogFlush();
}

//-----------------------------------------------------------------------------
// Settings
//-----------------------------------------------------------------------------
static void DescribeSettings( char *pOut, int nOutSize )
{
	const MaterialSystem_Config_t &config = materials->GetCurrentConfigForVideoCard();
	V_snprintf( pOut, nOutSize, "resolution=%dx%d%s", config.m_VideoMode.m_Width, config.m_VideoMode.m_Height,
		config.Windowed() ? " windowed" : " fullscreen" );

	for ( int i = 0; i < ARRAYSIZE( s_pszLoggedSettings ); i++ )
	{
		const ConVar *pVar = g_pCVar->FindVar( s_pszLoggedSettings[i] );
		if ( pVar )
		{
			V_strncat( pOut, CFmtStr( " %s=%s", s_pszLoggedSettings[i], pVar->GetString() ), nOutSize );
		}
	}
}

//-----------------------------------------------------------------------------
// VProf breakdown of main-thread time
//-----------------------------------------------------------------------------
struct ProfileEntry_t
{
	const char *m_pszName;
	double m_flMS;
};

static int __cdecl SortProfileEntries( const ProfileEntry_t *pA, const ProfileEntry_t *pB )
{
	return ( pA->m_flMS < pB->m_flMS ) ? 1 : ( ( pA->m_flMS > pB->m_flMS ) ? -1 : 0 );
}

static void AccumulateProfile( CVProfNode *pNode, CUtlVector< double > &groups, CUtlVector< ProfileEntry_t > &functions )
{
	for ( ; pNode; pNode = pNode->GetSibling() )
	{
		const double flSelfMS = pNode->GetTotalTimeLessChildren();
		const int nGroup = pNode->GetBudgetGroupID();
		if ( nGroup >= 0 && nGroup < groups.Count() )
		{
			groups[nGroup] += flSelfMS;
		}

		// The same scope appears once for every path that reaches it; merge those.
		int i;
		for ( i = 0; i < functions.Count(); i++ )
		{
			if ( functions[i].m_pszName == pNode->GetName() || !V_strcmp( functions[i].m_pszName, pNode->GetName() ) )
				break;
		}
		if ( i == functions.Count() )
		{
			ProfileEntry_t entry = { pNode->GetName(), 0.0 };
			functions.AddToTail( entry );
		}
		functions[i].m_flMS += flSelfMS;

		AccumulateProfile( pNode->GetChild(), groups, functions );
	}
}

static void WriteProfile()
{
	const int nFrames = g_VProfCurrentProfile.NumFramesSampled();
	if ( nFrames <= 0 )
		return;

	CUtlVector< double > groupTimes;
	groupTimes.SetCount( g_VProfCurrentProfile.GetNumBudgetGroups() );
	for ( int i = 0; i < groupTimes.Count(); i++ )
	{
		groupTimes[i] = 0.0;
	}
	CUtlVector< ProfileEntry_t > functions;
	AccumulateProfile( g_VProfCurrentProfile.GetRoot(), groupTimes, functions );

	CUtlVector< ProfileEntry_t > groups;
	for ( int i = 0; i < groupTimes.Count(); i++ )
	{
		ProfileEntry_t entry = { g_VProfCurrentProfile.GetBudgetGroupName( i ), groupTimes[i] };
		groups.AddToTail( entry );
	}
	groups.Sort( SortProfileEntries );
	functions.Sort( SortProfileEntries );

	LogPrintf( "main thread profile by budget group, ms per frame over %d frames:\n", nFrames );
	for ( int i = 0; i < groups.Count() && i < 15; i++ )
	{
		if ( groups[i].m_flMS / nFrames < 0.005 )
			break;
		LogPrintf( "  %7.3f  %s\n", groups[i].m_flMS / nFrames, groups[i].m_pszName );
	}
	LogPrintf( "main thread profile, top scopes by their own time, ms per frame:\n" );
	for ( int i = 0; i < functions.Count() && i < 25; i++ )
	{
		if ( functions[i].m_flMS / nFrames < 0.005 )
			break;
		LogPrintf( "  %7.3f  %s\n", functions[i].m_flMS / nFrames, functions[i].m_pszName );
	}
}

static void RestartProfile( bool bProfile )
{
	// VProf may only be started, stopped and reset between frames, at its root scope.
	if ( s_bProfiling )
	{
		g_VProfCurrentProfile.Stop();
		s_bProfiling = false;
	}
	if ( bProfile )
	{
		g_VProfCurrentProfile.Reset();
		g_VProfCurrentProfile.Start();
		s_bProfiling = true;
	}
}

//-----------------------------------------------------------------------------
// Segments
//-----------------------------------------------------------------------------
static void AddFrame( PerfTotals_t &totals, double flFrameMS, const HostFrameSegments_t &segments,
	double flRenderWaitMS, double flRenderThreadMS, int nDraws, int nPrimitives, bool bQueued )
{
	totals.m_nFrames++;
	totals.m_nQueuedFrames += bQueued ? 1 : 0;
	totals.m_flFrameMS += flFrameMS;
	totals.m_flFrameMaxMS = MAX( totals.m_flFrameMaxMS, flFrameMS );
	totals.m_flInputMS += segments.m_flInput;
	totals.m_flClientMS += segments.m_flClient;
	totals.m_flServerMS += segments.m_flServer;
	totals.m_flRenderMS += segments.m_flRender;
	totals.m_flRenderWaitMS += flRenderWaitMS;
	totals.m_flSoundMS += segments.m_flSound;
	totals.m_flClientDLLMS += segments.m_flClientDLL;
	totals.m_flCmdExecuteMS += segments.m_flCmdExecute;
	totals.m_flRenderThreadMS += flRenderThreadMS;
	totals.m_flDraws += nDraws;
	totals.m_flPrimitives += nPrimitives;
}

// Per-frame averages that both the sample lines and the summaries print.
struct PerfAverages_t
{
	explicit PerfAverages_t( const PerfTotals_t &t )
	{
		const double n = MAX( t.m_nFrames, 1 );
		m_flFrame = t.m_flFrameMS / n;
		m_flServer = t.m_flServerMS / n;
		m_flClient = ( t.m_flClientMS + t.m_flClientDLLMS ) / n;
		m_flRender = t.m_flRenderMS / n;
		m_flRenderWait = MIN( t.m_flRenderWaitMS / n, m_flRender );
		m_flOther = ( t.m_flInputMS + t.m_flSoundMS + t.m_flCmdExecuteMS ) / n;
		m_flIdle = MAX( m_flFrame - m_flServer - m_flClient - m_flRender - m_flOther, 0.0 );
		m_flRenderThread = t.m_flRenderThreadMS / n;
		m_flDraws = t.m_flDraws / n;
		m_flPrimitives = t.m_flPrimitives / n;
		m_flFPS = m_flFrame > 0.0 ? 1000.0 / m_flFrame : 0.0;
	}

	double m_flFrame, m_flServer, m_flClient, m_flRender, m_flRenderWait, m_flOther, m_flIdle;
	double m_flRenderThread, m_flDraws, m_flPrimitives, m_flFPS;
};

static double HistogramPercentileMS( int nFrames, double flFraction )
{
	const int nTarget = (int)ceil( nFrames * flFraction );
	int nCount = 0;
	for ( int i = 0; i < PERF_LOG_BUCKETS; i++ )
	{
		nCount += s_nHistogram[i];
		if ( nCount >= nTarget )
			return ( i + 1 ) * PERF_LOG_BUCKET_MS;
	}
	return PERF_LOG_BUCKETS * PERF_LOG_BUCKET_MS;
}

static void BeginSegment( double flNow )
{
	s_bSegmentOpen = true;
	s_nSegment++;
	s_flSegmentStart = flNow;
	s_flSampleStart = flNow;
	s_Segment.Reset();
	s_Sample.Reset();
	V_memset( s_nHistogram, 0, sizeof( s_nHistogram ) );
	V_strncpy( s_szMap, GetBaseLocalClient().m_szLevelNameShort, sizeof( s_szMap ) );
	RestartProfile( perf_log_profile.GetBool() );

	LogPrintf( "\n---- segment %d: %s ----\n", s_nSegment, s_szMap );
	LogPrintf( "settings: %s\n", s_szSettings );
	LogPrintf( "%8s %5s %5s %6s %6s %7s %6s %6s %6s %6s %6s %6s %7s %6s\n",
		"time_s", "human", "bots", "fps", "frame", "max", "server", "client", "render", "wait", "other", "idle", "rthread", "draws" );
	LogFlush();
}

static void WriteSample( double flNow )
{
	if ( !s_Sample.m_nFrames )
		return;

	const PerfAverages_t avg( s_Sample );
	const int nClients = sv.IsActive() ? sv.GetNumClients() : 0;
	const int nBots = sv.IsActive() ? sv.GetNumFakeClients() : 0;
	LogPrintf( "%8.1f %5d %5d %6.1f %6.2f %7.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %7.2f %6.0f\n",
		flNow - s_flSegmentStart, nClients - nBots, nBots, avg.m_flFPS, avg.m_flFrame, s_Sample.m_flFrameMaxMS,
		avg.m_flServer, avg.m_flClient, avg.m_flRender, avg.m_flRenderWait, avg.m_flOther, avg.m_flIdle,
		avg.m_flRenderThread, avg.m_flDraws );
	LogFlush();
	s_Sample.Reset();
	s_flSampleStart = flNow;
}

static void EndSegment( double flNow, const char *pszReason )
{
	if ( !s_bSegmentOpen )
		return;
	s_bSegmentOpen = false;

	WriteSample( flNow );

	const int nFrames = s_Segment.m_nFrames;
	LogPrintf( "---- summary of segment %d (%s): %.1f s, %d frames; ended because %s ----\n",
		s_nSegment, s_szMap, s_Segment.m_flFrameMS / 1000.0, nFrames, pszReason );
	if ( nFrames > 0 )
	{
		const PerfAverages_t avg( s_Segment );
		const double flP50 = HistogramPercentileMS( nFrames, 0.5 );
		const double flP90 = HistogramPercentileMS( nFrames, 0.9 );
		const double flP99 = HistogramPercentileMS( nFrames, 0.99 );
		const double flP999 = HistogramPercentileMS( nFrames, 0.999 );
		const double flQueued = 100.0 * s_Segment.m_nQueuedFrames / nFrames;

		LogPrintf( "fps: average %.1f; at the 99th percentile frame %.1f; at the 99.9th %.1f\n",
			avg.m_flFPS, 1000.0 / flP99, 1000.0 / flP999 );
		LogPrintf( "frame ms: median %.1f, 90th percentile %.1f, 99th %.1f, 99.9th %.1f, max %.1f\n",
			flP50, flP90, flP99, flP999, s_Segment.m_flFrameMaxMS );
		LogPrintf( "main thread ms per frame: server %.2f, client %.2f, render %.2f (%.2f of it waiting for the render thread), other %.2f, idle %.2f\n",
			avg.m_flServer, avg.m_flClient, avg.m_flRender, avg.m_flRenderWait, avg.m_flOther, avg.m_flIdle );
		LogPrintf( "render thread ms per frame: %.2f; a separate render thread ran %.0f%% of frames\n",
			avg.m_flRenderThread, flQueued );
		LogPrintf( "per frame: %.0f draw calls, %.0f primitives\n", avg.m_flDraws, avg.m_flPrimitives );

		const double flMainBusy = avg.m_flServer + avg.m_flClient + avg.m_flOther + avg.m_flRender - avg.m_flRenderWait;
		if ( avg.m_flIdle > 0.2 * avg.m_flFrame )
		{
			LogPrintf( "likely limit: the frame rate cap. The main thread is idle %.0f%% of each frame, waiting for fps_max or vsync.\n",
				100.0 * avg.m_flIdle / avg.m_flFrame );
		}
		else if ( flQueued > 50.0 && avg.m_flRenderWait > 0.1 * avg.m_flFrame )
		{
			LogPrintf( "likely limit: the render thread. The main thread waits %.2f ms per frame for it. Its time is OpenGL work plus any wait for the GPU; if a lower resolution or shadow setting shortens it, the GPU is the limit.\n",
				avg.m_flRenderWait );
		}
		else
		{
			LogPrintf( "likely limit: the main thread, busy %.2f ms per frame; server is game and bot simulation, client and render are the client's work.\n",
				flMainBusy );
		}
	}

	if ( s_bProfiling )
	{
		WriteProfile();
		RestartProfile( false );
	}
	LogFlush();
}

static void OpenLog()
{
	char szPath[MAX_PATH];
	V_ComposeFileName( host_parms.basedir ? host_parms.basedir : ".", "perf_log.txt", szPath, sizeof( szPath ) );
	s_hLog = g_pFileSystem->Open( szPath, "a" );
	if ( s_hLog == FILESYSTEM_INVALID_HANDLE )
	{
		Warning( "perf_log: could not open %s\n", szPath );
		perf_log.SetValue( 0 );
		return;
	}

	Msg( "perf_log: writing %s\n", szPath );
	WriteSessionHeader();
	s_nSegment = 0;
	s_szSettings[0] = '\0';
}

static void CloseLog( double flNow, const char *pszReason )
{
	EndSegment( flNow, pszReason );
	RestartProfile( false );
	if ( s_hLog != FILESYSTEM_INVALID_HANDLE )
	{
		g_pFileSystem->Close( s_hLog );
		s_hLog = FILESYSTEM_INVALID_HANDLE;
	}
}

//-----------------------------------------------------------------------------
// Per-frame entry point
//-----------------------------------------------------------------------------
void PerfLog_Frame()
{
	if ( sv.IsDedicated() )
		return;

	const double flNow = Plat_FloatTime();
	const double flFrameMS = s_flLastFrameTime > 0.0 ? ( flNow - s_flLastFrameTime ) * 1000.0 : 0.0;
	s_flLastFrameTime = flNow;

	// Draws since the last call; with a render thread they trail the main thread by a frame.
	uint64 nTotalDraws = 0, nTotalPrimitives = 0;
#if defined( DX_TO_GL_ABSTRACTION )
	toglGetDrawTotals( &nTotalDraws, &nTotalPrimitives );
#endif
	const int nDraws = (int)( nTotalDraws - s_nLastDraws );
	const int nPrimitives = (int)( nTotalPrimitives - s_nLastPrimitives );
	s_nLastDraws = nTotalDraws;
	s_nLastPrimitives = nTotalPrimitives;

	if ( !perf_log.GetBool() )
	{
		if ( s_hLog != FILESYSTEM_INVALID_HANDLE )
		{
			CloseLog( flNow, "perf_log was turned off" );
		}
		return;
	}

	if ( s_hLog == FILESYSTEM_INVALID_HANDLE )
	{
		OpenLog();
		if ( s_hLog == FILESYSTEM_INVALID_HANDLE )
			return;
	}

	CClientState &client = GetBaseLocalClient();
	if ( !client.IsActive() )
	{
		EndSegment( flNow, "the client left the game" );
		s_flJoinTime = 0.0;
		return;
	}
	if ( s_flJoinTime == 0.0 )
	{
		s_flJoinTime = flNow;
	}
	if ( flNow - s_flJoinTime < PERF_LOG_SETTLE_SECONDS || flFrameMS <= 0.0 )
		return;

	// Start a new segment when the map or a setting changes.
	if ( !s_bSegmentOpen || flNow - s_flLastSettingsCheck >= PERF_LOG_SETTINGS_CHECK_SECONDS )
	{
		s_flLastSettingsCheck = flNow;
		char szSettings[sizeof( s_szSettings )];
		DescribeSettings( szSettings, sizeof( szSettings ) );
		const bool bMapChanged = s_bSegmentOpen && V_strcmp( s_szMap, client.m_szLevelNameShort ) != 0;
		const bool bSettingsChanged = s_bSegmentOpen && V_strcmp( s_szSettings, szSettings ) != 0;
		if ( bMapChanged || bSettingsChanged )
		{
			EndSegment( flNow, bMapChanged ? "the map changed" : "a setting changed" );
		}
		if ( !s_bSegmentOpen )
		{
			V_strncpy( s_szSettings, szSettings, sizeof( s_szSettings ) );
			BeginSegment( flNow );
			return;		// the frame that ends here began before the segment
		}
	}

	HostFrameSegments_t segments;
	Host_GetLastFrameSegments( segments );
	const double flRenderWaitMS = g_PerfStats.m_Slots[PERF_STATS_SLOT_END_FRAME].m_PrevFrameTime.GetMillisecondsF();
	const double flRenderThreadMS = g_PerfStats.m_Slots[PERF_STATS_SLOT_RENDERTHREAD].m_PrevFrameTime.GetMillisecondsF();
	const bool bQueued = materials->GetThreadMode() == MATERIAL_QUEUED_THREADED;

	AddFrame( s_Segment, flFrameMS, segments, flRenderWaitMS, flRenderThreadMS, nDraws, nPrimitives, bQueued );
	AddFrame( s_Sample, flFrameMS, segments, flRenderWaitMS, flRenderThreadMS, nDraws, nPrimitives, bQueued );
	const int nBucket = (int)( flFrameMS / PERF_LOG_BUCKET_MS );
	s_nHistogram[ clamp( nBucket, 0, PERF_LOG_BUCKETS - 1 ) ]++;

	if ( flNow - s_flSampleStart >= perf_log_interval.GetFloat() )
	{
		WriteSample( flNow );
	}
}

void PerfLog_Shutdown()
{
	if ( s_hLog != FILESYSTEM_INVALID_HANDLE )
	{
		CloseLog( Plat_FloatTime(), "the game quit" );
	}
}

#endif // !DEDICATED
