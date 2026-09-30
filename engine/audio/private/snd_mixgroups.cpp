//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Main control for any streaming sound output device.
//
//===========================================================================//

#include "audio_pch.h"
#include "const.h"
#include "cdll_int.h"
#include "sound.h"
#include "client_class.h"
#include "icliententitylist.h"
#include "con_nprint.h"
#include "tier0/icommandline.h"
#include "vox_private.h"
#include "../../traceinit.h"
#include "../../cmd.h"
#include "toolframework/itoolframework.h"
#include "vstdlib/random.h"
#include "vstdlib/jobthread.h"
#include "vaudio/ivaudio.h"
#include "../../client.h"
#include "../../cl_main.h"
#include "utldict.h"
#include "mempool.h"
#include "../../enginetrace.h"			// for traceline
#include "../../public/bspflags.h"		// for traceline
#include "../../public/gametrace.h"		// for traceline
#include "vphysics_interface.h"		// for surface props
#include "../../ispatialpartitioninternal.h"	// for entity enumerator
#include "../../debugoverlay.h"
#include "icliententity.h"
#include "../../cmodel_engine.h"
#include "../../staticpropmgr.h"
#include "../../server.h"
#include "edict.h"
#include "../../pure_server.h"
#include "filesystem/IQueuedLoader.h"
#include "voice.h"
#include "snd_dma.h"
#include "snd_mixgroups.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


extern ConVar dsp_volume;
// extern ConVar volume;
extern ConVar snd_duckerattacktime;
extern ConVar snd_duckerreleasetime;


//--------------------------------------------------------------------------------------------------------------
// sound mixers
int g_csoundmixers	 = 0;					// total number of soundmixers found
int g_cgrouprules	 = 0;					// total number of group rules found
int g_cgroupclass	 = 0;
int g_cmixlayers	 = 0;


// these are temporary until I can put them into the mixer proper
ConVar snd_mixerMasterLevel("snd_mixer_master_level", "1.0", FCVAR_CHEAT );
ConVar snd_mixerMasterDSP("snd_mixer_master_dsp", "1.0", FCVAR_CHEAT );

ConVar snd_showclassname("snd_showclassname", "0", FCVAR_CHEAT );		// if 1, show classname of ent making sound
														// if 2, show all mixgroup matches
														// if 3, show all mixgroup matches with current soundmixer for ent

ConVar snd_list( "snd_list", "", FCVAR_CHEAT ); // lists all sounds played that match substring filter arg. "" = none, "*" = all


ConVar snd_showmixer("snd_showmixer", "0", FCVAR_CHEAT );	// set to 1 to show mixer every frame
static ConVar snd_disable_mixer_solo("snd_disable_mixer_solo", "0", FCVAR_CHEAT ); // for finding soloing bugs

//------------------------------------------------------------------------------
//
// Sound Mixers
//
// Sound mixers are referenced by name from Soundscapes, and are used to provide
// custom volume control over various sound categories, called 'mix groups'
//
// see scripts/soundmixers.txt for data format
//------------------------------------------------------------------------------

#define CMXRGROUPMAX		128					// up to n mixgroups
#define CMXRGROUPRULESMAX	(CMXRGROUPMAX + 16)	// max number of group rules
#define	CMXRSOUNDMIXERSMAX	32					// up to n sound mixers per project
#define	CMXRMIXLAYERSMAX	16					// up to n mix layers per project

// mix groups - these equivalent to submixes on an audio mixer

#define CMXRMATCHMAX	8

// list of rules for determining sound membership in mix groups.
// All conditions which are not null are ANDed together 
#define CMXRCLASSMAX	16
#define CMXRNAMEMAX		32

struct classlistelem_t
{
	char			szclassname[CMXRNAMEMAX];	// name of entities' class, such as CAI_BaseNPC or CHL2_Player
};


struct grouprule_t
{	
	char			szmixgroup[CMXRNAMEMAX];	// mix group name
	int				mixgroupid;					// mix group unique id
	char			szdir[CMXRNAMEMAX];			// substring to search for in ch->sfx
	int				classId;					// index of classname
	int				chantype;					// channel type (CHAN_WEAPON, etc)
	int				soundlevel_min;				// min soundlevel
	int				soundlevel_max;				// max soundlevel

	int				priority;					// 0..100 higher priority sound groups duck all lower pri groups if enabled
	short			is_ducked;					// if 1, sound group is ducked by all higher priority 'causes_duck" sounds
	short			is_voice;
	int				causes_ducking;				// if 1, sound group ducks other 'is_ducked' sounds of lower priority
	float			duck_target_pct;			// if sound group is ducked, target percent of original volume

	float			total_vol;					// total volume of all sounds in this group, if group can cause ducking
	float			ducker_threshold;			// ducking is caused by this group if total_vol > ducker_threshold

	float			trigger_vol;				// total volume of all sounds in this group, use for triggers

												// and causes_ducking is enabled.
	float			duck_target_vol;			// target volume while ducking	
	float			duck_ramp_val;				// current value of ramp - moves towards duck_target_vol
};

// sound mixer

struct soundmixer_t
{
	float			mixAmount;
	char			szsoundmixer[CMXRNAMEMAX];					// name of this soundmixer
	float ALIGN128	mapMixgroupidToVolume[CMXRGROUPMAX];		// sparse array of mix group volume values for this soundmixer
	float ALIGN128	mapMixgroupidToLevel[CMXRGROUPMAX];			// sparse array of mix group volume values for this soundmixer
	float ALIGN128	mapMixgroupidToDsp[CMXRGROUPMAX];			// sparse array of mix group volume values for this soundmixer
	float ALIGN128	mapMixgroupidToSolo[CMXRGROUPMAX];			// sparse array of mix group solo values for this soundmixer
	float ALIGN128	mapMixgroupidToMute[CMXRGROUPMAX];			// sparse array of mix group mute values for this soundmixer

};

#define CMXRTRIGGEREDLAYERMAX 16
/*struct layertrigger_t
{
	int			cmixlayers;									// number of layers
	int			imixlayer[CMXRTRIGGEREDLAYERMAX];			// triggering mix group
	float		fthreshold[CMXRTRIGGEREDLAYERMAX];			//
	float		fmixamount[CMXRTRIGGEREDLAYERMAX];			//
	float		fattack[CMXRTRIGGEREDLAYERMAX];				//
	float		frelease[CMXRTRIGGEREDLAYERMAX];			//

};*/

struct layertrigger_t
{
	bool		bhastrigger;
	bool		bistrigger[CMXRGROUPMAX];
	float		fthreshold[CMXRGROUPMAX];			//
	float		fmixamount[CMXRGROUPMAX];			//
	float		fattack[CMXRGROUPMAX];				//
	float		frelease[CMXRGROUPMAX];			//

};

float g_soloActive = 0.0;										// are any soundmixers solo'd?

int g_mapMixgroupidToGrouprulesid[CMXRGROUPMAX];				// map mixgroupid (one per unique group name)
																// back to 1st entry of this name in g_grouprules

// sound mixer globals

classlistelem_t g_groupclasslist[CMXRCLASSMAX];
soundmixer_t	g_soundmixers[CMXRSOUNDMIXERSMAX];	// all sound mixers	
soundmixer_t	g_mixlayers[CMXRMIXLAYERSMAX];	// all mix layers
soundmixer_t	g_mastermixlayer;

grouprule_t		g_grouprules[CMXRGROUPRULESMAX];	// all rules for determining mix group membership

layertrigger_t	g_layertriggers[CMXRMIXLAYERSMAX];   //                              


// set current soundmixer index g_isoundmixer, search for match in soundmixers
// Only change current soundmixer if new name is different from current name.

int g_isoundmixer = -1;								// index of current sound mixer
char g_szsoundmixer_cur[64];						// current soundmixer name

ConVar snd_soundmixer("snd_soundmixer", "Default_Mix");		// current soundmixer name


void MXR_SetCurrentSoundMixer( const char *szsoundmixer )
{
	// if soundmixer name is not different from current name, return

	if ( !Q_stricmp(szsoundmixer, g_szsoundmixer_cur) )
	{
		return;
	}

	for (int i = 0; i < g_csoundmixers; i++)
	{
		if ( !Q_stricmp(g_soundmixers[i].szsoundmixer, szsoundmixer) )
		{
			g_isoundmixer = i;

			// save new current sound mixer name
			V_strcpy_safe(g_szsoundmixer_cur, szsoundmixer);

			return;
		}
	}
}

static void MXR_AccumulateMasterMixLayer(void)
{
	// we are doing a weighted average in the case of multiple entries,
	// but we only want it averaged by the number of entries per mix group
	// if there is no mix group entry the defaults give zero difference
	// this whole thing could be optimized for memory but it's insignificant
	float totalAmount[CMXRGROUPMAX];
	for (int j = 0; j < CMXRGROUPMAX; j++)
	{
		// defaults
		g_mastermixlayer.mapMixgroupidToVolume[j] = 1.0;
		g_mastermixlayer.mapMixgroupidToLevel[j] = 1.0;
		g_mastermixlayer.mapMixgroupidToDsp[j] = 1.0;
		g_mastermixlayer.mapMixgroupidToSolo[j] = 0.0;
		g_mastermixlayer.mapMixgroupidToMute[j] = 0.0;

		totalAmount[j] = 0.0;

		for (int i = 0; i < CMXRMIXLAYERSMAX; i++)
		{
			// mix layers
			soundmixer_t *pmixlayer = &g_mixlayers[i];
			// volume entry < 0 = no entry
			if(pmixlayer->mapMixgroupidToVolume[j] >= 0.0)
				totalAmount[j] += pmixlayer->mixAmount;
		}
	}

	// using the accumulated "amounts" we can do a weighted average of the actual layer values
	for (int i = 0; i < CMXRMIXLAYERSMAX; i++)
	{
		// mix layers
		soundmixer_t *pmixlayer = &g_mixlayers[i];

		for (int j = 0; j < CMXRGROUPMAX; j++)
		{
			if(!(totalAmount[j] > 0.0))
				continue;
			float amount = pmixlayer->mixAmount * (pmixlayer->mixAmount / totalAmount[j]);
			// -1 entry volume = no entry
			if(pmixlayer->mapMixgroupidToVolume[j] < 0.0)
				continue;
			g_mastermixlayer.mapMixgroupidToVolume[j] = clamp(g_mastermixlayer.mapMixgroupidToVolume[j] + ((pmixlayer->mapMixgroupidToVolume[j] - 1.0) * amount), 0.0, 1.0);
			g_mastermixlayer.mapMixgroupidToLevel[j] = clamp(g_mastermixlayer.mapMixgroupidToLevel[j] + ((pmixlayer->mapMixgroupidToLevel[j] - 1.0) * amount), 0.0, 1.0);
			g_mastermixlayer.mapMixgroupidToDsp[j] = clamp(g_mastermixlayer.mapMixgroupidToDsp[j] + ((pmixlayer->mapMixgroupidToDsp[j] - 1.0) * amount), 0.0, 1.0);
			g_mastermixlayer.mapMixgroupidToSolo[j] = clamp(g_mastermixlayer.mapMixgroupidToSolo[j] + (pmixlayer->mapMixgroupidToSolo[j] * amount), 0.0, 1.0);
			g_mastermixlayer.mapMixgroupidToMute[j] = clamp(g_mastermixlayer.mapMixgroupidToMute[j] + (pmixlayer->mapMixgroupidToMute[j] * amount), 0.0, 1.0);
		}
	}
}



