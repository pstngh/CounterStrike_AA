//========= CounterStrike_AA ==================================================//
//
// Purpose: Performance log. With perf_log 1 the client writes frame timing,
//          where each frame's time goes and the settings that affect it to
//          perf_log.txt in the game directory, so a run can be shared and
//          compared with another.
//
//=============================================================================//

#ifndef PERFLOG_H
#define PERFLOG_H

#ifdef _WIN32
#pragma once
#endif

// Main-thread time of the last host frame in milliseconds, split into the
// segments host_speeds reports. Kept by host.cpp.
struct HostFrameSegments_t
{
	float m_flInput;
	float m_flClient;
	float m_flServer;
	float m_flRender;
	float m_flSound;
	float m_flClientDLL;
	float m_flCmdExecute;
};

void Host_GetLastFrameSegments( HostFrameSegments_t &segments );

#ifndef DEDICATED
// Called once per engine frame, before the profiler starts the next frame.
void PerfLog_Frame();

// Writes the summary of the current log segment and closes the log.
void PerfLog_Shutdown();
#endif

#endif // PERFLOG_H
