//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//


#include "render_pch.h"
#include "gl_lightmap.h"
#include "view.h"
#include "gl_cvars.h"
#include "zone.h"
#include "gl_water.h"
#include "r_local.h"
#include "gl_model_private.h"
#include "mathlib/bumpvects.h"
#include "gl_matsysiface.h"
#include <float.h>
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/imesh.h"
#include "tier0/dbg.h"
#include "tier0/vprof.h"
#include "tier1/callqueue.h"
#include "lightcache.h"
#include "cl_main.h"
#include "materialsystem/imaterial.h"
#include "utlsortvector.h"
#include "cache_hints.h"
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// globals
//-----------------------------------------------------------------------------

// Only enable this if you are testing lightstyle performance.
//#define UPDATE_LIGHTSTYLES_EVERY_FRAME

ALIGN128 Vector4D blocklights[NUM_BUMP_VECTS+1][ MAX_LIGHTMAP_DIM_INCLUDING_BORDER * MAX_LIGHTMAP_DIM_INCLUDING_BORDER ];

ConVar r_avglightmap( "r_avglightmap", "0", FCVAR_CHEAT | FCVAR_MATERIAL_SYSTEM_THREAD );
ConVar r_maxdlights( "r_maxdlights", "32" );

// Disable dlights on console by default (for the sake of memory and perf):
ConVar r_dlightsenable( "r_dlightsenable", "1", FCVAR_CHEAT | FCVAR_MATERIAL_SYSTEM_THREAD );


extern ConVar r_unloadlightmaps;
extern ConVar r_keepstyledlightmapsonly;
extern bool g_bHunkAllocLightmaps;

static int r_dlightvisible;
static int r_dlightvisiblethisframe;
static int s_nVisibleDLightCount;
static int s_nMaxVisibleDLightCount;

//-----------------------------------------------------------------------------
// Visible, not visible DLights
//-----------------------------------------------------------------------------
void R_MarkDLightVisible( int dlight )
{
	if ( (r_dlightvisible & ( 1 << dlight )) == 0 )
	{
		++s_nVisibleDLightCount;
		r_dlightvisible |= 1 << dlight;
	}
}

void R_MarkDLightNotVisible( int dlight )
{
	if ( r_dlightvisible & ( 1 << dlight ))
	{
		--s_nVisibleDLightCount;
		r_dlightvisible &= ~( 1 << dlight );
	}
}


//-----------------------------------------------------------------------------
// Must call these at the start + end of rendering each view
//-----------------------------------------------------------------------------
void R_DLightStartView()
{
	r_dlightvisiblethisframe = 0;
	s_nMaxVisibleDLightCount = r_maxdlights.GetInt();
}

void R_DLightEndView()
{
	if ( !g_bActiveDlights )
		return;
	for( int lnum=0 ; lnum<MAX_DLIGHTS; lnum++ )
	{
		if ( r_dlightvisiblethisframe & ( 1 << lnum ))
			continue;

		R_MarkDLightNotVisible( lnum );
	}
}


//-----------------------------------------------------------------------------
// Can we use another dynamic light, or is it just too expensive?
//-----------------------------------------------------------------------------
bool R_CanUseVisibleDLight( int dlight )
{
	r_dlightvisiblethisframe |= (1 << dlight);

	if ( r_dlightvisible & ( 1 << dlight ) )
		return true;

	if ( s_nVisibleDLightCount >= s_nMaxVisibleDLightCount )
		return false;

	R_MarkDLightVisible( dlight );
	return true;
}


//-----------------------------------------------------------------------------
// Adds a single dynamic light
//-----------------------------------------------------------------------------
static bool AddSingleDynamicLight( dlight_t& dl, SurfaceHandle_t surfID, const Vector &lightOrigin, float perpDistSq, float lightRadiusSq )
{
	// transform the light into brush local space
	Vector local;
	// Spotlight early outs...
	if (dl.m_OuterAngle)
	{
		if (dl.m_OuterAngle < 180.0f)
		{
			// Can't light anything from the rear...
			if (DotProduct(dl.m_Direction, MSurf_Plane( surfID ).normal) >= 0.0f)
				return false;
		}
	}

	// Transform the light center point into (u,v) space of the lightmap
	mtexinfo_t* tex = MSurf_TexInfo( surfID );
	local[0] = DotProduct (lightOrigin, tex->lightmapVecsLuxelsPerWorldUnits[0].AsVector3D()) + 
			   tex->lightmapVecsLuxelsPerWorldUnits[0][3];
	local[1] = DotProduct (lightOrigin, tex->lightmapVecsLuxelsPerWorldUnits[1].AsVector3D()) + 
			   tex->lightmapVecsLuxelsPerWorldUnits[1][3];

	// Now put the center points into the space of the lightmap rectangle
	// defined by the lightmapMins + lightmapExtents
	local[0] -= MSurf_LightmapMins( surfID )[0];
	local[1] -= MSurf_LightmapMins( surfID )[1];
	
	// Figure out the quadratic attenuation factor...
	Vector intensity;
	float lightStyleValue = LightStyleValue( dl.style );
	intensity[0] = TexLightToLinear( dl.color.r, dl.color.exponent ) * lightStyleValue;
	intensity[1] = TexLightToLinear( dl.color.g, dl.color.exponent ) * lightStyleValue;
	intensity[2] = TexLightToLinear( dl.color.b, dl.color.exponent ) * lightStyleValue;

	float minlight = fpmax( g_flMinLightingValue, dl.minlight );
	float ooQuadraticAttn = lightRadiusSq * minlight;
	float ooRadiusSq = 1.0f / lightRadiusSq;

	// Compute a color at each luxel
	// We want to know the square distance from luxel center to light
	// so we can compute an 1/r^2 falloff in light color
	int smax = MSurf_LightmapExtents( surfID )[0] + 1;
	int tmax = MSurf_LightmapExtents( surfID )[1] + 1;
	for (int t=0; t<tmax; ++t)
	{
		float td = (local[1] - t) * tex->worldUnitsPerLuxel;
		
		for (int s=0; s<smax; ++s)
		{
			float sd = (local[0] - s) * tex->worldUnitsPerLuxel;

			float inPlaneDistSq = sd * sd + td * td;
			float totalDistSq = inPlaneDistSq + perpDistSq;
			if (totalDistSq < lightRadiusSq)
			{
				// at least all floating point only happens when a luxel is lit.
				float scale = (totalDistSq != 0.0f) ? ooQuadraticAttn / totalDistSq : 1.0f;

				// Apply a little extra attenuation
				scale *= (1.0f - totalDistSq * ooRadiusSq);

				if (scale > 2.0f)
					scale = 2.0f;

				int idx = t*smax + s;

				// Compute the base lighting just as is done in the non-bump case...
				blocklights[0][idx][0] += scale * intensity[0];
				blocklights[0][idx][1] += scale * intensity[1];
				blocklights[0][idx][2] += scale * intensity[2];
			}
		}
	}
	return true;
}												