// Check in advance if ANY groups are solo'd
void MXR_SetSoloActive(void)
{
	MXR_AccumulateMasterMixLayer();

	g_soloActive = 0.0;

	if ( !snd_disable_mixer_solo.GetBool() )
	{
	    //lwss bugfix: this sometimes is -1 and is used to index the array below.
	    // Other portions of the code check for it to be < 0 and return early, so that's what I'll do
	    if( g_isoundmixer < 0 )
	        return;
	    //lwss end
		soundmixer_t *pmixer = &g_soundmixers[g_isoundmixer];

		// for every entry in mapMixgroupidToSolo which is not 0

		for (int i = 0; i < CMXRGROUPMAX; i++)
		{
			g_soloActive = MAX(g_soloActive, pmixer->mapMixgroupidToSolo[i]);
			g_soloActive = MAX(g_soloActive, g_mastermixlayer.mapMixgroupidToSolo[i]);
		}
	}
}

ClientClass *GetClientClass( SoundSource soundsource )
{
	IClientEntity *pClientEntity = NULL;
	if ( entitylist )
	{
		pClientEntity = entitylist->GetClientEntity( soundsource );
		if ( pClientEntity )
		{
			ClientClass *pClientClass = pClientEntity->GetClientClass();
			// check npc sounds 
			return pClientClass;
		}
	}

	return NULL;
}

// get the client class name if an entity was specified
const char *GetClientClassname( SoundSource soundsource )
{
	IClientEntity *pClientEntity = NULL;
	if ( entitylist )
	{
		pClientEntity = entitylist->GetClientEntity( soundsource );
		if ( pClientEntity )
		{
			ClientClass *pClientClass = pClientEntity->GetClientClass();
			// check npc sounds 
			if ( pClientClass )
			{
				return pClientClass->GetName();
			}
		}
	}

	return NULL;
}

// builds a cached list of rules that match the directory name on the sound
int MXR_GetMixGroupListFromDirName( const char *pDirname, byte *pList, int listMax )
{
	// if we call this before the groups are parsed we'll get bad data
	Assert(g_cgrouprules>0);
	int count = 0;
	for ( int i = 0; i < listMax; i++ )
	{
		pList[i] = 255;
	}

	for ( int i = 0; i < g_cgrouprules; i++ )
	{
		grouprule_t *prule = &g_grouprules[i];
		if ( prule->szdir[ 0 ] && V_strstr( pDirname, prule->szdir ) )
		{
			pList[count] = i;
			count++;
			if ( count >= listMax )
				return count;
		}
	}
	return count;
}

bool MXR_IsMusicGroup( int ruleIndex )
{
	if ( ruleIndex != 255 && ruleIndex >= 0 && ruleIndex < g_cgrouprules )
	{
		grouprule_t *prule = &g_grouprules[ruleIndex];
		if ( Q_stristr(prule->szmixgroup, "music") )
			return true;
	}
	return false;
}


// determine which mixgroups sound is in, and save those mixgroupids in sound.
// use current soundmixer indicated with g_isoundmixer, and contents of g_rgpgrouprules.
// Algorithm: 
//		1. all conditions in a row are AND conditions, 
//		2. all rows sharing the same groupname are OR conditions.
// so - if a sound matches all conditions of a row, it is given that row's mixgroup id
//		if a sound doesn't match all conditions of a row, the next row is checked.

// returns 0, default mixgroup if no match
void MXR_GetMixGroupFromSoundsource( channel_t *pchan )
{
	grouprule_t *prule;
	bool fmatch;
	bool classMatch[CMXRCLASSMAX];

	// init all mixgroups for channel
	for ( int i = 0; i < ARRAYSIZE( pchan->mixgroups ); i++ )
	{
		pchan->mixgroups[i] = -1;
	}

	char sndname[MAX_PATH];
	pchan->sfx->getname(sndname, sizeof(sndname));

	// Use forward slashes here
	Q_FixSlashes( sndname, '/' );
	const char *pszclassname = GetClientClassname( pchan->soundsource );

	if ( snd_showclassname.GetInt() == 1 && pszclassname )
	{
		// utility: show classname of ent making sound
		DevMsg( "(%s:%s) \n", pszclassname, sndname);
	}

	// check for player
	bool bIsPlayer = g_pSoundServices->IsPlayer( pchan->soundsource );

	for ( int i = 0; i < g_cgroupclass; i++ )
	{
		classMatch[i] = ( pszclassname && Q_stristr( pszclassname, g_groupclasslist[i].szclassname ) || 
						( bIsPlayer && !Q_strcmp( g_groupclasslist[i].szclassname, "localPlayer") ) );
	}

	// check all group rules for a match, save
	// up to CMXRMATCHMAX matches in channel mixgroup.

	int cmixgroups = 0;
	if ( !pchan->sfx->m_bMixGroupsCached )
	{
		pchan->sfx->OnNameChanged( sndname );
	}
	
	// since this is a sorted list (in group rule order) we only need to test against the next matching rule
	// this avoids a search inside the loop
	int currentDirRuleIndex = 0;
	int currentDirRule = pchan->sfx->m_mixGroupList[0];

	for ( int i = 0; i < g_cgrouprules; i++)
	{
		prule = &g_grouprules[i];
		fmatch = true;

		// check directory or name substring
#ifdef _DEBUG
		// check dir table is correct in CSfxTable cache
		if ( prule->szdir[ 0 ] && Q_stristr( sndname, prule->szdir ) )
		{
			Assert(currentDirRule == i);
		}
		else
		{
			Assert(currentDirRule != i);
		}
		if ( prule->classId >= 0 )
		{
			// rule has a valid class id and table is correct
			Assert(prule->classId < g_cgroupclass);
			bool bShouldBeTrue = ( pszclassname && Q_stristr(pszclassname, g_groupclasslist[prule->classId].szclassname) ) ||
				( !Q_strcmp( g_groupclasslist[prule->classId].szclassname, "localPlayer" ) && bIsPlayer );
			if ( bShouldBeTrue )
			{					  
				Assert(classMatch[prule->classId] == true);
			}
			else
			{
				Assert(classMatch[prule->classId] == false);
			}
		}
#endif
		// this is the next matching dir for this sound, no need to search
		// becuse the list is sorted and we visit all elements
		if ( currentDirRule == i )
		{
			Assert(prule->szdir[0]);
			currentDirRuleIndex++;
			currentDirRule = 255;
			if ( currentDirRuleIndex < pchan->sfx->m_mixGroupCount )
			{
				currentDirRule = pchan->sfx->m_mixGroupList[currentDirRuleIndex];
			}
		}
		else if ( prule->szdir[ 0 ] )
		{
			fmatch = false;	// substring doesn't match, keep looking
		}

		// check class name
		if ( fmatch && prule->classId >= 0 )
		{
			fmatch = classMatch[prule->classId];
		}

		// check channel type
		if ( fmatch && prule->chantype >= 0 )
		{
			if ( pchan->entchannel != prule->chantype  )
				fmatch = false;	// channel type doesn't match, keep looking
		}

		float soundlevel = pchan->m_flSoundLevel;

		// check sndlvlmin/max
		if ( fmatch && prule->soundlevel_min >= 0 )
		{
			if ( soundlevel < prule->soundlevel_min )
				fmatch = false;	// soundlevel is less than min, keep looking
		}

		if ( fmatch && prule->soundlevel_max >= 0)
		{
			if ( soundlevel > prule->soundlevel_max )
				fmatch = false; // soundlevel is greater than max, keep looking
		}

		if ( fmatch )
		{
			pchan->mixgroups[cmixgroups] = prule->mixgroupid;
			cmixgroups++;
			
			// only print the first match
			if ( cmixgroups == 1 )
			{
				// filtered listing of sounds
				const char *filter = snd_list.GetString();
				if ( filter[0] )
				{
					// utility: show classname of ent making sound
					if ( Q_stristr( sndname, filter ))
					{
						DevMsg( "%s", sndname );
						// show main mixgroup for this sound
						mixervalues_t mValues;
						int lastMixGroup;
						MXR_GetVolFromMixGroup( pchan, &mValues, &lastMixGroup );
						if ( prule->szmixgroup[0] )
						{
							DevMsg(" : %s : vol: %4.2f, sndlvl: %4.2f \n", prule->szmixgroup, mValues.volume, soundlevel);
						}
					}
				}
			}

			// a member of CMXRMATCHMAX groups?
			if ( cmixgroups >= CMXRMATCHMAX )
				return;		// too many matches, stop looking
		}
		
		if (fmatch && snd_showclassname.GetInt() >= 2)
		{
			// show all mixgroups for this sound
			if (cmixgroups == 1)
			{
				DevMsg("\n%s:%s: ", g_szsoundmixer_cur, sndname);	
			}
			if (prule->szmixgroup[0])
			{
			//	int rgmixgroupid[CMXRMATCHMAX];
			//	for (int i = 0; i < CMXRMATCHMAX; i++)
			//		rgmixgroupid[i] = -1;
			//	rgmixgroupid[0] = prule->mixgroupid;
			//	float vol = MXR_GetVolFromMixGroup( rgmixgroupid );
			//	DevMsg("%s(%1.2f) ", prule->szmixgroup, vol);
				DevMsg("%s ", prule->szmixgroup);
			}
		}
	}
}



