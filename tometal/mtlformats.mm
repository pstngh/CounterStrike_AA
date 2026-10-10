//================ Copyright (c) Valve Corporation. All Rights Reserved. =================
//
// mtlformats.mm
//
// D3D9 formats on Metal. The choices follow ToGL's format table, so textures
// sample the same: X8R8G8B8 and DXT1 read alpha as 1, L8 and A8L8 replicate
// luminance, R32F reads (r, 0, 0, 1), and Q8W8V8U8 is plain unsigned bytes
// that the shaders rescale. Formats Metal lacks are converted on upload.
//
//==================================================================================================
#include "mtlinternal.h"

#include "tier0/memdbgon.h"

#define SWZ( r, g, b, a ) MTLTextureSwizzleChannelsMake( MTLTextureSwizzle##r, MTLTextureSwizzle##g, MTLTextureSwizzle##b, MTLTextureSwizzle##a )
#define NOSWZ SWZ( Red, Green, Blue, Alpha )

static const MtlFormatDesc s_formats[] =
{
	// d3d format				name				storage								sRGB view								block	d3d	mtl		convert					swizzle?	swizzle							depth	stencil
	{ D3DFMT_D16,				"D16",				MTLPixelFormatDepth16Unorm,			MTLPixelFormatInvalid,					1,		2,	2,		kMtlConvertNone,		false,		NOSWZ,							true,	false },
	{ D3DFMT_D24X8,				"D24X8",			MTLPixelFormatDepth32Float,			MTLPixelFormatInvalid,					1,		4,	4,		kMtlConvertNone,		false,		NOSWZ,							true,	false },
	{ D3DFMT_D24S8,				"D24S8",			MTLPixelFormatDepth32Float_Stencil8,	MTLPixelFormatInvalid,				1,		4,	8,		kMtlConvertNone,		false,		NOSWZ,							true,	true },

	{ D3DFMT_A8R8G8B8,			"A8R8G8B8",			MTLPixelFormatBGRA8Unorm,			MTLPixelFormatBGRA8Unorm_sRGB,			1,		4,	4,		kMtlConvertNone,		false,		NOSWZ,							false,	false },
	{ D3DFMT_X8R8G8B8,			"X8R8G8B8",			MTLPixelFormatBGRA8Unorm,			MTLPixelFormatBGRA8Unorm_sRGB,			1,		4,	4,		kMtlConvertNone,		true,		SWZ( Red, Green, Blue, One ),	false,	false },
	{ D3DFMT_A4R4G4B4,			"A4R4G4B4",			MTLPixelFormatBGRA8Unorm,			MTLPixelFormatInvalid,					1,		2,	4,		kMtlConvertA4R4G4B4,	false,		NOSWZ,							false,	false },
	{ D3DFMT_X1R5G5B5,			"X1R5G5B5",			MTLPixelFormatBGRA8Unorm,			MTLPixelFormatInvalid,					1,		2,	4,		kMtlConvertX1R5G5B5,	false,		NOSWZ,							false,	false },
	{ D3DFMT_A1R5G5B5,			"A1R5G5B5",			MTLPixelFormatBGRA8Unorm,			MTLPixelFormatInvalid,					1,		2,	4,		kMtlConvertA1R5G5B5,	false,		NOSWZ,							false,	false },
	{ D3DFMT_R5G6B5,			"R5G6B5",			MTLPixelFormatBGRA8Unorm,			MTLPixelFormatBGRA8Unorm_sRGB,			1,		2,	4,		kMtlConvertR5G6B5,		false,		NOSWZ,							false,	false },
	{ D3DFMT_R8G8B8,			"R8G8B8",			MTLPixelFormatBGRA8Unorm,			MTLPixelFormatBGRA8Unorm_sRGB,			1,		3,	4,		kMtlConvertR8G8B8,		false,		NOSWZ,							false,	false },

	{ D3DFMT_L8,				"L8",				MTLPixelFormatR8Unorm,				MTLPixelFormatR8Unorm_sRGB,				1,		1,	1,		kMtlConvertNone,		true,		SWZ( Red, Red, Red, One ),		false,	false },
	{ D3DFMT_A8L8,				"A8L8",				MTLPixelFormatRG8Unorm,				MTLPixelFormatRG8Unorm_sRGB,			1,		2,	2,		kMtlConvertNone,		true,		SWZ( Red, Red, Red, Green ),	false,	false },
	{ D3DFMT_A8,				"A8",				MTLPixelFormatA8Unorm,				MTLPixelFormatInvalid,					1,		1,	1,		kMtlConvertNone,		false,		NOSWZ,							false,	false },

	{ D3DFMT_DXT1,				"DXT1",				MTLPixelFormatBC1_RGBA,				MTLPixelFormatBC1_RGBA_sRGB,			4,		8,	8,		kMtlConvertNone,		true,		SWZ( Red, Green, Blue, One ),	false,	false },
	{ D3DFMT_DXT3,				"DXT3",				MTLPixelFormatBC2_RGBA,				MTLPixelFormatBC2_RGBA_sRGB,			4,		16,	16,		kMtlConvertNone,		false,		NOSWZ,							false,	false },
	{ D3DFMT_DXT5,				"DXT5",				MTLPixelFormatBC3_RGBA,				MTLPixelFormatBC3_RGBA_sRGB,			4,		16,	16,		kMtlConvertNone,		false,		NOSWZ,							false,	false },

	{ D3DFMT_A16B16G16R16F,		"A16B16G16R16F",	MTLPixelFormatRGBA16Float,			MTLPixelFormatInvalid,					1,		8,	8,		kMtlConvertNone,		false,		NOSWZ,							false,	false },
	{ D3DFMT_A16B16G16R16,		"A16B16G16R16",		MTLPixelFormatRGBA16Unorm,			MTLPixelFormatInvalid,					1,		8,	8,		kMtlConvertNone,		false,		NOSWZ,							false,	false },
	{ D3DFMT_A32B32G32R32F,		"A32B32G32R32F",	MTLPixelFormatRGBA32Float,			MTLPixelFormatInvalid,					1,		16,	16,		kMtlConvertNone,		false,		NOSWZ,							false,	false },
	{ D3DFMT_R32F,				"R32F",				MTLPixelFormatR32Float,				MTLPixelFormatInvalid,					1,		4,	4,		kMtlConvertNone,		false,		NOSWZ,							false,	false },
	{ D3DFMT_A2R10G10B10,		"A2R10G10B10",		MTLPixelFormatBGR10A2Unorm,			MTLPixelFormatInvalid,					1,		4,	4,		kMtlConvertNone,		false,		NOSWZ,							false,	false },
	{ D3DFMT_A2B10G10R10,		"A2B10G10R10",		MTLPixelFormatRGB10A2Unorm,			MTLPixelFormatInvalid,					1,		4,	4,		kMtlConvertNone,		false,		NOSWZ,							false,	false },

	{ D3DFMT_Q8W8V8U8,			"Q8W8V8U8",			MTLPixelFormatBGRA8Unorm,			MTLPixelFormatInvalid,					1,		4,	4,		kMtlConvertNone,		false,		NOSWZ,							false,	false },
	{ D3DFMT_V8U8,				"V8U8",				MTLPixelFormatRG8Snorm,				MTLPixelFormatInvalid,					1,		2,	2,		kMtlConvertNone,		true,		SWZ( Red, Green, One, One ),	false,	false },
};

