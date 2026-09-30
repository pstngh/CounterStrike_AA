//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
#ifndef TIER_V0PROF_SN_HDR
#define TIER_V0PROF_SN_HDR

// enable this to get detailed SN Tuner markers. PS3 specific

class CVProfSnMarkerScope  { public: CVProfSnMarkerScope( const char * ) {} };

//lwss - These are ps3 specific profiling points - going to disable
    #define SNPROF(name) (void)0
    #define SNPROF_ANIM(name) (void)0
//lwss end

#endif