//-----------------------------------------------------------------------------
// Adds a dynamic light to the bumped lighting
//-----------------------------------------------------------------------------
static void AddSingleDynamicLightToBumpLighting( dlight_t& dl, SurfaceHandle_t surfID, 
	const Vector &lightOrigin, float perpDistSq, float lightRadiusSq, Vector* pBumpBasis, const Vector& luxelBasePosition )
{
	Vector local;
	// FIXME: For now, only elights can be spotlights
	// the lightmap computation could get expensive for spotlights...
	Assert( dl.m_OuterAngle == 0.0f );

	// Transform the light center point into (u,v) space of the lightmap
	mtexinfo_t *pTexInfo = MSurf_TexInfo( surfID );
	local[0] = DotProduct (lightOrigin, pTexInfo->lightmapVecsLuxelsPerWorldUnits[0].AsVector3D()) + 
			   pTexInfo->lightmapVecsLuxelsPerWorldUnits[0][3];
	local[1] = DotProduct (lightOrigin, pTexInfo->lightmapVecsLuxelsPerWorldUnits[1].AsVector3D()) + 
			   pTexInfo->lightmapVecsLuxelsPerWorldUnits[1][3];

	// Now put the center points into the space of the lightmap rectangle
	// defined by the lightmapMins + lightmapExtents
	local[0] -= MSurf_LightmapMins( surfID )[0];
	local[1] -= MSurf_LightmapMins( surfID )[1];

	// Figure out the quadratic attenuation factor...
	Vector intensity;
	float lightStyleValue = LightStyleValue( dl.style );
	intensity[0] = TexLightToLinear( dl.color.r, dl.color.exponent ) * lightStyleValue;
	intensity[1] = TexLightToLinear( dl.color.g, dl.color.exponent ) * lightStyleValue;
	intensity[2] = TexLightToLinear( dl.color.b, dl.color.exponent ) * lightStyleValue;

	float minlight = fpmax( g_flMinLightingValue, dl.minlight );
	float ooQuadraticAttn = lightRadiusSq * minlight;
	float ooRadiusSq = 1.0f / lightRadiusSq;

	// The algorithm here is necessary to make dynamic lights live in the
	// same world as the non-bumped dynamic lights. Therefore, we compute
	// the intensity of the flat lightmap the exact same way here as when
	// we've got a non-bumped surface.

	// Then, I compute an actual light direction vector per luxel (FIXME: !!expensive!!)
	// and compute what light would have to come in along that direction
	// in order to produce the same illumination on the flat lightmap. That's
	// computed by dividing the flat lightmap color by n dot l.
	Vector lightDirection, texelWorldPosition;
	bool useLightDirection = (dl.m_OuterAngle != 0.0f) &&
		(fabs(dl.m_Direction.LengthSqr() - 1.0f) < 1e-3);
	if (useLightDirection)
		VectorMultiply( dl.m_Direction, -1.0f, lightDirection );

	// Since there's a scale factor used when going from world to luxel,
	// we gotta undo that scale factor when going from luxel to world
	float fixupFactor = pTexInfo->worldUnitsPerLuxel * pTexInfo->worldUnitsPerLuxel;

	// Compute a color at each luxel
	// We want to know the square distance from luxel center to light
	// so we can compute an 1/r^2 falloff in light color
	int smax = MSurf_LightmapExtents( surfID )[0] + 1;
	int tmax = MSurf_LightmapExtents( surfID )[1] + 1;
	for (int t=0; t<tmax; ++t)
	{
		float td = (local[1] - t) * pTexInfo->worldUnitsPerLuxel;
		
		// Move along the v direction
		VectorMA( luxelBasePosition, t * fixupFactor, pTexInfo->lightmapVecsLuxelsPerWorldUnits[1].AsVector3D(), 
			texelWorldPosition );

		for (int s=0; s<smax; ++s)
		{
			float sd = (local[0] - s) * pTexInfo->worldUnitsPerLuxel;

			float inPlaneDistSq = sd * sd + td * td;
			float totalDistSq = inPlaneDistSq + perpDistSq;

			if (totalDistSq < lightRadiusSq)
			{
				// at least all floating point only happens when a luxel is lit.
				float scale = (totalDistSq != 0.0f) ? ooQuadraticAttn / totalDistSq : 1.0f;

				// Apply a little extra attenuation
				scale *= (1.0f - totalDistSq * ooRadiusSq);

				if (scale > 2.0f)
					scale = 2.0f;

				int idx = t*smax + s;

				// Compute the base lighting just as is done in the non-bump case...
				VectorMA( blocklights[0][idx].AsVector3D(), scale, intensity, blocklights[0][idx].AsVector3D() );

				if (!useLightDirection)
				{
					VectorSubtract( lightOrigin, texelWorldPosition, lightDirection );
					VectorNormalize( lightDirection );
				}
				
				float lDotN = DotProduct( lightDirection, MSurf_Plane( surfID ).normal );
				if (lDotN < 1e-3)
					lDotN = 1e-3;
				scale *= lDotN;

				int i;
				for( i = 1; i < NUM_BUMP_VECTS + 1; i++ )
				{
					float dot = DotProduct( lightDirection, pBumpBasis[i-1] );
					if( dot <= 0.0f )
						continue;
					
					VectorMA( blocklights[i][idx].AsVector3D(), dot * scale, intensity, 
						blocklights[i][idx].AsVector3D() );
				}
			}
		}

		// Move along u
		VectorMA( texelWorldPosition, fixupFactor, 
			pTexInfo->lightmapVecsLuxelsPerWorldUnits[0].AsVector3D(), texelWorldPosition );

	}
}

//-----------------------------------------------------------------------------
// Compute the bumpmap basis for this surface
//-----------------------------------------------------------------------------
static void R_ComputeSurfaceBasis( SurfaceHandle_t surfID, Vector *pBumpNormals, Vector &luxelBasePosition )
{
	// NOTE!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	// This function gives incorrect results when the plane made by the lightmapVecs isn't parallel to the surface plane.
	// buildmodelforworld has similar code that is correct.  Probably doesn't matter too much at this point since
	// we don't use dlights much anymore.
	// NOTE!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

	// Get the bump basis vects in the space of the surface.
	Vector sVect, tVect;
	VectorCopy( MSurf_TexInfo( surfID )->lightmapVecsLuxelsPerWorldUnits[0].AsVector3D(), sVect );
	VectorNormalize( sVect );
	VectorCopy( MSurf_TexInfo( surfID )->lightmapVecsLuxelsPerWorldUnits[1].AsVector3D(), tVect );
	VectorNormalize( tVect );
	GetBumpNormals( sVect, tVect, MSurf_Plane( surfID ).normal, MSurf_Plane( surfID ).normal, pBumpNormals );

	// Compute the location of the first luxel in worldspace

	// Since there's a scale factor used when going from world to luxel,
	// we gotta undo that scale factor when going from luxel to world
	float fixupFactor = 
		MSurf_TexInfo( surfID )->worldUnitsPerLuxel * 
		MSurf_TexInfo( surfID )->worldUnitsPerLuxel;

	// The starting u of the surface is surf->lightmapMins[0];
	// since N * P + D = u, N * P = u - D, therefore we gotta move (u-D) along uvec
	VectorMultiply( MSurf_TexInfo( surfID )->lightmapVecsLuxelsPerWorldUnits[0].AsVector3D(),
		(MSurf_LightmapMins( surfID )[0] - MSurf_TexInfo( surfID )->lightmapVecsLuxelsPerWorldUnits[0][3]) * fixupFactor,
		luxelBasePosition );

	// Do the same thing for the v direction.
	VectorMA( luxelBasePosition, 
		(MSurf_LightmapMins( surfID )[1] - 
		MSurf_TexInfo( surfID )->lightmapVecsLuxelsPerWorldUnits[1][3]) * fixupFactor,
		MSurf_TexInfo( surfID )->lightmapVecsLuxelsPerWorldUnits[1].AsVector3D(),
		luxelBasePosition );

	// Move out in the direction of the plane normal...
	VectorMA( luxelBasePosition, MSurf_Plane( surfID ).dist, MSurf_Plane( surfID ).normal, luxelBasePosition ); 
}

//-----------------------------------------------------------------------------
// Purpose: Compute the mask of which dlights affect a surface
//			NOTE: Also has the side effect of updating the surface lighting dlight flags!
//-----------------------------------------------------------------------------
unsigned int R_ComputeDynamicLightMask( dlight_t *pLights, SurfaceHandle_t surfID, msurfacelighting_t *pLighting, const matrix3x4_t& entityToWorld )
{
	ASSERT_SURF_VALID( surfID );
	Vector bumpNormals[3];
	Vector luxelBasePosition;

	// Displacements do dynamic lights different
	if( SurfaceHasDispInfo( surfID ) )
	{
		return MSurf_DispInfo( surfID )->ComputeDynamicLightMask(pLights);
	}

	if ( !g_bActiveDlights )
		return 0;

	int lightMask = 0;
	for ( int lnum = 0, testBit = 1, mask = r_dlightactive; lnum < MAX_DLIGHTS; lnum++, mask >>= 1, testBit <<= 1 )
	{
		if ( mask & 1 )
		{
			// not lit by this light
			if ( !(pLighting->m_fDLightBits & testBit ) )
				continue;

			// This light doesn't affect the world
			if ( pLights[lnum].flags & (DLIGHT_NO_WORLD_ILLUMINATION|DLIGHT_DISPLACEMENT_MASK))
				continue;

			// This is used to ensure a maximum number of dlights in a frame
			if ( !R_CanUseVisibleDLight( lnum ) ) 
				continue;

			// Cull surface to light radius
			Vector lightOrigin;

			VectorITransform( pLights[lnum].origin, entityToWorld, lightOrigin );

			// NOTE: Dist can be negative because muzzle flashes can actually get behind walls
			// since the gun isn't checked for collision tests.
			float perpDistSq = DotProduct (lightOrigin, MSurf_Plane( surfID ).normal) - MSurf_Plane( surfID ).dist;
			if (perpDistSq < DLIGHT_BEHIND_PLANE_DIST)
			{
				// update the surfacelighting and remove this light's bit
				pLighting->m_fDLightBits &= ~testBit;
				continue;
			}

			perpDistSq *= perpDistSq;

			// If the perp distance > radius of light, blow it off
			float lightRadiusSq = pLights[lnum].GetRadiusSquared();
			if (lightRadiusSq <= perpDistSq)
			{
				// update the surfacelighting and remove this light's bit
				pLighting->m_fDLightBits &= ~testBit;
				continue;
			}

			lightMask |= testBit;
		}
	}

	return lightMask;
}


