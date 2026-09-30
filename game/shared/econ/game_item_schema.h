//========= Copyright © 1996-2003, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
//=============================================================================
#ifndef GAME_ITEM_SCHEMA_H
#define GAME_ITEM_SCHEMA_H
#ifdef _WIN32
#pragma once
#endif

#if defined( CSTRIKE15 ) || defined( CSTRIKE_GC_DLL )
	class CCStrike15ItemSchema;
	class CCStrike15ItemDefinition;
	class CCStrike15ItemSystem;

	typedef CCStrike15ItemSchema		GameItemSchema_t;
	typedef CCStrike15ItemDefinition	GameItemDefinition_t;
	typedef CCStrike15ItemSystem		GameItemSystem_t;

	#include "cstrike15_item_schema.h"
#else
	// Fallback Case
	class CEconItemSchema;
	class CEconItemDefinition;
	class CEconItemSystem;

	typedef CEconItemSchema		GameItemSchema_t;
	typedef CEconItemDefinition	GameItemDefinition_t;
	typedef CEconItemSystem		GameItemSystem_t;

	#include "econ_item_schema.h"
#endif

extern GameItemSchema_t *GetItemSchema();

#endif // GAME_ITEM_SYSTEM_H
