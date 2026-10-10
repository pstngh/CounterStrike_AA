//================ Copyright (c) Valve Corporation. All Rights Reserved. =================
//
// mtladapter.mm
//
// IDirect3D9 on Metal: one adapter, the system default GPU on the main
// display. Caps, format support and identity follow what ToGL reports on an
// Apple GPU, so the shader API makes the same choices on either renderer.
//
//==================================================================================================
#include "mtlinternal.h"
#include "tier0/icommandline.h"
#include "tier1/utlvector.h"
#include "materialsystem/IShader.h"


#include "tier0/memdbgon.h"

struct MtlDisplayMode
{
	uint m_nWidth, m_nHeight, m_nRefreshHz;
};

static CUtlVector<MtlDisplayMode> s_modes;

// The main display's modes, filtered and ordered like ToGL's display database:
// usable for the desktop, at least 512x384, highest refresh rate first, then
// largest first.
static void PopulateModes()
{
	if ( s_modes.Count() )
		return;

	CFArrayRef modeList = CGDisplayCopyAllDisplayModes( CGMainDisplayID(), NULL );
	if ( modeList )
	{
		CFIndex count = CFArrayGetCount( modeList );
		for ( CFIndex i = 0; i < count; i++ )
		{
			CGDisplayModeRef mode = (CGDisplayModeRef)CFArrayGetValueAtIndex( modeList, i );
			if ( !CGDisplayModeIsUsableForDesktopGUI( mode ) )
				continue;

			MtlDisplayMode m;
			m.m_nWidth = (uint)CGDisplayModeGetWidth( mode );
			m.m_nHeight = (uint)CGDisplayModeGetHeight( mode );
			m.m_nRefreshHz = (uint)CGDisplayModeGetRefreshRate( mode );
			if ( m.m_nHeight >= 384 && m.m_nWidth >= 512 )
			{
				s_modes.AddToTail( m );
			}
		}
		CFRelease( modeList );
	}

	struct Sort
	{
		static int Compare( const MtlDisplayMode *a, const MtlDisplayMode *b )
		{
			if ( a->m_nRefreshHz != b->m_nRefreshHz )
				return a->m_nRefreshHz > b->m_nRefreshHz ? -1 : 1;
			uint areaA = a->m_nWidth * a->m_nHeight, areaB = b->m_nWidth * b->m_nHeight;
			if ( areaA != areaB )
				return areaA > areaB ? -1 : 1;
			return 0;
		}
	};
	s_modes.Sort( Sort::Compare );
}

static id<MTLDevice> AdapterDevice()
{
	static id<MTLDevice> s_device;
	if ( !s_device )
	{
		s_device = MTLCreateSystemDefaultDevice();
	}
	return s_device;
}

// The most samples a multisampled target can have
static int MaxSamples()
{
	id<MTLDevice> dev = AdapterDevice();
	if ( [dev supportsTextureSampleCount:8] )
		return 8;
	if ( [dev supportsTextureSampleCount:4] )
		return 4;
	if ( [dev supportsTextureSampleCount:2] )
		return 2;
	return 1;
}

IDirect3D9 *Direct3DCreate9( UINT SDKVersion )
{
	if ( !AdapterDevice() )
	{
		Warning( "ToMetal: no Metal device\n" );
		return NULL;
	}
	return new IDirect3D9;
}

IDirect3D9::~IDirect3D9()
{
}

UINT IDirect3D9::GetAdapterCount()
{
	return 1;
}

HRESULT IDirect3D9::GetDeviceCaps( UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps )
{
	Q_memset( pCaps, 0, sizeof(*pCaps) );

	pCaps->DeviceType					=	D3DDEVTYPE_HAL;
	pCaps->Caps							=	0;
	pCaps->Caps2						=	D3DCAPS2_DYNAMICTEXTURES;
	pCaps->CursorCaps					=	0;
	pCaps->DevCaps						=	D3DDEVCAPS_HWTRANSFORMANDLIGHT;
	pCaps->TextureCaps					=	D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_MIPCUBEMAP | D3DPTEXTURECAPS_NONPOW2CONDITIONAL | D3DPTEXTURECAPS_PROJECTED;
	pCaps->PrimitiveMiscCaps			=	0;
	pCaps->RasterCaps					=	D3DPRASTERCAPS_SCISSORTEST | D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS | D3DPRASTERCAPS_DEPTHBIAS;
	pCaps->TextureFilterCaps			=	D3DPTFILTERCAPS_MINFANISOTROPIC | D3DPTFILTERCAPS_MAGFANISOTROPIC;

	// The limits ToGL reports
	pCaps->MaxTextureWidth				=	4096;
	pCaps->MaxTextureHeight				=	4096;
	pCaps->MaxVolumeExtent				=	1024;
	pCaps->MaxTextureAspectRatio		=	0;
	pCaps->MaxAnisotropy				=	4;

	pCaps->TextureOpCaps				=	D3DTEXOPCAPS_ADD | D3DTEXOPCAPS_MODULATE2X;
	pCaps->VertexProcessingCaps			=	D3DVTXPCAPS_TEXGEN_SPHEREMAP;
	pCaps->MaxActiveLights				=	8;

	// Metal clips against two user planes in the vertex shader
	pCaps->MaxUserClipPlanes			=	CommandLine()->CheckParm( "-nouserclip" ) ? 0 : 2;

	pCaps->MaxVertexBlendMatrices		=	0;
	pCaps->MaxVertexBlendMatrixIndex	=	0;
	pCaps->MaxPrimitiveCount			=	32768;
	pCaps->MaxStreams					=	D3D_MAX_STREAMS;
	pCaps->VertexShaderVersion			=	0x300;
	pCaps->MaxVertexShaderConst			=	DXABSTRACT_VS_PARAM_SLOTS;
	pCaps->PixelShaderVersion			=	0x300;
	pCaps->DevCaps2						=	D3DDEVCAPS2_STREAMOFFSET;
	pCaps->PS20Caps.NumInstructionSlots	=	512;
	pCaps->NumSimultaneousRTs			=	1;
	pCaps->MaxVertexShader30InstructionSlots	=	0;
	pCaps->MaxPixelShader30InstructionSlots		=	0;

	pCaps->FakeSRGBWrite				=	0;
	pCaps->CanDoSRGBReadFromRTs			=	1;
	pCaps->MixedSizeTargets				=	1;

	return S_OK;
}

