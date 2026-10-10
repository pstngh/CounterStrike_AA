//================ Copyright (c) Valve Corporation. All Rights Reserved. =================
//
// mtld3ddevice.mm
//
// IDirect3DDevice9 on Metal: the D3D state the shader API sets, recorded for
// CMtlDevice to turn into pipelines, passes and draws.
//
//==================================================================================================
#include "mtlinternal.h"
#include "tier0/icommandline.h"
#include "tier1/convar.h"
#include "appframework/ilaunchermgr.h"


#include "tier0/memdbgon.h"

#define D3D_DEVICE_VALID_MARKER 0x12EBC845

static ConVar mat_vsync( "mat_vsync", "0", 0, "Force sync to vertical retrace", true, 0.0, true, 1.0 );

// ------------------------------------------------------------------------------------------------------------------------------ //
// Create / Reset
// ------------------------------------------------------------------------------------------------------------------------------ //

IDirect3DDevice9::IDirect3DDevice9() :
	m_mtl( NULL ), m_nValidMarker( D3D_DEVICE_VALID_MARKER ), m_nCurOwnerThreadId( ThreadGetCurrentId() ),
	m_pDefaultColorSurface( NULL ), m_pDefaultDepthStencilSurface( NULL )
{
	memset( &m_params, 0, sizeof( m_params ) );
}

IDirect3DDevice9::~IDirect3DDevice9()
{
	if ( m_mtl )
	{
		for ( int i = 0; i < MTL_MAX_RENDER_TARGETS; i++ )
			SetRenderTarget( i, NULL );
		SetDepthStencilSurface( NULL );
		if ( m_pDefaultColorSurface )
		{
			m_pDefaultColorSurface->Release( 0 );
			m_pDefaultColorSurface = NULL;
		}
		if ( m_pDefaultDepthStencilSurface )
		{
			m_pDefaultDepthStencilSurface->Release( 0 );
			m_pDefaultDepthStencilSurface = NULL;
		}
		m_mtl->Shutdown();
		if ( g_pMtlDevice == m_mtl )
		{
			g_pMtlDevice = NULL;
		}
		delete m_mtl;
		m_mtl = NULL;
	}
	m_nValidMarker = 0;
}

