//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#ifndef SND_DEV_XAUDIO_H
#define SND_DEV_XAUDIO_H
#pragma once
#include "audio_pch.h"
#include "inetmessage.h"
#include "netmessages.h"
#include "engine/ienginevoice.h"

class IAudioDevice;
IAudioDevice *Audio_CreateXAudioDevice( bool bInitVoice );



#endif // SND_DEV_XAUDIO_H
