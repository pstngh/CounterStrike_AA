//========== Copyright © 2005, Valve Corporation, All rights reserved. ========
//
// Purpose:
//
//=============================================================================

#ifndef TEXTUREHEAP_H
#define TEXTUREHEAP_H

// Portal2 Console is not using due to amount of memory free on Xbox and RSX memory on PS3.
// The desired console pattern is to have a similar footprint, because PS3 does not have a streaming solution and it has enough texture memory
// the texture content choices for the consoles will be made to adapt.

// Uncomment to allow system to operate
//#define SUPPORTS_TEXTURE_STREAMING

#endif // TEXTUREHEAP_H