//-----------------------------------------------------------------------------
// Purpose: Modifies blocklights[][][] to include the state of the dlights 
//			affecting this surface.
//			NOTE: Can be threaded, should not reference or modify any global state 
//			other than blocklights.
//-----------------------------------------------------------------------------
void R_AddDynamicLights( dlight_t *pLights, SurfaceHandle_t surfID, const matrix3x4_t& entityToWorld, bool needsBumpmap, unsigned int lightMask )
{
	ASSERT_SURF_VALID( surfID );
	VPROF( "R_AddDynamicLights" );

	// Early-out if dlights are disabled:
	if ( !r_dlightsenable.GetBool() )
		return;

	Vector bumpNormals[3];
	bool computedBumpBasis = false;
	Vector luxelBasePosition;

	// Displacements do dynamic lights different
	if( SurfaceHasDispInfo( surfID ) )
	{
		MSurf_DispInfo( surfID )->AddDynamicLights(pLights, lightMask);
		return;
	}

	// iterate all of the active dynamic lights.  Uses several iterators to keep 
	// the light mask (bit), light index, and active mask current
	for ( int lnum = 0, testBit = 1, mask = lightMask; lnum < MAX_DLIGHTS && mask != 0; lnum++, mask >>= 1, testBit <<= 1 )
	{
		// shift over the mask of active lights each iteration, if this one is active, apply it
		if ( mask & 1 )
		{
			// Cull surface to light radius
			Vector lightOrigin;

			VectorITransform( pLights[lnum].origin, entityToWorld, lightOrigin );

			// NOTE: Dist can be negative because muzzle flashes can actually get behind walls
			// since the gun isn't checked for collision tests.
			float perpDistSq = DotProduct (lightOrigin, MSurf_Plane( surfID ).normal) - MSurf_Plane( surfID ).dist;
			if (perpDistSq < DLIGHT_BEHIND_PLANE_DIST)
				continue;

			perpDistSq *= perpDistSq;

			// If the perp distance > radius of light, blow it off
			float lightRadiusSq = pLights[lnum].GetRadiusSquared();
			if (lightRadiusSq <= perpDistSq)
				continue;

			if (!needsBumpmap)
			{
				AddSingleDynamicLight( pLights[lnum], surfID, lightOrigin, perpDistSq, lightRadiusSq );
				continue;
			}

			// Here, I'm precomputing things needed by bumped lighting that
			// are the same for a surface...
			if (!computedBumpBasis)
			{
				R_ComputeSurfaceBasis( surfID, bumpNormals, luxelBasePosition );
				computedBumpBasis = true;
			}

			AddSingleDynamicLightToBumpLighting( pLights[lnum], surfID, lightOrigin, perpDistSq, lightRadiusSq, bumpNormals, luxelBasePosition );
		}
	}
}


// Fixed point (8.8) color/intensity ratios
#define I_RED		((int)(0.299*255))
#define I_GREEN		((int)(0.587*255))
#define I_BLUE		((int)(0.114*255))



ConVar mat_defaultlightmap( "mat_defaultlightmap", "1", FCVAR_NONE, "Default brightness for lightmaps where none have been created in the level." );


//-----------------------------------------------------------------------------
// Sets all elements in a lightmap to a particular opaque greyscale value
//-----------------------------------------------------------------------------
static void InitLMSamples( Vector4D *pSamples, int nSamples, float value )
{
	for( int i=0; i < nSamples; i++ )
	{
		pSamples[i][0] = pSamples[i][1] = pSamples[i][2] = value;
		pSamples[i][3] = 0.0f; // Init the alpha to 0.0
	}
}


//-----------------------------------------------------------------------------
// Computes the lightmap size
//-----------------------------------------------------------------------------
static int ComputeLightmapSize( SurfaceHandle_t surfID )
{
	int smax = ( MSurf_LightmapExtents( surfID )[0] ) + 1;
	int tmax = ( MSurf_LightmapExtents( surfID )[1] ) + 1;
	int size = smax * tmax;

	int nMaxSize = MSurf_MaxLightmapSizeWithBorder( surfID );
	if (size > nMaxSize * nMaxSize)
	{
		ConMsg("Bad lightmap extents on material \"%s\"\n", 
			materialSortInfoArray[MSurf_MaterialSortID( surfID )].material->GetName());
		return 0;
	}
	
	return size;
}


//#ifndef PLATFORM_PPC
//-----------------------------------------------------------------------------
// Compute the portion of the lightmap generated from lightstyles
//-----------------------------------------------------------------------------
static void AccumulateLightstyles( ColorRGBExp32* pLightmap, unsigned char *pLightmapExtraData, int lightmapSize, float scalar ) 
{
	Assert( pLightmap );
	for (int i=0; i<lightmapSize ; ++i)
	{
		float flR = scalar * TexLightToLinear( pLightmap[i].r, pLightmap[i].exponent );
		float flG = scalar * TexLightToLinear( pLightmap[i].g, pLightmap[i].exponent );
		float flB = scalar * TexLightToLinear( pLightmap[i].b, pLightmap[i].exponent );

		blocklights[0][i][0] += flR;
		blocklights[0][i][1] += flG;
		blocklights[0][i][2] += flB;

		// this won't work on platforms that have fp lightmaps
		// lightmapAlphaData3 implies new data in alpha for fixed CSM blending, old path for compatibility
		if ( g_bHasLightmapAlphaData3 )
		{
			Assert( pLightmapExtraData );
			blocklights[ 0 ][ i ][ 3 ] += ( (float)( pLightmapExtraData[ i * 4 ] ) ) * ( 1.0f / 255.0f );
		}
		else
		{
			blocklights[0][i][3] += pLightmapExtraData ? ( ( float )pLightmapExtraData[i] ) * ( 1.0f / 255.0f ) * ( flR * 0.2125 + flG * 0.7154 + flB * 0.0721 ) / 16.0f : 0.0f;
		}
	}
}

static void AccumulateLightstylesNoAlpha( ColorRGBExp32* pLightmap, unsigned char *pLightmapExtraData, int lightmapSize, float scalar )
{
	Assert( pLightmap );
	for ( int i = 0; i < lightmapSize; ++i )
	{
		float flR = scalar * TexLightToLinear( pLightmap[ i ].r, pLightmap[ i ].exponent );
		float flG = scalar * TexLightToLinear( pLightmap[ i ].g, pLightmap[ i ].exponent );
		float flB = scalar * TexLightToLinear( pLightmap[ i ].b, pLightmap[ i ].exponent );

		blocklights[ 0 ][ i ][ 0 ] += flR;
		blocklights[ 0 ][ i ][ 1 ] += flG;
		blocklights[ 0 ][ i ][ 2 ] += flB;
	}
}

static void AccumulateLightstylesFlat( ColorRGBExp32* pLightmap, unsigned char *pLightmapExtraData, int lightmapSize, float scalar ) 
{
	Assert( pLightmap );
	for (int i=0; i<lightmapSize ; ++i)
	{
 		float flR =	scalar * TexLightToLinear( pLightmap->r, pLightmap->exponent );
 		float flG =	scalar * TexLightToLinear( pLightmap->g, pLightmap->exponent );
 		float flB =	scalar * TexLightToLinear( pLightmap->b, pLightmap->exponent );

		blocklights[0][i][0] += flR;
		blocklights[0][i][1] += flG;
		blocklights[0][i][2] += flB;

		// this won't work on platforms that have fp lightmaps
		if ( g_bHasLightmapAlphaData3 )
		{
			Assert( pLightmapExtraData );
			blocklights[0][i][3] += ((float)(pLightmapExtraData[i*4])) * (1.0f / 255.0f);
		}
		else
		{
			blocklights[0][i][3] += pLightmapExtraData ? ( ( float )pLightmapExtraData[i] ) * ( 1.0f / 255.0f ) * ( flR * 0.2125 + flG * 0.7154 + flB * 0.0721 ) / 16.0f : 0.0f;
		}
	}
}

static void AccumulateLightstylesFlatNoAlpha( ColorRGBExp32* pLightmap, unsigned char *pLightmapExtraData, int lightmapSize, float scalar )
{
	Assert( pLightmap );
	for ( int i = 0; i < lightmapSize; ++i )
	{
		float flR = scalar * TexLightToLinear( pLightmap->r, pLightmap->exponent );
		float flG = scalar * TexLightToLinear( pLightmap->g, pLightmap->exponent );
		float flB = scalar * TexLightToLinear( pLightmap->b, pLightmap->exponent );

		blocklights[ 0 ][ i ][ 0 ] += flR;
		blocklights[ 0 ][ i ][ 1 ] += flG;
		blocklights[ 0 ][ i ][ 2 ] += flB;
	}
}


