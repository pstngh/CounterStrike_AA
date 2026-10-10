//========= Copyright (c), Valve LLC, All rights reserved. ============
//
// Purpose: Utility to interrogate and modify the data in the OSX IPC Server
//
// $NoKeywords: $
//=============================================================================
// README:README
//
// This file implements the --wrap for ld on linux that lets file i/o api's
// behave as if it were running on a case insensitive file system.  Unfortunately,
// this is needed by both steam2 and steam3.  It was decided to check the source
// into both locations, otherwise someone would find the .o and have no idea
// where to go for the source if it was in the 'other' tree.  Also, because this
// needs to be linked into every elf binary, the .o is checked in for Steam3 so that it is
// always available.  In Steam2 it sits with the PosixWin32.cpp implementation and gets
// compiled along side of it through the make system.  If you are reading this in Steam3,
// you will probably want to actually make your changes in steam2 and do a baseless merge
// to the steam3 copy.
//
// HOWTO: Add a new function.  Add the function with _WRAP to the makefiles as noted below.
// Add the implementation to pathmatch.cpp - probably mimicking the existing functions.
// Build steam2 and copy to matching steam3/client.  Take the pathmatch.o from steam 2
// and check it in to steam3 (in the location noted below).  Full rebuild (re-link really)
// of steam3.  Test steam and check in.
//
// If you are looking at updating this file, please update the following as needed:
//
// STEAM2.../Projects/GazelleProto/Client/Engine/obj/RELEASE_NORMAL/libsteam_linux/Common/Misc/pathmatch.o
//          This is where steam2 builds the pathmatch.o out to.
//
// STEAM2.../Projects/GazelleProto/Makefile.shlib.base - contains _WRAP references
// STEAM2.../Projects/Common/Misc/pathmatch.cpp - Where the source is checked in, keep in sync with:
// STEAM3.../src/common/pathmatch.cpp - should be identical to previous file, but discoverable in steam3.
// STEAM3.../src/lib/linux32/release/pathmatch.o - steam3 checked in version
// STEAM3.../src/devtools/makefile_base_posix.mak - look for the _WRAP references