ConVar snd_disable_mixer_duck("snd_disable_mixer_duck", "0", FCVAR_CHEAT );	// if 1, soundmixer ducking is disabled

// given mix group id, return current duck volume

float MXR_GetDuckVolume( int mixgroupid )
{

	if ( snd_disable_mixer_duck.GetInt() )
		return 1.0;

	Assert ( mixgroupid < g_cgrouprules );
	Assert( mixgroupid >= 0 && mixgroupid < ARRAYSIZE(g_mapMixgroupidToGrouprulesid) );

	int	grouprulesid = g_mapMixgroupidToGrouprulesid[mixgroupid];

	// if this mixgroup is not ducked, return 1.0

	if ( !g_grouprules[grouprulesid].is_ducked )
		return 1.0;

	// return current duck value for this group, scaled by current fade in/out ramp

	return g_grouprules[grouprulesid].duck_ramp_val;

}

#define SND_DUCKER_UPDATETIME	0.1		// seconds to wait between ducker updates

double g_mxr_ducktime = 0.0;			// time of last update to ducker

// Get total volume currently playing in all groups,
// process duck volumes for all groups
// Call once per frame - updates occur at 10hz

void MXR_UpdateAllDuckerVolumes( void )
{
	if ( snd_disable_mixer_duck.GetInt() )
		return;

	// check timer since last update, only update at 10hz
	double dtime = g_pSoundServices->GetHostTime();
	
	// don't update until timer expires

	if (fabs(dtime - g_mxr_ducktime) < SND_DUCKER_UPDATETIME)
			return;
	
	g_mxr_ducktime = dtime;

	// clear out all total volume values for groups

	for ( int i = 0; i < g_cgrouprules; i++)
	{
		g_grouprules[i].total_vol = 0.0;
		g_grouprules[i].trigger_vol = 0.0;
	}
	
	// for every channel in a mix group which can cause ducking:
	// get total volume, store total in grouprule:
	
	CChannelList list;
	int ch_idx;

	channel_t *pchan;
	bool b_found_ducked_channel = false;

	g_ActiveChannels.GetActiveChannels( list );

	for ( int i = 0; i < list.Count(); i++ )
	{
		ch_idx = list.GetChannelIndex(i);
		pchan = &channels[ch_idx];

		if (pchan->last_vol > 0.0)
		{
			// account for all mix groups this channel belongs to...

			for (int j = 0; j < CMXRMATCHMAX; j++)
			{
				int imixgroup = pchan->mixgroups[j];

				if (imixgroup < 0)
					continue;
				
				int	grouprulesid = g_mapMixgroupidToGrouprulesid[imixgroup];
			
				if (g_grouprules[grouprulesid].causes_ducking)
					g_grouprules[grouprulesid].total_vol += pchan->last_vol;

				g_grouprules[grouprulesid].trigger_vol += pchan->last_vol;

				if (g_grouprules[grouprulesid].is_ducked)
					b_found_ducked_channel = true;
			}
		}		
	}



	// we're going to hanld triggers here because it's convenient
	// this is all a bit messy and should be cleaned up at some point


	// layer trigger defaults
	for ( int i = 0; i < CMXRMIXLAYERSMAX; i++ )
	{
		layertrigger_t *playertriggers = &g_layertriggers[i];
		if(!playertriggers->bhastrigger)
			continue;

		soundmixer_t *pmixlayer = &g_mixlayers[i];
		float curMixLevel = pmixlayer->mixAmount;

		float maxNewLevel = 0.0;
		float maxTriggerLevel = 0.0;
		float maxAttack = 0.0;
		float maxRelease = 0.0;
		for( int j = 0; j < CMXRGROUPMAX; j++)
		{
			if(!playertriggers->bistrigger[j])
				continue;
			int	grouprulesid = g_mapMixgroupidToGrouprulesid[j];

			maxTriggerLevel =  MAX(maxTriggerLevel, playertriggers->fmixamount[j]);
			if(g_grouprules[grouprulesid].trigger_vol > playertriggers->fthreshold[j])
			{
				maxNewLevel =  MAX(maxNewLevel, playertriggers->fmixamount[j]);
			}
			maxAttack = MAX(playertriggers->fattack[j], maxAttack);
			maxRelease = MAX(playertriggers->frelease[j], maxRelease);
		}

		if(maxNewLevel != curMixLevel)
		{
			float ramptime = (maxNewLevel > curMixLevel) ?  maxAttack : maxRelease;

			// delta is volume change per update (we can do this 
			// since we run at an approximate fixed update rate of 10hz)

			// only if we have a fade
			if(ramptime > 0.0)
			{
				float delta	= maxTriggerLevel;
	
				delta *= ( SND_DUCKER_UPDATETIME / ramptime );
				
				if (curMixLevel > maxNewLevel)
					delta = -delta;

				// update ramps

				float updatedMixLevel = curMixLevel + delta;

				if (updatedMixLevel < maxNewLevel && delta < 0)
					updatedMixLevel = maxNewLevel;
				if (updatedMixLevel > maxNewLevel && delta > 0)
					updatedMixLevel = maxNewLevel;

				maxNewLevel = updatedMixLevel;
			}
		}
		pmixlayer->mixAmount = maxNewLevel;
	}


	// TODO: THIS IS DESIGNED FOR MULTIPLE TRIGGERS (MAX(a,b) IS PROBABLY BEST)
/*	for (int i = 0; i < CMXRGROUPMAX; i++)
	{
		int	grouprulesid = g_mapMixgroupidToGrouprulesid[i];

		layertrigger_t *playertrigger = &g_layertriggers[i];
		for(int j = 0; j < playertrigger->cmixlayers; j++)
		{
			int layergroupid = playertrigger->imixlayer[j];
			if(layergroupid > -1 && layergroupid < g_cmixlayers)
			{
				soundmixer_t *pmixlayer = &g_mixlayers[layergroupid];
				bool trig = false;
				float mixAmount = 0.0;
				if(g_grouprules[grouprulesid].trigger_vol > playertrigger->fthreshold[j])
				{
					mixAmount = playertrigger->fmixamount[j];
					DevMsg("***LAYERTRIGGER!!!\n");
					trig = true;
				}
				
				float ramptime = (mixAmount >= pmixlayer->mixAmount) ? playertrigger->fattack[j] : playertrigger->frelease[j];

				// delta is volume change per update (we can do this 
				// since we run at an approximate fixed update rate of 10hz)

				float delta	= playertrigger->fmixamount[j];
				
				delta *= ( SND_DUCKER_UPDATETIME / ramptime );
				
				if (pmixlayer->mixAmount > mixAmount)
					delta = -delta;

				// update ramps

				pmixlayer->mixAmount += delta;

				if (pmixlayer->mixAmount < mixAmount && delta < 0)
					pmixlayer->mixAmount = mixAmount;
				if (pmixlayer->mixAmount > mixAmount && delta > 0)
					pmixlayer->mixAmount = mixAmount;

				if(trig)
					DevMsg("%f\n", pmixlayer->mixAmount);

			}
		}
	}
*/




	// if no channels playing which may be ducked, do nothing

	if ( !b_found_ducked_channel )
		return;

	// for all groups that can be ducked:
	// see if a higher priority sound group has a volume > threshold, 
	// if so, then duck this group by setting duck_target_vol to duck_target_pct.
	// if no sound group is causing ducking in this group, reset duck_target_vol to 1.0

	for (int i = 0; i < g_cgrouprules; i++)
	{
		if (g_grouprules[i].is_ducked)
		{
			int priority = g_grouprules[i].priority;

			float duck_volume = 1.0;				// clear to 1.0 if no channel causing ducking

			// make sure we interact appropriately with global voice ducking...
			// if global voice ducking is active, skip sound group ducking and just set duck_volume target to 1.0

			if ( g_DuckScale >= 1.0 )
			{	
				// check all sound groups for higher priority duck trigger

				for (int j = 0; j < g_cgrouprules; j++)
				{
					if (g_grouprules[j].priority > priority && 
						g_grouprules[j].causes_ducking &&
						g_grouprules[j].total_vol > g_grouprules[j].ducker_threshold)
					{
						// a higher priority group is causing this group to be ducked
						// set duck volume target to the ducked group's duck target percent
						// and break

						duck_volume = g_grouprules[i].duck_target_pct;
						
						// UNDONE: to prevent edge condition caused by crossing threshold, may need to have secondary
						// UNDONE: timer which allows ducking at 0.2 hz

						break;
					}
				}
			}

			g_grouprules[i].duck_target_vol = duck_volume; 
		}
	}

	// update all ducker ramps if current duck value is not target
	// if ramp is greater than duck_volume, approach at 'attack rate'
	// if ramp is less than duck_volume, approach at 'decay rate'

	for ( int i = 0; i < g_cgrouprules; i++ )
	{
		float target	= g_grouprules[i].duck_target_vol;
		float current	= g_grouprules[i].duck_ramp_val;
			
		if (g_grouprules[i].is_ducked && (current != target))
		{

			float ramptime = target < current ? snd_duckerattacktime.GetFloat() : snd_duckerreleasetime.GetFloat();

			// delta is volume change per update (we can do this 
			// since we run at an approximate fixed update rate of 10hz)

			float delta	= (1.0 - g_grouprules[i].duck_target_pct);
			
			delta *= ( SND_DUCKER_UPDATETIME / ramptime );
			
			if (current > target)
				delta = -delta;

			// update ramps

			current += delta;

			if (current < target && delta < 0)
				current = target;
			if (current > target && delta > 0)
				current = target;

			g_grouprules[i].duck_ramp_val = current;
		}
	}

}