static void AccumulateBumpedLightstyles( ColorRGBExp32* pLightmap, unsigned char *pLightmapExtraData, int lightmapSize, float scalar ) 
{
	ColorRGBExp32 *pBumpedLightmaps[3];
	pBumpedLightmaps[0] = pLightmap + lightmapSize;
	pBumpedLightmaps[1] = pLightmap + 2 * lightmapSize;
	pBumpedLightmaps[2] = pLightmap + 3 * lightmapSize;

	float flR;
	float flG;
	float flB;

	if ( g_bHasLightmapAlphaData3 )
	{
		Assert( pLightmapExtraData );
	}

	// I chose to split up the loops this way because it was the best tradeoff
	// based on profiles between cache miss + loop overhead
	for (int i=0, j=0; i<lightmapSize ; ++i, j+=4 )
	{
		flR = scalar * TexLightToLinear( pLightmap[i].r, pLightmap[i].exponent );
		flG = scalar * TexLightToLinear( pLightmap[i].g, pLightmap[i].exponent );
		flB = scalar * TexLightToLinear( pLightmap[i].b, pLightmap[i].exponent );
		blocklights[0][i][0] += flR;
		blocklights[0][i][1] += flG;
		blocklights[0][i][2] += flB;
		// this won't work on platforms that have fp lightmaps
		if ( g_bHasLightmapAlphaData3 )
		{
			blocklights[0][i][3] += ((float)(pLightmapExtraData[j])) * (1.0f / 255.0f);
		}
		else
		{
			blocklights[0][i][3] += pLightmapExtraData ? ( ( float )pLightmapExtraData[i] ) * ( 1.0f / 255.0f ) * ( flR * 0.2125 + flG * 0.7154 + flB * 0.0721 ) / 16.0f : 0.0f;
		}
		Assert( blocklights[0][i][0] >= 0.0f );
		Assert( blocklights[0][i][1] >= 0.0f );
		Assert( blocklights[0][i][2] >= 0.0f );

		flR = scalar * TexLightToLinear( pBumpedLightmaps[0][i].r, pBumpedLightmaps[0][i].exponent );
		flG = scalar * TexLightToLinear( pBumpedLightmaps[0][i].g, pBumpedLightmaps[0][i].exponent );
		flB = scalar * TexLightToLinear( pBumpedLightmaps[0][i].b, pBumpedLightmaps[0][i].exponent );
		blocklights[1][i][0] += flR;
		blocklights[1][i][1] += flG;
		blocklights[1][i][2] += flB;
		// this won't work on platforms that have fp lightmaps
		if ( g_bHasLightmapAlphaData3 )
		{
			blocklights[1][i][3] += ((float)pLightmapExtraData[j + 1]) * (1.0f / 255.0f);
		}
		else
		{
			blocklights[1][i][3] += pLightmapExtraData ? ( ( float )pLightmapExtraData[i] ) * ( 1.0f / 255.0f ) * ( flR * 0.2125 + flG * 0.7154 + flB * 0.0721 ) / 16.0f : 0.0f;
		}

		Assert( blocklights[1][i][0] >= 0.0f );
		Assert( blocklights[1][i][1] >= 0.0f );
		Assert( blocklights[1][i][2] >= 0.0f );
	}

	for ( int i=0, j=0 ; i<lightmapSize ; ++i, j+=4 )
	{
		flR = scalar * TexLightToLinear( pBumpedLightmaps[1][i].r, pBumpedLightmaps[1][i].exponent );
		flG = scalar * TexLightToLinear( pBumpedLightmaps[1][i].g, pBumpedLightmaps[1][i].exponent );
		flB = scalar * TexLightToLinear( pBumpedLightmaps[1][i].b, pBumpedLightmaps[1][i].exponent );
		blocklights[2][i][0] += flR;
		blocklights[2][i][1] += flG;
		blocklights[2][i][2] += flB;
		// this won't work on platforms that have fp lightmaps
		if ( g_bHasLightmapAlphaData3 )
		{
			blocklights[2][i][3] += ((float)pLightmapExtraData[j + 2]) * (1.0f / 255.0f);
		}
		else
		{
			blocklights[2][i][3] += pLightmapExtraData ? ( ( float )pLightmapExtraData[i] ) * ( 1.0f / 255.0f ) * ( flR * 0.2125 + flG * 0.7154 + flB * 0.0721 ) / 16.0f : 0.0f;
		}
		Assert( blocklights[2][i][0] >= 0.0f );
		Assert( blocklights[2][i][1] >= 0.0f );
		Assert( blocklights[2][i][2] >= 0.0f );

		flR = scalar * TexLightToLinear( pBumpedLightmaps[2][i].r, pBumpedLightmaps[2][i].exponent );
		flG = scalar * TexLightToLinear( pBumpedLightmaps[2][i].g, pBumpedLightmaps[2][i].exponent );
		flB = scalar * TexLightToLinear( pBumpedLightmaps[2][i].b, pBumpedLightmaps[2][i].exponent );
		blocklights[3][i][0] += flR;
		blocklights[3][i][1] += flG;
		blocklights[3][i][2] += flB;
		// this won't work on platforms that have fp lightmaps
		if ( g_bHasLightmapAlphaData3 )
		{
			blocklights[3][i][3] += ((float)pLightmapExtraData[j + 3]) * (1.0f / 255.0f);
		}
		else
		{
			blocklights[3][i][3] += pLightmapExtraData ? ( ( float )pLightmapExtraData[i] ) * ( 1.0f / 255.0f ) * ( flR * 0.2125 + flG * 0.7154 + flB * 0.0721 ) / 16.0f : 0.0f;
		}
		Assert( blocklights[3][i][0] >= 0.0f );
		Assert( blocklights[3][i][1] >= 0.0f );
		Assert( blocklights[3][i][2] >= 0.0f );
	}
}

static void AccumulateBumpedLightstylesNoAlpha( ColorRGBExp32* pLightmap, unsigned char *pLightmapExtraData, int lightmapSize, float scalar )
{
	ColorRGBExp32 *pBumpedLightmaps[ 3 ];
	pBumpedLightmaps[ 0 ] = pLightmap + lightmapSize;
	pBumpedLightmaps[ 1 ] = pLightmap + 2 * lightmapSize;
	pBumpedLightmaps[ 2 ] = pLightmap + 3 * lightmapSize;

	float flR;
	float flG;
	float flB;

	if ( g_bHasLightmapAlphaData3 )
	{
		Assert( pLightmapExtraData );
	}

	// I chose to split up the loops this way because it was the best tradeoff
	// based on profiles between cache miss + loop overhead
	for ( int i = 0, j = 0; i < lightmapSize; ++i, j += 4 )
	{
		flR = scalar * TexLightToLinear( pLightmap[ i ].r, pLightmap[ i ].exponent );
		flG = scalar * TexLightToLinear( pLightmap[ i ].g, pLightmap[ i ].exponent );
		flB = scalar * TexLightToLinear( pLightmap[ i ].b, pLightmap[ i ].exponent );
		blocklights[ 0 ][ i ][ 0 ] += flR;
		blocklights[ 0 ][ i ][ 1 ] += flG;
		blocklights[ 0 ][ i ][ 2 ] += flB;
		Assert( blocklights[ 0 ][ i ][ 0 ] >= 0.0f );
		Assert( blocklights[ 0 ][ i ][ 1 ] >= 0.0f );
		Assert( blocklights[ 0 ][ i ][ 2 ] >= 0.0f );

		flR = scalar * TexLightToLinear( pBumpedLightmaps[ 0 ][ i ].r, pBumpedLightmaps[ 0 ][ i ].exponent );
		flG = scalar * TexLightToLinear( pBumpedLightmaps[ 0 ][ i ].g, pBumpedLightmaps[ 0 ][ i ].exponent );
		flB = scalar * TexLightToLinear( pBumpedLightmaps[ 0 ][ i ].b, pBumpedLightmaps[ 0 ][ i ].exponent );
		blocklights[ 1 ][ i ][ 0 ] += flR;
		blocklights[ 1 ][ i ][ 1 ] += flG;
		blocklights[ 1 ][ i ][ 2 ] += flB;

		Assert( blocklights[ 1 ][ i ][ 0 ] >= 0.0f );
		Assert( blocklights[ 1 ][ i ][ 1 ] >= 0.0f );
		Assert( blocklights[ 1 ][ i ][ 2 ] >= 0.0f );
	}

	for ( int i = 0, j = 0; i < lightmapSize; ++i, j += 4 )
	{
		flR = scalar * TexLightToLinear( pBumpedLightmaps[ 1 ][ i ].r, pBumpedLightmaps[ 1 ][ i ].exponent );
		flG = scalar * TexLightToLinear( pBumpedLightmaps[ 1 ][ i ].g, pBumpedLightmaps[ 1 ][ i ].exponent );
		flB = scalar * TexLightToLinear( pBumpedLightmaps[ 1 ][ i ].b, pBumpedLightmaps[ 1 ][ i ].exponent );
		blocklights[ 2 ][ i ][ 0 ] += flR;
		blocklights[ 2 ][ i ][ 1 ] += flG;
		blocklights[ 2 ][ i ][ 2 ] += flB;
		Assert( blocklights[ 2 ][ i ][ 0 ] >= 0.0f );
		Assert( blocklights[ 2 ][ i ][ 1 ] >= 0.0f );
		Assert( blocklights[ 2 ][ i ][ 2 ] >= 0.0f );

		flR = scalar * TexLightToLinear( pBumpedLightmaps[ 2 ][ i ].r, pBumpedLightmaps[ 2 ][ i ].exponent );
		flG = scalar * TexLightToLinear( pBumpedLightmaps[ 2 ][ i ].g, pBumpedLightmaps[ 2 ][ i ].exponent );
		flB = scalar * TexLightToLinear( pBumpedLightmaps[ 2 ][ i ].b, pBumpedLightmaps[ 2 ][ i ].exponent );
		blocklights[ 3 ][ i ][ 0 ] += flR;
		blocklights[ 3 ][ i ][ 1 ] += flG;
		blocklights[ 3 ][ i ][ 2 ] += flB;
		Assert( blocklights[ 3 ][ i ][ 0 ] >= 0.0f );
		Assert( blocklights[ 3 ][ i ][ 1 ] >= 0.0f );
		Assert( blocklights[ 3 ][ i ][ 2 ] >= 0.0f );
	}
}