// The D3D9 defaults, except the scissor test, which ToGL turns on with a
// backbuffer-sized box
static void InitStates( CMtlDevice *dev, uint width, uint height )
{
	DWORD *rs = dev->m_renderStates;
	memset( rs, 0, sizeof( dev->m_renderStates ) );
	rs[D3DRS_ZENABLE] = D3DZB_TRUE;
	rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
	rs[D3DRS_ZWRITEENABLE] = TRUE;
	rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
	rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
	rs[D3DRS_CULLMODE] = D3DCULL_CCW;
	rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
	rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
	rs[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
	rs[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
	rs[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
	rs[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
	rs[D3DRS_STENCILMASK] = 0xFFFFFFFF;
	rs[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF;
	rs[D3DRS_COLORWRITEENABLE] = 0xF;
	rs[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
	rs[D3DRS_SCISSORTESTENABLE] = TRUE;

	dev->m_scissorRect.left = 0;
	dev->m_scissorRect.top = 0;
	dev->m_scissorRect.right = width;
	dev->m_scissorRect.bottom = height;

	dev->m_viewport.X = 0;
	dev->m_viewport.Y = 0;
	dev->m_viewport.Width = width;
	dev->m_viewport.Height = height;
	dev->m_viewport.MinZ = 0.0f;
	dev->m_viewport.MaxZ = 1.0f;

	for ( int i = 0; i < MTL_SAMPLER_COUNT; i++ )
	{
		MtlSamplerState &s = dev->m_samplers[i].m_state;
		memset( &s, 0, sizeof( s ) );
		s.m_addressU = s.m_addressV = s.m_addressW = D3DTADDRESS_WRAP;
		s.m_minFilter = s.m_magFilter = D3DTEXF_POINT;
		s.m_mipFilter = D3DTEXF_NONE;
		s.m_maxAniso = 1;
	}

	dev->m_bDynamicStateDirty = true;
	dev->m_bPassDirty = true;
}

HRESULT IDirect3DDevice9::CreateDefaultSurfaces()
{
	const D3DPRESENT_PARAMETERS &pp = m_params.m_presentationParameters;

	HRESULT result = CreateRenderTarget( pp.BackBufferWidth, pp.BackBufferHeight, pp.BackBufferFormat, pp.MultiSampleType, pp.MultiSampleQuality, true, &m_pDefaultColorSurface, NULL, (char*)"InternalRT0" );
	if ( result != S_OK )
		return result;
	SetDepthStencilSurface( NULL );
	result = SetRenderTarget( 0, m_pDefaultColorSurface );
	if ( result != S_OK )
		return result;

	result = CreateDepthStencilSurface( pp.BackBufferWidth, pp.BackBufferHeight, pp.AutoDepthStencilFormat, pp.MultiSampleType, pp.MultiSampleQuality, TRUE, &m_pDefaultDepthStencilSurface, NULL );
	if ( result != S_OK )
		return result;
	result = SetDepthStencilSurface( m_pDefaultDepthStencilSurface );
	if ( result != S_OK )
		return result;

	InitStates( m_mtl, pp.BackBufferWidth, pp.BackBufferHeight );

	// so GetClientRect can return sane answers
	uint width = pp.BackBufferWidth, height = pp.BackBufferHeight;
	if ( g_pLauncherMgr )
	{
		g_pLauncherMgr->RenderedSize( width, height, true );
	}
	return S_OK;
}

HRESULT IDirect3DDevice9::Create( IDirect3DDevice9Params *params )
{
	m_params = *params;

	m_mtl = new CMtlDevice;
	g_pMtlDevice = m_mtl;
	if ( !m_mtl->Init( this, &params->m_presentationParameters ) )
	{
		g_pMtlDevice = NULL;
		delete m_mtl;
		m_mtl = NULL;
		return E_FAIL;
	}

	return CreateDefaultSurfaces();
}

HRESULT IDirect3DDevice9::Reset( D3DPRESENT_PARAMETERS* pPresentationParameters )
{
	m_params.m_presentationParameters = *pPresentationParameters;

	SetDepthStencilSurface( NULL );
	for ( int i = 0; i < MTL_MAX_RENDER_TARGETS; i++ )
		SetRenderTarget( i, NULL );
	if ( m_pDefaultColorSurface )
	{
		m_pDefaultColorSurface->Release( 0 );
		m_pDefaultColorSurface = NULL;
	}
	if ( m_pDefaultDepthStencilSurface )
	{
		m_pDefaultDepthStencilSurface->Release( 0 );
		m_pDefaultDepthStencilSurface = NULL;
	}

	return CreateDefaultSurfaces();
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Basics
// ------------------------------------------------------------------------------------------------------------------------------ //

HRESULT IDirect3DDevice9::SetViewport( CONST D3DVIEWPORT9* pViewport )
{
	m_mtl->m_viewport = *pViewport;
	m_mtl->m_bDynamicStateDirty = true;
	return S_OK;
}

HRESULT IDirect3DDevice9::GetViewport( D3DVIEWPORT9* pViewport )
{
	*pViewport = m_mtl->m_viewport;
	return S_OK;
}

HRESULT IDirect3DDevice9::BeginScene()
{
	return S_OK;
}

HRESULT IDirect3DDevice9::EndScene()
{
	return S_OK;
}

HRESULT IDirect3DDevice9::Clear( DWORD Count, CONST D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil )
{
	m_mtl->Clear( Count, pRects, Flags, Color, Z, Stencil );
	return S_OK;
}

HRESULT IDirect3DDevice9::Present( CONST RECT* pSourceRect, CONST RECT* pDestRect, VD3DHWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion )
{
	m_mtl->Present( m_pDefaultColorSurface, mat_vsync.GetBool() );
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Textures and samplers
// ------------------------------------------------------------------------------------------------------------------------------ //

HRESULT IDirect3DDevice9::SetTexture( DWORD Stage, IDirect3DBaseTexture9* pTexture )
{
	if ( Stage >= MTL_SAMPLER_COUNT )
		return D3DERR_INVALIDCALL;
	m_mtl->m_samplers[Stage].m_pTexture = pTexture;
	return S_OK;
}

HRESULT IDirect3DDevice9::GetTexture( DWORD Stage, IDirect3DBaseTexture9** ppTexture )
{
	if ( Stage >= MTL_SAMPLER_COUNT )
		return D3DERR_INVALIDCALL;
	*ppTexture = m_mtl->m_samplers[Stage].m_pTexture;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetSamplerState( DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value )
{
	if ( Sampler >= MTL_SAMPLER_COUNT )
		return D3DERR_INVALIDCALL;

	MtlSamplerState &s = m_mtl->m_samplers[Sampler].m_state;
	switch ( Type )
	{
	case D3DSAMP_ADDRESSU:		s.m_addressU = (uint8)Value; break;
	case D3DSAMP_ADDRESSV:		s.m_addressV = (uint8)Value; break;
	case D3DSAMP_ADDRESSW:		s.m_addressW = (uint8)Value; break;
	case D3DSAMP_BORDERCOLOR:	s.m_borderColor = Value; break;
	case D3DSAMP_MAGFILTER:		s.m_magFilter = (uint8)Value; break;
	case D3DSAMP_MINFILTER:		s.m_minFilter = (uint8)Value; break;
	case D3DSAMP_MIPFILTER:		s.m_mipFilter = (uint8)Value; break;
	case D3DSAMP_MIPMAPLODBIAS:	break;		// not supported, as in ToGL
	case D3DSAMP_MAXMIPLEVEL:	s.m_minLOD = (uint8)Value; break;
	case D3DSAMP_MAXANISOTROPY:	s.m_maxAniso = (uint8)MIN( Value, 16u ); break;
	case D3DSAMP_SRGBTEXTURE:	s.m_srgb = Value ? 1 : 0; break;
	case D3DSAMP_SHADOWFILTER:	s.m_compare = Value ? 1 : 0; break;
	default:					break;
	}
	return S_OK;
}

void IDirect3DDevice9::SetSamplerStates( DWORD Sampler, DWORD AddressU, DWORD AddressV, DWORD AddressW, DWORD MinFilter, DWORD MagFilter, DWORD MipFilter )
{
	if ( Sampler >= MTL_SAMPLER_COUNT )
		return;
	MtlSamplerState &s = m_mtl->m_samplers[Sampler].m_state;
	s.m_addressU = (uint8)AddressU;
	s.m_addressV = (uint8)AddressV;
	s.m_addressW = (uint8)AddressW;
	s.m_minFilter = (uint8)MinFilter;
	s.m_magFilter = (uint8)MagFilter;
	s.m_mipFilter = (uint8)MipFilter;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Render targets
// ------------------------------------------------------------------------------------------------------------------------------ //

HRESULT IDirect3DDevice9::SetRenderTarget( DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget )
{
	if ( RenderTargetIndex >= MTL_MAX_RENDER_TARGETS )
		return D3DERR_INVALIDCALL;

	IDirect3DSurface9 *pOld = m_mtl->m_pRenderTargets[RenderTargetIndex];
	if ( pOld == pRenderTarget )
		return S_OK;

	// Draws and clears so far belong to the old targets
	m_mtl->FlushPass();

	if ( pRenderTarget )
	{
		pRenderTarget->AddRef( 1 );
	}
	m_mtl->m_pRenderTargets[RenderTargetIndex] = pRenderTarget;
	if ( pOld )
	{
		pOld->Release( 1 );
	}
	m_mtl->m_bPassDirty = true;
	return S_OK;
}

HRESULT IDirect3DDevice9::GetRenderTarget( DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget )
{
	if ( RenderTargetIndex >= MTL_MAX_RENDER_TARGETS || !ppRenderTarget )
		return D3DERR_INVALIDCALL;
	if ( !m_mtl->m_pRenderTargets[RenderTargetIndex] )
		return D3DERR_NOTFOUND;

	m_mtl->m_pRenderTargets[RenderTargetIndex]->AddRef( 0 );
	*ppRenderTarget = m_mtl->m_pRenderTargets[RenderTargetIndex];
	return S_OK;
}

HRESULT IDirect3DDevice9::SetDepthStencilSurface( IDirect3DSurface9* pNewZStencil )
{
	IDirect3DSurface9 *pOld = m_mtl->m_pDepthStencil;
	if ( pOld == pNewZStencil )
		return S_OK;

	m_mtl->FlushPass();

	if ( pNewZStencil )
	{
		pNewZStencil->AddRef( 1 );
	}
	m_mtl->m_pDepthStencil = pNewZStencil;
	if ( pOld )
	{
		pOld->Release( 1 );
	}
	m_mtl->m_bPassDirty = true;
	return S_OK;
}

HRESULT IDirect3DDevice9::GetDepthStencilSurface( IDirect3DSurface9** ppZStencilSurface )
{
	if ( !m_mtl->m_pDepthStencil )
		return D3DERR_NOTFOUND;
	m_mtl->m_pDepthStencil->AddRef( 0 );
	*ppZStencilSurface = m_mtl->m_pDepthStencil;
	return S_OK;
}

HRESULT IDirect3DDevice9::GetRenderTargetData( IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface )
{
	m_mtl->StretchRect( pRenderTarget, NULL, pDestSurface, NULL, D3DTEXF_NONE );
	return S_OK;
}

HRESULT IDirect3DDevice9::GetFrontBufferData( UINT iSwapChain, IDirect3DSurface9* pDestSurface )
{
	DXABSTRACT_BREAK_ON_ERROR();
	return S_OK;
}

HRESULT IDirect3DDevice9::StretchRect( IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface, CONST RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter )
{
	m_mtl->StretchRect( pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter );
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Shaders and constants
// ------------------------------------------------------------------------------------------------------------------------------ //

HRESULT IDirect3DDevice9::SetPixelShader( IDirect3DPixelShader9* pShader )
{
	m_mtl->m_pPixelShader = pShader;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetPixelShaderConstantF( UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount )
{
	if ( StartRegister + Vector4fCount > MTL_PS_CONSTANT_COUNT )
	{
		if ( StartRegister >= MTL_PS_CONSTANT_COUNT )
			return D3DERR_INVALIDCALL;
		Vector4fCount = MTL_PS_CONSTANT_COUNT - StartRegister;
	}
	memcpy( m_mtl->m_psConstants[StartRegister], pConstantData, Vector4fCount * 16 );
	m_mtl->m_bPSConstantsDirty = true;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetPixelShaderConstantB( UINT StartRegister, CONST BOOL* pConstantData, UINT BoolCount )
{
	for ( UINT i = 0; i < BoolCount && StartRegister + i < 16; i++ )
		m_mtl->m_psMisc.m_BoolConsts[StartRegister + i] = pConstantData[i] ? 1 : 0;
	m_mtl->m_bPSMiscDirty = true;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetPixelShaderConstantI( UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount )
{
	for ( UINT i = 0; i < Vector4iCount && StartRegister + i < 16; i++ )
		memcpy( m_mtl->m_psMisc.m_IntConsts[StartRegister + i], pConstantData + i * 4, 16 );
	m_mtl->m_bPSMiscDirty = true;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetVertexShader( IDirect3DVertexShader9* pShader )
{
	m_mtl->m_pVertexShader = pShader;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetVertexShaderConstantF( UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount )
{
	if ( StartRegister + Vector4fCount > MTL_VS_CONSTANT_COUNT )
	{
		if ( StartRegister >= MTL_VS_CONSTANT_COUNT )
			return D3DERR_INVALIDCALL;
		Vector4fCount = MTL_VS_CONSTANT_COUNT - StartRegister;
	}
	memcpy( m_mtl->m_vsConstants[StartRegister], pConstantData, Vector4fCount * 16 );
	m_mtl->m_bVSConstantsDirty = true;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetVertexShaderConstantB( UINT StartRegister, CONST BOOL* pConstantData, UINT BoolCount )
{
	for ( UINT i = 0; i < BoolCount && StartRegister + i < 16; i++ )
		m_mtl->m_vsMisc.m_BoolConsts[StartRegister + i] = pConstantData[i] ? 1 : 0;
	m_mtl->m_bVSMiscDirty = true;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetVertexShaderConstantI( UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount )
{
	for ( UINT i = 0; i < Vector4iCount && StartRegister + i < 16; i++ )
		memcpy( m_mtl->m_vsMisc.m_IntConsts[StartRegister + i], pConstantData + i * 4, 16 );
	m_mtl->m_bVSMiscDirty = true;
	return S_OK;
}

void IDirect3DDevice9::SetMaxUsedVertexShaderConstantsHint( uint nMaxReg )
{
	// Each shader knows the registers it reads; nothing to do
	m_mtl->m_nMaxVSConstantHint = nMaxReg;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Vertex input
// ------------------------------------------------------------------------------------------------------------------------------ //

HRESULT IDirect3DDevice9::SetVertexDeclaration( IDirect3DVertexDeclaration9* pDecl )
{
	m_mtl->m_pVertDecl = pDecl;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetFVF( DWORD FVF )
{
	return S_OK;
}

HRESULT IDirect3DDevice9::GetFVF( DWORD* pFVF )
{
	*pFVF = 0;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetStreamSource( UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride )
{
	if ( StreamNumber >= MTL_MAX_STREAMS )
		return D3DERR_INVALIDCALL;
	D3DStreamDesc &s = m_mtl->m_streams[StreamNumber];
	s.m_vtxBuffer = pStreamData;
	s.m_offset = OffsetInBytes;
	s.m_stride = Stride;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetIndices( IDirect3DIndexBuffer9* pIndexData )
{
	m_mtl->m_pIndices = pIndexData;
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Render state
// ------------------------------------------------------------------------------------------------------------------------------ //

HRESULT IDirect3DDevice9::SetRenderState( D3DRENDERSTATETYPE State, DWORD Value )
{
	if ( (uint)State >= ARRAYSIZE( m_mtl->m_renderStates ) )
		return S_OK;

	DWORD &cur = m_mtl->m_renderStates[State];
	if ( cur == Value )
		return S_OK;

	switch ( State )
	{
	case D3DRS_SRGBWRITEENABLE:
	{
		// Rendering through the sRGB views is a property of the pass
		bool bAffects = false;
		for ( int i = 0; i < MTL_MAX_RENDER_TARGETS; i++ )
		{
			IDirect3DSurface9 *rt = m_mtl->m_pRenderTargets[i];
			bAffects |= rt && rt->m_tex && rt->m_tex->m_texRenderSRGB;
		}
		if ( bAffects )
		{
			m_mtl->FlushPass();
			m_mtl->m_bPassDirty = true;
		}
		break;
	}

	case D3DRS_SCISSORTESTENABLE:
	case D3DRS_CULLMODE:
	case D3DRS_FILLMODE:
	case D3DRS_DEPTHBIAS:
	case D3DRS_SLOPESCALEDEPTHBIAS:
	case D3DRS_STENCILREF:
		m_mtl->m_bDynamicStateDirty = true;
		break;

	default:
		break;
	}

	cur = Value;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetScissorRect( CONST RECT* pRect )
{
	m_mtl->m_scissorRect = *pRect;
	m_mtl->m_bDynamicStateDirty = true;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetClipPlane( DWORD Index, CONST float* pPlane )
{
	if ( Index >= 2 )
		return D3DERR_INVALIDCALL;
	// Planes are in clip space, which is the same in Metal
	memcpy( m_mtl->m_vsMisc.m_ClipPlanes[Index], pPlane, 16 );
	m_mtl->m_bVSMiscDirty = true;
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Draws
// ------------------------------------------------------------------------------------------------------------------------------ //

HRESULT IDirect3DDevice9::DrawPrimitive( D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount )
{
	m_mtl->Draw( PrimitiveType, StartVertex, PrimitiveCount );
	return S_OK;
}

HRESULT IDirect3DDevice9::DrawIndexedPrimitive( D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount )
{
	m_mtl->DrawIndexed( PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount );
	return S_OK;
}

HRESULT IDirect3DDevice9::DrawIndexedPrimitiveUP( D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride )
{
	// Unused by the shader API, and unimplemented in ToGL
	DXABSTRACT_BREAK_ON_ERROR();
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Misc
// ------------------------------------------------------------------------------------------------------------------------------ //

BOOL IDirect3DDevice9::ShowCursor( BOOL bShow )
{
	return TRUE;
}

HRESULT IDirect3DDevice9::ValidateDevice( DWORD* pNumPasses )
{
	return S_OK;
}

HRESULT IDirect3DDevice9::SetMaterial( CONST D3DMATERIAL9* pMaterial )
{
	return S_OK;
}

HRESULT IDirect3DDevice9::LightEnable( DWORD Index, BOOL Enable )
{
	return S_OK;
}

HRESULT IDirect3DDevice9::GetDeviceCaps( D3DCAPS9* pCaps )
{
	IDirect3D9 d3d;
	return d3d.GetDeviceCaps( 0, D3DDEVTYPE_HAL, pCaps );
}

HRESULT IDirect3DDevice9::TestCooperativeLevel()
{
	return S_OK;
}

HRESULT IDirect3DDevice9::EvictManagedResources()
{
	return S_OK;
}

HRESULT IDirect3DDevice9::SetLight( DWORD Index, CONST D3DLIGHT9* )
{
	return S_OK;
}

void IDirect3DDevice9::SetGammaRamp( UINT iSwapChain, DWORD Flags, CONST D3DGAMMARAMP* pRamp )
{
	// As ToGL: the display's transfer table
	CGGammaValue redt[256], grnt[256], blut[256];
	for ( int i = 0; i < 256; i++ )
	{
		redt[i] = pRamp->red[i] / 65535.0f;
		grnt[i] = pRamp->green[i] / 65535.0f;
		blut[i] = pRamp->blue[i] / 65535.0f;
	}
	CGSetDisplayTransferByTable( 0, 256, redt, grnt, blut );
}

void IDirect3DDevice9::SaveGLState()
{
}

void IDirect3DDevice9::RestoreGLState()
{
	m_mtl->m_bPassDirty = true;
	m_mtl->m_bDynamicStateDirty = true;
}

HRESULT IDirect3DDevice9::SetTransform( D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix )
{
	return S_OK;
}

HRESULT IDirect3DDevice9::SetTextureStageState( DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value )
{
	return S_OK;
}

void IDirect3DDevice9::AcquireThreadOwnership()
{
	m_nCurOwnerThreadId = ThreadGetCurrentId();
}

void IDirect3DDevice9::ReleaseThreadOwnership()
{
	m_nCurOwnerThreadId = 0;
}

void IDirect3DDevice9::DumpStatsToConsole( const CCommand *pArgs )
{
	ConMsg( "ToMetal: %u pipelines, %u depth/stencil states, %u samplers\n",
		(uint)m_mtl->m_pipelines.size(), (uint)m_mtl->m_depthStencilStates.size(), (uint)m_mtl->m_samplerStates.size() );
}