//-----------------------------------------------------------------
//
// Setting mixer values
//
//-----------------------------------------------------------------
bool bPrintSetMixerDebug = false;

// this will set every mix group who's name contains the passed string
void S_SetIndexedMixGroupOfMixer( int imixgroup, const char *szparam, float val, soundmixer_t *pmixer, int setMixerType )
{
	// TODO: need to lose these string compares for cdllint as well, make int enums!!
	if(imixgroup >= 0)
	{
		if(!Q_stricmp("vol", szparam))
		{
			pmixer->mapMixgroupidToVolume[imixgroup] = val;
		}
		else if(!Q_stricmp("level", szparam))
		{
				pmixer->mapMixgroupidToLevel[imixgroup] = val;
		}
		else if(!Q_stricmp("dsp", szparam))
		{
				pmixer->mapMixgroupidToDsp[imixgroup] = val;
		}
		else if(!Q_stricmp("mute", szparam))
		{
				pmixer->mapMixgroupidToMute[imixgroup] = val;
		}
		else if(!Q_stricmp("solo", szparam))
		{
				pmixer->mapMixgroupidToSolo[imixgroup] = val;
		}
		else if(!Q_stricmp("mix", szparam))
		{
				pmixer->mixAmount = val;
		}
	}
}
void S_SetIndexedMixGroupOfMixer( int imixgroup, MXRMixGroupFields_t nMixGroupField, float val, soundmixer_t *pmixer )
{
	if(imixgroup >= 0)
	{
		switch( nMixGroupField )
		{
		case MXR_MIXGROUP_VOL:
			pmixer->mapMixgroupidToVolume[imixgroup] = val;
			break;
		case MXR_MIXGROUP_LEVEL:
			pmixer->mapMixgroupidToLevel[imixgroup] = val;
			break;
		case MXR_MIXGROUP_DSP:
			pmixer->mapMixgroupidToDsp[imixgroup] = val;
			break;
		case MXR_MIXGROUP_SOLO:
			pmixer->mapMixgroupidToSolo[imixgroup] = val;
			break;
		case MXR_MIXGROUP_MUTE:
			pmixer->mapMixgroupidToMute[imixgroup] = val;
			break;
		}
	}
}

void S_SetMixGroupOfMixer( const char *szgroupname, const char *szparam, float val, soundmixer_t *pmixer, int setMixerType )
{
	if ( !szgroupname )
		return;
	
	if ( Q_strlen(szgroupname) == 0 )
		return;

	// scan group rules for mapping from name to id
	for (int i = 0; i < g_cgrouprules; i++)
	{
		// if the mix groups name contains the string we set it
		if ( Q_stristr( g_grouprules[i].szmixgroup, szgroupname ) )
		{
			if(bPrintSetMixerDebug)
				DevMsg("Setting Mixer %s: MixGroup %s: %s : %f\n", pmixer->szsoundmixer, g_grouprules[i].szmixgroup, szparam,  val );

			S_SetIndexedMixGroupOfMixer(g_grouprules[i].mixgroupid, szparam, val, pmixer, setMixerType );
		}
	}
}

// this will set every mix group who's name contains the passed string
void S_SetMixGroupOfCurrentMixer( const char *szgroupname, const char *szparam, float val, int setMixerType )
{

	// get current mixer
	if ( g_isoundmixer < 0 )
		return;

	soundmixer_t *pmixer = &g_soundmixers[g_isoundmixer];

	S_SetMixGroupOfMixer( szgroupname, szparam, val, pmixer, setMixerType );

}
// this will set every mix group who's name contains the passed string
void S_SetMixGroupOfMixLayer( int nMixGroupIndex, int nMixLayerIndex, MXRMixGroupFields_t nMixGroupField, float flValue )
{
	soundmixer_t *pMixLayer = &g_mixlayers[ nMixLayerIndex ];
	S_SetIndexedMixGroupOfMixer( nMixGroupIndex, nMixGroupField, flValue, pMixLayer );

}

 int S_GetMixGroupIndex( const char *pMixGroupName )
{
	int imixgroupid = MXR_GetMixgroupFromName( pMixGroupName );

	if( imixgroupid < 0 || imixgroupid >= CMXRGROUPMAX )
	{
		DevWarning( "Error: MixGroup %s cannot be resolved!\n", pMixGroupName );
		return -1;
	}
	return imixgroupid;
}
int MXR_GetMixLayerIndexFromName( const char *szmixlayername )
{
	for (int i = 0; i < CMXRMIXLAYERSMAX; i++)
	{
		// sound mixers
		soundmixer_t *pmixer = &g_mixlayers[i];
		if ( !Q_stricmp( pmixer->szsoundmixer, szmixlayername ) )
		{
			return i;
		}
	}
	return -1;
}
int S_GetMixLayerIndex(const char *szmixlayername)
{
	return MXR_GetMixLayerIndexFromName(szmixlayername);
}
void S_SetMixLayerLevel(int index, float level)
{
	soundmixer_t *pmixlayer = &g_mixlayers[index];
	pmixlayer->mixAmount = level;
}

void S_SetMixLayerTriggerFactor( int nMixLayerIndex, int nMixGroupIndex, float flFactor )
{
	Assert( nMixLayerIndex != -1 );

	layertrigger_t *playertriggers = &g_layertriggers[ nMixLayerIndex ];

	if( nMixGroupIndex < 0 || nMixGroupIndex >= CMXRGROUPMAX)
	{
		DevMsg("Error: MixGroup %i, in LayerTriggers cannot be resolved!\n", nMixGroupIndex );
		return;
	}

	if( ! playertriggers->bistrigger[ nMixGroupIndex ] || !playertriggers->bhastrigger )
	{
		// error here
		return;
	}
	playertriggers->fmixamount[ nMixGroupIndex ] = flFactor;
}

void S_SetMixLayerTriggerFactor( const char *pMixLayerName, const char *pMixGroupName, float flFactor )
{
	int nMixLayerIndex = MXR_GetMixLayerIndexFromName( pMixLayerName );
	int nMixGroupIndex = MXR_GetMixgroupFromName( pMixGroupName );

	if( nMixGroupIndex < 0 || nMixGroupIndex >= CMXRGROUPMAX)
	{
		DevMsg("Error: MixGroup %s, in LayerTriggers cannot be resolved!\n", pMixGroupName );
		return;
	}

	S_SetMixLayerTriggerFactor( nMixLayerIndex, nMixGroupIndex, flFactor );
}

//-----------------------------------------------------------------------
//
// ConCommands to set mixer values
//
//-----------------------------------------------------------------------

static void MXR_SetSoundMixer( const CCommand &args )
{
	if ( args.ArgC() != 4 )
	{
		DevMsg("Parameters: mix group name, [vol, mute, solo], value");
		return;
	}

	const char *szgroupname = args[1];
	const char *szparam = args[2];
	float val = atof( args[3] );

	bPrintSetMixerDebug = true;
	S_SetMixGroupOfCurrentMixer(szgroupname, szparam, val, MIXER_SET );
	bPrintSetMixerDebug = false;
}

static ConCommand snd_setmixer("snd_setmixer", MXR_SetSoundMixer, "Set named Mixgroup of current mixer to mix vol, mute, solo.", FCVAR_CHEAT );
// set the named mixgroup volume to vol for the current soundmixer

static void MXR_SetMixLayer( const CCommand &args )
{
	if ( args.ArgC() != 5 )
	{
		DevMsg("Parameters: mix group name, layer name, [vol, mute, solo], value, amount");
		return;
	}

	const char *szlayername = args[1];
	const char *szgroupname = args[2];
	const char *szparam = args[3];
	float val = atof( args[4] );

	bPrintSetMixerDebug = true;
	for( int i = 0; i < g_cmixlayers; i++)
	{
		soundmixer_t *pmixlayer = &g_mixlayers[i];
		if(!Q_stricmp(pmixlayer->szsoundmixer, szlayername))
		{
			DevMsg("Setting MixLayer %s\n", pmixlayer->szsoundmixer);
			S_SetMixGroupOfMixer(szgroupname, szparam, val, pmixlayer, MIXER_SET );
		}
	}
	bPrintSetMixerDebug = false;
}
static ConCommand snd_setmixlayer("snd_setmixlayer", MXR_SetMixLayer, "Set named Mixgroup of named mix layer to mix vol, mute, solo.", FCVAR_CHEAT );

static void MXR_SetMixLayerAmount( const CCommand &args )
{
	if ( args.ArgC() != 3 )
	{
		DevMsg("Parameters: mixer name, mix amount");
		return;
	}

	const char *szlayername = args[1];
	float val = atof( args[2] );

	for( int i = 0; i < g_cmixlayers; i++)
	{
		soundmixer_t *pmixlayer = &g_mixlayers[i];
		if(!Q_stricmp(pmixlayer->szsoundmixer, szlayername))
		{
			DevMsg("Setting MixLayer %s : mix %f\n", pmixlayer->szsoundmixer, val);
			pmixlayer->mixAmount = val;
			break;
		}
	}
}
static ConCommand snd_setmixlayeramount("snd_setmixlayer_amount", MXR_SetMixLayerAmount, "Set named mix layer mix amount.", FCVAR_CHEAT );

static void MXR_SetMixLayerTriggerFactor( const CCommand &args )
{
	if ( args.ArgC() != 4 )
	{
		DevMsg("Parameters: mix layer name, mix group name, trigger amount");
		return;
	}

	const char *szlayername = args[1];
	const char *szgroupname = args[2];
	float val = atof( args[3] );

	S_SetMixLayerTriggerFactor( szlayername, szgroupname, val );

}
static ConCommand snd_soundmixer_set_trigger_factor("snd_soundmixer_set_trigger_factor", MXR_SetMixLayerTriggerFactor, "Set named mix layer / mix group, trigger amount.", FCVAR_CHEAT );




