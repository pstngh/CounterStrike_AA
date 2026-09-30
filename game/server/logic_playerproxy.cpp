//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ====
//
// Purpose:		Player for HL2.
//
//=============================================================================

#include "cbase.h"
#include "logic_playerproxy.h"
#include "filters.h"

LINK_ENTITY_TO_CLASS( logic_playerproxy, CLogicPlayerProxy);

BEGIN_DATADESC( CLogicPlayerProxy )

// Base
DEFINE_OUTPUT( m_RequestedPlayerHealth,		"PlayerHealth" ),
DEFINE_OUTPUT( m_PlayerDied,				"PlayerDied" ),
DEFINE_FIELD( m_hPlayer, FIELD_EHANDLE ),

DEFINE_OUTPUT( m_OnJump,					"OnJump" ),
DEFINE_OUTPUT( m_OnDuck,					"OnDuck" ),
DEFINE_OUTPUT( m_OnUnDuck,					"OnUnDuck" ),

// Portal 2

// HL2 / Episodic

END_DATADESC()

void CLogicPlayerProxy::Activate( void )
{
	BaseClass::Activate();

	if ( m_hPlayer == NULL )
	{
		m_hPlayer = AI_GetSinglePlayer();
	}
}

bool CLogicPlayerProxy::PassesDamageFilter( const CTakeDamageInfo &info )
{
	if (m_hDamageFilter)
	{
		CBaseFilter *pFilter = (CBaseFilter *)(m_hDamageFilter.Get());
		return pFilter->PassesDamageFilter(info);
	}

	return true;
}

void CLogicPlayerProxy::InputSetPlayerHealth( inputdata_t &inputdata )
{
	if ( m_hPlayer == NULL )
		return;

	m_hPlayer->SetHealth( inputdata.value.Int() );

}

void CLogicPlayerProxy::InputRequestPlayerHealth( inputdata_t &inputdata )
{
	if ( m_hPlayer == NULL )
		return;

	m_RequestedPlayerHealth.Set( m_hPlayer->GetHealth(), inputdata.pActivator, inputdata.pCaller );
}