//-----------------------------------------------------------------------------
// Compute the portion of the lightmap generated from lightstyles
//-----------------------------------------------------------------------------
static void ComputeLightmapFromLightstyle( msurfacelighting_t *pLighting, bool computeLightmap, 
				bool computeBumpmap, int lightmapSize, bool hasBumpmapLightmapData )
{
	VPROF( "ComputeLightmapFromLightstyle" );

	ColorRGBExp32 *pLightmap = pLighting->m_pSamples;

	// This data should only exist on the PC. We strip out the data and clear the flag in makegamedata for consoles.
	unsigned char *pLightmapExtraData = NULL;
	if ( g_bHasLightmapAlphaData )
	{
		pLightmapExtraData = ( unsigned char * )&( pLighting->m_pSamples[ hasBumpmapLightmapData ? lightmapSize * ( NUM_BUMP_VECTS + 1 ) : lightmapSize ] );
	}

	// Compute iteration range
	int minmap, maxmap;
#ifdef USE_CONVARS
	if( r_lightmap.GetInt() != -1 )
	{
		minmap = r_lightmap.GetInt();
		maxmap = minmap + 1;
	}
	else
#endif
	{
		minmap = 0; maxmap = MAXLIGHTMAPS;
	}

	for (int maps = minmap; maps < maxmap && pLighting->m_nStyles[maps] != 255; ++maps)
	{
		if( r_lightstyle.GetInt() != -1 && pLighting->m_nStyles[maps] != r_lightstyle.GetInt())
		{
			continue;
		}

		float fscalar = LightStyleValue( pLighting->m_nStyles[maps] );

		// hack - don't know why we are getting negative values here.
//		if (scalar > 0.0f && maps > 0 )
		if (fscalar > 0.0f)
		{
//#ifdef PLATFORM_PPC
			const float &scalar = fscalar;
			if( computeBumpmap )
			{
				// don't accumulate alpha for other lightstyles
				if ( maps == 0 )
				{
 					AccumulateBumpedLightstyles( pLightmap, pLightmapExtraData, lightmapSize, scalar );
				}
 				else
 				{
					AccumulateBumpedLightstylesNoAlpha( pLightmap, pLightmapExtraData, lightmapSize, scalar );
				}
			}
			else if( computeLightmap )
			{
				if (r_avglightmap.GetInt())
				{
					pLightmap = pLighting->AvgLightColor(maps);
					// don't accumulate alpha for other lightstyles
					if ( maps == 0 )
					{
 						AccumulateLightstylesFlat( pLightmap, pLightmapExtraData, lightmapSize, scalar );
					}
					else
					{
						AccumulateLightstylesFlatNoAlpha( pLightmap, pLightmapExtraData, lightmapSize, scalar );
					}
				}
				else
				{
					// don't accumulate alpha for other lightstyles
					if ( maps == 0 )
					{
 						AccumulateLightstyles( pLightmap, pLightmapExtraData, lightmapSize, scalar );
					}
					else
					{
						AccumulateLightstylesNoAlpha( pLightmap, pLightmapExtraData, lightmapSize, scalar );
					}
				}
			}
		}

		// skip to next lightmap. If we store bump lightmap data, we need to jump forward 5 (1 x regular lmap, 3 x bump lmaps, 1 x extra alpha csm data)
		// otherwise 2 (1 x regular lmap, 1 x extra alpha csm data)
		pLightmap += hasBumpmapLightmapData ? lightmapSize * ( NUM_BUMP_VECTS + 2 ) : ( lightmapSize * 2 );
	}
}

//-----------------------------------------------------------------------------
// Version of above to support old lightmap lump layout (before lightstyles were fixed)
// Added to avoid modders re-baking maps that used 'broken' lightstyle data in a manner that worked for them (i.e. without CSMs)
//-----------------------------------------------------------------------------
static void ComputeLightmapFromLightstyleOLD( msurfacelighting_t *pLighting, bool computeLightmap,
										   bool computeBumpmap, int lightmapSize, bool hasBumpmapLightmapData )
{
	VPROF( "ComputeLightmapFromLightstyleOLD" );

	ColorRGBExp32 *pLightmap = pLighting->m_pSamples;

	// This data should only exist on the PC. We strip out the data and clear the flag in makegamedata for consoles.
	unsigned char *pLightmapExtraData = NULL;
	if ( g_bHasLightmapAlphaData )
	{
		pLightmapExtraData = ( unsigned char * )&( pLighting->m_pSamples[ hasBumpmapLightmapData ? lightmapSize * ( NUM_BUMP_VECTS + 1 ) : lightmapSize ] );
	}

	// Compute iteration range
	int minmap, maxmap;
#ifdef USE_CONVARS
	if ( r_lightmap.GetInt() != -1 )
	{
		minmap = r_lightmap.GetInt();
		maxmap = minmap + 1;
	}
	else
#endif
	{
		minmap = 0; maxmap = MAXLIGHTMAPS;
	}

	for ( int maps = minmap; maps < maxmap && pLighting->m_nStyles[ maps ] != 255; ++maps )
	{
		if ( r_lightstyle.GetInt() != -1 && pLighting->m_nStyles[ maps ] != r_lightstyle.GetInt() )
		{
			continue;
		}

		float fscalar = LightStyleValue( pLighting->m_nStyles[ maps ] );

		// hack - don't know why we are getting negative values here.
		//		if (scalar > 0.0f && maps > 0 )
		if ( fscalar > 0.0f )
		{
			//#ifdef PLATFORM_PPC
			const float &scalar = fscalar;

			if ( computeBumpmap )
			{
				AccumulateBumpedLightstyles( pLightmap, pLightmapExtraData, lightmapSize, scalar );
			}
			else if ( computeLightmap )
			{
				if ( r_avglightmap.GetInt() )
				{
					pLightmap = pLighting->AvgLightColor( maps );
					AccumulateLightstylesFlat( pLightmap, pLightmapExtraData, lightmapSize, scalar );
				}
				else
				{
					AccumulateLightstyles( pLightmap, pLightmapExtraData, lightmapSize, scalar );
				}
			}
		}

		// skip to next lightmap. If we store lightmap data, we need to jump forward 4
		pLightmap += hasBumpmapLightmapData ? lightmapSize * ( NUM_BUMP_VECTS + 1 ) : lightmapSize;
	}
}

// instrumentation to measure locks
/*
static CUtlVector<int> g_LightmapLocks;
static int g_Lastdlightframe = -1;
static int g_lastlock = -1;
static int g_unsorted = 0;
void MarkPage( int pageID )
{
	if ( g_Lastdlightframe != r_framecount )
	{
		int total = 0;
		int locks = 0;
		for ( int i = 0; i < g_LightmapLocks.Count(); i++ )
		{
			int count = g_LightmapLocks[i];
			if ( count )
			{
				total++;
				locks += count;
			}
			g_LightmapLocks[i] = 0;
		}
		g_Lastdlightframe = r_framecount;
		g_lastlock = -1;
		if ( locks )
		Msg("Total pages %d, locks %d, unsorted locks %d\n", total, locks, g_unsorted );
		g_unsorted = 0;
	}
	if ( pageID != g_lastlock )
	{
		g_lastlock = pageID;
		g_unsorted++;
	}
	g_LightmapLocks.EnsureCount(pageID+1);
	g_LightmapLocks[pageID]++;
}
*/
//-----------------------------------------------------------------------------
// Update the lightmaps...
//-----------------------------------------------------------------------------
static void UpdateLightmapTextures( SurfaceHandle_t surfID, bool needsBumpmap )
{
	ASSERT_SURF_VALID( surfID );

	if( materialSortInfoArray )
	{
		int lightmapSize[2];
		int offsetIntoLightmapPage[2];
		lightmapSize[0] = ( MSurf_LightmapExtents( surfID )[0] ) + 1;
		lightmapSize[1] = ( MSurf_LightmapExtents( surfID )[1] ) + 1;
		offsetIntoLightmapPage[0] = MSurf_OffsetIntoLightmapPage( surfID )[0];
		offsetIntoLightmapPage[1] = MSurf_OffsetIntoLightmapPage( surfID )[1];
		Assert( MSurf_MaterialSortID( surfID ) >= 0 && 
			MSurf_MaterialSortID( surfID ) < g_WorldStaticMeshes.Count() );
		// FIXME: Should differentiate between bumped and unbumped since the perf characteristics
		// are completely different?
//		MarkPage( materialSortInfoArray[MSurf_MaterialSortID( surfID )].lightmapPageID );

		if( needsBumpmap )
		{
			materials->UpdateLightmap( materialSortInfoArray[MSurf_MaterialSortID( surfID )].lightmapPageID,
				lightmapSize, offsetIntoLightmapPage, 
				&blocklights[0][0][0], &blocklights[1][0][0], &blocklights[2][0][0], &blocklights[3][0][0] );
		}
		else
		{
			materials->UpdateLightmap( materialSortInfoArray[MSurf_MaterialSortID( surfID )].lightmapPageID,
				lightmapSize, offsetIntoLightmapPage, 
				&blocklights[0][0][0], NULL, NULL, NULL );
		}
	}
}


