//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
// nav.h
// Data structures and constants for the Navigation Mesh system
// Author: Michael S. Booth (mike@turtlerockstudios.com), January 2003

#ifndef _CS_NAV_H_
#define _CS_NAV_H_

#include "nav.h"

/**
 * Below are several constants used by the navigation system.
 * @todo Move these into TheNavMesh singleton.
 */
const float BotRadius = 10.0f;				///< circular extent that contains bot

class CNavArea;
class CSNavNode;

#endif // _CS_NAV_H_
