//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//

#ifndef ISTEAMOVERLAYMGR_H
#define ISTEAMOVERLAYMGR_H

abstract_class ISteamOverlayManager
{
public:
	virtual void		Create( vgui::VPANEL parent ) = 0;
	virtual void		GameBootReady() = 0;
	virtual void		SetEnhancedOverlayInput( bool bEnable ) = 0;
	virtual void		Destroy( void ) = 0;
};

#define g_pISteamOverlayMgr ( ( ISteamOverlayManager * ) 0 )

#endif // ISTEAMOVERLAYMGR_H
