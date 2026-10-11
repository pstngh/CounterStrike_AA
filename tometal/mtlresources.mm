//================ Copyright (c) Valve Corporation. All Rights Reserved. =================
//
// mtlresources.mm
//
// D3D9 resources on Metal: textures and surfaces, vertex and index buffers,
// vertex declarations, queries and shaders.
//
// Textures that are only sampled live in shared memory and are written with
// replaceRegion when the GPU isn't using them, or through a blit otherwise.
// Render targets live in private memory. A lock hands out a CPU copy of the
// slice in D3D layout; unlocking uploads the locked box.
//
//==================================================================================================
#include "mtlinternal.h"
#include "tier0/icommandline.h"
#include "tier1/strtools.h"

#include "tier0/memdbgon.h"

#define D3D_DEVICE_VALID_MARKER 0x12EBC845

static ConVar mtl_shader_spew( "mtl_shader_spew", "0", 0, "Print every translated Metal shader to the console" );

// ------------------------------------------------------------------------------------------------------------------------------ //
// CMtlTex
// ------------------------------------------------------------------------------------------------------------------------------ //

CMtlTex::CMtlTex() :
	m_pFormat( NULL ), m_d3dFormat( D3DFMT_UNKNOWN ), m_type( MTLTextureType2D ), m_nFlags( 0 ),
	m_nWidth( 0 ), m_nHeight( 0 ), m_nDepth( 0 ), m_nMips( 0 ), m_nFaces( 0 ), m_nSamples( 1 ),
	m_pBacking( NULL ), m_pSliceValid( NULL ), m_nLastUseSerial( 0 ), m_nLastWriteSerial( 0 ), m_pDebugLabel( NULL )
{
}

CMtlTex::~CMtlTex()
{
	if ( m_pBacking )
	{
		for ( uint i = 0; i < m_nFaces * m_nMips; i++ )
		{
			free( m_pBacking[i].m_pData );
		}
		delete[] m_pBacking;
	}
	delete[] m_pSliceValid;
	delete[] m_pDebugLabel;
}

static uint BlocksAcross( const MtlFormatDesc *pDesc, uint nTexels )
{
	return ( nTexels + pDesc->m_nBlockSize - 1 ) / pDesc->m_nBlockSize;
}

// Makes a texture. nLevels is what D3D asked for: like ToGL, a texture with
// more than one level (or automatic mipmaps) gets the full chain, and levels
// the caller never fills read as zero.
static CMtlTex *NewMtlTex( MTLTextureType type, D3DFORMAT format, uint w, uint h, uint d, uint nLevels, bool bAutoMip, uint nFlags, uint nSamples, const char *pLabel )
{
	const MtlFormatDesc *pDesc = MtlGetFormatDesc( format );
	if ( !pDesc )
	{
		Warning( "ToMetal: unsupported texture format %d (%s)\n", (int)format, pLabel ? pLabel : "" );
		return NULL;
	}

	CMtlTex *pTex = new CMtlTex;
	pTex->m_pFormat = pDesc;
	pTex->m_d3dFormat = format;
	pTex->m_type = type;
	pTex->m_nFlags = nFlags;
	pTex->m_nWidth = MAX( 1u, w );
	pTex->m_nHeight = MAX( 1u, h );
	pTex->m_nDepth = MAX( 1u, d );
	pTex->m_nSamples = MAX( 1u, nSamples );
	pTex->m_nFaces = ( type == MTLTextureTypeCube ) ? 6 : 1;
	if ( pDesc->m_bDepth )
	{
		pTex->m_nFlags |= kMtlTexDepth | ( pDesc->m_bStencil ? kMtlTexStencil : 0 );
	}
	if ( pTex->m_nSamples > 1 )
	{
		pTex->m_nFlags |= kMtlTexMultisampled;
	}

	uint nMips = 1;
	if ( ( nLevels > 1 || bAutoMip ) && pTex->m_nSamples == 1 )
	{
		uint largest = MAX( pTex->m_nWidth, MAX( pTex->m_nHeight, pTex->m_nDepth ) );
		nMips = 0;
		while ( largest > 0 )
		{
			nMips++;
			largest >>= 1;
		}
	}
	pTex->m_nMips = nMips;

	if ( pLabel )
	{
		pTex->m_pDebugLabel = V_strdup( pLabel );
	}

	bool bRenderable = ( nFlags & kMtlTexRenderable ) != 0;
	bool bSRGBViews = pDesc->m_mtlFormatSRGB != MTLPixelFormatInvalid;
	if ( bSRGBViews )
	{
		pTex->m_nFlags |= kMtlTexSRGB;
	}

	MTLTextureDescriptor *desc = [MTLTextureDescriptor new];
	desc.textureType = ( pTex->m_nSamples > 1 ) ? MTLTextureType2DMultisample : type;
	desc.pixelFormat = pDesc->m_mtlFormat;
	desc.width = pTex->m_nWidth;
	desc.height = pTex->m_nHeight;
	desc.depth = ( type == MTLTextureType3D ) ? pTex->m_nDepth : 1;
	desc.mipmapLevelCount = nMips;
	desc.sampleCount = pTex->m_nSamples;
	desc.usage = MTLTextureUsageShaderRead;
	if ( bRenderable )
	{
		desc.usage |= MTLTextureUsageRenderTarget;
	}
	if ( bSRGBViews || pDesc->m_bSwizzle )
	{
		desc.usage |= MTLTextureUsagePixelFormatView;
	}
	desc.storageMode = ( bRenderable || pTex->m_nSamples > 1 ) ? MTLStorageModePrivate : MTLStorageModeShared;
	desc.cpuCacheMode = MTLCPUCacheModeWriteCombined;

	pTex->m_tex = [g_pMtlDevice->m_device newTextureWithDescriptor:desc];
	if ( !pTex->m_tex )
	{
		Warning( "ToMetal: couldn't create a %ux%ux%u %s texture (%s)\n", pTex->m_nWidth, pTex->m_nHeight, pTex->m_nDepth, pDesc->m_pName, pLabel ? pLabel : "" );
		delete pTex;
		return NULL;
	}
	if ( pLabel )
	{
		pTex->m_tex.label = [NSString stringWithUTF8String:pLabel];
	}

	NSRange levels = NSMakeRange( 0, nMips );
	NSRange slices = NSMakeRange( 0, pTex->m_nFaces );
	MTLTextureType viewType = desc.textureType;
	if ( pDesc->m_bSwizzle && pTex->m_nSamples == 1 )
	{
		pTex->m_texSample = [pTex->m_tex newTextureViewWithPixelFormat:pDesc->m_mtlFormat textureType:viewType levels:levels slices:slices swizzle:pDesc->m_swizzle];
	}
	else
	{
		pTex->m_texSample = pTex->m_tex;
	}
	if ( bSRGBViews )
	{
		pTex->m_texRenderSRGB = [pTex->m_tex newTextureViewWithPixelFormat:pDesc->m_mtlFormatSRGB];
		if ( pDesc->m_bSwizzle && pTex->m_nSamples == 1 )
		{
			pTex->m_texSampleSRGB = [pTex->m_tex newTextureViewWithPixelFormat:pDesc->m_mtlFormatSRGB textureType:viewType levels:levels slices:slices swizzle:pDesc->m_swizzle];
		}
		else
		{
			pTex->m_texSampleSRGB = pTex->m_texRenderSRGB;
		}
	}

	uint nSlices = pTex->m_nFaces * nMips;
	pTex->m_pSliceValid = new uint8[ nSlices ];
	memset( pTex->m_pSliceValid, 0, nSlices );

	// Textures read as zero until written, as in ToGL. Levels beyond those the
	// caller asked for, or beyond the first with automatic mipmaps (which ToGL
	// doesn't generate either), are never filled.
	uint nFirstUnfilled = bAutoMip ? MAX( 1u, MIN( nLevels, nMips ) ) : MIN( MAX( 1u, nLevels ), nMips );
	g_pMtlDevice->ZeroTexture( pTex, nFirstUnfilled );
	return pTex;
}

