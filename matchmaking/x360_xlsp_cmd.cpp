//===== Copyright © 1996-2009, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#include "mm_framework.h"

#include "vstdlib/random.h"

#include "protocol.h"
#include "proto_oob.h"
#include "filesystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar mm_dedicated_xlsp_max_dcs( "mm_dedicated_xlsp_max_dcs", "25", FCVAR_DEVELOPMENTONLY, "Max number of XLSP datacenters supported" );
ConVar mm_dedicated_xlsp_force_dc( "mm_dedicated_xlsp_force_dc", "", FCVAR_DEVELOPMENTONLY, "Name of XLSP datacenter to force connection to" );
ConVar mm_dedicated_xlsp_timeout( "mm_dedicated_xlsp_timeout", "20", FCVAR_DEVELOPMENTONLY, "Timeout for XLSP operations" );
ConVar mm_dedicated_xlsp_command_timeout( "mm_dedicated_xlsp_command_timeout", "10", FCVAR_DEVELOPMENTONLY, "Timeout for XLSP command" );

ConVar mm_dedicated_xlsp_force_dc_offset( "mm_dedicated_xlsp_force_dc_offset", "0", FCVAR_DEVELOPMENTONLY, "Offset of XLSP datacenter master to debug master servers" );


static int s_nXlspConnectionCmdReplyId = 0x10000000;
static int GetNextXlspConnectionCmdReplyId()
{
	return ++ s_nXlspConnectionCmdReplyId;
}