unsigned int R_UpdateDlightState( dlight_t *pLights, SurfaceHandle_t surfID, const matrix3x4_t& entityToWorld, bool bOnlyUseLightStyles, bool bLightmap )
{
	unsigned int dlightMask = 0;
	// Mark the surface with the particular cached light values...
	msurfacelighting_t *pLighting = SurfaceLighting( surfID );

	// Retire dlights that are no longer active
	pLighting->m_fDLightBits &= r_dlightactive;
	pLighting->m_nLastComputedFrame = r_framecount;

	// Here, it's got the data it needs. So use it!
	if ( !bOnlyUseLightStyles )
	{
		// add all the dynamic lights
        // lwss add: hack this so we can restore 32-byte alignment
        //if( bLightmap && ( pLighting->m_nDLightFrame == r_framecount ) )
        if( bLightmap && ( pLighting->m_wantsDlightThisFrame) )
		{
			dlightMask = R_ComputeDynamicLightMask( pLights, surfID, pLighting, entityToWorld );
		}

		// if there was no dlight after adding, remove/disable
		if ( !dlightMask || !pLighting->m_fDLightBits )
		{
			pLighting->m_fDLightBits = 0;
			MSurf_Flags(surfID) &= ~SURFDRAW_HASDLIGHT;
		}
        // lwss add: turn this off after computing the lightmask
		pLighting->m_wantsDlightThisFrame = false;
	}
	return dlightMask;
}

//-----------------------------------------------------------------------------
// Purpose: Build the blocklights array for a given surface and copy to dest
//			Combine and scale multiple lightmaps into the 8.8 format in blocklights
// Input  : *psurf - surface to rebuild
//			*dest - texture pointer to receive copy in lightmap texture format
//			stride - stride of *dest memory
//-----------------------------------------------------------------------------
void R_BuildLightMapGuts( dlight_t *pLights, SurfaceHandle_t surfID, const matrix3x4_t& entityToWorld, unsigned int dlightMask, bool needsBumpmap, bool needsLightmap )
{
	VPROF_("R_BuildLightMapGuts", 1, VPROF_BUDGETGROUP_DLIGHT_RENDERING, false, 0);
	int bumpID;

	// Lightmap data can be dumped to save memory - this precludes any dynamic lighting on the world
	Assert( !host_state.worldbrush->m_bUnloadedAllLightmaps );

	// Mark the surface with the particular cached light values...
	msurfacelighting_t *pLighting = SurfaceLighting( surfID );

	int size = ComputeLightmapSize( surfID );
	if (size == 0)
		return;

	bool hasBumpmap = SurfHasBumpedLightmaps( surfID );
	bool hasLightmap = SurfHasLightmap( surfID );
	extern bool g_bLightstylesWithCSM;

	// clear to no light
	if( needsLightmap )
	{
		// set to full bright if no light data
		InitLMSamples( blocklights[0], size, hasLightmap ? 0.0f : mat_defaultlightmap.GetFloat() );
	}

	if( needsBumpmap )
	{
		// set to full bright if no light data
		for( bumpID = 1; bumpID < NUM_BUMP_VECTS + 1; bumpID++ )
		{
			InitLMSamples( blocklights[bumpID], size, hasBumpmap ? 0.0f : mat_defaultlightmap.GetFloat() );
		}
	}

	// add all the lightmaps
	// Here, it's got the data it needs. So use it!
	if( ( hasLightmap && needsLightmap ) || ( hasBumpmap && needsBumpmap ) )
	{
 		if ( g_bLightstylesWithCSM )
 		{
			ComputeLightmapFromLightstyle( pLighting, ( hasLightmap && needsLightmap ),
				( hasBumpmap && needsBumpmap ), size, hasBumpmap );
 		}
 		else
 		{
 			ComputeLightmapFromLightstyleOLD( pLighting, ( hasLightmap && needsLightmap ),
 				( hasBumpmap && needsBumpmap ), size, hasBumpmap );
 		}
	}
	else if( !hasBumpmap && needsBumpmap && hasLightmap )
	{
		// make something up for the bumped lights if you need them but don't have the data
		// if you have a lightmap, use that, otherwise fullbright
		if ( g_bLightstylesWithCSM )
		{
			ComputeLightmapFromLightstyle( pLighting, true, false, size, hasBumpmap );
		}
		else
		{
			ComputeLightmapFromLightstyleOLD( pLighting, true, false, size, hasBumpmap );
		}

		for( bumpID = 0; bumpID < ( hasBumpmap ? ( NUM_BUMP_VECTS + 1 ) : 1 ); bumpID++ )
		{
			for (int i=0 ; i<size ; i++)
			{
				blocklights[bumpID][i].AsVector3D() = blocklights[0][i].AsVector3D();
			}
		}
	}
	else if( needsBumpmap && !hasLightmap )
	{
		// set to full bright if no light data
		InitLMSamples( blocklights[1], size, 0.0f );
		InitLMSamples( blocklights[2], size, 0.0f );
		InitLMSamples( blocklights[3], size, 0.0f );
	}
	else if( !needsBumpmap && !needsLightmap )
	{
	}
	else if( needsLightmap && !hasLightmap )
	{
	}
	else
	{
		Assert( 0 );
	}

	// add all the dynamic lights
	if ( dlightMask && (needsLightmap || needsBumpmap) )
	{
		R_AddDynamicLights( pLights, surfID, entityToWorld, needsBumpmap, dlightMask );
	}

	// Update the texture state
	UpdateLightmapTextures( surfID, needsBumpmap );
}