static void DeleteMtlTex( CMtlTex *pTex )
{
	if ( !pTex )
		return;
	if ( g_pMtlDevice )
	{
		g_pMtlDevice->ReleasedMtlTex( pTex );
	}
	delete pTex;	// the command buffers that use its textures keep them alive
}

// Size of one face/mip in D3D layout
static void SliceLayout( const CMtlTex *pTex, uint mip, uint *pPitch, uint *pSlicePitch, uint *pSize )
{
	const MtlFormatDesc *pDesc = pTex->m_pFormat;
	uint rowBlocks = BlocksAcross( pDesc, pTex->MipWidth( mip ) );
	uint rows = BlocksAcross( pDesc, pTex->MipHeight( mip ) );
	*pPitch = rowBlocks * pDesc->m_nD3DBytesPerBlock;
	*pSlicePitch = *pPitch * rows;
	*pSize = *pSlicePitch * pTex->MipDepth( mip );
}

static HRESULT LockSlice( CMtlTex *pTex, uint face, uint mip, uint x0, uint y0, uint z0, uint x1, uint y1, uint z1, DWORD flags, void **ppBits, int *pPitch, int *pSlicePitch )
{
	if ( !pTex || mip >= pTex->m_nMips || face >= pTex->m_nFaces )
		return D3DERR_INVALIDCALL;

	if ( pTex->m_nFlags & kMtlTexMultisampled )
	{
		Warning( "ToMetal: can't lock a multisampled surface\n" );
		return D3DERR_INVALIDCALL;
	}

	if ( !pTex->m_pBacking )
	{
		uint n = pTex->m_nFaces * pTex->m_nMips;
		pTex->m_pBacking = new MtlSliceBacking[ n ];
		memset( pTex->m_pBacking, 0, sizeof( MtlSliceBacking ) * n );
	}

	uint nSlice = face * pTex->m_nMips + mip;
	MtlSliceBacking &b = pTex->m_pBacking[ nSlice ];
	if ( b.m_bLocked )
	{
		Warning( "ToMetal: surface locked twice\n" );
		return D3DERR_INVALIDCALL;
	}

	uint w = pTex->MipWidth( mip ), h = pTex->MipHeight( mip ), d = pTex->MipDepth( mip );
	x1 = MIN( x1, w ); y1 = MIN( y1, h ); z1 = MIN( z1, d );
	if ( x0 >= x1 || y0 >= y1 || z0 >= z1 )
		return D3DERR_INVALIDCALL;

	// Block compressed formats lock whole blocks
	const MtlFormatDesc *pDesc = pTex->m_pFormat;
	uint bs = pDesc->m_nBlockSize;
	x0 = ( x0 / bs ) * bs; y0 = ( y0 / bs ) * bs;

	bool bPartial = ( x0 > 0 || y0 > 0 || z0 > 0 || x1 < w || y1 < h || z1 < d );
	bool bReadBack = ( flags & ( D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK ) ) == ( D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK );

	uint pitch, slicePitch, size;
	SliceLayout( pTex, mip, &pitch, &slicePitch, &size );
	// Render targets hold whatever the GPU drew; other slices hold data once uploaded
	bool bRenderable = ( pTex->m_nFlags & kMtlTexRenderable ) != 0;
	bool bHasData = bRenderable || pTex->m_pSliceValid[ nSlice ];
	if ( !b.m_pData )
	{
		b.m_pData = (uint8*)calloc( 1, size );
		b.m_nPitch = pitch;
		b.m_nSlicePitch = slicePitch;
		// As in D3D (and ToGL), a lock that doesn't discard sees the slice's
		// contents, even when it covers all of it: lightmap pages are locked
		// whole and only the surfaces that changed are written.
		if ( bHasData && !( flags & D3DLOCK_DISCARD ) )
		{
			g_pMtlDevice->ReadSlice( pTex, face, mip, b );
			b.m_bKeep = !bRenderable;
		}
	}
	else if ( bReadBack && bRenderable )
	{
		// The GPU may have drawn into it since the copy was made
		g_pMtlDevice->ReadSlice( pTex, face, mip, b );
	}

	b.m_bLocked = true;
	b.m_bReadOnly = ( flags & D3DLOCK_READONLY ) != 0;
	b.m_bPartial = bPartial;
	b.m_x = x0; b.m_y = y0; b.m_z = z0;
	b.m_w = x1 - x0; b.m_h = y1 - y0; b.m_d = z1 - z0;

	*ppBits = b.m_pData + z0 * slicePitch + ( y0 / bs ) * pitch + ( x0 / bs ) * pDesc->m_nD3DBytesPerBlock;
	*pPitch = pitch;
	if ( pSlicePitch )
	{
		*pSlicePitch = slicePitch;
	}
	return S_OK;
}