HRESULT IDirect3D9::GetAdapterIdentifier( UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier )
{
	Q_memset( pIdentifier, 0, sizeof(*pIdentifier) );

	id<MTLDevice> dev = AdapterDevice();
	const char *pName = [[dev name] UTF8String];

	Q_snprintf( pIdentifier->Driver, sizeof(pIdentifier->Driver), "Metal %s", pName );
	Q_snprintf( pIdentifier->Description, sizeof(pIdentifier->Description), "%s - %dx%d",
		pName, (int)CGDisplayPixelsWide( CGMainDisplayID() ), (int)CGDisplayPixelsHigh( CGMainDisplayID() ) );

	// An Apple GPU has no PCI identity; the OpenGL path reports zeros and the
	// renderer's maximum for video memory, and so does this.
	pIdentifier->VendorId				= 0;
	pIdentifier->DeviceId				= 0;
	pIdentifier->SubSysId				= 0;
	pIdentifier->Revision				= 0;
	pIdentifier->VideoMemory			= 0x7FFFFFFF;

	return S_OK;
}

HRESULT IDirect3D9::CheckDeviceFormat( UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat )
{
	HRESULT result = D3DERR_NOTAVAILABLE;

	DWORD legalUsage = 0;
	if ( AdapterFormat != D3DFMT_X8R8G8B8 )
		return D3DERR_NOTAVAILABLE;

	switch ( RType )
	{
	case D3DRTYPE_TEXTURE:
		switch ( CheckFormat )
		{
		case D3DFMT_DXT1:
		case D3DFMT_DXT3:
		case D3DFMT_DXT5:
			legalUsage = D3DUSAGE_DYNAMIC | D3DUSAGE_AUTOGENMIPMAP | D3DUSAGE_QUERY_FILTER | D3DUSAGE_QUERY_SRGBREAD;
			break;

		case D3DFMT_A8R8G8B8:
		case D3DFMT_A2R10G10B10:
		case D3DFMT_A2B10G10R10:
		case D3DFMT_R32F:
		case D3DFMT_A16B16G16R16:
		case D3DFMT_A16B16G16R16F:
		case D3DFMT_A32B32G32R32F:
			legalUsage = D3DUSAGE_DYNAMIC | D3DUSAGE_AUTOGENMIPMAP | D3DUSAGE_QUERY_FILTER;
			legalUsage |= D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_SRGBREAD | D3DUSAGE_QUERY_SRGBWRITE | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING;
			break;

		case D3DFMT_R8G8B8:
			legalUsage = D3DUSAGE_DYNAMIC | D3DUSAGE_AUTOGENMIPMAP | D3DUSAGE_QUERY_FILTER | D3DUSAGE_QUERY_SRGBREAD;
			break;

		case D3DFMT_X8R8G8B8:
			legalUsage = D3DUSAGE_DYNAMIC | D3DUSAGE_AUTOGENMIPMAP | D3DUSAGE_QUERY_FILTER | D3DUSAGE_QUERY_SRGBREAD | D3DUSAGE_QUERY_SRGBWRITE;
			break;

		case D3DFMT_R5G6B5:
		case D3DFMT_L8:
		case D3DFMT_A8L8:
		case D3DFMT_A8:
		case D3DFMT_X1R5G5B5:
		case D3DFMT_A4R4G4B4:
		case D3DFMT_A1R5G5B5:
		case D3DFMT_V8U8:
		case D3DFMT_Q8W8V8U8:
		case D3DFMT_X8L8V8U8:
			legalUsage = D3DUSAGE_DYNAMIC | D3DUSAGE_AUTOGENMIPMAP | D3DUSAGE_QUERY_FILTER;
			break;

		case D3DFMT_D16:
		case D3DFMT_D24S8:
			legalUsage = D3DUSAGE_DYNAMIC | D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL;
			break;

		default:
			// vendor formats (INTZ, RAWZ, NULL, ATI depth and 1N/2N) aren't available
			legalUsage = 0;
			break;
		}

		if ( legalUsage && ( Usage & legalUsage ) == Usage )
		{
			// X8L8V8U8 has no storage format here
			result = MtlGetFormatDesc( CheckFormat ) ? S_OK : D3DERR_NOTAVAILABLE;
		}
		break;

	case D3DRTYPE_SURFACE:
		if ( CheckFormat == D3DFMT_D24S8 )
		{
			result = S_OK;
		}
		break;

	default:
		break;
	}

	return result;
}