void R_BuildLightMap( dlight_t *pLights, ICallQueue *pCallQueue, SurfaceHandle_t surfID, const matrix3x4_t &entityToWorld, bool bOnlyUseLightStyles )
{
	bool needsBumpmap = SurfNeedsBumpedLightmaps( surfID );
	bool needsLightmap = SurfNeedsLightmap( surfID );

	if( !needsBumpmap && !needsLightmap )
		return;
	
	if( materialSortInfoArray )
	{
		Assert( MSurf_MaterialSortID( surfID ) >= 0 && 
			    MSurf_MaterialSortID( surfID ) < g_WorldStaticMeshes.Count() );
		if (( materialSortInfoArray[MSurf_MaterialSortID( surfID )].lightmapPageID == MATERIAL_SYSTEM_LIGHTMAP_PAGE_WHITE )	||
		   ( materialSortInfoArray[MSurf_MaterialSortID( surfID )].lightmapPageID == MATERIAL_SYSTEM_LIGHTMAP_PAGE_WHITE_BUMP ) )
		{
			return;
		}
	}

	bool bDlightsInLightmap = needsLightmap || needsBumpmap;
	unsigned int dlightMask = R_UpdateDlightState( pLights, surfID, entityToWorld, bOnlyUseLightStyles, bDlightsInLightmap );

	// update the state, but don't render any dlights if only lightstyles requested
	if ( bOnlyUseLightStyles )
		dlightMask = 0;

	if ( !pCallQueue )
	{
		R_BuildLightMapGuts( pLights, surfID, entityToWorld, dlightMask, needsBumpmap, needsLightmap );
	}
	else
	{
		pCallQueue->QueueCall( R_BuildLightMapGuts, pLights, surfID, RefToVal( entityToWorld ), dlightMask, needsBumpmap, needsLightmap );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Save off the average light values, and dump the rest of the lightmap data.
// Can be used to save memory, at the expense of dynamic lights and lightstyles.
//-----------------------------------------------------------------------------
void CacheAndUnloadLightmapData()
{
	Assert( !g_bHunkAllocLightmaps );
	if ( g_bHunkAllocLightmaps )
	{
		// for safety, can't discard if lighting data is hunk allocated
		return;
	}

	worldbrushdata_t *pBrushData = host_state.worldbrush;
	msurfacelighting_t *pLighting = pBrushData->surfacelighting;
	int numSurfaces = pBrushData->numsurfaces;

	// This will allocate more data than necessary, but only 1-2K max
	byte *pDestBase = (byte*)malloc( numSurfaces * MAXLIGHTMAPS * sizeof( ColorRGBExp32 ) );
	byte *pDest = pDestBase;

	for ( int i = 0; i < numSurfaces; ++i, ++pLighting )
	{
		int nStyleCt = 0;
		for ( int map = 0 ; map < MAXLIGHTMAPS; ++map )
		{
			if ( pLighting->m_nStyles[map] != 255 )
				++nStyleCt;
		}

		const int nHdrBytes = nStyleCt * sizeof( ColorRGBExp32 );
		byte *pHdr = (byte*)pLighting->m_pSamples - nHdrBytes;

		// Copy just the 0-4 average color entries
		Q_memcpy( pDest, pHdr, nHdrBytes );

		// m_pSamples needs to stay pointing AFTER the average color data
		// other code expects to back up and find it there
		pDest += nHdrBytes;
		pLighting->m_pSamples = (ColorRGBExp32*)pDest;
	}

	// discard previous and update the lightdata
	DeallocateLightingData( host_state.worldbrush );
	host_state.worldbrush->lightdata = (ColorRGBExp32*)pDestBase;

	// track this specific hack
	host_state.worldbrush->m_bUnloadedAllLightmaps = true;
}

class SurfaceLessFunc
{
public:
	// ascending sort the lighting pointers
	bool Less( const int &src1, const int &src2, void *pCtx )
	{
		msurfacelighting_t *pLighting = (msurfacelighting_t *)pCtx;
		return ( ( ( uintp )pLighting[src1].m_pSamples ) < ( ( uintp )pLighting[src2].m_pSamples ) );
	}
};


//-----------------------------------------------------------------------------
// All lightmaps should have been uploaded, can now compact portions of all
// the lighting data, fixup those surfaces, and decommit the unused portion
// of the lighting data.
//-----------------------------------------------------------------------------
void DiscardStaticLightmapData()
{
	Assert( !g_bHunkAllocLightmaps );
	if ( g_bHunkAllocLightmaps )
	{
		// for safety, can't discard if lighting data is hunk allocated
		return;
	}

	worldbrushdata_t *pBrushData = host_state.worldbrush;
	msurfacelighting_t *pLighting = pBrushData->surfacelighting;
	int numSurfaces = pBrushData->numsurfaces;

	if ( !numSurfaces || !pBrushData->m_pLightingDataStack )
		return;

	// sort all the surfaces lighting pointers
	// want the pointers to be numerically ascending
	int *pSurfaceIndexes = (int *)stackalloc( numSurfaces * sizeof( int ) );
	CUtlSortVector< int, SurfaceLessFunc > surfaceSort( pSurfaceIndexes, numSurfaces );
	surfaceSort.SetLessContext( pLighting );
	for ( int i = 0; i < numSurfaces; i++ )
	{
		surfaceSort.InsertNoSort( i );
	}
	surfaceSort.RedoSort();

	// for saftey, validate the pointers are sorted as expected, otherwise memory corruption
	ColorRGBExp32 *pLast = pLighting[surfaceSort[0]].m_pSamples;
	for ( int i = 1; i < numSurfaces; i++ )
	{
		ColorRGBExp32 *pCurrent = pLighting[surfaceSort[i]].m_pSamples;
		if ( pCurrent && pLast && (uintp)pCurrent == (uintp)pLast )
		{
			// the lighting data pointers cannot be pointing to the same valid location
			// abandon compaction, memory corruption would occur
			DevMsg( "DiscardStaticLightmapData: Surface Lighting data aliased.\n" );
			Assert( 0 );
			return;
		}
		else if ( (uintp)pCurrent < (uintp)pLast )
		{
			// the lighting data pointers must be in ascending order
			// abandon compaction, memory corruption would occur
			DevMsg( "DiscardStaticLightmapData: Surface Lighting data out of order.\n" );
			Assert( 0 );
			return;
		}
		pLast = pCurrent;
	}

	// iterate through sorted surfaces, compacting surface lighting by shifting over discarded regions
	ColorRGBExp32 *pTarget = pBrushData->lightdata;
	for ( int i = 0; i < numSurfaces; i++ )
	{
		int nSortedIndex = surfaceSort[i];

		SurfaceHandle_t surfID = SurfaceHandleFromIndex( nSortedIndex );

		if ( !SurfHasLightmap( surfID ) )
		{
			// not a candidate
			continue;
		}

		int offset = ComputeLightmapSize( surfID );
		if ( SurfHasBumpedLightmaps( surfID ) )
		{
			offset *= ( NUM_BUMP_VECTS + 1 );
		}

		// count this surface's number of lightmaps
		int nNumMaps;
		for ( nNumMaps = 0; nNumMaps < MAXLIGHTMAPS && pLighting[nSortedIndex].m_nStyles[nNumMaps] != 255; nNumMaps++ )
		{
		}

		if ( !nNumMaps )
		{
			// odd, marked for lightmaps, but no styles
			// ignore
			continue;
		}

		// account for the avgcolors
		int nSurfaceLightSize = nNumMaps;
		if ( nNumMaps > 1 && ( MSurf_Flags( surfID ) & SURFDRAW_HASLIGHTSYTLES ) )
		{
			// account for the lightmaps
			nSurfaceLightSize += nNumMaps * offset;
		}

		// position the source properly
		// the avgcolors are stored behind the lightmaps
		ColorRGBExp32 *pSource = pLighting[nSortedIndex].m_pSamples - nNumMaps;
		if ( pSource != pTarget )
		{
			memmove( pTarget, pSource, nSurfaceLightSize * sizeof( ColorRGBExp32 ) );

			// fixup the surface to the new location
			// the surface points to the data AFTER the avgcolors
			pLighting[nSortedIndex].m_pSamples = pTarget + nNumMaps;
		}
	
		// advance past
		pTarget += nSurfaceLightSize;
	}
	
	unsigned int nDynamicSize = size_cast< unsigned int >( (uintp)pTarget - (uintp)pBrushData->lightdata );

	// shrink the original allocation in place
	pBrushData->m_pLightingDataStack->FreeToAllocPoint( nDynamicSize );

	const char *mapName = modelloader->GetName( host_state.worldmodel );
	Msg( "(%s) Original Full Lighting Data:           %.2f MB\n", mapName, (float)pBrushData->m_nLightingDataSize / ( 1024.0f * 1024.0f ) );
	Msg( "(%s) Reduced To Only Dynamic Lighting Data: %.2f MB\n", mapName, (float)nDynamicSize / ( 1024.0f * 1024.0f ) );
}

//sorts the surfaces in place
static void SortSurfacesByLightmapID( SurfaceHandle_t *pToSort, int iSurfaceCount )
{
	SurfaceHandle_t *pSortTemp = (SurfaceHandle_t *)stackalloc( sizeof( SurfaceHandle_t ) * iSurfaceCount );
	
	//radix sort
	for( int radix = 0; radix != 4; ++radix )
	{
		//swap the inputs for the next pass
		{
			SurfaceHandle_t *pTemp = pToSort;
			pToSort = pSortTemp;
			pSortTemp = pTemp;
		}

		int iCounts[256] = { 0 };
		int iBitOffset = radix * 8;
		for( int i = 0; i != iSurfaceCount; ++i )
		{
			uint8 val = (materialSortInfoArray[MSurf_MaterialSortID( pSortTemp[i] )].lightmapPageID >> iBitOffset) & 0xFF;
			++iCounts[val];
		}

		int iOffsetTable[256];
		iOffsetTable[0] = 0;
		for( int i = 0; i != 255; ++i )
		{
			iOffsetTable[i + 1] = iOffsetTable[i] + iCounts[i];
		}

		for( int i = 0; i != iSurfaceCount; ++i )
		{
			uint8 val = (materialSortInfoArray[MSurf_MaterialSortID( pSortTemp[i] )].lightmapPageID >> iBitOffset) & 0xFF;
			int iWriteIndex = iOffsetTable[val];
			pToSort[iWriteIndex] = pSortTemp[i];
			++iOffsetTable[val];
		}
	}
}

void R_RedownloadAllLightmaps()
{
#ifdef _DEBUG
	static bool initializedBlockLights = false;
	if ( !initializedBlockLights )
	{
		memset( &blocklights[0][0][0], 0, MAX_LIGHTMAP_DIM_INCLUDING_BORDER * MAX_LIGHTMAP_DIM_INCLUDING_BORDER * (NUM_BUMP_VECTS + 1) * sizeof( Vector ) );
		initializedBlockLights = true;
	}
#endif

	double st = Sys_FloatTime();

	if ( !host_state.worldbrush->m_bUnloadedAllLightmaps )
	{		
		bool bOnlyUseLightStyles = false;
		if ( r_dynamic.GetInt() == 0 || r_keepstyledlightmapsonly.GetBool() )
		{
			bOnlyUseLightStyles = true;
		}

		// Can't build lightmaps if the source data has been dumped
		CMatRenderContextPtr pRenderContext( materials );
		ICallQueue *pCallQueue = pRenderContext->GetCallQueue();

		int iSurfaceCount = host_state.worldbrush->numsurfaces;
		
		SurfaceHandle_t *pSortedSurfaces = (SurfaceHandle_t *)stackalloc( sizeof( SurfaceHandle_t ) * iSurfaceCount );
		for( int surfaceIndex = 0; surfaceIndex < iSurfaceCount; surfaceIndex++ )
		{
			SurfaceHandle_t surfID = SurfaceHandleFromIndex( surfaceIndex );
			pSortedSurfaces[surfaceIndex] = surfID;
		}

		SortSurfacesByLightmapID( pSortedSurfaces, iSurfaceCount ); //sorts in place, so now the array really is sorted

		if( pCallQueue )
			pCallQueue->QueueCall( materials, &IMaterialSystem::BeginUpdateLightmaps );
		else
			materials->BeginUpdateLightmaps();
		
		matrix3x4_t xform;
		SetIdentityMatrix(xform);
		for( int surfaceIndex = 0; surfaceIndex < iSurfaceCount; surfaceIndex++ )
		{
			SurfaceHandle_t surfID = pSortedSurfaces[surfaceIndex];

			ASSERT_SURF_VALID( surfID );
			R_BuildLightMap( &cl_dlights[0], pCallQueue, surfID, xform, bOnlyUseLightStyles );
		}

		if( pCallQueue )
			pCallQueue->QueueCall( materials, &IMaterialSystem::EndUpdateLightmaps );
		else
			materials->EndUpdateLightmaps();		

		if ( !g_bHunkAllocLightmaps )
		{
			if ( r_unloadlightmaps.GetInt() == 1 )
			{
				// Delete the lightmap data from memory
				if ( !pCallQueue )
				{
					CacheAndUnloadLightmapData();
				}
				else
				{
					pCallQueue->QueueCall( CacheAndUnloadLightmapData );
				}
			}
			else if ( r_keepstyledlightmapsonly.GetBool() )
			{
				if ( !pCallQueue )
				{
					DiscardStaticLightmapData();
				}
				else
				{
					pCallQueue->QueueCall( DiscardStaticLightmapData );
				}
			}
		}
	}

	float elapsed = ( float )( Sys_FloatTime() - st ) * 1000.0;
	DevMsg( "R_RedownloadAllLightmaps took %.3f msec!\n", elapsed );

	g_RebuildLightmaps = false;
}

//-----------------------------------------------------------------------------
// Purpose: flag the lightmaps as needing to be rebuilt (gamma change)
//-----------------------------------------------------------------------------
bool g_RebuildLightmaps = false;

void GL_RebuildLightmaps( void )
{
	g_RebuildLightmaps = true;
}


//-----------------------------------------------------------------------------
// Purpose: Update the in-RAM texture for the given surface's lightmap
// Input  : *fa - surface pointer
//-----------------------------------------------------------------------------

//#ifdef UPDATE_LIGHTSTYLES_EVERY_FRAME
ConVar mat_updatelightstyleseveryframe( "mat_updatelightstyleseveryframe", "0" );
//#endif

int __cdecl LightmapPageCompareFunc( const void *pElem0, const void *pElem1 )
{
	const LightmapUpdateInfo_t *pSurf0 = (const LightmapUpdateInfo_t *)pElem0;
	const LightmapUpdateInfo_t *pSurf1 = (const LightmapUpdateInfo_t *)pElem1;
	int page0 = materialSortInfoArray[MSurf_MaterialSortID( (pSurf0->m_SurfHandle) )].lightmapPageID;
	int page1 = materialSortInfoArray[MSurf_MaterialSortID( (pSurf1->m_SurfHandle) )].lightmapPageID;
	return page0 - page1;
}

void R_BuildLightmapUpdateList()
{
	CMatRenderContextPtr pRenderContext( materials );
	ICallQueue *pCallQueue = pRenderContext->GetCallQueue();
	dlight_t *pLights = &cl_dlights[0];
	// only do the copy when there are valid dlights to process and threading is on
	if ( g_bActiveDlights && pCallQueue )
	{
		// keep a copy of the current dlight state around for the thread to work on 
		// in parallel.  This way the main thread can continue to modify this state without
		// generating any bad results
		static dlight_t threadDlights[MAX_DLIGHTS*2];
		static int threadFrameCount = 0;
		pLights = &threadDlights[MAX_DLIGHTS*threadFrameCount];
		Q_memcpy( pLights, cl_dlights, sizeof(dlight_t) * MAX_DLIGHTS );
		threadFrameCount = (threadFrameCount+1) & 1;
	}

	qsort( g_LightmapUpdateList.Base(), g_LightmapUpdateList.Count(), sizeof(g_LightmapUpdateList.Element(0)), LightmapPageCompareFunc );
	for ( int i = 0; i < g_LightmapUpdateList.Count(); i++ )
	{
		const LightmapUpdateInfo_t &info = g_LightmapUpdateList.Element(i);
		if ( !pCallQueue )
		{
			R_BuildLightMapGuts( pLights, info.m_SurfHandle, g_LightmapTransformList[info.m_nTransformIndex].xform, 
				info.m_nDlightMask, info.m_bNeedsBumpmap, info.m_bNeedsLightmap );
		}
		else
		{
			pCallQueue->QueueCall( R_BuildLightMapGuts, pLights, info.m_SurfHandle, RefToVal( g_LightmapTransformList[info.m_nTransformIndex].xform ), 
				info.m_nDlightMask, info.m_bNeedsBumpmap, info.m_bNeedsLightmap );
		}
	}
}

void R_CheckForLightmapUpdates( SurfaceHandle_t surfID, int nTransformIndex )
{
	msurfacelighting_t *pLighting = SurfaceLighting( surfID );
	if ( pLighting->m_nLastComputedFrame == r_framecount )
        return;

    int nFlags = MSurf_Flags( surfID );

    if( nFlags & SURFDRAW_NOLIGHT )
        return;

    // check for lightmap modification
    bool bChanged = false;
    if( nFlags & SURFDRAW_HASLIGHTSYTLES )
    {
//#ifdef UPDATE_LIGHTSTYLES_EVERY_FRAME //lwss: enable this
        if( mat_updatelightstyleseveryframe.GetBool() && ( pLighting->m_nStyles[0] != 0 || pLighting->m_nStyles[1] != 255 ) )
        {
            bChanged = true;
        }
        else
//#endif
        {
            for( int maps = 0; maps < MAXLIGHTMAPS && pLighting->m_nStyles[maps] != 255; maps++ )
            {
                if( d_lightstyleframe[pLighting->m_nStyles[maps]] > pLighting->m_nLastComputedFrame )
                {
                    bChanged = true;
                    break;
                }
            }
        }
    }

    // was it dynamic this frame (pLighting->m_nDLightFrame == r_framecount)
    // or dynamic previously (pLighting->m_fDLightBits)
    // lwss add: hack this so we can restore 32-byte alignment
    //bool bDLightChanged = ( pLighting->m_nDLightFrame == r_framecount ) || pLighting->m_fDLightBits;
    bool bDLightChanged = pLighting->m_wantsDlightThisFrame || pLighting->m_fDLightBits;

    bool bOnlyUseLightStyles = false;
    if ( r_dynamic.GetInt() == 0 || r_keepstyledlightmapsonly.GetBool() )
    {
        bOnlyUseLightStyles = true;
    }
    else
    {
        bChanged |= bDLightChanged;
    }

    if ( bChanged )
    {
        bool bNeedsBumpmap = SurfNeedsBumpedLightmaps( surfID );
        bool bNeedsLightmap = SurfNeedsLightmap( surfID );

        if( !bNeedsBumpmap && !bNeedsLightmap )
            return;

        if( materialSortInfoArray )
        {
            int nSortID = MSurf_MaterialSortID( surfID );
            Assert( nSortID >= 0 && nSortID < g_WorldStaticMeshes.Count() );
            if (( materialSortInfoArray[nSortID].lightmapPageID == MATERIAL_SYSTEM_LIGHTMAP_PAGE_WHITE )	||
                ( materialSortInfoArray[nSortID].lightmapPageID == MATERIAL_SYSTEM_LIGHTMAP_PAGE_WHITE_BUMP ) )
            {
                return;
            }
        }
        bool bDlightsInLightmap = bNeedsLightmap || bNeedsBumpmap;
        unsigned int nDlightMask = R_UpdateDlightState( cl_dlights, surfID, g_LightmapTransformList[nTransformIndex].xform, bOnlyUseLightStyles, bDlightsInLightmap );


        int nIndex = g_LightmapUpdateList.AddToTail();
        g_LightmapUpdateList[nIndex].m_SurfHandle = surfID;
        g_LightmapUpdateList[nIndex].m_nTransformIndex = nTransformIndex;
        g_LightmapUpdateList[nIndex].m_nDlightMask= nDlightMask;
        g_LightmapUpdateList[nIndex].m_bNeedsLightmap = bNeedsLightmap;
        g_LightmapUpdateList[nIndex].m_bNeedsBumpmap = bNeedsBumpmap;
    }
}