static HRESULT UnlockSlice( CMtlTex *pTex, uint face, uint mip )
{
	if ( !pTex || !pTex->m_pBacking || mip >= pTex->m_nMips || face >= pTex->m_nFaces )
		return D3DERR_INVALIDCALL;

	uint nSlice = face * pTex->m_nMips + mip;
	MtlSliceBacking &b = pTex->m_pBacking[ nSlice ];
	if ( !b.m_bLocked )
		return D3DERR_INVALIDCALL;
	b.m_bLocked = false;

	if ( !b.m_bReadOnly )
	{
		g_pMtlDevice->UploadSlice( pTex, face, mip, b, b.m_x, b.m_y, b.m_z, b.m_w, b.m_h, b.m_d );
		pTex->m_pSliceValid[ nSlice ] = 1;
	}

	// Keep the copy of slices updated piecemeal or after their first upload
	// (lightmaps, for one) and of formats converted on upload, which can't be
	// read back; free the rest.
	if ( !b.m_bPartial && !b.m_bKeep && pTex->m_pFormat->m_eConvert == kMtlConvertNone && !( pTex->m_nFlags & kMtlTexRenderable ) )
	{
		free( b.m_pData );
		b.m_pData = NULL;
	}
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DResource9 / IDirect3DBaseTexture9
// ------------------------------------------------------------------------------------------------------------------------------ //

DWORD IDirect3DResource9::SetPriority( DWORD PriorityNew )
{
	return 0;
}

IDirect3DBaseTexture9::~IDirect3DBaseTexture9()
{
	if ( m_device )
	{
		if ( g_pMtlDevice )
		{
			g_pMtlDevice->ReleasedTexture( this );
		}
		DeleteMtlTex( m_tex );
		m_tex = NULL;
		m_device = NULL;
	}
}

D3DRESOURCETYPE IDirect3DBaseTexture9::GetType()
{
	return m_restype;
}

DWORD IDirect3DBaseTexture9::GetLevelCount()
{
	return m_tex ? m_tex->m_nMips : 0;
}

HRESULT IDirect3DBaseTexture9::GetLevelDesc( UINT Level, D3DSURFACE_DESC *pDesc )
{
	D3DSURFACE_DESC result = m_descZero;
	result.Width = m_tex->MipWidth( Level );
	result.Height = m_tex->MipHeight( Level );
	*pDesc = result;
	return S_OK;
}

static IDirect3DSurface9 *NewSurfaceRef( IDirect3DDevice9 *pDevice, const D3DSURFACE_DESC &desc, CMtlTex *pTex, int face, int mip )
{
	IDirect3DSurface9 *surf = new IDirect3DSurface9;
	surf->m_restype = (D3DRESOURCETYPE)0;	// 0: refers to a texture's tex, doesn't own it
	surf->m_device = pDevice;
	surf->m_desc = desc;
	surf->m_desc.Width = pTex->MipWidth( mip );
	surf->m_desc.Height = pTex->MipHeight( mip );
	surf->m_tex = pTex;
	surf->m_face = face;
	surf->m_mip = mip;
	return surf;
}

static D3DSURFACE_DESC TextureDesc( D3DFORMAT Format, D3DRESOURCETYPE type, DWORD Usage, D3DPOOL Pool, UINT Width, UINT Height )
{
	D3DSURFACE_DESC desc;
	memset( &desc, 0, sizeof( desc ) );
	desc.Format = Format;
	desc.Type = type;
	desc.Usage = Usage;
	desc.Pool = Pool;
	desc.MultiSampleType = D3DMULTISAMPLE_NONE;
	desc.MultiSampleQuality = 0;
	desc.Width = Width;
	desc.Height = Height;
	return desc;
}

static uint TexFlagsForUsage( DWORD Usage )
{
	uint nFlags = 0;
	if ( Usage & ( D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL ) )
	{
		nFlags |= kMtlTexRenderable;
	}
	return nFlags;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DTexture9
// ------------------------------------------------------------------------------------------------------------------------------ //

HRESULT IDirect3DDevice9::CreateTexture( UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, VD3DHANDLE* pSharedHandle, char *pDebugLabel )
{
	*ppTexture = NULL;
	CMtlTex *tex = NewMtlTex( MTLTextureType2D, Format, Width, Height, 1, Levels, ( Usage & D3DUSAGE_AUTOGENMIPMAP ) != 0, TexFlagsForUsage( Usage ), 1, pDebugLabel );
	if ( !tex )
		return D3DERR_OUTOFVIDEOMEMORY;

	IDirect3DTexture9 *dxtex = new IDirect3DTexture9;
	dxtex->m_restype = D3DRTYPE_TEXTURE;
	dxtex->m_device = this;
	dxtex->m_descZero = TextureDesc( Format, D3DRTYPE_TEXTURE, Usage, Pool, Width, Height );
	dxtex->m_tex = tex;
	dxtex->m_surfZero = NewSurfaceRef( this, dxtex->m_descZero, tex, 0, 0 );

	*ppTexture = dxtex;
	return S_OK;
}

IDirect3DTexture9::~IDirect3DTexture9()
{
	if ( m_device )
	{
		if ( g_pMtlDevice )
		{
			g_pMtlDevice->ReleasedTexture( this );
		}
		if ( m_surfZero )
		{
			m_surfZero->Release( 0 );
			m_surfZero = NULL;
		}
	}
}

HRESULT IDirect3DTexture9::LockRect( UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags )
{
	uint x0 = pRect ? pRect->left : 0, y0 = pRect ? pRect->top : 0;
	uint x1 = pRect ? pRect->right : m_tex->MipWidth( Level ), y1 = pRect ? pRect->bottom : m_tex->MipHeight( Level );
	int pitch;
	HRESULT hr = LockSlice( m_tex, 0, Level, x0, y0, 0, x1, y1, 1, Flags, &pLockedRect->pBits, &pitch, NULL );
	pLockedRect->Pitch = pitch;
	return hr;
}

HRESULT IDirect3DTexture9::UnlockRect( UINT Level )
{
	return UnlockSlice( m_tex, 0, Level );
}

HRESULT IDirect3DTexture9::GetSurfaceLevel( UINT Level, IDirect3DSurface9** ppSurfaceLevel )
{
	*ppSurfaceLevel = NewSurfaceRef( m_device, m_descZero, m_tex, 0, Level );
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DCubeTexture9
// ------------------------------------------------------------------------------------------------------------------------------ //

HRESULT IDirect3DDevice9::CreateCubeTexture( UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, VD3DHANDLE* pSharedHandle, char *pDebugLabel )
{
	*ppCubeTexture = NULL;
	CMtlTex *tex = NewMtlTex( MTLTextureTypeCube, Format, EdgeLength, EdgeLength, 1, Levels, ( Usage & D3DUSAGE_AUTOGENMIPMAP ) != 0, TexFlagsForUsage( Usage ), 1, pDebugLabel );
	if ( !tex )
		return D3DERR_OUTOFVIDEOMEMORY;

	IDirect3DCubeTexture9 *dxtex = new IDirect3DCubeTexture9;
	dxtex->m_restype = D3DRTYPE_CUBETEXTURE;
	dxtex->m_device = this;
	dxtex->m_descZero = TextureDesc( Format, D3DRTYPE_CUBETEXTURE, Usage, Pool, EdgeLength, EdgeLength );
	dxtex->m_tex = tex;
	for ( int face = 0; face < 6; face++ )
	{
		dxtex->m_surfZero[face] = NewSurfaceRef( this, dxtex->m_descZero, tex, face, 0 );
	}

	*ppCubeTexture = dxtex;
	return S_OK;
}

IDirect3DCubeTexture9::~IDirect3DCubeTexture9()
{
	if ( m_device )
	{
		if ( g_pMtlDevice )
		{
			g_pMtlDevice->ReleasedTexture( this );
		}
		for ( int face = 0; face < 6; face++ )
		{
			if ( m_surfZero[face] )
			{
				m_surfZero[face]->Release( 0 );
				m_surfZero[face] = NULL;
			}
		}
	}
}

HRESULT IDirect3DCubeTexture9::GetCubeMapSurface( D3DCUBEMAP_FACES FaceType, UINT Level, IDirect3DSurface9** ppCubeMapSurface )
{
	// The D3D face order (+X, -X, +Y, -Y, +Z, -Z) is Metal's slice order
	*ppCubeMapSurface = NewSurfaceRef( m_device, m_descZero, m_tex, FaceType, Level );
	return S_OK;
}

HRESULT IDirect3DCubeTexture9::GetLevelDesc( UINT Level, D3DSURFACE_DESC *pDesc )
{
	return IDirect3DBaseTexture9::GetLevelDesc( Level, pDesc );
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DVolumeTexture9
// ------------------------------------------------------------------------------------------------------------------------------ //

HRESULT IDirect3DDevice9::CreateVolumeTexture( UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, VD3DHANDLE* pSharedHandle, char *pDebugLabel )
{
	*ppVolumeTexture = NULL;
	CMtlTex *tex = NewMtlTex( MTLTextureType3D, Format, Width, Height, Depth, Levels, ( Usage & D3DUSAGE_AUTOGENMIPMAP ) != 0, TexFlagsForUsage( Usage ), 1, pDebugLabel );
	if ( !tex )
		return D3DERR_OUTOFVIDEOMEMORY;

	IDirect3DVolumeTexture9 *dxtex = new IDirect3DVolumeTexture9;
	dxtex->m_restype = D3DRTYPE_VOLUMETEXTURE;
	dxtex->m_device = this;
	dxtex->m_descZero = TextureDesc( Format, D3DRTYPE_VOLUMETEXTURE, Usage, Pool, Width, Height );
	dxtex->m_tex = tex;

	memset( &dxtex->m_volDescZero, 0, sizeof( dxtex->m_volDescZero ) );
	dxtex->m_volDescZero.Format = Format;
	dxtex->m_volDescZero.Type = D3DRTYPE_VOLUMETEXTURE;
	dxtex->m_volDescZero.Usage = Usage;
	dxtex->m_volDescZero.Pool = Pool;
	dxtex->m_volDescZero.Width = Width;
	dxtex->m_volDescZero.Height = Height;
	dxtex->m_volDescZero.Depth = Depth;

	dxtex->m_surfZero = NewSurfaceRef( this, dxtex->m_descZero, tex, 0, 0 );

	*ppVolumeTexture = dxtex;
	return S_OK;
}

IDirect3DVolumeTexture9::~IDirect3DVolumeTexture9()
{
	if ( m_device )
	{
		if ( g_pMtlDevice )
		{
			g_pMtlDevice->ReleasedTexture( this );
		}
		if ( m_surfZero )
		{
			m_surfZero->Release( 0 );
			m_surfZero = NULL;
		}
	}
}

HRESULT IDirect3DVolumeTexture9::LockBox( UINT Level, D3DLOCKED_BOX* pLockedVolume, CONST D3DBOX* pBox, DWORD Flags )
{
	uint x0 = pBox ? pBox->Left : 0, y0 = pBox ? pBox->Top : 0, z0 = pBox ? pBox->Front : 0;
	uint x1 = pBox ? pBox->Right : m_tex->MipWidth( Level ), y1 = pBox ? pBox->Bottom : m_tex->MipHeight( Level ), z1 = pBox ? pBox->Back : m_tex->MipDepth( Level );
	int pitch, slicePitch;
	HRESULT hr = LockSlice( m_tex, 0, Level, x0, y0, z0, x1, y1, z1, Flags, &pLockedVolume->pBits, &pitch, &slicePitch );
	pLockedVolume->RowPitch = pitch;
	pLockedVolume->SlicePitch = slicePitch;
	return hr;
}

HRESULT IDirect3DVolumeTexture9::UnlockBox( UINT Level )
{
	return UnlockSlice( m_tex, 0, Level );
}

HRESULT IDirect3DVolumeTexture9::GetLevelDesc( UINT Level, D3DVOLUME_DESC *pDesc )
{
	D3DVOLUME_DESC result = m_volDescZero;
	result.Width = m_tex->MipWidth( Level );
	result.Height = m_tex->MipHeight( Level );
	result.Depth = m_tex->MipDepth( Level );
	*pDesc = result;
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DSurface9
// ------------------------------------------------------------------------------------------------------------------------------ //

IDirect3DSurface9::~IDirect3DSurface9()
{
	if ( m_device )
	{
		if ( g_pMtlDevice )
		{
			g_pMtlDevice->ReleasedSurface( this );
		}
		if ( m_restype == D3DRTYPE_SURFACE )
		{
			DeleteMtlTex( m_tex );
		}
		m_tex = NULL;
		m_device = NULL;
	}
}

HRESULT IDirect3DSurface9::LockRect( D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags )
{
	uint x0 = pRect ? pRect->left : 0, y0 = pRect ? pRect->top : 0;
	uint x1 = pRect ? pRect->right : m_tex->MipWidth( m_mip ), y1 = pRect ? pRect->bottom : m_tex->MipHeight( m_mip );
	int pitch;
	HRESULT hr = LockSlice( m_tex, m_face, m_mip, x0, y0, 0, x1, y1, 1, Flags, &pLockedRect->pBits, &pitch, NULL );
	pLockedRect->Pitch = pitch;
	return hr;
}

HRESULT IDirect3DSurface9::UnlockRect()
{
	return UnlockSlice( m_tex, m_face, m_mip );
}

HRESULT IDirect3DSurface9::GetDesc( D3DSURFACE_DESC *pDesc )
{
	*pDesc = m_desc;
	return S_OK;
}

static IDirect3DSurface9 *NewOwningSurface( IDirect3DDevice9 *pDevice, CMtlTex *pTex, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, UINT Width, UINT Height )
{
	IDirect3DSurface9 *surf = new IDirect3DSurface9;
	surf->m_restype = D3DRTYPE_SURFACE;
	surf->m_device = pDevice;
	surf->m_tex = pTex;
	surf->m_face = 0;
	surf->m_mip = 0;
	memset( &surf->m_desc, 0, sizeof( surf->m_desc ) );
	surf->m_desc.Format = Format;
	surf->m_desc.Type = D3DRTYPE_SURFACE;
	surf->m_desc.Usage = 0;
	surf->m_desc.Pool = D3DPOOL_DEFAULT;
	surf->m_desc.MultiSampleType = MultiSample;
	surf->m_desc.MultiSampleQuality = MultisampleQuality;
	surf->m_desc.Width = Width;
	surf->m_desc.Height = Height;
	return surf;
}

HRESULT IDirect3DDevice9::CreateRenderTarget( UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, VD3DHANDLE* pSharedHandle, char *pDebugLabel )
{
	*ppSurface = NULL;
	CMtlTex *tex = NewMtlTex( MTLTextureType2D, Format, Width, Height, 1, 1, false, kMtlTexRenderable, MultiSample ? MultiSample : 1, pDebugLabel );
	if ( !tex )
		return D3DERR_OUTOFVIDEOMEMORY;
	*ppSurface = NewOwningSurface( this, tex, Format, MultiSample, MultisampleQuality, Width, Height );
	return S_OK;
}

HRESULT IDirect3DDevice9::CreateOffscreenPlainSurface( UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, VD3DHANDLE* pSharedHandle )
{
	// Like ToGL, a renderable surface, so StretchRect and GetRenderTargetData can copy into it
	*ppSurface = NULL;
	CMtlTex *tex = NewMtlTex( MTLTextureType2D, Format, Width, Height, 1, 1, false, kMtlTexRenderable, 1, "offscreen plain surface" );
	if ( !tex )
		return D3DERR_OUTOFVIDEOMEMORY;
	*ppSurface = NewOwningSurface( this, tex, Format, D3DMULTISAMPLE_NONE, 0, Width, Height );
	return S_OK;
}

HRESULT IDirect3DDevice9::CreateDepthStencilSurface( UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, VD3DHANDLE* pSharedHandle )
{
	*ppSurface = NULL;
	CMtlTex *tex = NewMtlTex( MTLTextureType2D, Format, Width, Height, 1, 1, false, kMtlTexRenderable, MultiSample ? MultiSample : 1, "depth-stencil surface" );
	if ( !tex )
		return D3DERR_OUTOFVIDEOMEMORY;
	*ppSurface = NewOwningSurface( this, tex, Format, MultiSample, MultisampleQuality, Width, Height );
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Vertex and index buffers
// ------------------------------------------------------------------------------------------------------------------------------ //

static CMtlBuffer *NewMtlBuffer( uint nSize, DWORD Usage )
{
	CMtlBuffer *pBuf = new CMtlBuffer;
	pBuf->m_nSize = MAX( 4u, nSize );
	pBuf->m_buf = g_pMtlDevice->NewBuffer( pBuf->m_nSize );
	pBuf->m_nLastUseSerial = 0;
	pBuf->m_bDynamic = ( Usage & D3DUSAGE_DYNAMIC ) != 0;
	pBuf->m_bIndex32 = false;
	pBuf->m_pStaging = NULL;
	pBuf->m_nLockOffset = pBuf->m_nLockSize = 0;
	return pBuf;
}

static void DeleteMtlBuffer( CMtlBuffer *pBuf )
{
	if ( !pBuf )
		return;
	// Dynamic buffers go back to the pool; the command buffers still using a
	// static one keep it alive
	if ( g_pMtlDevice && pBuf->m_bDynamic )
	{
		g_pMtlDevice->RetireBuffer( pBuf->m_buf, pBuf->m_nLastUseSerial );
	}
	delete pBuf;
}

// D3D buffer lock semantics on shared memory:
//   D3DLOCK_DISCARD: if the GPU may still read the buffer, switch to a fresh one.
//   D3DLOCK_NOOVERWRITE: the caller promises not to touch data in use, write in place.
//   neither: D3D would wait for the GPU; switch to a fresh copy of the buffer instead.
static void *LockMtlBuffer( CMtlBuffer *pBuf, uint offset, uint size, DWORD flags )
{
	if ( size == 0 )
	{
		size = pBuf->m_nSize - offset;
	}
	if ( offset + size > pBuf->m_nSize )
		return NULL;

	if ( !( flags & D3DLOCK_NOOVERWRITE ) && !( flags & D3DLOCK_READONLY ) && g_pMtlDevice->IsBusy( pBuf->m_nLastUseSerial ) )
	{
		id<MTLBuffer> fresh = g_pMtlDevice->NewBuffer( pBuf->m_nSize );
		if ( !( flags & D3DLOCK_DISCARD ) )
		{
			memcpy( [fresh contents], [pBuf->m_buf contents], pBuf->m_nSize );
		}
		g_pMtlDevice->RetireBuffer( pBuf->m_buf, pBuf->m_nLastUseSerial );
		pBuf->m_buf = fresh;
		pBuf->m_nLastUseSerial = 0;
	}

	pBuf->m_nLockOffset = offset;
	pBuf->m_nLockSize = size;
	return (uint8*)[pBuf->m_buf contents] + offset;
}

HRESULT IDirect3DDevice9::CreateVertexBuffer( UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, VD3DHANDLE* pSharedHandle )
{
	IDirect3DVertexBuffer9 *vb = new IDirect3DVertexBuffer9;
	vb->m_restype = D3DRTYPE_VERTEXBUFFER;
	vb->m_device = this;
	vb->m_vtxBuffer = NewMtlBuffer( Length, Usage );

	memset( &vb->m_vtxDesc, 0, sizeof( vb->m_vtxDesc ) );
	vb->m_vtxDesc.Format = D3DFMT_VERTEXDATA;
	vb->m_vtxDesc.Type = D3DRTYPE_VERTEXBUFFER;
	vb->m_vtxDesc.Usage = Usage;
	vb->m_vtxDesc.Pool = Pool;
	vb->m_vtxDesc.Size = Length;
	vb->m_vtxDesc.FVF = FVF;

	*ppVertexBuffer = vb;
	return S_OK;
}

IDirect3DVertexBuffer9::~IDirect3DVertexBuffer9()
{
	if ( g_pMtlDevice )
	{
		g_pMtlDevice->ReleasedVertexBuffer( this );
	}
	DeleteMtlBuffer( m_vtxBuffer );
	m_vtxBuffer = NULL;
}

HRESULT IDirect3DVertexBuffer9::Lock( UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags )
{
	*ppbData = LockMtlBuffer( m_vtxBuffer, OffsetToLock, SizeToLock, Flags );
	return *ppbData ? S_OK : D3DERR_INVALIDCALL;
}

HRESULT IDirect3DVertexBuffer9::Unlock()
{
	// Shared memory is coherent; nothing to flush
	return S_OK;
}

void IDirect3DVertexBuffer9::UnlockActualSize( uint nActualSize, const void *pActualData )
{
	if ( pActualData )
	{
		memcpy( (uint8*)[m_vtxBuffer->m_buf contents] + m_vtxBuffer->m_nLockOffset, pActualData, MIN( nActualSize, m_vtxBuffer->m_nLockSize ) );
	}
}

HRESULT IDirect3DDevice9::CreateIndexBuffer( UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, VD3DHANDLE* pSharedHandle )
{
	IDirect3DIndexBuffer9 *ib = new IDirect3DIndexBuffer9;
	ib->m_restype = D3DRTYPE_INDEXBUFFER;
	ib->m_device = this;
	ib->m_idxBuffer = NewMtlBuffer( Length, Usage );
	ib->m_idxBuffer->m_bIndex32 = ( Format == D3DFMT_INDEX32 );

	memset( &ib->m_idxDesc, 0, sizeof( ib->m_idxDesc ) );
	ib->m_idxDesc.Format = Format;
	ib->m_idxDesc.Type = D3DRTYPE_INDEXBUFFER;
	ib->m_idxDesc.Usage = Usage;
	ib->m_idxDesc.Pool = Pool;
	ib->m_idxDesc.Size = Length;

	*ppIndexBuffer = ib;
	return S_OK;
}

IDirect3DIndexBuffer9::~IDirect3DIndexBuffer9()
{
	if ( g_pMtlDevice )
	{
		g_pMtlDevice->ReleasedIndexBuffer( this );
	}
	DeleteMtlBuffer( m_idxBuffer );
	m_idxBuffer = NULL;
}

HRESULT IDirect3DIndexBuffer9::Lock( UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags )
{
	*ppbData = LockMtlBuffer( m_idxBuffer, OffsetToLock, SizeToLock, Flags );
	return *ppbData ? S_OK : D3DERR_INVALIDCALL;
}

HRESULT IDirect3DIndexBuffer9::Unlock()
{
	return S_OK;
}

void IDirect3DIndexBuffer9::UnlockActualSize( uint nActualSize, const void *pActualData )
{
	if ( pActualData )
	{
		memcpy( (uint8*)[m_idxBuffer->m_buf contents] + m_idxBuffer->m_nLockOffset, pActualData, MIN( nActualSize, m_idxBuffer->m_nLockSize ) );
	}
}

HRESULT IDirect3DIndexBuffer9::GetDesc( D3DINDEXBUFFER_DESC *pDesc )
{
	*pDesc = m_idxDesc;
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Vertex declarations
// ------------------------------------------------------------------------------------------------------------------------------ //

static MTLVertexFormat VertexFormat( BYTE type )
{
	// D3DCOLOR arrives as RGBA bytes: with OPENGL_SWAP_COLORS the shader API
	// writes colors in that order, as it does for ToGL.
	switch ( type )
	{
	case D3DDECLTYPE_FLOAT1:	return MTLVertexFormatFloat;
	case D3DDECLTYPE_FLOAT2:	return MTLVertexFormatFloat2;
	case D3DDECLTYPE_FLOAT3:	return MTLVertexFormatFloat3;
	case D3DDECLTYPE_FLOAT4:	return MTLVertexFormatFloat4;
	case D3DDECLTYPE_D3DCOLOR:	return MTLVertexFormatUChar4Normalized;
	case D3DDECLTYPE_UBYTE4:	return MTLVertexFormatUChar4;
	case D3DDECLTYPE_UBYTE4N:	return MTLVertexFormatUChar4Normalized;
	case D3DDECLTYPE_SHORT2:	return MTLVertexFormatShort2;
	case D3DDECLTYPE_SHORT4:	return MTLVertexFormatShort4;
	case D3DDECLTYPE_SHORT2N:	return MTLVertexFormatShort2Normalized;
	case D3DDECLTYPE_SHORT4N:	return MTLVertexFormatShort4Normalized;
	case D3DDECLTYPE_USHORT2N:	return MTLVertexFormatUShort2Normalized;
	case D3DDECLTYPE_USHORT4N:	return MTLVertexFormatUShort4Normalized;
	case D3DDECLTYPE_FLOAT16_2:	return MTLVertexFormatHalf2;
	case D3DDECLTYPE_FLOAT16_4:	return MTLVertexFormatHalf4;
	default:					return MTLVertexFormatInvalid;
	}
}

HRESULT IDirect3DDevice9::CreateVertexDeclaration( CONST D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl )
{
	*ppDecl = NULL;

	CMtlVertexDecl *pDecl = new CMtlVertexDecl;
	memset( pDecl, 0, sizeof( *pDecl ) );
	memset( pDecl->m_usageToElement, 0xFF, sizeof( pDecl->m_usageToElement ) );

	for ( const D3DVERTEXELEMENT9 *src = pVertexElements; src->Stream != 0xFF; src++ )
	{
		if ( pDecl->m_nElements >= MAX_D3DVERTEXELEMENTS || src->Stream >= MTL_MAX_STREAMS )
		{
			delete pDecl;
			return D3DERR_INVALIDCALL;
		}
		MTLVertexFormat fmt = VertexFormat( src->Type );
		if ( fmt == MTLVertexFormatInvalid )
		{
			Warning( "ToMetal: unsupported vertex element type %d\n", src->Type );
			delete pDecl;
			return D3DERR_INVALIDCALL;
		}

		uint j = pDecl->m_nElements++;
		pDecl->m_elements[j] = *src;
		pDecl->m_formats[j] = fmt;
		pDecl->m_usageToElement[ ( src->Usage << 4 ) | src->UsageIndex ] = j;
		pDecl->m_nStreamMask |= 1 << src->Stream;
	}

	// Hash for pipeline keys
	uint32 hash = 2166136261u;
	const uint8 *p = (const uint8*)pDecl->m_elements;
	for ( uint i = 0; i < pDecl->m_nElements * sizeof( D3DVERTEXELEMENT9 ); i++ )
	{
		hash = ( hash ^ p[i] ) * 16777619u;
	}
	pDecl->m_nHash = hash ^ pDecl->m_nElements;

	IDirect3DVertexDeclaration9 *decl9 = new IDirect3DVertexDeclaration9;
	decl9->m_device = this;
	decl9->m_decl = pDecl;
	*ppDecl = decl9;
	return S_OK;
}

IDirect3DVertexDeclaration9::~IDirect3DVertexDeclaration9()
{
	if ( g_pMtlDevice )
	{
		g_pMtlDevice->ReleasedVertexDecl( this );
	}
	delete m_decl;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Queries
// ------------------------------------------------------------------------------------------------------------------------------ //

HRESULT IDirect3DDevice9::CreateQuery( D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery )
{
	*ppQuery = NULL;
	if ( Type != D3DQUERYTYPE_OCCLUSION && Type != D3DQUERYTYPE_EVENT )
		return D3DERR_NOTAVAILABLE;

	IDirect3DQuery9 *q = new IDirect3DQuery9;
	q->m_device = this;
	q->m_restype = (D3DRESOURCETYPE)0;
	q->m_type = Type;
	q->m_query = new CMtlQuery;
	q->m_query->m_type = Type;
	q->m_query->m_bStarted = q->m_query->m_bStopped = q->m_query->m_bActive = false;
	q->m_query->m_nEndSerial = 0;
	q->m_query->m_nIssueStartDrawCallIndex = q->m_query->m_nIssueEndDrawCallIndex = 0;
	q->m_query->m_nIssueStartThreadID = q->m_query->m_nIssueEndThreadID = 0;
	q->m_query->m_nIssueStartFrameIndex = q->m_query->m_nIssueEndFrameIndex = 0;
	*ppQuery = q;
	return S_OK;
}

IDirect3DQuery9::~IDirect3DQuery9()
{
	if ( g_pMtlDevice )
	{
		g_pMtlDevice->ReleasedQuery( m_query );
	}
	delete m_query;
}

HRESULT IDirect3DQuery9::Issue( DWORD dwIssueFlags )
{
	CMtlQuery *q = m_query;
	CMtlDevice *dev = g_pMtlDevice;

	if ( dwIssueFlags & D3DISSUE_BEGIN )
	{
		q->m_nIssueStartThreadID = ThreadGetCurrentId();
		q->m_nIssueStartDrawCallIndex = dev->m_nDrawCallIndex;
		q->m_nIssueStartFrameIndex = dev->m_nFrame;
		if ( m_type == D3DQUERYTYPE_OCCLUSION )
		{
			dev->BeginOcclusion( q );
			q->m_bStarted = true;
			q->m_bStopped = false;
		}
	}

	if ( dwIssueFlags & D3DISSUE_END )
	{
		q->m_nIssueEndThreadID = ThreadGetCurrentId();
		q->m_nIssueEndDrawCallIndex = dev->m_nDrawCallIndex;
		q->m_nIssueEndFrameIndex = dev->m_nFrame;
		if ( m_type == D3DQUERYTYPE_OCCLUSION )
		{
			dev->EndOcclusion( q );
		}
		else
		{
			// An event marks a point in the command stream; End places it
			q->m_nIssueStartThreadID = q->m_nIssueEndThreadID;
			q->m_nIssueStartDrawCallIndex = q->m_nIssueEndDrawCallIndex;
			q->m_nIssueStartFrameIndex = q->m_nIssueEndFrameIndex;
			q->m_bStarted = true;
		}
		q->m_bStopped = true;
		q->m_nEndSerial = dev->CurrentSerial();
	}
	return S_OK;
}

HRESULT IDirect3DQuery9::GetData( void* pData, DWORD dwSize, DWORD dwGetDataFlags )
{
	CMtlQuery *q = m_query;
	CMtlDevice *dev = g_pMtlDevice;

	if ( pData )
	{
		*(uint*)pData = 0;
	}

	if ( !q->m_bStarted || !q->m_bStopped )
		return S_FALSE;

	bool bFlush = ( dwGetDataFlags & D3DGETDATA_FLUSH ) != 0;

	if ( m_type == D3DQUERYTYPE_OCCLUSION )
	{
		// The cases ToGL refuses, with the same non-standard result so the
		// caller behaves the same: issued and read on different threads, no
		// draws inside, or spanning frames.
		ThreadId_t nCurThreadId = ThreadGetCurrentId();
		if ( q->m_nIssueStartThreadID != nCurThreadId || q->m_nIssueEndThreadID != nCurThreadId ||
			 q->m_nIssueStartDrawCallIndex == q->m_nIssueEndDrawCallIndex || q->m_nIssueStartFrameIndex != q->m_nIssueEndFrameIndex )
		{
			return D3DERR_NOTAVAILABLE;
		}
	}

	if ( dev->IsBusy( q->m_nEndSerial ) )
	{
		if ( !bFlush )
			return S_FALSE;

		if ( q->m_nEndSerial == dev->CurrentSerial() )
		{
			dev->Submit( false );
		}
		if ( m_type == D3DQUERYTYPE_OCCLUSION )
		{
			// As in ToGL, a flushed occlusion query waits for its answer
			dev->WaitForSerial( q->m_nEndSerial );
		}
		else if ( dev->IsBusy( q->m_nEndSerial ) )
		{
			return S_FALSE;
		}
	}

	if ( m_type == D3DQUERYTYPE_OCCLUSION && pData )
	{
		*(uint*)pData = dev->OcclusionResult( q );
	}
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Shaders
// ------------------------------------------------------------------------------------------------------------------------------ //

CMtlShader::CMtlShader() :
	m_bVertex( false ), m_bValid( false ), m_pCode( NULL ), m_nCodeDWords( 0 ), m_pDebugLabel( NULL ),
	m_nShadowMask( 0 ), m_nCentroidMask( 0 ), m_nId( 0 )
{
	m_name[0] = 0;
	memset( &m_info, 0, sizeof( m_info ) );
}

CMtlShader::~CMtlShader()
{
	delete[] m_pCode;
	delete[] m_pDebugLabel;
}

// Length of a D3D9 token stream up to and including the end token
static uint ShaderLengthDWords( const DWORD *pFunction )
{
	uint i = 1;		// version token
	for ( ;; )
	{
		DWORD tok = pFunction[i];
		if ( tok == 0x0000FFFF )
			return i + 1;
		if ( ( tok & 0xFFFF ) == D3DSIO_COMMENT )
		{
			i += 1 + ( ( tok & 0x7FFF0000 ) >> 16 );
			continue;
		}
		i++;
	}
}

static CMtlShader *NewMtlShader( bool bVertex, const DWORD *pFunction, const char *pShaderName, const char *pDebugLabel )
{
	CMtlShader *s = new CMtlShader;
	s->m_bVertex = bVertex;
	s->m_nCodeDWords = ShaderLengthDWords( pFunction );
	s->m_pCode = new uint32[ s->m_nCodeDWords ];
	memcpy( s->m_pCode, pFunction, s->m_nCodeDWords * sizeof( uint32 ) );
	V_strncpy( s->m_name, pShaderName ? pShaderName : "", sizeof( s->m_name ) );
	if ( pDebugLabel )
	{
		s->m_pDebugLabel = V_strdup( pDebugLabel );
	}
	s->m_nShadowMask = bVertex ? 0 : D3DToMetalShadowDepthSamplerMaskFromName( pShaderName );
	s->m_nCentroidMask = D3DToMetalCentroidMaskFromName( !bVertex, pShaderName );
	s->m_nId = ++g_pMtlDevice->m_nNextShaderId;

	// Translate now for the shader's interface (inputs, outputs, samplers);
	// the program is compiled when a pipeline first needs it.
	D3DToMetal xlat;
	CUtlBuffer msl( 0, 0, CUtlBuffer::TEXT_BUFFER );
	s->m_bValid = xlat.TranslateShader( s->m_pCode, &msl, &s->m_info, 0, s->m_nShadowMask, s->m_nCentroidMask, 0, pDebugLabel ? pDebugLabel : pShaderName );
	if ( !s->m_bValid )
	{
		Warning( "ToMetal: can't translate %s shader %s (dx9asmtometal.cpp line %d); draws using it are skipped\n",
			bVertex ? "vertex" : "pixel", pDebugLabel ? pDebugLabel : s->m_name, D3DToMetal::GetLastErrorLine() );
	}
	else if ( mtl_shader_spew.GetBool() )
	{
		Msg( "%.*s\n", (int)msl.TellPut(), (const char*)msl.Base() );
	}
	return s;
}

HRESULT IDirect3DDevice9::CreatePixelShader( CONST DWORD* pFunction, IDirect3DPixelShader9** ppShader, const char *pShaderName, char *pDebugLabel, const uint32 *pCentroidMask )
{
	IDirect3DPixelShader9 *ps = new IDirect3DPixelShader9;
	ps->m_device = this;
	ps->m_restype = (D3DRESOURCETYPE)0;
	ps->m_shader = NewMtlShader( false, (const DWORD*)pFunction, pShaderName, pDebugLabel );
	*ppShader = ps;
	return S_OK;
}

IDirect3DPixelShader9::~IDirect3DPixelShader9()
{
	if ( g_pMtlDevice )
	{
		g_pMtlDevice->ReleasedShader( m_shader );
	}
	delete m_shader;
}

HRESULT IDirect3DDevice9::CreateVertexShader( CONST DWORD* pFunction, IDirect3DVertexShader9** ppShader, const char *pShaderName, char *pDebugLabel )
{
	IDirect3DVertexShader9 *vs = new IDirect3DVertexShader9;
	vs->m_device = this;
	vs->m_restype = (D3DRESOURCETYPE)0;
	vs->m_shader = NewMtlShader( true, (const DWORD*)pFunction, pShaderName, pDebugLabel );
	*ppShader = vs;
	return S_OK;
}

IDirect3DVertexShader9::~IDirect3DVertexShader9()
{
	if ( g_pMtlDevice )
	{
		g_pMtlDevice->ReleasedShader( m_shader );
	}
	delete m_shader;
}

HRESULT IDirect3DDevice9::LinkShaderPair( IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps )
{
	return S_OK;
}

HRESULT IDirect3DDevice9::ValidateShaderPair( IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps )
{
	return S_OK;
}

HRESULT IDirect3DDevice9::QueryShaderPair( int index, GLMShaderPairInfo *infoOut )
{
	memset( infoOut, 0, sizeof( *infoOut ) );
	infoOut->m_status = -1;		// no pairs; ends the caller's loop
	return S_OK;
}