int MXR_GetFirstValidMixGroup( channel_t *pChannel )
{
	for (int i = 0; i < CMXRMATCHMAX; i++)
	{
		int imixgroup = pChannel->mixgroups[i];
		//rgmixgroupid[i];

		if (imixgroup < 0)
			continue;
	
		return imixgroup;
	}
	return -1;
}

// ---------------------------------------------------------------------
// given array of groupids (ie: the sound is in these groups),
// return a mix volume.

// return first mixgroup id in the provided array
// which maps to a non -1 volume value for this
// sound mixer
// ---------------------------------------------------------------------
soundmixer_t *MXR_GetCurrentMixer( )
{

	// if no soundmixer currently set, return 1.0 volume
	if (g_isoundmixer < 0)
	{
		return NULL;
	}
	if (g_csoundmixers)
	{
		soundmixer_t *pmixer = &g_soundmixers[g_isoundmixer];

		if (pmixer)
		{
			return pmixer;
		}
	}
	return NULL;
}
void MXR_GetValuesFromMixGroupIndex( mixervalues_t *mixValues, int imixgroup )
{

	// save lowest duck gain value for any of the mix groups this sound is in
	Assert(imixgroup < CMXRGROUPMAX);

	soundmixer_t *pmixer = MXR_GetCurrentMixer( );
	if( !pmixer )
	{
		// NEEDS ERROR!
		return;
	}

	if ( pmixer->mapMixgroupidToVolume[imixgroup] >= 0)
	{

		// level
		mixValues->level = pmixer->mapMixgroupidToLevel[imixgroup] * g_mastermixlayer.mapMixgroupidToLevel[imixgroup];
		mixValues->level *= snd_mixerMasterLevel.GetFloat();
		// dsp
		mixValues->dsp = pmixer->mapMixgroupidToDsp[imixgroup] * g_mastermixlayer.mapMixgroupidToDsp[imixgroup];
		mixValues->dsp *= snd_mixerMasterDSP.GetFloat();

		// modify gain with ducker settings for this group
		mixValues->volume = pmixer->mapMixgroupidToVolume[imixgroup] * g_mastermixlayer.mapMixgroupidToVolume[imixgroup];

		// check for muting
		mixValues->volume *= (1.0 - (MAX(pmixer->mapMixgroupidToMute[imixgroup], g_mastermixlayer.mapMixgroupidToMute[imixgroup])));

		// If any group is solo'd && not this one, mute.
		if(g_soloActive > 0.0)
		{
			// by definition current solo value is less than g_soloActive (max of mixer)
			// not positive this math is the right approach
			float factor = 1.0 - ((1.0 - (MAX(pmixer->mapMixgroupidToSolo[imixgroup], g_mastermixlayer.mapMixgroupidToSolo[imixgroup]) / g_soloActive)) * g_soloActive);
			mixValues->volume *= factor;
		}
	}
}


void MXR_GetVolFromMixGroup( channel_t *ch, mixervalues_t *mixValues, int *plast_mixgroupid )
{
	soundmixer_t *pmixer = MXR_GetCurrentMixer( );
	if( !pmixer )
	{
		// NEEDS ERROR!
		*plast_mixgroupid = 0;
		mixValues->volume = 1.0;
		return;
	}

	float duckgain = 1.0;

	// search mixgroupid array, return first match (non -1)

	for (int i = 0; i < CMXRMATCHMAX; i++)
	{
		int imixgroup = ch->mixgroups[i];
			//rgmixgroupid[i];

		if (imixgroup < 0)
			continue;

		// save lowest duck gain value for any of the mix groups this sound is in

		float duckgain_new = MXR_GetDuckVolume( imixgroup );

		if ( duckgain_new < duckgain)
			duckgain = duckgain_new;


		Assert(imixgroup < CMXRGROUPMAX);
		
		// return first mixgroup id in the passed in array
		// that maps to a non -1 volume value for this
		// sound mixer
		if ( pmixer->mapMixgroupidToVolume[imixgroup] >= 0)
		{
			*plast_mixgroupid = imixgroup;
			MXR_GetValuesFromMixGroupIndex( mixValues, imixgroup );

			// apply ducking although this isn't fully working because it doesn't collect up the lowest duck amount
			// Did this get broken on L4D1?
			mixValues->volume *= duckgain;
			return;
		}	
	}

	*plast_mixgroupid = 0;
	mixValues->volume = duckgain;

	return;
}

// get id of mixgroup name

int MXR_GetMixgroupFromName( const char *pszgroupname )
{
	// scan group rules for mapping from name to id
	if ( !pszgroupname )
		return -1;
	
	if ( Q_strlen(pszgroupname) == 0 )
		return -1;

	for (int i = 0; i < g_cgrouprules; i++)
	{
		if ( !Q_stricmp(g_grouprules[i].szmixgroup, pszgroupname ) )
			return g_grouprules[i].mixgroupid;
	}	

	return -1;
}

// get mixgroup name from id
char *MXR_GetGroupnameFromId( int mixgroupid)
{
	// scan group rules for mapping from name to id
	if (mixgroupid < 0)
		return NULL;

	for (int i = 0; i < g_cgrouprules; i++)
	{
		if ( g_grouprules[i].mixgroupid == mixgroupid)
			return g_grouprules[i].szmixgroup;
	}	

	return NULL;
}


// assign a unique mixgroup id to each unique named mix group
// within grouprules. Note: all mixgroupids in grouprules must be -1
// when this routine starts.

void MXR_AssignGroupIds( void )
{
	int cmixgroupid = 0;

	for (int i = 0; i < g_cgrouprules; i++)
	{
		int mixgroupid = MXR_GetMixgroupFromName( g_grouprules[i].szmixgroup );

		if (mixgroupid == -1)
		{
			// groupname is not yet assigned, provide a unique mixgroupid.

			g_grouprules[i].mixgroupid = cmixgroupid;

			// save reverse mapping, from mixgroupid to the first grouprules entry for this name

			g_mapMixgroupidToGrouprulesid[cmixgroupid] = i;

			cmixgroupid++;
		}	
		else
		{
			g_grouprules[i].mixgroupid = mixgroupid;
		}
	}
}

int MXR_AddClassname( const char *pName )
{
	char szclassname[CMXRNAMEMAX];
	Q_strncpy( szclassname, pName, CMXRNAMEMAX );
	for ( int i = 0; i < g_cgroupclass; i++ )
	{
		if ( !Q_stricmp( szclassname, g_groupclasslist[i].szclassname ) )
			return i;
	}
	if ( g_cgroupclass >= CMXRCLASSMAX )
	{
		Assert(g_cgroupclass < CMXRCLASSMAX);
		return -1;
	}
	Q_memcpy(g_groupclasslist[g_cgroupclass].szclassname, pName, MIN(CMXRNAMEMAX-1, strlen(pName)));
	g_cgroupclass++;
	return g_cgroupclass-1;
}


ConVar snd_soundmixer_version("snd_soundmixer_version", "2" );
#define CHAR_LEFT_PAREN		'{'
#define CHAR_RIGHT_PAREN	'}'

// load group rules and sound mixers from file

void S_FlushMixers( const CCommand &args )
{
	MXR_LoadAllSoundMixers();
}

static ConCommand SoundMixersFlush("snd_soundmixer_flush", S_FlushMixers, "Reload soundmixers.txt file.", FCVAR_CHEAT );

enum
{
	MXRPARSE_NONE,
	MXRPARSE_MIXGROUPS,
	MXRPARSE_SOUNDMIXERS,
	MXRPARSE_SOUNDMIXERGROUPS,
	MXRPARSE_MIXLAYERS,
	MXRPARSE_MIXLAYERGROUPS,
	MXRPARSE_LAYERTRIGGERS

};
#define MIXGROUPS_STRING "MixGroups"
#define SOUNDMIXERS_STRING "SoundMixers"
#define MIXLAYERS_STRING "MixLayers"
#define LAYERTRIGGERS_STRING "LayerTriggers"

ConVar DebugMXRParse("snd_soundmixer_parse_debug", "0");

