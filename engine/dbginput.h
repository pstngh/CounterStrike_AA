//========= Copyright � , Valve Corporation, All rights reserved. ============//

#ifndef DBGINPUT_HDR
#define DBGINPUT_HDR

#include "threadtools.h"

class CDebugInputThread: public CThread
{
public:
	CThreadMutex m_mx;
	CUtlString m_inputString;
	bool m_bStop;
	
	CDebugInputThread()
	{
		m_bStop = false;
	}
	~CDebugInputThread()
	{
	
	}
	
	void Stop()
	{
		m_bStop = true;
		CThread::Stop();
	}
	
	virtual int Run( void )
	{
		return 0;
	}
};

extern CDebugInputThread * g_pDebugInputThread;

#endif