const MtlFormatDesc *MtlGetFormatDesc( D3DFORMAT format )
{
	for ( int i = 0; i < ARRAYSIZE( s_formats ); i++ )
	{
		if ( s_formats[i].m_d3dFormat == format )
			return &s_formats[i];
	}
	return NULL;
}

static inline uint8 Expand5( uint v ) { return (uint8)( ( v << 3 ) | ( v >> 2 ) ); }
static inline uint8 Expand6( uint v ) { return (uint8)( ( v << 2 ) | ( v >> 4 ) ); }
static inline uint8 Expand4( uint v ) { return (uint8)( ( v << 4 ) | v ); }

// Converts one row of D3D texels into the Metal storage format (BGRA8).
void MtlConvertRow( const MtlFormatDesc *pDesc, const uint8 *pSrc, uint8 *pDst, uint nTexels )
{
	switch ( pDesc->m_eConvert )
	{
	case kMtlConvertR8G8B8:
		for ( uint i = 0; i < nTexels; i++, pSrc += 3, pDst += 4 )
		{
			pDst[0] = pSrc[0]; pDst[1] = pSrc[1]; pDst[2] = pSrc[2]; pDst[3] = 255;
		}
		break;
	case kMtlConvertR5G6B5:
		for ( uint i = 0; i < nTexels; i++, pSrc += 2, pDst += 4 )
		{
			uint v = pSrc[0] | ( pSrc[1] << 8 );
			pDst[0] = Expand5( v & 31 ); pDst[1] = Expand6( ( v >> 5 ) & 63 ); pDst[2] = Expand5( v >> 11 ); pDst[3] = 255;
		}
		break;
	case kMtlConvertX1R5G5B5:
	case kMtlConvertA1R5G5B5:
		for ( uint i = 0; i < nTexels; i++, pSrc += 2, pDst += 4 )
		{
			uint v = pSrc[0] | ( pSrc[1] << 8 );
			pDst[0] = Expand5( v & 31 ); pDst[1] = Expand5( ( v >> 5 ) & 31 ); pDst[2] = Expand5( ( v >> 10 ) & 31 );
			pDst[3] = ( pDesc->m_eConvert == kMtlConvertX1R5G5B5 || ( v & 0x8000 ) ) ? 255 : 0;
		}
		break;
	case kMtlConvertA4R4G4B4:
		for ( uint i = 0; i < nTexels; i++, pSrc += 2, pDst += 4 )
		{
			uint v = pSrc[0] | ( pSrc[1] << 8 );
			pDst[0] = Expand4( v & 15 ); pDst[1] = Expand4( ( v >> 4 ) & 15 ); pDst[2] = Expand4( ( v >> 8 ) & 15 ); pDst[3] = Expand4( v >> 12 );
		}
		break;
	case kMtlConvertNone:
		memcpy( pDst, pSrc, nTexels * pDesc->m_nMtlBytesPerBlock );
		break;
	}
}
