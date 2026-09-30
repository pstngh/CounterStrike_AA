//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#ifndef _MATH_PFNS_H_
#define _MATH_PFNS_H_

#include <limits>

// YUP_ACTIVE is from Source2. It's (obviously) not supported on this branch, just including it here to help merge camera.cpp/.h and the CSM shadow code.
//#define YUP_ACTIVE 1

enum MatrixAxisType_t
{
#ifdef YUP_ACTIVE
	FORWARD_AXIS = 2,
	LEFT_AXIS = 0,
	UP_AXIS = 1,
#else
	FORWARD_AXIS = 0,
	LEFT_AXIS = 1,
	UP_AXIS = 2,
#endif

	X_AXIS = 0,
	Y_AXIS = 1,
	Z_AXIS = 2,
	ORIGIN = 3,
	PROJECTIVE = 3,
};

// If we are not PPC based or SPU based, then assumes it is SSE2. We should make this code cleaner.

#include <xmmintrin.h>



// These globals are initialized by mathlib and redirected based on available fpu features

// The following are not declared as macros because they are often used in limiting situations,
// and sometimes the compiler simply refuses to inline them for some reason
FORCEINLINE float VECTORCALL FastSqrt( float x )
{
	__m128 root = _mm_sqrt_ss( _mm_load_ss( &x ) );
	return *( reinterpret_cast<float *>( &root ) );
}

FORCEINLINE float VECTORCALL FastRSqrtFast( float x )
{
	// use intrinsics
	__m128 rroot = _mm_rsqrt_ss( _mm_load_ss( &x ) );
	return *( reinterpret_cast<float *>( &rroot ) );
}
// Single iteration NewtonRaphson reciprocal square root:
// 0.5 * rsqrtps * (3 - x * rsqrtps(x) * rsqrtps(x)) 	
// Very low error, and fine to use in place of 1.f / sqrtf(x).	
FORCEINLINE float VECTORCALL FastRSqrt( float x )
{
	float rroot = FastRSqrtFast( x );
	return (0.5f * rroot) * (3.f - (x * rroot) * rroot);
}

void FastSinCos( float x, float* s, float* c );  // any x
float FastCos( float x );



inline float FastRecip(float x) {return 1.0f / x;}
// Simple SSE rsqrt.  Usually accurate to around 6 (relative) decimal places 
// or so, so ok for closed transforms.  (ie, computing lighting normals)
inline float FastSqrtEst(float x) { return FastRSqrtFast(x) * x; }


#endif // _MATH_PFNS_H_