UINT IDirect3D9::GetAdapterModeCount( UINT Adapter, D3DFORMAT Format )
{
	PopulateModes();
	return s_modes.Count();
}

HRESULT IDirect3D9::EnumAdapterModes( UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode )
{
	PopulateModes();
	if ( Mode >= (UINT)s_modes.Count() )
		return D3DERR_NOTAVAILABLE;

	pMode->Width		= s_modes[Mode].m_nWidth;
	pMode->Height		= s_modes[Mode].m_nHeight;
	pMode->RefreshRate	= s_modes[Mode].m_nRefreshHz;
	pMode->Format		= Format;
	return S_OK;
}

HRESULT IDirect3D9::CheckDeviceType( UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed )
{
	return S_OK;
}

HRESULT IDirect3D9::GetAdapterDisplayMode( UINT Adapter, D3DDISPLAYMODE* pMode )
{
	CGDisplayModeRef mode = CGDisplayCopyDisplayMode( CGMainDisplayID() );
	if ( !mode )
		return D3DERR_INVALIDCALL;

	pMode->Width		= (UINT)CGDisplayModeGetWidth( mode );
	pMode->Height		= (UINT)CGDisplayModeGetHeight( mode );
	pMode->RefreshRate	= (UINT)CGDisplayModeGetRefreshRate( mode );
	pMode->Format		= D3DFMT_X8R8G8B8;
	CGDisplayModeRelease( mode );
	return S_OK;
}

HRESULT IDirect3D9::CheckDepthStencilMatch( UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat )
{
	if ( AdapterFormat == D3DFMT_X8R8G8B8 && RenderTargetFormat == D3DFMT_A8R8G8B8 && DepthStencilFormat == D3DFMT_D24S8 )
		return S_OK;
	return D3DERR_NOTAVAILABLE;
}

HRESULT IDirect3D9::CheckDeviceMultiSampleType( UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD* pQualityLevels )
{
	switch ( MultiSampleType )
	{
	case D3DMULTISAMPLE_NONE:
		if ( pQualityLevels )
			*pQualityLevels = 1;
		return S_OK;

	case D3DMULTISAMPLE_2_SAMPLES:
	case D3DMULTISAMPLE_4_SAMPLES:
	case D3DMULTISAMPLE_8_SAMPLES:
		if ( MaxSamples() >= (int)MultiSampleType && [AdapterDevice() supportsTextureSampleCount:(NSUInteger)MultiSampleType] )
		{
			if ( pQualityLevels )
				*pQualityLevels = 1;
			return S_OK;
		}
		return D3DERR_NOTAVAILABLE;

	default:
		if ( pQualityLevels )
			*pQualityLevels = 0;
		return D3DERR_NOTAVAILABLE;
	}
}

HRESULT IDirect3D9::CreateDevice( UINT Adapter, D3DDEVTYPE DeviceType, VD3DHWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface )
{
	*ppReturnedDeviceInterface = NULL;

	if ( DeviceType == D3DDEVTYPE_NULLREF )
	{
		Warning( "ToMetal: the NULL device isn't supported\n" );
		return E_FAIL;
	}

	if ( pPresentationParameters->AutoDepthStencilFormat != D3DFMT_D24S8 )
	{
		DXABSTRACT_BREAK_ON_ERROR();
		return D3DERR_NOTAVAILABLE;
	}

	IDirect3DDevice9Params devparams;
	memset( &devparams, 0, sizeof(devparams) );
	devparams.m_adapter					= Adapter;
	devparams.m_deviceType				= DeviceType;
	devparams.m_focusWindow				= hFocusWindow;
	devparams.m_behaviorFlags			= BehaviorFlags;
	devparams.m_presentationParameters	= *pPresentationParameters;

	IDirect3DDevice9 *dev = new IDirect3DDevice9;
	HRESULT result = dev->Create( &devparams );
	if ( result == S_OK )
	{
		*ppReturnedDeviceInterface = dev;
	}
	else
	{
		dev->Release();
	}
	return result;
}