const char *MXR_ParseMixGroup(const char *pstart)
{
	int parse_debug = DebugMXRParse.GetInt();

	grouprule_t *pgroup = &g_grouprules[g_cgrouprules];

	// copy mixgroup name, directory, classname
	// if no value specified, set to 0 length string
	if(parse_debug)
		DevMsg("MixGroup %s:\n", com_token);
	Q_memcpy(pgroup->szmixgroup, com_token, MIN(CMXRNAMEMAX-1, strlen(com_token)));

	// make sure all copied strings are null terminated
	pgroup->szmixgroup[CMXRNAMEMAX-1]	= 0;

	// path rule string
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if (com_token[0])
		{
			Q_memcpy(pgroup->szdir, com_token, MIN(CMXRNAMEMAX-1, strlen(com_token)));
			V_strlower( pgroup->szdir );
			// HACK to find group that affects voice channels
			if ( V_strstr(pgroup->szdir, "?voice") )
			{
				pgroup->is_voice = 1;
			}
		}
	}
	else
		DevMsg("Error: Parsing soundmixers.txt, mixgroup rules incomplete!\n");

	// make sure all copied strings are null terminated
	pgroup->szdir[CMXRNAMEMAX-1]		= 0;


	// classname
	pgroup->classId = -1;
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if (com_token[0])
		{
			pgroup->classId = MXR_AddClassname( com_token );
		}
	}
	else
		DevMsg("Error: Parsing soundmixers.txt, mixgroup rules incomplete!\n");


	// lookup chan
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if (com_token[0])
		{
			if (!Q_stricmp(com_token, "CHAN_STATIC"))
				pgroup->chantype = CHAN_STATIC;
			else if (!Q_stricmp(com_token, "CHAN_WEAPON"))
				pgroup->chantype = CHAN_WEAPON;
			else if (!Q_stricmp(com_token, "CHAN_VOICE"))
				pgroup->chantype = CHAN_VOICE;
			else if (!Q_stricmp(com_token, "CHAN_BODY"))
				pgroup->chantype = CHAN_BODY;
			else if (!Q_stricmp(com_token, "CHAN_ITEM"))
				pgroup->chantype = CHAN_ITEM;
		}
		else
			pgroup->chantype = -1;
	}
	else
		DevMsg("Error: Parsing soundmixers.txt, mixgroup rules incomplete!\n");

	// get sndlvls
	
	// soundlevel min
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if (com_token[0])
			pgroup->soundlevel_min = atoi(com_token);
		else
			pgroup->soundlevel_min = -1;
	}
	else
		DevMsg("Error: Parsing soundmixers.txt, mixgroup rules incomplete!\n");

	// soundlevel max
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if (com_token[0])
			pgroup->soundlevel_max = atoi(com_token);
		else
			pgroup->soundlevel_max = -1;
	}
	else
		DevMsg("Error: Parsing soundmixers.txt, mixgroup rules incomplete!\n");

	// get duck priority, IsDucked, Causes_ducking, duck_target_pct
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if (com_token[0])
			pgroup->priority = atoi(com_token);
		else
			pgroup->priority = 50;
	}
	else
		DevMsg("Error: Parsing soundmixers.txt, mixgroup rules incomplete!\n");


	// mix group is ducked
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if (com_token[0])
			pgroup->is_ducked = atoi(com_token);
		else
			pgroup->is_ducked = 0;
	}
	else
		DevMsg("Error: Parsing soundmixers.txt, mixgroup rules incomplete!\n");

	// mix group causes ducking
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if (com_token[0])
			pgroup->causes_ducking = atoi(com_token);
		else
			pgroup->causes_ducking = 0;
	}
	else
		DevMsg("Error: Parsing soundmixers.txt, mixgroup rules incomplete!\n");

	// ducking target pct
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if (com_token[0])
			pgroup->duck_target_pct = ((float)(atoi(com_token))) / 100.0f;
		else
			pgroup->duck_target_pct = 0.5f;
	}
	else
		DevMsg("Error: Parsing soundmixers.txt, mixgroup rules incomplete!\n");


	// ducking target pct
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if (com_token[0])
			pgroup->ducker_threshold = ((float)(atoi(com_token))) / 100.0f;
		else
			pgroup->ducker_threshold = 0.5f;
	}
	else
		DevMsg("Error: Parsing soundmixers.txt, mixgroup rules incomplete!\n");


	// set default values
	pgroup->duck_ramp_val = 1.0;
	pgroup->duck_target_vol = 1.0;
	pgroup->total_vol = 0.0;
	pgroup->trigger_vol = 0.0;

	// set mixgroup id to -1
	pgroup->mixgroupid = -1;

	// update rule count
	g_cgrouprules++;
	return pstart;
}

const char *MXR_ParseSoundMixer(const char *pstart, soundmixer_t *pmixer)
{

	int parse_debug = DebugMXRParse.GetInt();

	// lookup mixgroupid for groupname
	char szgroupname[CMXRNAMEMAX];
	V_strcpy_safe(szgroupname, com_token);

	//	int mixgroupid = MXR_GetMixgroupFromName( com_token );


	float volume = 1.0;
	float level = 1.0;
	float dsp = 1.0;
	float solo = 0.0;
	float mute = 0.0;

	// get mix value
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if( com_token[0] )
			volume = atof( com_token );
		else
			volume = 1.0;

	}
	else
		DevMsg("Error: Parsing soundmixers.txt, soundmixer mix group values incomplete!\n");
		
	// are we using new soundmixer features?
	if(snd_soundmixer_version.GetInt() >= 2)
	{
		// checking for new mixer features
		if(COM_TokenWaiting( pstart ))
		{
			// get "level" value
			pstart = COM_Parse( pstart );
			level = atof( com_token );
		}

		if(COM_TokenWaiting( pstart ))
		{
			// get "dsp" value
			pstart = COM_Parse( pstart );
			dsp = atof( com_token );
		}

		if(COM_TokenWaiting( pstart ))
		{
			pstart = COM_Parse( pstart );
			solo = atof( com_token );
		}

		if(COM_TokenWaiting( pstart ))
		{
			pstart = COM_Parse( pstart );
			mute = atof( com_token );
		}

	}

	// scan group rules for mapping from name to id
	for (int i = 0; i < g_cgrouprules; i++)
	{
		// if the mix groups name contains the string we set it
		if ( !Q_strcmp( g_grouprules[i].szmixgroup, szgroupname ) )
		{
			// sanity check mix group
			if(g_grouprules[i].mixgroupid < 0 || g_grouprules[i].mixgroupid >= CMXRGROUPMAX)
			{
				DevMsg("Error: MixGroup %s, in SoundMixer %s, cannot be resolved!\n", com_token, pmixer->szsoundmixer);
			}
			else
			{
				if(parse_debug)
					DevMsg("MixGroup %s: %f : %f : %f : %f : %f \n", szgroupname, volume, level, dsp, solo, mute);

				// store value for mixgroupid
				pmixer->mapMixgroupidToVolume[g_grouprules[i].mixgroupid] = MAX(volume, 0.0);
				pmixer->mapMixgroupidToLevel[g_grouprules[i].mixgroupid] = MAX(level, 0.0);
				pmixer->mapMixgroupidToDsp[g_grouprules[i].mixgroupid] = MAX(dsp, 0.0);
				pmixer->mapMixgroupidToSolo[g_grouprules[i].mixgroupid] = MAX(solo, 0.0);
				pmixer->mapMixgroupidToMute[g_grouprules[i].mixgroupid] = MAX(mute, 0.0);
			}
		}
	}

	return pstart;
}

const char *MXR_ParseLayerTriggers(const char *pstart)
{
	int parse_debug = DebugMXRParse.GetInt();

	// copy mixgroup name, directory, classname
	// if no value specified, set to 0 length string
	if(parse_debug)
		DevMsg("MixLayer triggered %s:\n", com_token);


	int imixlayerid = MXR_GetMixLayerIndexFromName( com_token );
	if ( imixlayerid == -1 )
	{
		Warning( "Failed to get mix layer %s!\n", com_token );
		return pstart;
	}
	layertrigger_t *playertriggers = &g_layertriggers[imixlayerid];

	// sanity check mix group

	int imixgroupid = -1;
	// path rule string
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if (com_token[0])
		{
			imixgroupid = MXR_GetMixgroupFromName( com_token );
		}
	}
	else
	{
		DevMsg("Error: MixLayer Trigger entries require minimum 2 arguments\n");
		return pstart;
	}

	if(imixgroupid < 0 || imixgroupid >= CMXRGROUPMAX)
	{
		DevMsg("Error: MixGroup %s, in LayerTriggers cannot be resolved!\n", com_token);
		return pstart;
	}

	playertriggers->bistrigger[imixgroupid] = true;
	playertriggers->bhastrigger = true;

	// threshold
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if (com_token[0])
		{
			playertriggers->fthreshold[imixgroupid] = atof( com_token );
		}
	}
	else
		return pstart;

	// mixamount
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if (com_token[0])
		{
			playertriggers->fmixamount[imixgroupid] = atof( com_token );
		}
	}
	else
		return pstart;

	// attack
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if (com_token[0])
		{
			playertriggers->fattack[imixgroupid] = atof( com_token );
		}
	}
	else
		return pstart;

	// release
	if(COM_TokenWaiting( pstart ))
	{
		pstart = COM_Parse( pstart );
		if (com_token[0])
		{
			playertriggers->frelease[imixgroupid] = atof( com_token );
		}
	}

	return pstart;
}

