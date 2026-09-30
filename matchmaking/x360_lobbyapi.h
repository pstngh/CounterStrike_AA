//===== Copyright © 1996-2009, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#ifndef X360_LOBBYAPI_H
#define X360_LOBBYAPI_H

#ifdef _WIN32
#pragma once
#endif

class CSteamLobbyObject
{
public:
	CSteamLobbyObject() { memset( this, 0, sizeof( *this ) ); }

public:
	uint64 GetSessionId() const { return m_uiLobbyID; }

public:
	uint64 m_uiLobbyID;
	enum LobbyState_t {
		STATE_DEFAULT = 0,
		STATE_ACTIVE_GAME,
		STATE_DISCONNECTED_FROM_STEAM,
	};
	LobbyState_t m_eLobbyState;
};

#endif