bool MXR_LoadAllSoundMixers( void )
{
	// init soundmixer globals

	g_isoundmixer = -1;
	g_szsoundmixer_cur[0] = 0;

	g_csoundmixers	= 0;					// total number of soundmixers found
	g_cmixlayers	= 0;					// total number of soundmixers found
	g_cgrouprules	= 0;					// total number of group rules found

	Q_memset(g_soundmixers, 0, sizeof(g_soundmixers));
	Q_memset(g_mixlayers, 0, sizeof(g_mixlayers));
	Q_memset(g_grouprules, 0, sizeof(g_grouprules));

		// init all mix group mixer values to -1.
	for (int i = 0; i < CMXRSOUNDMIXERSMAX; i++)
	{
		// sound mixers
		soundmixer_t *pmixer = &g_soundmixers[i];
		V_strcpy_safe(pmixer->szsoundmixer, "");
		// sound mixers default to full on
		pmixer->mixAmount = 1.0;
		for (int j = 0; j < CMXRGROUPMAX; j++)
		{
			pmixer->mapMixgroupidToVolume[j] = -1.0;
			pmixer->mapMixgroupidToLevel[j] = 1.0;
			pmixer->mapMixgroupidToDsp[j] = 1.0;
			pmixer->mapMixgroupidToSolo[j] = 0.0;
			pmixer->mapMixgroupidToMute[j] = 0.0;
		}
	}
	for (int i = 0; i < CMXRMIXLAYERSMAX; i++)
	{
		// mix layers
		soundmixer_t *pmixlayers = &g_mixlayers[i];
		V_strcpy_safe(pmixlayers->szsoundmixer, "");
		// mix layers default to all off
		pmixlayers->mixAmount = 0.0;
		for (int j = 0; j < CMXRGROUPMAX; j++)
		{	
			pmixlayers->mapMixgroupidToVolume[j] = -1.0;
			pmixlayers->mapMixgroupidToLevel[j] = 1.0;
			pmixlayers->mapMixgroupidToDsp[j] = 1.0;
			pmixlayers->mapMixgroupidToSolo[j] = 0.0;
			pmixlayers->mapMixgroupidToMute[j] = 0.0;
		}
	}

	// layer trigger defaults
	for (int i = 0; i < CMXRMIXLAYERSMAX; i++)
	{
		layertrigger_t *playertriggers = &g_layertriggers[i];
		playertriggers->bhastrigger = false;
		for( int j = 0; j < CMXRGROUPMAX; j++)
		{
			playertriggers->bistrigger[j] = false;
			playertriggers->fthreshold[j] = 0.0;
			playertriggers->fmixamount[j] = 1.0;
			playertriggers->fattack[j]    = 0.0;
			playertriggers->frelease[j]   = 0.0;

		}
	}

	// sound mixers file
	char szFile[MAX_OSPATH];
	const char *pstart;
	bool bResult = false;
	char *pbuffer;

	Q_snprintf( szFile, sizeof( szFile ), "scripts/soundmixers.txt" );

	pbuffer = (char *)COM_LoadFile( szFile, 5, NULL ); // Use malloc - free at end of this routine
	if ( !pbuffer )
	{
		Error( "MXR_LoadAllSoundMixers: unable to open '%s'\n", szFile );
		return bResult;
	}

	pstart = pbuffer;
	
	int parse_debug = DebugMXRParse.GetInt();


	int currentMxrParse = MXRPARSE_NONE;
	int currentParseLevel = 0;

	// check for first CHAR_LEFT_PAREN
	
	while (1)
	{
		pstart = COM_Parse( pstart );
	
		if ( strlen(com_token) <= 0)
			break; // eof

		// handle in and out of brackets
		if ( com_token[0] == CHAR_LEFT_PAREN )
		{
			currentParseLevel++;
			//if(parse_debug)
				//DevMsg("parse level %i:\n", currentParseLevel);
			continue;
		}
		else if ( com_token[0] == CHAR_RIGHT_PAREN )
		{
			// do any clean up processing for the previous block
			currentParseLevel--;
			//if(parse_debug)
				//DevMsg("parse level %i:\n", currentParseLevel);

			if(currentMxrParse == MXRPARSE_MIXGROUPS)
			{
				// now process all groupids in groups, such that
				// each mixgroup gets a unique id.

				MXR_AssignGroupIds();
				currentMxrParse = MXRPARSE_NONE;
				if(parse_debug)
					DevMsg("Total Mix Groups Rules: %i\n", g_cgrouprules);
			}
			else if(currentMxrParse == MXRPARSE_SOUNDMIXERGROUPS)
			{
				currentMxrParse = MXRPARSE_SOUNDMIXERS;
				g_csoundmixers++;
			}
			else if(currentMxrParse == MXRPARSE_SOUNDMIXERS)
			{
				currentMxrParse = MXRPARSE_NONE;
			}
			else if(currentMxrParse == MXRPARSE_MIXLAYERGROUPS)
			{
				currentMxrParse = MXRPARSE_MIXLAYERS;
				g_cmixlayers++;
			}
			else if(currentMxrParse == MXRPARSE_MIXLAYERS)
			{
				currentMxrParse = MXRPARSE_NONE;
				if(parse_debug)
					DevMsg("Total Mix Layers: %i\n", g_cmixlayers);

			}
			else if(currentMxrParse == MXRPARSE_LAYERTRIGGERS)
			{
				currentMxrParse = MXRPARSE_NONE;
			}
			continue;
		}

		// parsing the outside?
		if( currentMxrParse == MXRPARSE_NONE)
		{
			if (!Q_strcmp( com_token, MIXGROUPS_STRING ) )
			{
				currentMxrParse = MXRPARSE_MIXGROUPS;
				if(parse_debug)
					DevMsg("Parsing MixGroups:\n");
				continue;
			}
			else if (!Q_strcmp( com_token, SOUNDMIXERS_STRING ) )
			{
				currentMxrParse = MXRPARSE_SOUNDMIXERS;
				if(parse_debug)
					DevMsg("Parsing SoundMixers:\n");
				continue;
			}
			else if (!Q_strcmp( com_token, MIXLAYERS_STRING ) )
			{
				currentMxrParse = MXRPARSE_MIXLAYERS;
				if(parse_debug)
					DevMsg("Parsing MixLayers:\n");
				continue;
			}
			else if (!Q_strcmp( com_token, LAYERTRIGGERS_STRING ) )
			{
				currentMxrParse = MXRPARSE_LAYERTRIGGERS;
				if(parse_debug)
					DevMsg("Parsing LayerTriggers:\n");
				continue;
			}
		}
		else if ( currentMxrParse == MXRPARSE_MIXGROUPS )
		{
			if (g_cgrouprules > CMXRGROUPRULESMAX)
			{
				// UNDONE: error! too many rules
				DevMsg("Error: Too many mix groups! MixGroup %s ignored\n", com_token);  
				continue;
			}
			pstart = MXR_ParseMixGroup(pstart);

		}
		else if ( currentMxrParse == MXRPARSE_LAYERTRIGGERS )
		{
			/*if (g_cgrouprules > CMXRGROUPRULESMAX)
			{
				// UNDONE: error! too many rules
				DevMsg("Error: Too many mix groups! MixGroup %s ignored\n", com_token);  
				continue;
			}*/
			pstart = MXR_ParseLayerTriggers(pstart);

		}
		else if ( currentMxrParse == MXRPARSE_SOUNDMIXERS && currentParseLevel < 2 )
		{
			// save name in soundmixer
			if (g_csoundmixers > CMXRSOUNDMIXERSMAX)
			{
				DevMsg("Error: Too many sound mixers! SoundMixer %s ignored\n", com_token);  	
				continue;
			}
			soundmixer_t *pmixer = &g_soundmixers[g_csoundmixers];

			if(parse_debug)
				DevMsg("SoundMixer %s:\n", com_token);

			currentMxrParse = MXRPARSE_SOUNDMIXERGROUPS;

			Q_memcpy(pmixer->szsoundmixer, com_token, MIN(CMXRNAMEMAX-1, strlen(com_token)));
		}
		else if ( currentMxrParse == MXRPARSE_SOUNDMIXERGROUPS &&  currentParseLevel == 2 )
		{
			soundmixer_t *pmixer = &g_soundmixers[g_csoundmixers];

			pstart = MXR_ParseSoundMixer(pstart, pmixer );
		}
		// mix layers
		else if ( currentMxrParse == MXRPARSE_MIXLAYERS && currentParseLevel < 2 )
		{
			// save name in soundmixer
			if (g_cmixlayers > CMXRMIXLAYERSMAX)
			{
				DevMsg("Error: Too many mix layers! MixLayer %s ignored\n", com_token);  	
				continue;
			}
			soundmixer_t *pmixlayer = &g_mixlayers[g_cmixlayers];

			if(parse_debug)
				DevMsg("MixLayers %s:\n", com_token);

			currentMxrParse = MXRPARSE_MIXLAYERGROUPS;

			Q_memcpy(pmixlayer->szsoundmixer, com_token, MIN(CMXRNAMEMAX-1, strlen(com_token)));

		}
		else if ( currentMxrParse == MXRPARSE_MIXLAYERGROUPS &&  currentParseLevel == 2 )
		{
			soundmixer_t *pmixlayer = &g_mixlayers[g_cmixlayers];
			pstart = MXR_ParseSoundMixer(pstart, pmixlayer);
		}
	}
	bResult = true;

// loadmxr_exit:
	free( pbuffer );
	return bResult;
}

void MXR_ReleaseMemory( void )
{
	// free all resources
}


//--------------------------------------------------------------------------------------------
//
// Debug and diagnostics
//
//---------------------------------------------------------------------------------------------
static void MXR_PrintMixGroups( soundmixer_t *pmixer )
{
	int imixgroup = 0;
	int nMixGroupId = 0;
	for( int i = 0; i < g_cgrouprules; i++ )
	{

		imixgroup = g_grouprules[i].mixgroupid;

		if( imixgroup < 0 )
		{
			nMixGroupId++;
			continue;
		}
		else if ( imixgroup != nMixGroupId ) // only uniquely id'd
		{
			continue;
		}

		if( pmixer->mapMixgroupidToVolume[imixgroup] < 0.0 )
		{
			nMixGroupId++;
			continue;
		}
	
		int nStrLen = V_strlen( g_grouprules[i].szmixgroup );
		int nDiff = 32 - nStrLen;
		char nTmpStr[32];
		for( int j = 0; j < nDiff; j++ )
		{
			nTmpStr[j] = ' ';
		}
		nTmpStr[nDiff] = NULL;
		DevMsg("%s: %s", g_grouprules[i].szmixgroup, nTmpStr );

		DevMsg("vol: %3.2f ", pmixer->mapMixgroupidToVolume[imixgroup]);
		DevMsg("lvl: %3.2f ", pmixer->mapMixgroupidToLevel[imixgroup]);
		DevMsg("dsp: %3.2f ", pmixer->mapMixgroupidToDsp[imixgroup]);
		DevMsg("solo: %3.2f ", pmixer->mapMixgroupidToSolo[imixgroup]);
		DevMsg("mute: %3.2f\n", pmixer->mapMixgroupidToMute[imixgroup]);
		nMixGroupId++;
	}
}

static void MXR_ListMixers( const CCommand &args )
{
	
	for (int i = 0; i < g_csoundmixers; i++)
	{
		soundmixer_t *pmixer = &g_soundmixers[i];
		DevMsg("%s:\n", pmixer->szsoundmixer);
		MXR_PrintMixGroups( pmixer );
	}
}
static ConCommand snd_list_mixers("snd_soundmixer_list_mixers", MXR_ListMixers, "List all mixers to dev console." );

static void MXR_ListMixLayers( const CCommand &args )
{
	for (int i = 0; i < g_cmixlayers; i++)
	{
		soundmixer_t *pmixer = &g_mixlayers[i];
		DevMsg("%s: %f\n", pmixer->szsoundmixer, pmixer->mixAmount );
		MXR_PrintMixGroups( pmixer );
	}
	DevMsg( "g_mastermixlayer:\n" );
	MXR_PrintMixGroups( &g_mastermixlayer );

}
static ConCommand snd_list_mix_layers("snd_soundmixer_list_mix_layers", MXR_ListMixLayers, "List all mix layers to dev console.");




//---------------------------------------------------------------------------
//
// list all mix groups and their values
//
//---------------------------------------------------------------------------
static ConCommand snd_list_mix_groups("snd_soundmixer_list_mix_groups", MXR_ListMixGroups, "List all mix groups to dev console.");
static void MXR_ListMixGroups( const CCommand &args )
{
	soundmixer_t *pmixer = &g_soundmixers[g_isoundmixer];
	MXR_PrintMixGroups( pmixer );
}



void S_GetMixGroupOfCurrentMixer( const char *szgroupname, soundmixer_t *pmixer)
{

	// iterate over groups
	int imixgroup = 0;
	for (int i = 0; i < g_cgrouprules; i++)
	{
		if(Q_stristr(g_grouprules[i].szmixgroup, szgroupname))
		{
			imixgroup = g_grouprules[i].mixgroupid;
				
		//	float dynVolume = g_mastermixlayer.mapMixgroupidToVolume[imixgroup];
			float volume = pmixer->mapMixgroupidToVolume[imixgroup];
		//	float dynLevel = g_mastermixlayer.mapMixgroupidToLevel[imixgroup];
			float level = pmixer->mapMixgroupidToLevel[imixgroup];
		//	float dynDsp = g_mastermixlayer.mapMixgroupidToDsp[imixgroup];
			float dsp = pmixer->mapMixgroupidToDsp[imixgroup];
		//	float dynMute = g_mastermixlayer.mapMixgroupidToMute[imixgroup];
			float mute = pmixer->mapMixgroupidToMute[imixgroup];
		//	float dynSolo = g_mastermixlayer.mapMixgroupidToSolo[imixgroup];
			float solo = pmixer->mapMixgroupidToSolo[imixgroup];

			DevMsg("%s:\n", g_grouprules[i].szmixgroup);
			DevMsg("\tVOL: %f\n\tLVL: %f\n\tDSP: %f\n\tMUTE: %f\n\tSOLO: %f\n\n",
				volume, level, dsp, mute, solo);

		}
	}
}

static void MXR_GetSoundMixer( const CCommand &args )
{
	soundmixer_t *pmixer = NULL;

	if ( args.ArgC() == 2)
	{
		// get current mixer
		if ( g_isoundmixer < 0 )
			return;
		pmixer = &g_soundmixers[g_isoundmixer];
	}
	else
	{
		//DevMsg("Parameters: mix group name, [vol, mute, solo], value");
		return;
	}

	const char *szgroupname = args[1];
	S_GetMixGroupOfCurrentMixer(szgroupname, pmixer);
}

static ConCommand snd_getmixer("snd_getmixer", MXR_GetSoundMixer, "Get data related to mix group matching string");



struct debug_showvols_t
{
	char *psz;			// group name
	int	  mixgroupid;	// groupid
	float vol;			// group volume
	float totalvol;		// total volume of all sounds playing in this group
};

// show the current soundmixer output
// display routine for MXR_DebugShowMixVolumes

#define MXR_DEBUG_INCY	(1.0/40.0)			// vertical text spacing
#define MXR_DEBUG_GREENSTART 0.3			// start position on screen of bar

#define MXR_DEBUG_MAXVOL		1.0			// max volume scale
#define MXR_DEBUG_REDLIMIT		1.0			// volume limit into yellow
#define MXR_DEBUG_YELLOWLIMIT	0.7			// volume limit into red

#define MXR_DEBUG_VOLSCALE 48				// length of graph in characters
#define MXR_DEBUG_CHAR			'-'			// bar character


int g_debug_mxr_displaycount = 0;

void MXR_DebugGraphMixVolumes( debug_showvols_t *groupvols, int cgroups)
{
	float flXpos, flYpos, flXposBar, duration;
	int r,g,b,a;
	int rb, gb, bb, ab;
	flXpos = 0;
	flYpos = 0;
	char text[128];
	char bartext[MXR_DEBUG_VOLSCALE*3];

	duration = 0.01;

	g_debug_mxr_displaycount++;

	if (!(g_debug_mxr_displaycount % 10))
		return;		// only display every 10 frames


	r = 96; g = 86; b = 226; a = 255; ab = 255;
	
	// show volume, dsp_volume

/*	Q_snprintf( text, 128, "Game Volume: %1.2f", volume.GetFloat());
	CDebugOverlay::AddScreenTextOverlay(flXpos, flYpos, duration, r, g, b,a,  text); 
	flYpos += MXR_DEBUG_INCY; */

	Q_snprintf( text, 128, "DSP Volume: %1.2f", dsp_volume.GetFloat());
	CDebugOverlay::AddScreenTextOverlay(flXpos, flYpos, duration, r, g, b,a,  text); 
	flYpos += MXR_DEBUG_INCY;
	
	for (int i = 0; i < cgroups; i++)
	{	
		// r += 64; g += 64; b += 16;

		r = r % 255; g = g % 255; b = b % 255;
		
		Q_snprintf( text, 128, "%s: %1.2f (%1.2f)", groupvols[i].psz, 
					groupvols[i].vol * g_DuckScale, groupvols[i].totalvol * g_DuckScale);

		CDebugOverlay::AddScreenTextOverlay(flXpos, flYpos, duration, r, g, b,a,  text);

		// draw volume bar graph
		
		float vol = (groupvols[i].totalvol * g_DuckScale) / MXR_DEBUG_MAXVOL;
		
		// draw first 70% green
		float vol1 = 0.0;
		float vol2 = 0.0;
		float vol3 = 0.0;
		int cbars;

		vol1 = clamp(vol, 0.0, 0.7);
		vol2 = clamp(vol, 0.0, 0.95);
		vol3 = vol;

		flXposBar = flXpos + MXR_DEBUG_GREENSTART;

		if (vol1 > 0.0)
		{
			//flXposBar = flXpos + MXR_DEBUG_GREENSTART;

			rb = 0; gb= 255; bb = 0;		// green bar
			Q_memset(bartext, 0, sizeof(bartext));

			cbars = (int)((float)vol1 * (float)MXR_DEBUG_VOLSCALE);
			cbars = clamp(cbars, 0, MXR_DEBUG_VOLSCALE*3-1);
			Q_memset(bartext, MXR_DEBUG_CHAR, cbars);

			CDebugOverlay::AddScreenTextOverlay(flXposBar, flYpos, duration, rb, gb, bb,ab,  bartext);
		}

		
		// yellow bar
		if (vol2 > MXR_DEBUG_YELLOWLIMIT)	
		{
			rb = 255; gb = 255; bb = 0;	
			Q_memset(bartext, 0, sizeof(bartext));

			cbars = (int)((float)vol2 * (float)MXR_DEBUG_VOLSCALE);
			cbars = clamp(cbars, 0, MXR_DEBUG_VOLSCALE*3-1);
			Q_memset(bartext, MXR_DEBUG_CHAR, cbars);

			CDebugOverlay::AddScreenTextOverlay(flXposBar, flYpos, duration, rb, gb, bb,ab,  bartext);
		}

		// red bar
		if (vol3 > MXR_DEBUG_REDLIMIT)
		{
			//flXposBar = flXpos + MXR_DEBUG_REDSTART;
			rb = 255; gb = 0; bb = 0;
			Q_memset(bartext, 0, sizeof(bartext));

			cbars = (int)((float)vol3 * (float)MXR_DEBUG_VOLSCALE);
			cbars = clamp(cbars, 0, MXR_DEBUG_VOLSCALE*3-1);
			Q_memset(bartext, MXR_DEBUG_CHAR, cbars);

			CDebugOverlay::AddScreenTextOverlay(flXposBar, flYpos, duration, rb, gb, bb,ab,  bartext);
		}

		flYpos += MXR_DEBUG_INCY;
	}
}
void MXR_DebugShowMixVolumes( void )
{
	if (snd_showmixer.GetInt() == 0)
		return;

	// for the current soundmixer:
	// make a totalvolume bucket for each mixgroup type in the soundmixer.
	// for every active channel, add its spatialized volume to 
	// totalvolume bucket for that channel's selected mixgroup
	
	// display all mixgroup/volume/totalvolume values as horizontal bars

	debug_showvols_t groupvols[CMXRGROUPMAX];

	int i;
	int cgroups = 0;

	if (g_isoundmixer < 0)
	{
		DevMsg("No sound mixer selected!");
		return;
	}
	
	soundmixer_t *pmixer = &g_soundmixers[g_isoundmixer];

	// for every entry in mapMixgroupidToVolume which is not -1, 
	// set up groupvols

	for (i = 0; i < CMXRGROUPMAX; i++)
	{
		// currently sound mixers are required and entry per mix group for anything to work!!
		// TODO: change this and make sound mixers operate sparsely, like layers
		if (pmixer->mapMixgroupidToVolume[i] >= 0)
		{
			groupvols[cgroups].mixgroupid = i;
			groupvols[cgroups].psz = MXR_GetGroupnameFromId( i );
			groupvols[cgroups].totalvol = 0.0;
			groupvols[cgroups].vol = pmixer->mapMixgroupidToVolume[i] * g_mastermixlayer.mapMixgroupidToVolume[i];
			cgroups++;
		}
	}

	// for every active channel, get its volume and 
	// the selected mixgroupid, add to groupvols totalvol

	CChannelList list;
	int ch_idx;
	channel_t *pchan;

	g_ActiveChannels.GetActiveChannels( list );

	for ( i = 0; i < list.Count(); i++ )
	{
		ch_idx = list.GetChannelIndex(i);
		pchan = &channels[ch_idx];
		if (pchan->last_vol > 0.0)
		{
			// find entry in groupvols
			for (int j = 0; j < CMXRGROUPMAX; j++)
			{
				if (pchan->last_mixgroupid == groupvols[j].mixgroupid)
				{
					groupvols[j].totalvol += pchan->last_vol;
					break;
				}
			}
		}	
	}

	// groupvols is now fully initialized - just display it

	MXR_DebugGraphMixVolumes( groupvols, cgroups);
}

#ifdef _DEBUG

// set the named mixgroup volume to vol for the current soundmixer
void MXR_DebugSetMixGroupVolume( const CCommand &args )
{
	if ( args.ArgC() != 3 )
	{
		DevMsg("Parameters: mix group name, volume");
		return;
	}

	const char *szgroupname = args[1];
	float vol = atof( args[2] );

	int imixgroup = MXR_GetMixgroupFromName( szgroupname );

	if ( g_isoundmixer < 0 )
		return;
	
	soundmixer_t *pmixer = &g_soundmixers[g_isoundmixer];

	pmixer->mapMixgroupidToVolume[imixgroup] = vol;
}

#endif //_DEBUG


