//================ Copyright (c) Valve Corporation. All Rights Reserved. =================
//
// mtldevice.mm
//
// The Metal device behind IDirect3DDevice9: command buffers, render passes,
// pipeline and state caches, draws, clears, copies and present.
//
// One command buffer is recorded at a time and committed at Present (or
// earlier when the CPU must wait for a result). Each has a serial; resources
// remember the last serial that used them, so the CPU knows when it may write
// to them directly. Render passes open lazily at the first draw after the
// targets change; a clear that comes first becomes the pass's load action.
// D3DRS_SRGBWRITEENABLE picks the views a pass draws its targets through
// when it opens; later draws that want the other views convert in their
// pixel shaders, so the pass goes on.
//
//==================================================================================================
#include "mtlinternal.h"
#include "tier1/convar.h"
#include "tier1/strtools.h"
#include "filesystem.h"
#include "appframework/ilaunchermgr.h"

#include <thread>
#include <condition_variable>


#include "tier0/memdbgon.h"

CMtlDevice *g_pMtlDevice;

static const uint kUploadChunkSize = 4 * 1024 * 1024;
static const uint kVisibilitySlots = 8192;

// ------------------------------------------------------------------------------------------------------------------------------ //
// Built-in programs
// ------------------------------------------------------------------------------------------------------------------------------ //

static const char *s_pUtilShaders =
	"#include <metal_stdlib>\n"
	"using namespace metal;\n"
	"struct QuadParams { float4 dst; float4 src; float4 color; float depth; };\n"
	"struct QuadOut { float4 pos [[ position ]]; float2 uv; };\n"
	"vertex QuadOut quad_vs( uint vid [[ vertex_id ]], constant QuadParams &p [[ buffer( 0 ) ]] )\n"
	"{\n"
	"	float2 t = float2( vid & 1, vid >> 1 );\n"
	"	QuadOut o;\n"
	"	o.pos = float4( mix( p.dst.xy, p.dst.zw, t ), p.depth, 1.0 );\n"
	"	o.uv = mix( p.src.xy, p.src.zw, t );\n"
	"	return o;\n"
	"}\n"
	"struct ClearOut { float4 c0 [[ color( 0 ) ]]; float4 c1 [[ color( 1 ) ]]; float4 c2 [[ color( 2 ) ]]; float4 c3 [[ color( 3 ) ]]; };\n"
	"fragment ClearOut clear_ps( QuadOut in [[ stage_in ]], constant QuadParams &p [[ buffer( 0 ) ]] )\n"
	"{\n"
	"	ClearOut o;\n"
	"	o.c0 = p.color; o.c1 = p.color; o.c2 = p.color; o.c3 = p.color;\n"
	"	return o;\n"
	"}\n"
	"fragment float4 blit_ps( QuadOut in [[ stage_in ]], texture2d<float> t [[ texture( 0 ) ]], sampler s [[ sampler( 0 ) ]] )\n"
	"{\n"
	"	return t.sample( s, in.uv );\n"
	"}\n"
	"struct DepthOut { float depth [[ depth( any ) ]]; };\n"
	"fragment DepthOut blit_depth_ps( QuadOut in [[ stage_in ]], depth2d<float> t [[ texture( 0 ) ]], sampler s [[ sampler( 0 ) ]] )\n"
	"{\n"
	"	DepthOut o;\n"
	"	o.depth = t.sample( s, in.uv );\n"
	"	return o;\n"
	"}\n";

struct QuadParams
{
	float dst[4];
	float src[4];
	float color[4];
	float depth;
	float pad[3];
};

// ------------------------------------------------------------------------------------------------------------------------------ //
// Helpers
// ------------------------------------------------------------------------------------------------------------------------------ //

static uint64 HashBytes( const void *p, size_t n )
{
	uint64 h = 14695981039346656037ull;
	const uint8 *b = (const uint8*)p;
	for ( size_t i = 0; i < n; i++ )
	{
		h = ( h ^ b[i] ) * 1099511628211ull;
	}
	return h;
}

static MTLCompareFunction CompareFunc( DWORD d3d )
{
	// D3DCMP_NEVER (1) .. D3DCMP_ALWAYS (8) are MTLCompareFunctionNever (0) .. Always (7)
	if ( d3d < 1 || d3d > 8 )
		return MTLCompareFunctionAlways;
	return (MTLCompareFunction)( d3d - 1 );
}

static MTLStencilOperation StencilOp( DWORD d3d )
{
	// D3DSTENCILOP_KEEP (1) .. DECR (8) are MTLStencilOperationKeep (0) .. DecrementWrap (7)
	if ( d3d < 1 || d3d > 8 )
		return MTLStencilOperationKeep;
	return (MTLStencilOperation)( d3d - 1 );
}

static MTLBlendFactor BlendFactor( DWORD d3d )
{
	switch ( d3d )
	{
	case D3DBLEND_ZERO:				return MTLBlendFactorZero;
	case D3DBLEND_ONE:				return MTLBlendFactorOne;
	case D3DBLEND_SRCCOLOR:			return MTLBlendFactorSourceColor;
	case D3DBLEND_INVSRCCOLOR:		return MTLBlendFactorOneMinusSourceColor;
	case D3DBLEND_SRCALPHA:			return MTLBlendFactorSourceAlpha;
	case D3DBLEND_INVSRCALPHA:		return MTLBlendFactorOneMinusSourceAlpha;
	case D3DBLEND_DESTALPHA:		return MTLBlendFactorDestinationAlpha;
	case D3DBLEND_INVDESTALPHA:		return MTLBlendFactorOneMinusDestinationAlpha;
	case D3DBLEND_DESTCOLOR:		return MTLBlendFactorDestinationColor;
	case D3DBLEND_INVDESTCOLOR:		return MTLBlendFactorOneMinusDestinationColor;
	case D3DBLEND_SRCALPHASAT:		return MTLBlendFactorSourceAlphaSaturated;
	case D3DBLEND_BOTHSRCALPHA:		return MTLBlendFactorSourceAlpha;
	case D3DBLEND_BOTHINVSRCALPHA:	return MTLBlendFactorOneMinusSourceAlpha;
	case D3DBLEND_BLENDFACTOR:		return MTLBlendFactorBlendColor;
	case D3DBLEND_INVBLENDFACTOR:	return MTLBlendFactorOneMinusBlendColor;
	default:						return MTLBlendFactorOne;
	}
}

static MTLBlendOperation BlendOp( DWORD d3d )
{
	switch ( d3d )
	{
	case D3DBLENDOP_SUBTRACT:		return MTLBlendOperationSubtract;
	case D3DBLENDOP_REVSUBTRACT:	return MTLBlendOperationReverseSubtract;
	case D3DBLENDOP_MIN:			return MTLBlendOperationMin;
	case D3DBLENDOP_MAX:			return MTLBlendOperationMax;
	default:						return MTLBlendOperationAdd;
	}
}

static bool IsDepthFormat( MTLPixelFormat f )
{
	return f == MTLPixelFormatDepth16Unorm || f == MTLPixelFormatDepth32Float || f == MTLPixelFormatDepth32Float_Stencil8;
}

static double LinearToSRGB( double c )
{
	c = MAX( 0.0, MIN( 1.0, c ) );
	return ( c <= 0.0031308 ) ? c * 12.92 : 1.055 * pow( c, 1.0 / 2.4 ) - 0.055;
}

static double SRGBToLinear( double c )
{
	c = MAX( 0.0, MIN( 1.0, c ) );
	return ( c <= 0.04045 ) ? c / 12.92 : pow( ( c + 0.055 ) / 1.055, 2.4 );
}

// A clear color, given with sRGB write on or off, for a target drawn through
// an sRGB view or a linear one: what the view must be given to store what
// the clear meant
static MTLClearColor ClearColorForView( MTLClearColor c, bool bSRGBWrite, bool bSRGBView )
{
	if ( bSRGBWrite == bSRGBView )
		return c;
	double ( *convert )( double ) = bSRGBWrite ? LinearToSRGB : SRGBToLinear;
	return MTLClearColorMake( convert( c.red ), convert( c.green ), convert( c.blue ), c.alpha );
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Init / shutdown
// ------------------------------------------------------------------------------------------------------------------------------ //

CMtlDevice::CMtlDevice()
{
	m_layer = nil;
	m_pD3D = NULL;
	for ( int i = 0; i < MTL_MAX_RENDER_TARGETS; i++ )
		m_pRenderTargets[i] = NULL;
	m_pDepthStencil = NULL;
	m_pVertDecl = NULL;
	memset( m_streams, 0, sizeof( m_streams ) );
	m_pIndices = NULL;
	m_pVertexShader = NULL;
	m_pPixelShader = NULL;
	for ( int i = 0; i < MTL_SAMPLER_COUNT; i++ )
	{
		m_samplers[i].m_pTexture = NULL;
		memset( &m_samplers[i].m_state, 0, sizeof( m_samplers[i].m_state ) );
	}
	memset( &m_viewport, 0, sizeof( m_viewport ) );
	memset( &m_scissorRect, 0, sizeof( m_scissorRect ) );
	memset( m_renderStates, 0, sizeof( m_renderStates ) );
	memset( m_clipPlanes, 0, sizeof( m_clipPlanes ) );
	memset( m_vsConstants, 0, sizeof( m_vsConstants ) );
	memset( m_psConstants, 0, sizeof( m_psConstants ) );
	memset( &m_vsMisc, 0, sizeof( m_vsMisc ) );
	memset( &m_psMisc, 0, sizeof( m_psMisc ) );
	m_bVSConstantsDirty = m_bPSConstantsDirty = m_bVSMiscDirty = m_bPSMiscDirty = true;
	m_nMaxVSConstantHint = MTL_VS_CONSTANT_COUNT;

	m_nSerial = 1;
	m_pSync = std::make_shared<MtlFrameSync>();
	m_pSync->m_nCompletedSerial = 0;
	m_frameSemaphore = dispatch_semaphore_create( MTL_MAX_FRAMES_IN_FLIGHT );

	m_bPassDirty = true;
	m_bPassHasDraws = false;
	memset( &m_passKey, 0, sizeof( m_passKey ) );
	m_nPassWidth = m_nPassHeight = 0;
	m_nPassSRGBViewMask = m_nPassSRGBMask = 0;
	m_bFramebufferFetch = false;
	m_bPendingClearColor = m_bPendingClearDepth = m_bPendingClearStencil = false;
	m_bPendingClearSRGB = false;
	m_pendingClearColor = MTLClearColorMake( 0, 0, 0, 0 );
	m_flPendingClearDepth = 1.0;
	m_nPendingClearStencil = 0;

	for ( int i = 0; i <= MTL_MAX_STREAMS; i++ )
		m_curVertexOffsets[i] = 0;
	m_bDynamicStateDirty = true;
	m_bVSConstantsBound = m_bPSConstantsBound = false;
	m_nCurStencilRef = 0;

	m_nNextShaderId = 0;
	m_nCurUploadChunk = -1;
	m_pActiveQuery = NULL;
	m_nFrame = 0;
	m_nDrawCallIndex = 0;
	m_nVSUploadedCount = m_nPSUploadedCount = 0;
	m_pPresenter = NULL;
}

CMtlDevice::~CMtlDevice()
{
}

static id<MTLTexture> NewDummyTexture( id<MTLDevice> dev, MTLTextureType type, MTLPixelFormat format )
{
	MTLTextureDescriptor *desc = [MTLTextureDescriptor new];
	desc.textureType = type;
	desc.pixelFormat = format;
	desc.width = desc.height = 1;
	desc.depth = 1;
	desc.usage = MTLTextureUsageShaderRead | ( IsDepthFormat( format ) ? MTLTextureUsageRenderTarget : 0 );
	desc.storageMode = IsDepthFormat( format ) ? MTLStorageModePrivate : MTLStorageModeShared;
	id<MTLTexture> tex = [dev newTextureWithDescriptor:desc];
	if ( !IsDepthFormat( format ) )
	{
		// Opaque black, as an unbound sampler reads in ToGL
		static const uint8 black[4] = { 0, 0, 0, 255 };
		uint nSlices = ( type == MTLTextureTypeCube ) ? 6 : 1;
		for ( uint s = 0; s < nSlices; s++ )
		{
			[tex replaceRegion:MTLRegionMake3D( 0, 0, 0, 1, 1, 1 ) mipmapLevel:0 slice:s withBytes:black bytesPerRow:4 bytesPerImage:4];
		}
	}
	return tex;
}

bool CMtlDevice::Init( IDirect3DDevice9 *pD3D, const D3DPRESENT_PARAMETERS *pParams )
{
	m_pD3D = pD3D;
	m_device = MTLCreateSystemDefaultDevice();
	if ( !m_device )
		return false;
	m_queue = [m_device newCommandQueueWithMaxCommandBufferCount:64];
	m_bFramebufferFetch = [m_device supportsFamily:MTLGPUFamilyApple1];		// all Apple GPUs

	m_layer = g_pLauncherMgr ? (__bridge CAMetalLayer*)g_pLauncherMgr->GetMetalLayer() : nil;
	if ( !m_layer )
	{
		Warning( "ToMetal: the game window has no Metal layer (start the game with -metal)\n" );
		return false;
	}
	m_layer.device = m_device;
	m_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
	m_layer.framebufferOnly = YES;

	NSError *err = nil;
	m_utilLibrary = [m_device newLibraryWithSource:[NSString stringWithUTF8String:s_pUtilShaders] options:[MTLCompileOptions new] error:&err];
	if ( !m_utilLibrary )
	{
		Warning( "ToMetal: built-in shaders failed to compile: %s\n", [[err description] UTF8String] );
		return false;
	}

	for ( int i = 0; i < 4; i++ )
	{
		MTLDepthStencilDescriptor *dsd = [MTLDepthStencilDescriptor new];
		dsd.depthCompareFunction = MTLCompareFunctionAlways;
		dsd.depthWriteEnabled = ( i & 1 ) != 0;
		if ( i & 2 )
		{
			MTLStencilDescriptor *sd = [MTLStencilDescriptor new];
			sd.stencilCompareFunction = MTLCompareFunctionAlways;
			sd.stencilFailureOperation = MTLStencilOperationReplace;
			sd.depthFailureOperation = MTLStencilOperationReplace;
			sd.depthStencilPassOperation = MTLStencilOperationReplace;
			sd.readMask = 0xFF;
			sd.writeMask = 0xFF;
			dsd.frontFaceStencil = sd;
			dsd.backFaceStencil = sd;
		}
		m_clearDepthStencil[i] = [m_device newDepthStencilStateWithDescriptor:dsd];
	}

	m_zeroBuffer = [m_device newBufferWithLength:256 options:MTLResourceStorageModeShared];
	memset( [m_zeroBuffer contents], 0, 256 );

	m_dummyTex2D = NewDummyTexture( m_device, MTLTextureType2D, MTLPixelFormatRGBA8Unorm );
	m_dummyTexCube = NewDummyTexture( m_device, MTLTextureTypeCube, MTLPixelFormatRGBA8Unorm );
	m_dummyTex3D = NewDummyTexture( m_device, MTLTextureType3D, MTLPixelFormatRGBA8Unorm );
	m_dummyDepth = NewDummyTexture( m_device, MTLTextureType2D, MTLPixelFormatDepth32Float );
	{
		// A shadow lookup into the dummy depth map passes: depth 1
		MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
		rp.depthAttachment.texture = m_dummyDepth;
		rp.depthAttachment.loadAction = MTLLoadActionClear;
		rp.depthAttachment.storeAction = MTLStoreActionStore;
		rp.depthAttachment.clearDepth = 1.0;
		id<MTLRenderCommandEncoder> enc = [CommandBuffer() renderCommandEncoderWithDescriptor:rp];
		[enc endEncoding];
	}

	m_visibilityBuffer = [m_device newBufferWithLength:kVisibilitySlots * sizeof( uint64 ) options:MTLResourceStorageModeShared];
	memset( [m_visibilityBuffer contents], 0, kVisibilitySlots * sizeof( uint64 ) );
	m_freeVisibilitySlots.reserve( kVisibilitySlots );
	for ( int i = kVisibilitySlots - 1; i >= 0; i-- )
	{
		m_freeVisibilitySlots.push_back( i );
	}

	return true;
}

static void DeletePresenter( CMtlPresenter *pPresenter );

void CMtlDevice::Shutdown()
{
	DeletePresenter( m_pPresenter );
	m_pPresenter = NULL;
	Submit( true );
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Command buffers
// ------------------------------------------------------------------------------------------------------------------------------ //

id<MTLCommandBuffer> CMtlDevice::CommandBuffer()
{
	if ( !m_cmdBuf )
	{
		m_cmdBuf = [m_queue commandBuffer];
	}
	return m_cmdBuf;
}

void CMtlDevice::EndEncoders()
{
	if ( m_renderEncoder )
	{
		[m_renderEncoder endEncoding];
		m_renderEncoder = nil;
		m_bPassDirty = true;
	}
	if ( m_blitEncoder )
	{
		[m_blitEncoder endEncoding];
		m_blitEncoder = nil;
	}
}

// Ends the encoders, first opening the pass if clears are waiting on it so
// they reach the targets they were meant for.
void CMtlDevice::FlushPass()
{
	if ( ( m_bPendingClearColor || m_bPendingClearDepth || m_bPendingClearStencil ) && !m_renderEncoder )
	{
		RenderEncoder();
	}
	EndEncoders();
}

id<MTLBlitCommandEncoder> CMtlDevice::BlitEncoder()
{
	if ( !m_blitEncoder )
	{
		FlushPass();
		m_blitEncoder = [CommandBuffer() blitCommandEncoder];
	}
	return m_blitEncoder;
}

void CMtlDevice::Submit( bool bWait )
{
	FlushPass();

	// Commit even an empty command buffer, so everything stamped with this
	// serial (a query, say) completes
	CommandBuffer();

	uint64 serial = m_nSerial;
	std::shared_ptr<MtlFrameSync> pSync = m_pSync;
	[m_cmdBuf addCompletedHandler:^( id<MTLCommandBuffer> cb ) {
		uint64 cur = pSync->m_nCompletedSerial.load();
		while ( serial > cur && !pSync->m_nCompletedSerial.compare_exchange_weak( cur, serial ) )
		{
		}
	}];
	[m_cmdBuf commit];

	m_inFlight.push_back( std::make_pair( serial, m_cmdBuf ) );
	if ( bWait )
	{
		[m_cmdBuf waitUntilCompleted];
	}
	m_cmdBuf = nil;
	m_nSerial++;

	// Forget command buffers that have finished
	uint64 completed = CompletedSerial();
	size_t n = 0;
	for ( size_t i = 0; i < m_inFlight.size(); i++ )
	{
		if ( m_inFlight[i].first > completed )
		{
			m_inFlight[n++] = m_inFlight[i];
		}
	}
	m_inFlight.resize( n );
}

void CMtlDevice::WaitForSerial( uint64 nSerial )
{
	if ( !IsBusy( nSerial ) )
		return;
	if ( nSerial >= m_nSerial )
	{
		Submit( true );
		return;
	}
	for ( size_t i = 0; i < m_inFlight.size(); i++ )
	{
		if ( m_inFlight[i].first >= nSerial )
		{
			[m_inFlight[i].second waitUntilCompleted];
			if ( !IsBusy( nSerial ) )
				return;
		}
	}
	// The completion handler runs just after the wait returns
	while ( IsBusy( nSerial ) )
	{
		ThreadSleep( 0 );
	}
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Memory
// ------------------------------------------------------------------------------------------------------------------------------ //

id<MTLBuffer> CMtlDevice::Upload( const void *pData, uint nSize, uint nAlign, uint *pOffset )
{
	if ( m_nCurUploadChunk >= 0 )
	{
		MtlUploadChunk &c = m_uploadChunks[ m_nCurUploadChunk ];
		uint offset = ( c.m_nUsed + nAlign - 1 ) & ~( nAlign - 1 );
		if ( offset + nSize <= [c.m_buf length] )
		{
			if ( pData )
			{
				memcpy( (uint8*)[c.m_buf contents] + offset, pData, nSize );
			}
			c.m_nUsed = offset + nSize;
			c.m_nSerial = m_nSerial;
			*pOffset = offset;
			return c.m_buf;
		}
	}

	// Find a chunk the GPU is done with, or make one
	int found = -1;
	for ( size_t i = 0; i < m_uploadChunks.size(); i++ )
	{
		if ( (int)i != m_nCurUploadChunk && !IsBusy( m_uploadChunks[i].m_nSerial ) && [m_uploadChunks[i].m_buf length] >= nSize )
		{
			found = (int)i;
			break;
		}
	}
	if ( found < 0 )
	{
		MtlUploadChunk c;
		c.m_buf = [m_device newBufferWithLength:MAX( kUploadChunkSize, ( nSize + 4095 ) & ~4095u ) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
		c.m_nUsed = 0;
		c.m_nSerial = 0;
		m_uploadChunks.push_back( c );
		found = (int)m_uploadChunks.size() - 1;
	}
	m_nCurUploadChunk = found;
	MtlUploadChunk &c = m_uploadChunks[ found ];
	c.m_nUsed = nSize;
	c.m_nSerial = m_nSerial;
	if ( pData )
	{
		memcpy( [c.m_buf contents], pData, nSize );
	}
	*pOffset = 0;
	return c.m_buf;
}

id<MTLBuffer> CMtlDevice::NewBuffer( uint nSize )
{
	auto it = m_bufferPool.find( nSize );
	if ( it != m_bufferPool.end() )
	{
		std::vector< std::pair<uint64, id<MTLBuffer> > > &pool = it->second;
		for ( size_t i = 0; i < pool.size(); i++ )
		{
			if ( !IsBusy( pool[i].first ) )
			{
				id<MTLBuffer> buf = pool[i].second;
				pool[i] = pool.back();
				pool.pop_back();
				return buf;
			}
		}
	}
	return [m_device newBufferWithLength:nSize options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
}

void CMtlDevice::RetireBuffer( id<MTLBuffer> buf, uint64 nSerial )
{
	if ( !buf )
		return;
	// Only dynamic-sized churn is worth pooling; keep a few of each size
	std::vector< std::pair<uint64, id<MTLBuffer> > > &pool = m_bufferPool[ (uint)[buf length] ];
	if ( pool.size() < 8 )
	{
		pool.push_back( std::make_pair( nSerial, buf ) );
	}
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Render passes
// ------------------------------------------------------------------------------------------------------------------------------ //

// The view a render target is drawn through
static id<MTLTexture> RenderView( CMtlTex *pTex, bool bSRGBWrite )
{
	return ( bSRGBWrite && pTex->m_texRenderSRGB ) ? pTex->m_texRenderSRGB : pTex->m_tex;
}

id<MTLRenderCommandEncoder> CMtlDevice::RenderEncoder()
{
	if ( m_renderEncoder && !m_bPassDirty )
		return m_renderEncoder;

	EndEncoders();

	bool bSRGBWrite = m_renderStates[ D3DRS_SRGBWRITEENABLE ] != 0;

	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	MtlRenderPassKey key;
	memset( &key, 0, sizeof( key ) );
	uint width = 0x7FFFFFFF, height = 0x7FFFFFFF;
	uint nSamples = 0;
	uint nSRGBViewMask = 0, nSRGBMask = 0;
	bool bAny = false;

	for ( int i = 0; i < MTL_MAX_RENDER_TARGETS; i++ )
	{
		IDirect3DSurface9 *surf = m_pRenderTargets[i];
		if ( !surf || !surf->m_tex )
			continue;
		CMtlTex *tex = surf->m_tex;
		if ( nSamples && tex->m_nSamples != nSamples )
			continue;
		nSamples = tex->m_nSamples;

		id<MTLTexture> view = RenderView( tex, bSRGBWrite );
		bool bSRGBView = view != tex->m_tex;
		nSRGBViewMask |= tex->m_texRenderSRGB ? ( 1 << i ) : 0;
		nSRGBMask |= bSRGBView ? ( 1 << i ) : 0;
		MTLRenderPassColorAttachmentDescriptor *att = rp.colorAttachments[i];
		att.texture = view;
		att.level = surf->m_mip;
		att.slice = ( tex->m_type == MTLTextureTypeCube ) ? surf->m_face : 0;
		att.loadAction = m_bPendingClearColor ? MTLLoadActionClear : MTLLoadActionLoad;
		att.clearColor = ClearColorForView( m_pendingClearColor, m_bPendingClearSRGB && tex->m_texRenderSRGB, bSRGBView );
		att.storeAction = MTLStoreActionStore;
		key.m_color[i] = view.pixelFormat;
		width = MIN( width, tex->MipWidth( surf->m_mip ) );
		height = MIN( height, tex->MipHeight( surf->m_mip ) );
		tex->m_nLastUseSerial = tex->m_nLastWriteSerial = m_nSerial;
		bAny = true;
	}

	if ( m_pDepthStencil && m_pDepthStencil->m_tex )
	{
		CMtlTex *tex = m_pDepthStencil->m_tex;
		if ( !nSamples || tex->m_nSamples == nSamples )
		{
			nSamples = tex->m_nSamples;
			rp.depthAttachment.texture = tex->m_tex;
			rp.depthAttachment.loadAction = m_bPendingClearDepth ? MTLLoadActionClear : MTLLoadActionLoad;
			rp.depthAttachment.clearDepth = m_flPendingClearDepth;
			rp.depthAttachment.storeAction = MTLStoreActionStore;
			key.m_depth = tex->m_tex.pixelFormat;
			if ( tex->m_nFlags & kMtlTexStencil )
			{
				rp.stencilAttachment.texture = tex->m_tex;
				rp.stencilAttachment.loadAction = m_bPendingClearStencil ? MTLLoadActionClear : MTLLoadActionLoad;
				rp.stencilAttachment.clearStencil = m_nPendingClearStencil;
				rp.stencilAttachment.storeAction = MTLStoreActionStore;
				key.m_stencil = tex->m_tex.pixelFormat;
			}
			width = MIN( width, tex->m_nWidth );
			height = MIN( height, tex->m_nHeight );
			tex->m_nLastUseSerial = tex->m_nLastWriteSerial = m_nSerial;
			bAny = true;
		}
	}

	m_bPendingClearColor = m_bPendingClearDepth = m_bPendingClearStencil = false;

	if ( !bAny )
		return nil;

	key.m_nSamples = nSamples;
	rp.renderTargetWidth = width;
	rp.renderTargetHeight = height;
	rp.visibilityResultBuffer = m_visibilityBuffer;

	m_renderEncoder = [CommandBuffer() renderCommandEncoderWithDescriptor:rp];
	m_passKey = key;
	m_nPassWidth = width;
	m_nPassHeight = height;
	m_nPassSRGBViewMask = nSRGBViewMask;
	m_nPassSRGBMask = nSRGBMask;
	if ( g_pMtlDump )
		MtlDumpPass( this, rp );
	m_bPassDirty = false;
	m_bPassHasDraws = false;

	// Nothing is bound in a new encoder
	m_curPipeline = nil;
	m_curDepthStencil = nil;
	for ( int i = 0; i <= MTL_MAX_STREAMS; i++ )
	{
		m_curVertexBuffers[i] = nil;
		m_curVertexOffsets[i] = 0;
	}
	for ( int i = 0; i < MTL_SAMPLER_COUNT; i++ )
	{
		m_curTextures[i] = nil;
		m_curSamplers[i] = nil;
	}
	m_bDynamicStateDirty = true;
	m_bVSConstantsBound = m_bPSConstantsBound = false;
	m_bVSMiscDirty = m_bPSMiscDirty = true;

	// An occlusion query spanning passes counts each pass into its own slot
	if ( m_pActiveQuery && !m_freeVisibilitySlots.empty() )
	{
		uint slot = m_freeVisibilitySlots.back();
		m_freeVisibilitySlots.pop_back();
		( (uint64*)[m_visibilityBuffer contents] )[slot] = 0;
		m_pActiveQuery->m_slots.push_back( slot );
		[m_renderEncoder setVisibilityResultMode:MTLVisibilityResultModeCounting offset:slot * sizeof( uint64 )];
	}

	return m_renderEncoder;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Shaders and pipelines
// ------------------------------------------------------------------------------------------------------------------------------ //

// Pixel functions are keyed by what is compiled into them: the alpha test's
// D3DCMPFUNC (bits 0-3), the sRGB conversion of color 0 (bits 4-5) and, when
// the shader blends, that (bit 6) and the D3DBLEND factors (bits 8-12 and
// 13-17) and D3DBLENDOP (bits 18-20).
static const uint32 kPixelKeyShaderBlend = 1 << 6;

// A blend factor as the shader computes it: D3DBLEND_ZERO to SRCALPHASAT,
// mapped as BlendFactor maps the rest
static uint ShaderBlendFactor( uint f )
{
	if ( f == D3DBLEND_BOTHSRCALPHA )
		return D3DBLEND_SRCALPHA;
	if ( f == D3DBLEND_BOTHINVSRCALPHA )
		return D3DBLEND_INVSRCALPHA;
	return ( f >= D3DBLEND_ZERO && f <= D3DBLEND_SRCALPHASAT ) ? f : D3DBLEND_ONE;
}

static uint32 PixelFunctionKey( const MtlBlendState &blend )
{
	uint32 key = blend.m_alphaTestFunc | ( blend.m_srgbConvert << 4 );
	if ( blend.m_srgbConvert && blend.m_bBlendEnable )
	{
		// The factors as ApplyBlend sets them
		uint src = blend.m_srcBlend, dst = blend.m_dstBlend;
		if ( src == D3DBLEND_BOTHINVSRCALPHA )
		{
			src = D3DBLEND_INVSRCALPHA;
			dst = D3DBLEND_SRCALPHA;
		}
		else if ( src == D3DBLEND_BOTHSRCALPHA )
		{
			dst = D3DBLEND_INVSRCALPHA;
		}
		uint op = ( blend.m_blendOp >= D3DBLENDOP_ADD && blend.m_blendOp <= D3DBLENDOP_MAX ) ? blend.m_blendOp : D3DBLENDOP_ADD;
		key |= kPixelKeyShaderBlend | ( ShaderBlendFactor( src ) << 8 ) | ( ShaderBlendFactor( dst ) << 13 ) | ( op << 18 );
	}
	return key;
}

// Vertex functions are keyed by the interpolants added for the pixel shader;
// pixel functions by what is compiled into them (PixelFunctionKey).
id<MTLFunction> CMtlDevice::GetFunction( CMtlShader *pShader, uint32 nKey )
{
	std::lock_guard<std::mutex> lock( pShader->m_mutex );

	auto it = pShader->m_functions.find( nKey );
	if ( it != pShader->m_functions.end() )
		return it->second;

	id<MTLFunction> fn = nil;
	if ( pShader->m_bValid )
	{
		D3DToMetal xlat;
		CUtlBuffer msl( 0, 0, CUtlBuffer::TEXT_BUFFER );
		D3DToMetalShaderInfo info;
		uint32 nExtra = pShader->m_bVertex ? nKey : 0;
		const char *pLabel = pShader->m_pDebugLabel ? pShader->m_pDebugLabel : pShader->m_name;
		if ( xlat.TranslateShader( pShader->m_pCode, &msl, &info, 0, pShader->m_nShadowMask, pShader->m_nCentroidMask, nExtra, pLabel ) )
		{
			msl.PutChar( 0 );
			NSError *err = nil;
			NSString *source = [NSString stringWithUTF8String:(const char*)msl.Base()];
			if ( !pShader->m_bVertex && nKey )
			{
				// Picks the alpha test, sRGB conversion and blend compiled into the translation
				NSMutableString *defines = [NSMutableString stringWithFormat:@"#define %s %u\n#define %s %u\n",
					D3DTOMETAL_ALPHA_TEST_MACRO, nKey & 15, D3DTOMETAL_SRGB_WRITE_MACRO, ( nKey >> 4 ) & 3];
				if ( nKey & kPixelKeyShaderBlend )
				{
					[defines appendFormat:@"#define %s 1\n#define %s %u\n#define %s %u\n#define %s %u\n", D3DTOMETAL_SHADER_BLEND_MACRO,
						D3DTOMETAL_BLEND_SRC_MACRO, ( nKey >> 8 ) & 31, D3DTOMETAL_BLEND_DST_MACRO, ( nKey >> 13 ) & 31, D3DTOMETAL_BLEND_OP_MACRO, ( nKey >> 18 ) & 7];
				}
				source = [defines stringByAppendingString:source];
			}
			id<MTLLibrary> lib = [m_device newLibraryWithSource:source options:[MTLCompileOptions new] error:&err];
			if ( lib )
			{
				fn = [lib newFunctionWithName:pShader->m_bVertex ? @"dx9vs" : @"dx9ps"];
			}
			if ( !fn )
			{
				Warning( "ToMetal: %s shader %s failed to compile: %s\n", pShader->m_bVertex ? "vertex" : "pixel", pLabel, err ? [[err description] UTF8String] : "no entry point" );
			}
		}
	}

	pShader->m_functions[ nKey ] = fn;
	return fn;
}

static MTLVertexDescriptor *BuildVertexDescriptor( CMtlDevice *dev, const CMtlShader *pVS, const CMtlVertexDecl *pDecl )
{
	MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
	bool bDummy = false;
	uint nUsedStreams = 0;		// Metal rejects layouts for buffers no attribute reads
	for ( int reg = 0; reg < 16; reg++ )
	{
		uint8 attrib = pVS->m_info.m_AttribMap[reg];
		if ( attrib == 0xBB )
			continue;

		uint8 elem = pDecl ? pDecl->m_usageToElement[ attrib ] : 0xFF;
		if ( elem != 0xFF )
		{
			const D3DVERTEXELEMENT9 &e = pDecl->m_elements[elem];
			vd.attributes[reg].format = pDecl->m_formats[elem];
			vd.attributes[reg].offset = e.Offset;
			vd.attributes[reg].bufferIndex = e.Stream;
			nUsedStreams |= 1 << e.Stream;
		}
		else
		{
			// The declaration lacks this input: read zeros, as ToGL does
			vd.attributes[reg].format = ( ( attrib >> 4 ) == D3DDECLUSAGE_COLOR ) ? MTLVertexFormatUChar4Normalized : MTLVertexFormatFloat3;
			vd.attributes[reg].offset = 0;
			vd.attributes[reg].bufferIndex = MTL_DUMMY_VERTEX_BUFFER_INDEX;
			bDummy = true;
		}
	}

	if ( pDecl )
	{
		for ( int s = 0; s < MTL_MAX_STREAMS; s++ )
		{
			if ( !( nUsedStreams & ( 1 << s ) ) )
				continue;
			uint stride = dev->m_streams[s].m_stride;
			if ( stride )
			{
				vd.layouts[s].stride = stride;
				vd.layouts[s].stepFunction = MTLVertexStepFunctionPerVertex;
				vd.layouts[s].stepRate = 1;
			}
			else
			{
				vd.layouts[s].stride = 16;
				vd.layouts[s].stepFunction = MTLVertexStepFunctionConstant;
				vd.layouts[s].stepRate = 0;
			}
		}
	}
	if ( bDummy )
	{
		vd.layouts[MTL_DUMMY_VERTEX_BUFFER_INDEX].stride = 16;
		vd.layouts[MTL_DUMMY_VERTEX_BUFFER_INDEX].stepFunction = MTLVertexStepFunctionConstant;
		vd.layouts[MTL_DUMMY_VERTEX_BUFFER_INDEX].stepRate = 0;
	}
	return vd;
}

static void ApplyBlend( MTLRenderPipelineColorAttachmentDescriptor *ca, const MtlBlendState &blend )
{
	MTLColorWriteMask mask = MTLColorWriteMaskNone;
	if ( blend.m_colorWriteMask & D3DCOLORWRITEENABLE_RED )		mask |= MTLColorWriteMaskRed;
	if ( blend.m_colorWriteMask & D3DCOLORWRITEENABLE_GREEN )	mask |= MTLColorWriteMaskGreen;
	if ( blend.m_colorWriteMask & D3DCOLORWRITEENABLE_BLUE )	mask |= MTLColorWriteMaskBlue;
	if ( blend.m_colorWriteMask & D3DCOLORWRITEENABLE_ALPHA )	mask |= MTLColorWriteMaskAlpha;
	ca.writeMask = mask;

	// A draw that converts its sRGB write blends in the pixel shader
	if ( blend.m_bBlendEnable && !blend.m_srgbConvert )
	{
		// ToGL applies one blend function to color and alpha; so does this
		ca.blendingEnabled = YES;
		ca.sourceRGBBlendFactor = ca.sourceAlphaBlendFactor = BlendFactor( blend.m_srcBlend );
		ca.destinationRGBBlendFactor = ca.destinationAlphaBlendFactor = BlendFactor( blend.m_dstBlend );
		ca.rgbBlendOperation = ca.alphaBlendOperation = BlendOp( blend.m_blendOp );
		if ( blend.m_srcBlend == D3DBLEND_BOTHINVSRCALPHA )
		{
			ca.sourceRGBBlendFactor = ca.sourceAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
			ca.destinationRGBBlendFactor = ca.destinationAlphaBlendFactor = MTLBlendFactorSourceAlpha;
		}
		else if ( blend.m_srcBlend == D3DBLEND_BOTHSRCALPHA )
		{
			ca.destinationRGBBlendFactor = ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
		}
	}
}

static id<MTLRenderPipelineState> CreatePipeline( CMtlDevice *dev, MTLRenderPipelineDescriptor *pd, const char *pLabel )
{
	NSError *err = nil;
	id<MTLRenderPipelineState> ps = [dev->m_device newRenderPipelineStateWithDescriptor:pd error:&err];
	if ( !ps )
	{
		Warning( "ToMetal: pipeline for %s failed: %s\n", pLabel, err ? [[err description] UTF8String] : "?" );
	}
	return ps;
}

static id<MTLRenderPipelineState> GetPipeline( CMtlDevice *dev, const MtlPipelineKey &key, CMtlShader *pVS, CMtlShader *pPS, const CMtlVertexDecl *pDecl )
{
	uint64 hash = HashBytes( &key, sizeof( key ) );
	auto it = dev->m_pipelines.find( hash );
	if ( it != dev->m_pipelines.end() )
		return it->second;

	id<MTLRenderPipelineState> ps = nil;
	id<MTLFunction> vfn = dev->GetFunction( pVS, key.m_nExtraOutputs );
	id<MTLFunction> ffn = dev->GetFunction( pPS, PixelFunctionKey( key.m_blend ) );
	if ( vfn && ffn )
	{
		MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
		pd.vertexFunction = vfn;
		pd.fragmentFunction = ffn;
		pd.vertexDescriptor = BuildVertexDescriptor( dev, pVS, pDecl );
		for ( int i = 0; i < MTL_MAX_RENDER_TARGETS; i++ )
		{
			pd.colorAttachments[i].pixelFormat = key.m_pass.m_color[i];
			if ( key.m_pass.m_color[i] != MTLPixelFormatInvalid )
			{
				ApplyBlend( pd.colorAttachments[i], key.m_blend );
			}
		}
		pd.depthAttachmentPixelFormat = key.m_pass.m_depth;
		pd.stencilAttachmentPixelFormat = key.m_pass.m_stencil;
		pd.rasterSampleCount = MAX( 1u, key.m_pass.m_nSamples );
		pd.alphaToCoverageEnabled = key.m_blend.m_bAlphaToCoverage && key.m_pass.m_nSamples > 1;

		char label[256];
		V_snprintf( label, sizeof( label ), "%s + %s", pVS->m_pDebugLabel ? pVS->m_pDebugLabel : pVS->m_name, pPS->m_pDebugLabel ? pPS->m_pDebugLabel : pPS->m_name );
		ps = CreatePipeline( dev, pd, label );
		if ( !ps && key.m_blend.m_bBlendEnable && !key.m_blend.m_srgbConvert )
		{
			// Some formats can't blend (32 bit float on older GPUs); draw unblended
			for ( int i = 0; i < MTL_MAX_RENDER_TARGETS; i++ )
				pd.colorAttachments[i].blendingEnabled = NO;
			ps = CreatePipeline( dev, pd, label );
		}
	}

	dev->m_pipelines[ hash ] = ps;
	return ps;
}

static id<MTLDepthStencilState> GetDepthStencilState( CMtlDevice *dev )
{
	const DWORD *rs = dev->m_renderStates;
	MtlDepthStencilKey key;
	memset( &key, 0, sizeof( key ) );
	key.m_bDepthEnable = rs[D3DRS_ZENABLE] != 0;
	key.m_bDepthWrite = key.m_bDepthEnable && rs[D3DRS_ZWRITEENABLE] != 0;
	key.m_depthFunc = key.m_bDepthEnable ? (uint8)rs[D3DRS_ZFUNC] : D3DCMP_ALWAYS;
	key.m_bStencilEnable = rs[D3DRS_STENCILENABLE] != 0;
	if ( key.m_bStencilEnable )
	{
		key.m_stencilFunc = (uint8)rs[D3DRS_STENCILFUNC];
		key.m_stencilFail = (uint8)rs[D3DRS_STENCILFAIL];
		key.m_stencilZFail = (uint8)rs[D3DRS_STENCILZFAIL];
		key.m_stencilPass = (uint8)rs[D3DRS_STENCILPASS];
		key.m_stencilReadMask = (uint8)rs[D3DRS_STENCILMASK];
		key.m_stencilWriteMask = (uint8)rs[D3DRS_STENCILWRITEMASK];
	}

	uint32 hash = (uint32)HashBytes( &key, sizeof( key ) );
	auto it = dev->m_depthStencilStates.find( hash );
	if ( it != dev->m_depthStencilStates.end() )
		return it->second;

	MTLDepthStencilDescriptor *dsd = [MTLDepthStencilDescriptor new];
	dsd.depthCompareFunction = CompareFunc( key.m_depthFunc );
	dsd.depthWriteEnabled = key.m_bDepthWrite;
	if ( key.m_bStencilEnable )
	{
		// Two-sided stencil isn't used; ToGL applies one setting to both faces
		MTLStencilDescriptor *sd = [MTLStencilDescriptor new];
		sd.stencilCompareFunction = CompareFunc( key.m_stencilFunc );
		sd.stencilFailureOperation = StencilOp( key.m_stencilFail );
		sd.depthFailureOperation = StencilOp( key.m_stencilZFail );
		sd.depthStencilPassOperation = StencilOp( key.m_stencilPass );
		sd.readMask = key.m_stencilReadMask;
		sd.writeMask = key.m_stencilWriteMask;
		dsd.frontFaceStencil = sd;
		dsd.backFaceStencil = sd;
	}
	id<MTLDepthStencilState> state = [dev->m_device newDepthStencilStateWithDescriptor:dsd];
	dev->m_depthStencilStates[ hash ] = state;
	return state;
}

static id<MTLSamplerState> GetSamplerState( CMtlDevice *dev, const MtlSamplerState &s, bool bCompare )
{
	MtlSamplerState key = s;
	key.m_srgb = 0;
	key.m_compare = bCompare ? 1 : 0;
	uint64 hash = HashBytes( &key, sizeof( key ) );
	auto it = dev->m_samplerStates.find( hash );
	if ( it != dev->m_samplerStates.end() )
		return it->second;

	static const MTLSamplerAddressMode addressModes[] = { MTLSamplerAddressModeRepeat, MTLSamplerAddressModeClampToEdge, MTLSamplerAddressModeClampToBorderColor };
	MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
	sd.sAddressMode = addressModes[ MIN( key.m_addressU, (uint8)2 ) ];
	sd.tAddressMode = addressModes[ MIN( key.m_addressV, (uint8)2 ) ];
	sd.rAddressMode = addressModes[ MIN( key.m_addressW, (uint8)2 ) ];
	sd.minFilter = ( key.m_minFilter >= D3DTEXF_LINEAR ) ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
	sd.magFilter = ( key.m_magFilter >= D3DTEXF_LINEAR ) ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
	sd.mipFilter = ( key.m_mipFilter == D3DTEXF_NONE ) ? MTLSamplerMipFilterNotMipmapped : ( ( key.m_mipFilter == D3DTEXF_POINT ) ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear );
	sd.maxAnisotropy = MAX( 1, MIN( 16, (int)key.m_maxAniso ) );
	sd.lodMinClamp = key.m_minLOD;

	// Metal has three border colors; pick the nearest
	uint8 a = key.m_borderColor >> 24, r = ( key.m_borderColor >> 16 ) & 0xFF, g = ( key.m_borderColor >> 8 ) & 0xFF, b = key.m_borderColor & 0xFF;
	if ( a < 128 )
		sd.borderColor = MTLSamplerBorderColorTransparentBlack;
	else if ( r + g + b < 384 )
		sd.borderColor = MTLSamplerBorderColorOpaqueBlack;
	else
		sd.borderColor = MTLSamplerBorderColorOpaqueWhite;

	if ( bCompare )
	{
		// A shadow lookup passes when the reference is at or in front of the stored depth
		sd.compareFunction = MTLCompareFunctionLessEqual;
	}

	id<MTLSamplerState> state = [dev->m_device newSamplerStateWithDescriptor:sd];
	dev->m_samplerStates[ hash ] = state;
	return state;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Draws
// ------------------------------------------------------------------------------------------------------------------------------ //

// The texture a sampler reads, after the D3D state has had its say
static id<MTLTexture> SampledTexture( CMtlDevice *dev, int nSampler, uint nType, bool bShadow )
{
	IDirect3DBaseTexture9 *pBase = dev->m_samplers[nSampler].m_pTexture;
	CMtlTex *tex = pBase ? pBase->m_tex : NULL;

	if ( bShadow )
	{
		if ( tex && ( tex->m_nFlags & kMtlTexDepth ) && tex->m_nSamples == 1 && tex->m_type == MTLTextureType2D )
		{
			tex->m_nLastUseSerial = dev->CurrentSerial();
			return tex->m_tex;
		}
		return dev->m_dummyDepth;
	}

	MTLTextureType want = ( nType == SAMPLER_TYPE_CUBE ) ? MTLTextureTypeCube : ( ( nType == SAMPLER_TYPE_3D ) ? MTLTextureType3D : MTLTextureType2D );
	id<MTLTexture> dummy = ( want == MTLTextureTypeCube ) ? dev->m_dummyTexCube : ( ( want == MTLTextureType3D ) ? dev->m_dummyTex3D : dev->m_dummyTex2D );
	if ( !tex || tex->m_type != want )
		return dummy;

	if ( tex->m_nSamples > 1 )
		return dummy;
	tex->m_nLastUseSerial = dev->CurrentSerial();
	if ( dev->m_samplers[nSampler].m_state.m_srgb && tex->m_texSampleSRGB )
		return tex->m_texSampleSRGB;
	return tex->m_texSample;
}

// How the next draw's sRGB write is met in the open pass. The views the pass
// draws its targets through were chosen when it opened; a draw that wants the
// other view of target 0 converts in its pixel shader, and blends there too,
// reading the target, if it blends. A draw that writes no color needn't.
// Other targets aren't converted, nor blends by D3DRS_BLENDFACTOR: then the
// pass must reopen.
bool CMtlDevice::SRGBConversion( uint8 *pConvert )
{
	const DWORD *rs = m_renderStates;
	uint nWant = rs[D3DRS_SRGBWRITEENABLE] ? m_nPassSRGBViewMask : 0;
	*pConvert = kMtlSRGBConvertNone;
	if ( nWant == m_nPassSRGBMask || !( rs[D3DRS_COLORWRITEENABLE] & 0xF ) )
		return true;

	for ( int i = 1; i < MTL_MAX_RENDER_TARGETS; i++ )
	{
		if ( m_passKey.m_color[i] != MTLPixelFormatInvalid )
			return false;
	}
	if ( rs[D3DRS_ALPHABLENDENABLE] )
	{
		DWORD src = rs[D3DRS_SRCBLEND], dst = rs[D3DRS_DESTBLEND];
		if ( !m_bFramebufferFetch || src == D3DBLEND_BLENDFACTOR || src == D3DBLEND_INVBLENDFACTOR || dst == D3DBLEND_BLENDFACTOR || dst == D3DBLEND_INVBLENDFACTOR )
			return false;
	}
	*pConvert = ( nWant & 1 ) ? kMtlSRGBConvertEncode : kMtlSRGBConvertDecode;
	return true;
}

bool CMtlDevice::PrepareDraw( D3DPRIMITIVETYPE type )
{
	if ( !m_pVertexShader || !m_pPixelShader )
		return false;
	CMtlShader *pVS = m_pVertexShader->m_shader;
	CMtlShader *pPS = m_pPixelShader->m_shader;
	if ( !pVS->m_bValid || !pPS->m_bValid )
		return false;
	const CMtlVertexDecl *pDecl = m_pVertDecl ? m_pVertDecl->m_decl : NULL;

	id<MTLRenderCommandEncoder> enc = RenderEncoder();
	if ( !enc )
		return false;

	const DWORD *rs = m_renderStates;

	// sRGB write through the views the pass opened with, converting if need be
	uint8 srgbConvert;
	if ( !SRGBConversion( &srgbConvert ) )
	{
		EndEncoders();
		enc = RenderEncoder();
		if ( !enc )
			return false;
		srgbConvert = kMtlSRGBConvertNone;
	}

	// Pipeline
	MtlPipelineKey key;
	memset( &key, 0, sizeof( key ) );
	key.m_vsId = pVS->m_nId;
	key.m_psId = pPS->m_nId;
	key.m_nExtraOutputs = pPS->m_info.m_nInterpolantMask & ~pVS->m_info.m_nInterpolantMask;
	key.m_declHash = pDecl ? pDecl->m_nHash : 0;
	if ( pDecl )
	{
		for ( int s = 0; s < MTL_MAX_STREAMS; s++ )
		{
			if ( pDecl->m_nStreamMask & ( 1 << s ) )
				key.m_strides[s] = m_streams[s].m_stride;
		}
	}
	key.m_pass = m_passKey;
	key.m_blend.m_bBlendEnable = rs[D3DRS_ALPHABLENDENABLE] != 0;
	if ( key.m_blend.m_bBlendEnable )
	{
		key.m_blend.m_srcBlend = (uint8)rs[D3DRS_SRCBLEND];
		key.m_blend.m_dstBlend = (uint8)rs[D3DRS_DESTBLEND];
		key.m_blend.m_blendOp = (uint8)rs[D3DRS_BLENDOP];
	}
	key.m_blend.m_colorWriteMask = (uint8)( rs[D3DRS_COLORWRITEENABLE] & 0xF );
	key.m_blend.m_bAlphaToCoverage = rs[D3DRS_ADAPTIVETESS_Y] != 0;		// ToGL's alpha to coverage switch
	if ( rs[D3DRS_ALPHATESTENABLE] && rs[D3DRS_ALPHAFUNC] != D3DCMP_ALWAYS )
	{
		key.m_blend.m_alphaTestFunc = (uint8)rs[D3DRS_ALPHAFUNC];
	}
	key.m_blend.m_srgbConvert = srgbConvert;

	id<MTLRenderPipelineState> pipeline = GetPipeline( this, key, pVS, pPS, pDecl );
	if ( !pipeline )
		return false;
	if ( pipeline != m_curPipeline )
	{
		[enc setRenderPipelineState:pipeline];
		m_curPipeline = pipeline;
	}

	id<MTLDepthStencilState> ds = GetDepthStencilState( this );
	if ( ds != m_curDepthStencil )
	{
		[enc setDepthStencilState:ds];
		m_curDepthStencil = ds;
	}

	// Rasterizer state
	if ( m_bDynamicStateDirty )
	{
		MTLViewport vp;
		vp.originX = m_viewport.X;
		vp.originY = m_viewport.Y;
		vp.width = m_viewport.Width;
		vp.height = m_viewport.Height;
		vp.znear = m_viewport.MinZ;
		vp.zfar = m_viewport.MaxZ;
		[enc setViewport:vp];

		MTLScissorRect sr = { 0, 0, m_nPassWidth, m_nPassHeight };
		if ( rs[D3DRS_SCISSORTESTENABLE] )
		{
			int x0 = MAX( 0, (int)m_scissorRect.left ), y0 = MAX( 0, (int)m_scissorRect.top );
			int x1 = MIN( (int)m_nPassWidth, (int)m_scissorRect.right ), y1 = MIN( (int)m_nPassHeight, (int)m_scissorRect.bottom );
			if ( x1 <= x0 || y1 <= y0 )
				return false;
			sr.x = x0; sr.y = y0; sr.width = x1 - x0; sr.height = y1 - y0;
		}
		[enc setScissorRect:sr];

		switch ( rs[D3DRS_CULLMODE] )
		{
		case D3DCULL_CW:
			[enc setFrontFacingWinding:MTLWindingCounterClockwise];
			[enc setCullMode:MTLCullModeBack];
			break;
		case D3DCULL_CCW:
			[enc setFrontFacingWinding:MTLWindingClockwise];
			[enc setCullMode:MTLCullModeBack];
			break;
		default:
			[enc setCullMode:MTLCullModeNone];
			break;
		}
		[enc setTriangleFillMode:( rs[D3DRS_FILLMODE] == D3DFILL_SOLID ) ? MTLTriangleFillModeFill : MTLTriangleFillModeLines];

		DWORD bias = rs[D3DRS_DEPTHBIAS], slope = rs[D3DRS_SLOPESCALEDEPTHBIAS];
		[enc setDepthBias:*(float*)&bias slopeScale:*(float*)&slope clamp:0.0f];

		m_nCurStencilRef = rs[D3DRS_STENCILREF] & 0xFF;
		[enc setStencilReferenceValue:m_nCurStencilRef];

		m_bDynamicStateDirty = false;
	}
	else if ( rs[D3DRS_SCISSORTESTENABLE] )
	{
		int x0 = MAX( 0, (int)m_scissorRect.left ), y0 = MAX( 0, (int)m_scissorRect.top );
		int x1 = MIN( (int)m_nPassWidth, (int)m_scissorRect.right ), y1 = MIN( (int)m_nPassHeight, (int)m_scissorRect.bottom );
		if ( x1 <= x0 || y1 <= y0 )
			return false;
	}

	// Vertex streams
	if ( pDecl )
	{
		for ( int s = 0; s < MTL_MAX_STREAMS; s++ )
		{
			if ( !( pDecl->m_nStreamMask & ( 1 << s ) ) )
				continue;
			IDirect3DVertexBuffer9 *vb = m_streams[s].m_vtxBuffer;
			id<MTLBuffer> buf = vb ? vb->m_vtxBuffer->m_buf : m_zeroBuffer;
			uint offset = vb ? m_streams[s].m_offset : 0;
			if ( vb )
			{
				vb->m_vtxBuffer->m_nLastUseSerial = m_nSerial;
			}
			if ( buf != m_curVertexBuffers[s] )
			{
				[enc setVertexBuffer:buf offset:offset atIndex:s];
				m_curVertexBuffers[s] = buf;
				m_curVertexOffsets[s] = offset;
			}
			else if ( offset != m_curVertexOffsets[s] )
			{
				[enc setVertexBufferOffset:offset atIndex:s];
				m_curVertexOffsets[s] = offset;
			}
		}
	}
	if ( m_curVertexBuffers[MTL_DUMMY_VERTEX_BUFFER_INDEX] != m_zeroBuffer )
	{
		[enc setVertexBuffer:m_zeroBuffer offset:0 atIndex:MTL_DUMMY_VERTEX_BUFFER_INDEX];
		m_curVertexBuffers[MTL_DUMMY_VERTEX_BUFFER_INDEX] = m_zeroBuffer;
	}

	// Constants
	uint nVSCount = MIN( (uint)MTL_VS_CONSTANT_COUNT, MAX( 1u, pVS->m_info.m_nHighWater ) );
	if ( m_bVSConstantsDirty || !m_bVSConstantsBound || nVSCount > m_nVSUploadedCount )
	{
		uint offset;
		id<MTLBuffer> buf = Upload( m_vsConstants, nVSCount * 16, 256, &offset );
		[enc setVertexBuffer:buf offset:offset atIndex:D3DTOMETAL_VS_CONSTANT_BUFFER];
		m_bVSConstantsDirty = false;
		m_bVSConstantsBound = true;
		m_nVSUploadedCount = nVSCount;
	}
	uint nPSCount = MIN( (uint)MTL_PS_CONSTANT_COUNT, MAX( 1u, pPS->m_info.m_nHighWater ) );
	if ( m_bPSConstantsDirty || !m_bPSConstantsBound || nPSCount > m_nPSUploadedCount )
	{
		uint offset;
		id<MTLBuffer> buf = Upload( m_psConstants, nPSCount * 16, 256, &offset );
		[enc setFragmentBuffer:buf offset:offset atIndex:D3DTOMETAL_PS_CONSTANT_BUFFER];
		m_bPSConstantsDirty = false;
		m_bPSConstantsBound = true;
		m_nPSUploadedCount = nPSCount;
	}

	// vcscreen follows the viewport; clip planes follow their enable
	float w = (float)MAX( 1u, (uint)m_viewport.Width ), h = (float)MAX( 1u, (uint)m_viewport.Height );
	int nClipEnable = (int)( rs[D3DRS_CLIPPLANEENABLE] & 3 );
	if ( m_vsMisc.m_vScreen[2] != w || m_vsMisc.m_vScreen[3] != h || m_vsMisc.m_nClipPlaneEnable != nClipEnable )
	{
		m_vsMisc.m_vScreen[0] = 1.0f / w;
		m_vsMisc.m_vScreen[1] = 1.0f / h;
		m_vsMisc.m_vScreen[2] = w;
		m_vsMisc.m_vScreen[3] = h;
		m_vsMisc.m_nClipPlaneEnable = nClipEnable;
		m_bVSMiscDirty = true;
	}
	if ( m_bVSMiscDirty )
	{
		[enc setVertexBytes:&m_vsMisc length:sizeof( m_vsMisc ) atIndex:D3DTOMETAL_VS_MISC_BUFFER];
		m_bVSMiscDirty = false;
	}
	float flAlphaRef = ( rs[D3DRS_ALPHAREF] & 0xFF ) / 255.0f;
	if ( m_psMisc.m_flAlphaRef != flAlphaRef )
	{
		m_psMisc.m_flAlphaRef = flAlphaRef;
		m_bPSMiscDirty = true;
	}
	if ( m_bPSMiscDirty )
	{
		[enc setFragmentBytes:&m_psMisc length:sizeof( m_psMisc ) atIndex:D3DTOMETAL_PS_MISC_BUFFER];
		m_bPSMiscDirty = false;
	}

	// Textures and samplers
	uint nMask = pPS->m_info.m_nSamplerMask;
	for ( int i = 0; i < MTL_SAMPLER_COUNT; i++ )
	{
		if ( !( nMask & ( 1 << i ) ) )
			continue;
		uint nType = ( pPS->m_info.m_nSamplerTypes >> ( i * 2 ) ) & 3;
		bool bShadow = ( pPS->m_info.m_nShadowSamplerMask & ( 1 << i ) ) != 0;
		id<MTLTexture> tex = SampledTexture( this, i, nType, bShadow );
		if ( tex != m_curTextures[i] )
		{
			[enc setFragmentTexture:tex atIndex:i];
			m_curTextures[i] = tex;
		}
		id<MTLSamplerState> samp = GetSamplerState( this, m_samplers[i].m_state, bShadow );
		if ( samp != m_curSamplers[i] )
		{
			[enc setFragmentSamplerState:samp atIndex:i];
			m_curSamplers[i] = samp;
		}
	}

	m_bPassHasDraws = true;
	return true;
}

static MTLPrimitiveType PrimitiveType( D3DPRIMITIVETYPE type, uint nPrims, uint *pnVertices )
{
	switch ( type )
	{
	case D3DPT_POINTLIST:		*pnVertices = nPrims;		return MTLPrimitiveTypePoint;
	case D3DPT_LINELIST:		*pnVertices = nPrims * 2;	return MTLPrimitiveTypeLine;
	case D3DPT_LINESTRIP:		*pnVertices = nPrims + 1;	return MTLPrimitiveTypeLineStrip;
	case D3DPT_TRIANGLESTRIP:	*pnVertices = nPrims + 2;	return MTLPrimitiveTypeTriangleStrip;
	case D3DPT_TRIANGLEFAN:		*pnVertices = nPrims * 3;	return MTLPrimitiveTypeTriangle;		// drawn as a list
	default:					*pnVertices = nPrims * 3;	return MTLPrimitiveTypeTriangle;
	}
}

void CMtlDevice::Draw( D3DPRIMITIVETYPE type, uint nStartVertex, uint nPrimCount )
{
	if ( !nPrimCount || type == D3DPT_POINTLIST )
		return;
	bool bOK = PrepareDraw( type );
	if ( g_pMtlDump )
		MtlDumpDraw( this, type, nPrimCount, bOK );
	if ( !bOK )
		return;

	m_nDrawCallIndex++;
	g_nMtlTotalDraws++;
	g_nMtlTotalPrimitives += nPrimCount;

	uint nVertices;
	MTLPrimitiveType prim = PrimitiveType( type, nPrimCount, &nVertices );
	if ( type == D3DPT_TRIANGLEFAN )
	{
		// Metal has no fans: index them as a triangle list
		std::vector<uint32> indices( nPrimCount * 3 );
		for ( uint i = 0; i < nPrimCount; i++ )
		{
			indices[i*3+0] = nStartVertex;
			indices[i*3+1] = nStartVertex + i + 1;
			indices[i*3+2] = nStartVertex + i + 2;
		}
		uint offset;
		id<MTLBuffer> ib = Upload( indices.data(), (uint)indices.size() * 4, 4, &offset );
		[m_renderEncoder drawIndexedPrimitives:prim indexCount:indices.size() indexType:MTLIndexTypeUInt32 indexBuffer:ib indexBufferOffset:offset];
		return;
	}
	[m_renderEncoder drawPrimitives:prim vertexStart:nStartVertex vertexCount:nVertices];
}

void CMtlDevice::DrawIndexed( D3DPRIMITIVETYPE type, int nBaseVertex, uint nMinVertex, uint nNumVertices, uint nStartIndex, uint nPrimCount )
{
	if ( !nPrimCount || type == D3DPT_POINTLIST || !m_pIndices )
		return;
	bool bOK = PrepareDraw( type );
	if ( g_pMtlDump )
		MtlDumpDraw( this, type, nPrimCount, bOK );
	if ( !bOK )
		return;

	m_nDrawCallIndex++;
	g_nMtlTotalDraws++;
	g_nMtlTotalPrimitives += nPrimCount;

	CMtlBuffer *pIB = m_pIndices->m_idxBuffer;
	pIB->m_nLastUseSerial = m_nSerial;
	uint nIndexSize = pIB->m_bIndex32 ? 4 : 2;
	MTLIndexType indexType = pIB->m_bIndex32 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;

	uint nIndices;
	MTLPrimitiveType prim = PrimitiveType( type, nPrimCount, &nIndices );
	id<MTLBuffer> ib = pIB->m_buf;
	uint offset = nStartIndex * nIndexSize;

	if ( type == D3DPT_TRIANGLEFAN )
	{
		std::vector<uint32> indices( nPrimCount * 3 );
		const uint8 *src = (const uint8*)[ib contents] + offset;
		for ( uint i = 0; i < nPrimCount; i++ )
		{
			for ( uint k = 0; k < 3; k++ )
			{
				uint n = ( k == 0 ) ? 0 : i + k;
				indices[i*3+k] = pIB->m_bIndex32 ? ((const uint32*)src)[n] : ((const uint16*)src)[n];
			}
		}
		ib = Upload( indices.data(), (uint)indices.size() * 4, 4, &offset );
		indexType = MTLIndexTypeUInt32;
	}
	else if ( offset & 3 )
	{
		// Index buffer offsets must be 4 byte aligned
		ib = Upload( (const uint8*)[pIB->m_buf contents] + offset, nIndices * nIndexSize, 4, &offset );
	}

	[m_renderEncoder drawIndexedPrimitives:prim indexCount:nIndices indexType:indexType indexBuffer:ib indexBufferOffset:offset instanceCount:1 baseVertex:nBaseVertex baseInstance:0];
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Clears
// ------------------------------------------------------------------------------------------------------------------------------ //

static id<MTLRenderPipelineState> UtilPipeline( CMtlDevice *dev, const char *pFragment, const MtlRenderPassKey &pass, bool bColorWrite, bool bDepthOnly )
{
	struct { MtlRenderPassKey pass; uint64 frag; uint8 colorWrite, depthOnly, pad[6]; } key;
	memset( &key, 0, sizeof( key ) );
	key.pass = pass;
	key.frag = HashBytes( pFragment ? pFragment : "", pFragment ? strlen( pFragment ) : 0 );
	key.colorWrite = bColorWrite;
	key.depthOnly = bDepthOnly;
	uint64 hash = HashBytes( &key, sizeof( key ) );
	auto it = dev->m_utilPipelines.find( hash );
	if ( it != dev->m_utilPipelines.end() )
		return it->second;

	MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
	pd.vertexFunction = [dev->m_utilLibrary newFunctionWithName:@"quad_vs"];
	pd.fragmentFunction = pFragment ? [dev->m_utilLibrary newFunctionWithName:[NSString stringWithUTF8String:pFragment]] : nil;
	for ( int i = 0; i < MTL_MAX_RENDER_TARGETS; i++ )
	{
		pd.colorAttachments[i].pixelFormat = pass.m_color[i];
		pd.colorAttachments[i].writeMask = bColorWrite ? MTLColorWriteMaskAll : MTLColorWriteMaskNone;
	}
	pd.depthAttachmentPixelFormat = pass.m_depth;
	pd.stencilAttachmentPixelFormat = pass.m_stencil;
	pd.rasterSampleCount = MAX( 1u, pass.m_nSamples );

	NSError *err = nil;
	id<MTLRenderPipelineState> ps = [dev->m_device newRenderPipelineStateWithDescriptor:pd error:&err];
	if ( !ps )
	{
		Warning( "ToMetal: built-in pipeline %s failed: %s\n", pFragment ? pFragment : "(depth)", [[err description] UTF8String] );
	}
	dev->m_utilPipelines[ hash ] = ps;
	return ps;
}

// D3D Clear ignores the color, depth and stencil write masks. A rect limits
// it; without one it honours the scissor, as in ToGL.
void CMtlDevice::Clear( DWORD nCount, const D3DRECT *pRects, DWORD nFlags, D3DCOLOR color, float z, DWORD stencil )
{
	bool bColor = ( nFlags & D3DCLEAR_TARGET ) != 0;
	bool bDepth = ( nFlags & D3DCLEAR_ZBUFFER ) != 0 && m_pDepthStencil;
	bool bStencil = ( nFlags & D3DCLEAR_STENCIL ) != 0 && m_pDepthStencil && ( m_pDepthStencil->m_tex->m_nFlags & kMtlTexStencil );
	if ( !bColor && !bDepth && !bStencil )
		return;

	m_nDrawCallIndex++;

	// D3DCOLOR is ARGB
	float r = ( ( color >> 16 ) & 0xFF ) / 255.0f, g = ( ( color >> 8 ) & 0xFF ) / 255.0f, b = ( color & 0xFF ) / 255.0f, a = ( color >> 24 ) / 255.0f;

	// The region: the targets' common size, cut by the rect or scissor
	uint width = 0x7FFFFFFF, height = 0x7FFFFFFF;
	for ( int i = 0; i < MTL_MAX_RENDER_TARGETS; i++ )
	{
		if ( m_pRenderTargets[i] && m_pRenderTargets[i]->m_tex )
		{
			width = MIN( width, m_pRenderTargets[i]->m_tex->MipWidth( m_pRenderTargets[i]->m_mip ) );
			height = MIN( height, m_pRenderTargets[i]->m_tex->MipHeight( m_pRenderTargets[i]->m_mip ) );
		}
	}
	if ( m_pDepthStencil && m_pDepthStencil->m_tex )
	{
		width = MIN( width, m_pDepthStencil->m_tex->m_nWidth );
		height = MIN( height, m_pDepthStencil->m_tex->m_nHeight );
	}
	if ( width == 0x7FFFFFFF )
		return;

	int x0 = 0, y0 = 0, x1 = width, y1 = height;
	if ( nCount && pRects )
	{
		x0 = MAX( x0, (int)pRects[0].x1 ); y0 = MAX( y0, (int)pRects[0].y1 );
		x1 = MIN( x1, (int)pRects[0].x2 ); y1 = MIN( y1, (int)pRects[0].y2 );
	}
	else if ( m_renderStates[D3DRS_SCISSORTESTENABLE] )
	{
		x0 = MAX( x0, (int)m_scissorRect.left ); y0 = MAX( y0, (int)m_scissorRect.top );
		x1 = MIN( x1, (int)m_scissorRect.right ); y1 = MIN( y1, (int)m_scissorRect.bottom );
	}
	if ( x1 <= x0 || y1 <= y0 )
		return;

	bool bFull = ( x0 == 0 && y0 == 0 && x1 == (int)width && y1 == (int)height );
	if ( g_pMtlDump )
		MtlDumpClear( this, nCount, nFlags, color, z, stencil, bFull && nCount <= 1 && ( !m_renderEncoder || m_bPassDirty || !m_bPassHasDraws ) );

	// Nothing drawn yet in this pass: clear with the load actions
	if ( bFull && nCount <= 1 && ( !m_renderEncoder || m_bPassDirty || !m_bPassHasDraws ) )
	{
		if ( m_renderEncoder && !m_bPassDirty )
		{
			EndEncoders();
		}
		m_bPassDirty = true;
		if ( bColor )
		{
			m_bPendingClearColor = true;
			m_bPendingClearSRGB = m_renderStates[D3DRS_SRGBWRITEENABLE] != 0;
			m_pendingClearColor = MTLClearColorMake( r, g, b, a );
		}
		if ( bDepth )
		{
			m_bPendingClearDepth = true;
			m_flPendingClearDepth = z;
		}
		if ( bStencil )
		{
			m_bPendingClearStencil = true;
			m_nPendingClearStencil = stencil & 0xFF;
		}
		return;
	}

	id<MTLRenderCommandEncoder> enc = RenderEncoder();
	if ( !enc )
		return;

	// The color for the view the pass draws target 0 through; one color goes
	// to every target, so the pass reopens if others are drawn through views
	// of the other kind
	uint nWantSRGB = m_renderStates[D3DRS_SRGBWRITEENABLE] ? m_nPassSRGBViewMask : 0;
	if ( ( nWantSRGB ^ m_nPassSRGBMask ) & ~1u )
	{
		EndEncoders();
		enc = RenderEncoder();
		if ( !enc )
			return;
	}
	MTLClearColor clearColor = ClearColorForView( MTLClearColorMake( r, g, b, a ), ( nWantSRGB & 1 ) != 0, ( m_nPassSRGBMask & 1 ) != 0 );

	bool bAnyColor = false;
	for ( int i = 0; i < MTL_MAX_RENDER_TARGETS; i++ )
		bAnyColor |= m_passKey.m_color[i] != MTLPixelFormatInvalid;

	id<MTLRenderPipelineState> ps = UtilPipeline( this, bAnyColor ? "clear_ps" : NULL, m_passKey, bColor, false );
	if ( !ps )
		return;

	for ( DWORD n = 0; n < MAX( 1u, (uint)nCount ); n++ )
	{
		if ( n > 0 )
		{
			x0 = MAX( 0, (int)pRects[n].x1 ); y0 = MAX( 0, (int)pRects[n].y1 );
			x1 = MIN( (int)width, (int)pRects[n].x2 ); y1 = MIN( (int)height, (int)pRects[n].y2 );
			if ( x1 <= x0 || y1 <= y0 )
				continue;
		}

		[enc setRenderPipelineState:ps];
		[enc setDepthStencilState:m_clearDepthStencil[ ( bDepth ? 1 : 0 ) | ( bStencil ? 2 : 0 ) ]];
		[enc setStencilReferenceValue:stencil & 0xFF];
		MTLViewport vp = { 0, 0, (double)m_nPassWidth, (double)m_nPassHeight, 0, 1 };
		[enc setViewport:vp];
		MTLScissorRect sr = { (NSUInteger)x0, (NSUInteger)y0, (NSUInteger)( x1 - x0 ), (NSUInteger)( y1 - y0 ) };
		[enc setScissorRect:sr];
		[enc setCullMode:MTLCullModeNone];
		[enc setTriangleFillMode:MTLTriangleFillModeFill];
		[enc setDepthBias:0 slopeScale:0 clamp:0];

		QuadParams qp;
		memset( &qp, 0, sizeof( qp ) );
		qp.dst[0] = -1; qp.dst[1] = 1; qp.dst[2] = 1; qp.dst[3] = -1;
		qp.color[0] = clearColor.red; qp.color[1] = clearColor.green; qp.color[2] = clearColor.blue; qp.color[3] = clearColor.alpha;
		qp.depth = MAX( 0.0f, MIN( 1.0f, z ) );
		[enc setVertexBytes:&qp length:sizeof( qp ) atIndex:0];
		if ( bAnyColor )
		{
			[enc setFragmentBytes:&qp length:sizeof( qp ) atIndex:0];
		}
		[enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
	}

	// The clear clobbered the encoder's state, its parameters included: they
	// went to vertex buffer 0, stream 0's, and fragment buffer 0, the pixel
	// shader constants'
	m_curPipeline = nil;
	m_curDepthStencil = nil;
	m_curVertexBuffers[0] = nil;
	m_bPSConstantsBound = false;
	m_bDynamicStateDirty = true;
	m_bPassHasDraws = true;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Textures: upload, readback, zero, resolve
// ------------------------------------------------------------------------------------------------------------------------------ //

void CMtlDevice::UploadSlice( CMtlTex *pTex, uint face, uint mip, const MtlSliceBacking &b, uint x, uint y, uint z, uint w, uint h, uint d )
{
	const MtlFormatDesc *pDesc = pTex->m_pFormat;
	uint bs = pDesc->m_nBlockSize;
	uint rowBlocks = ( w + bs - 1 ) / bs;
	uint rows = ( h + bs - 1 ) / bs;
	uint mtlRowBytes = rowBlocks * pDesc->m_nMtlBytesPerBlock;
	const uint8 *pSrc = b.m_pData + z * b.m_nSlicePitch + ( y / bs ) * b.m_nPitch + ( x / bs ) * pDesc->m_nD3DBytesPerBlock;

	MTLRegion region = MTLRegionMake3D( x, y, z, w, h, d );
	uint slice = ( pTex->m_type == MTLTextureTypeCube ) ? face : 0;

	bool bDirect = !( pTex->m_nFlags & kMtlTexRenderable ) && pTex->m_tex.storageMode == MTLStorageModeShared && !IsBusy( pTex->m_nLastUseSerial );
	bool bConvert = pDesc->m_eConvert != kMtlConvertNone;

	if ( bDirect && !bConvert )
	{
		[pTex->m_tex replaceRegion:region mipmapLevel:mip slice:slice withBytes:pSrc bytesPerRow:b.m_nPitch bytesPerImage:b.m_nSlicePitch];
		return;
	}

	// Pack the box tightly in Metal's format
	uint nBytes = mtlRowBytes * rows * d;
	uint offset = 0;
	id<MTLBuffer> staging = nil;
	uint8 *pDst;
	std::vector<uint8> temp;
	if ( bDirect )
	{
		temp.resize( nBytes );
		pDst = temp.data();
	}
	else
	{
		staging = Upload( NULL, nBytes, 256, &offset );
		pDst = (uint8*)[staging contents] + offset;
	}
	for ( uint zz = 0; zz < d; zz++ )
	{
		for ( uint row = 0; row < rows; row++ )
		{
			const uint8 *s = pSrc + zz * b.m_nSlicePitch + row * b.m_nPitch;
			uint8 *t = pDst + ( zz * rows + row ) * mtlRowBytes;
			MtlConvertRow( pDesc, s, t, rowBlocks );
		}
	}

	if ( bDirect )
	{
		[pTex->m_tex replaceRegion:region mipmapLevel:mip slice:slice withBytes:pDst bytesPerRow:mtlRowBytes bytesPerImage:mtlRowBytes * rows];
		return;
	}

	id<MTLBlitCommandEncoder> blit = BlitEncoder();
	[blit copyFromBuffer:staging sourceOffset:offset sourceBytesPerRow:mtlRowBytes sourceBytesPerImage:mtlRowBytes * rows sourceSize:MTLSizeMake( w, h, d )
		toTexture:pTex->m_tex destinationSlice:slice destinationLevel:mip destinationOrigin:MTLOriginMake( x, y, z )];
	pTex->m_nLastUseSerial = pTex->m_nLastWriteSerial = m_nSerial;
}

void CMtlDevice::ReadSlice( CMtlTex *pTex, uint face, uint mip, MtlSliceBacking &b )
{
	const MtlFormatDesc *pDesc = pTex->m_pFormat;
	if ( pDesc->m_eConvert != kMtlConvertNone || pDesc->m_bDepth )
	{
		Warning( "ToMetal: can't read back %s texels\n", pDesc->m_pName );
		return;
	}

	uint w = pTex->MipWidth( mip ), h = pTex->MipHeight( mip ), d = pTex->MipDepth( mip );
	uint slice = ( pTex->m_type == MTLTextureTypeCube ) ? face : 0;
	MTLRegion region = MTLRegionMake3D( 0, 0, 0, w, h, d );

	if ( pTex->m_tex.storageMode == MTLStorageModeShared )
	{
		WaitForSerial( pTex->m_nLastWriteSerial );
		[pTex->m_tex getBytes:b.m_pData bytesPerRow:b.m_nPitch bytesPerImage:b.m_nSlicePitch fromRegion:region mipmapLevel:mip slice:slice];
		return;
	}

	// Private: copy into a shared buffer and wait
	uint nBytes = b.m_nSlicePitch * d;
	id<MTLBuffer> buf = [m_device newBufferWithLength:nBytes options:MTLResourceStorageModeShared];
	id<MTLTexture> src = ( pTex->m_nSamples > 1 ) ? ResolvedTexture( pTex ) : pTex->m_tex;
	id<MTLBlitCommandEncoder> blit = BlitEncoder();
	[blit copyFromTexture:src sourceSlice:slice sourceLevel:mip sourceOrigin:MTLOriginMake( 0, 0, 0 ) sourceSize:MTLSizeMake( w, h, d )
		toBuffer:buf destinationOffset:0 destinationBytesPerRow:b.m_nPitch destinationBytesPerImage:b.m_nSlicePitch];
	Submit( true );
	memcpy( b.m_pData, [buf contents], nBytes );
}

// New render targets are cleared to zero, as ToGL's are; sampled textures
// whose mip chain the caller fills only partly get zeros from nFirstMip on.
void CMtlDevice::ZeroTexture( CMtlTex *pTex, uint nFirstMip )
{
	if ( pTex->m_nFlags & kMtlTexRenderable )
	{
		for ( uint face = 0; face < pTex->m_nFaces; face++ )
		{
			MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
			if ( pTex->m_nFlags & kMtlTexDepth )
			{
				rp.depthAttachment.texture = pTex->m_tex;
				rp.depthAttachment.loadAction = MTLLoadActionClear;
				rp.depthAttachment.clearDepth = 0.0;
				rp.depthAttachment.storeAction = MTLStoreActionStore;
				if ( pTex->m_nFlags & kMtlTexStencil )
				{
					rp.stencilAttachment.texture = pTex->m_tex;
					rp.stencilAttachment.loadAction = MTLLoadActionClear;
					rp.stencilAttachment.clearStencil = 0;
					rp.stencilAttachment.storeAction = MTLStoreActionStore;
				}
			}
			else
			{
				rp.colorAttachments[0].texture = pTex->m_tex;
				rp.colorAttachments[0].slice = face;
				rp.colorAttachments[0].loadAction = MTLLoadActionClear;
				rp.colorAttachments[0].clearColor = MTLClearColorMake( 0, 0, 0, 0 );
				rp.colorAttachments[0].storeAction = MTLStoreActionStore;
			}
			FlushPass();
			id<MTLRenderCommandEncoder> enc = [CommandBuffer() renderCommandEncoderWithDescriptor:rp];
			[enc endEncoding];
		}
		pTex->m_nLastUseSerial = pTex->m_nLastWriteSerial = m_nSerial;
		return;
	}

	if ( nFirstMip >= pTex->m_nMips )
		return;
	const MtlFormatDesc *pDesc = pTex->m_pFormat;
	uint rowBytes = ( ( pTex->m_nWidth + pDesc->m_nBlockSize - 1 ) / pDesc->m_nBlockSize ) * pDesc->m_nMtlBytesPerBlock;
	uint rows = ( pTex->m_nHeight + pDesc->m_nBlockSize - 1 ) / pDesc->m_nBlockSize;
	std::vector<uint8> zeros( (size_t)rowBytes * rows * pTex->m_nDepth, 0 );
	for ( uint face = 0; face < pTex->m_nFaces; face++ )
	{
		for ( uint mip = nFirstMip; mip < pTex->m_nMips; mip++ )
		{
			uint w = pTex->MipWidth( mip ), h = pTex->MipHeight( mip ), d = pTex->MipDepth( mip );
			uint rb = ( ( w + pDesc->m_nBlockSize - 1 ) / pDesc->m_nBlockSize ) * pDesc->m_nMtlBytesPerBlock;
			uint r = ( h + pDesc->m_nBlockSize - 1 ) / pDesc->m_nBlockSize;
			[pTex->m_tex replaceRegion:MTLRegionMake3D( 0, 0, 0, w, h, d ) mipmapLevel:mip slice:( pTex->m_type == MTLTextureTypeCube ) ? face : 0
				withBytes:zeros.data() bytesPerRow:rb bytesPerImage:rb * r];
		}
	}
}

// A single sample copy of a multisampled target, brought up to date
id<MTLTexture> CMtlDevice::ResolvedTexture( CMtlTex *pTex )
{
	if ( pTex->m_nSamples <= 1 )
		return pTex->m_tex;

	if ( !pTex->m_texResolved )
	{
		MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pTex->m_tex.pixelFormat width:pTex->m_nWidth height:pTex->m_nHeight mipmapped:NO];
		desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget | ( pTex->m_texRenderSRGB ? MTLTextureUsagePixelFormatView : 0 );
		desc.storageMode = MTLStorageModePrivate;
		pTex->m_texResolved = [m_device newTextureWithDescriptor:desc];
	}

	// Resolved on every request; callers ask once per copy or present
	{
		FlushPass();
		MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
		if ( pTex->m_nFlags & kMtlTexDepth )
		{
			rp.depthAttachment.texture = pTex->m_tex;
			rp.depthAttachment.loadAction = MTLLoadActionLoad;
			rp.depthAttachment.storeAction = MTLStoreActionStoreAndMultisampleResolve;
			rp.depthAttachment.resolveTexture = pTex->m_texResolved;
			rp.depthAttachment.depthResolveFilter = MTLMultisampleDepthResolveFilterSample0;
		}
		else
		{
			rp.colorAttachments[0].texture = pTex->m_tex;
			rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
			rp.colorAttachments[0].storeAction = MTLStoreActionStoreAndMultisampleResolve;
			rp.colorAttachments[0].resolveTexture = pTex->m_texResolved;
		}
		id<MTLRenderCommandEncoder> enc = [CommandBuffer() renderCommandEncoderWithDescriptor:rp];
		[enc endEncoding];
		pTex->m_nLastUseSerial = m_nSerial;
	}
	return pTex->m_texResolved;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// StretchRect
// ------------------------------------------------------------------------------------------------------------------------------ //

void CMtlDevice::StretchRect( IDirect3DSurface9 *pSrcSurf, const RECT *pSrcRect, IDirect3DSurface9 *pDstSurf, const RECT *pDstRect, D3DTEXTUREFILTERTYPE filter )
{
	if ( !pSrcSurf || !pDstSurf || !pSrcSurf->m_tex || !pDstSurf->m_tex )
		return;

	if ( g_pMtlDump )
		MtlDumpCopy( "stretchrect", pSrcSurf, pDstSurf );

	CMtlTex *src = pSrcSurf->m_tex;
	CMtlTex *dst = pDstSurf->m_tex;
	if ( dst->m_nSamples > 1 )
	{
		Warning( "ToMetal: StretchRect into a multisampled surface isn't supported\n" );
		return;
	}

	uint srcMip = pSrcSurf->m_mip, dstMip = pDstSurf->m_mip;
	RECT s = pSrcRect ? *pSrcRect : RECT{ 0, 0, (int)src->MipWidth( srcMip ), (int)src->MipHeight( srcMip ) };
	RECT d = pDstRect ? *pDstRect : RECT{ 0, 0, (int)dst->MipWidth( dstMip ), (int)dst->MipHeight( dstMip ) };
	int sw = s.right - s.left, sh = s.bottom - s.top, dw = d.right - d.left, dh = d.bottom - d.top;
	if ( sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 )
		return;

	id<MTLTexture> srcTex = ( src->m_nSamples > 1 ) ? ResolvedTexture( src ) : src->m_tex;
	uint srcSlice = ( src->m_type == MTLTextureTypeCube ) ? pSrcSurf->m_face : 0;
	uint dstSlice = ( dst->m_type == MTLTextureTypeCube ) ? pDstSurf->m_face : 0;
	bool bDepth = ( src->m_nFlags & kMtlTexDepth ) != 0;

	src->m_nLastUseSerial = m_nSerial;
	dst->m_nLastUseSerial = dst->m_nLastWriteSerial = m_nSerial;

	// Same size and format: a copy
	if ( sw == dw && sh == dh && srcTex.pixelFormat == dst->m_tex.pixelFormat )
	{
		id<MTLBlitCommandEncoder> blit = BlitEncoder();
		[blit copyFromTexture:srcTex sourceSlice:srcSlice sourceLevel:( src->m_nSamples > 1 ) ? 0 : srcMip sourceOrigin:MTLOriginMake( s.left, s.top, 0 ) sourceSize:MTLSizeMake( sw, sh, 1 )
			toTexture:dst->m_tex destinationSlice:dstSlice destinationLevel:dstMip destinationOrigin:MTLOriginMake( d.left, d.top, 0 )];
		return;
	}

	// Otherwise draw the source into the destination, scaling the raw values
	FlushPass();
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	MtlRenderPassKey key;
	memset( &key, 0, sizeof( key ) );
	key.m_nSamples = 1;
	if ( bDepth )
	{
		rp.depthAttachment.texture = dst->m_tex;
		rp.depthAttachment.level = dstMip;
		rp.depthAttachment.loadAction = MTLLoadActionLoad;
		rp.depthAttachment.storeAction = MTLStoreActionStore;
		key.m_depth = dst->m_tex.pixelFormat;
		if ( dst->m_nFlags & kMtlTexStencil )
		{
			rp.stencilAttachment.texture = dst->m_tex;
			rp.stencilAttachment.level = dstMip;
			rp.stencilAttachment.loadAction = MTLLoadActionLoad;
			rp.stencilAttachment.storeAction = MTLStoreActionStore;
			key.m_stencil = dst->m_tex.pixelFormat;
		}
	}
	else
	{
		rp.colorAttachments[0].texture = dst->m_tex;
		rp.colorAttachments[0].level = dstMip;
		rp.colorAttachments[0].slice = dstSlice;
		rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
		rp.colorAttachments[0].storeAction = MTLStoreActionStore;
		key.m_color[0] = dst->m_tex.pixelFormat;
	}

	id<MTLRenderPipelineState> ps = UtilPipeline( this, bDepth ? "blit_depth_ps" : "blit_ps", key, true, bDepth );
	if ( !ps )
		return;

	id<MTLRenderCommandEncoder> enc = [CommandBuffer() renderCommandEncoderWithDescriptor:rp];
	[enc setRenderPipelineState:ps];
	if ( bDepth )
	{
		[enc setDepthStencilState:m_clearDepthStencil[1]];
	}
	MTLViewport vp = { (double)d.left, (double)d.top, (double)dw, (double)dh, 0, 1 };
	[enc setViewport:vp];

	float srcW = (float)src->MipWidth( srcMip ), srcH = (float)src->MipHeight( srcMip );
	QuadParams qp;
	memset( &qp, 0, sizeof( qp ) );
	qp.dst[0] = -1; qp.dst[1] = 1; qp.dst[2] = 1; qp.dst[3] = -1;
	qp.src[0] = s.left / srcW; qp.src[1] = s.top / srcH; qp.src[2] = s.right / srcW; qp.src[3] = s.bottom / srcH;
	[enc setVertexBytes:&qp length:sizeof( qp ) atIndex:0];

	MtlSamplerState ss;
	memset( &ss, 0, sizeof( ss ) );
	ss.m_addressU = ss.m_addressV = ss.m_addressW = D3DTADDRESS_CLAMP;
	ss.m_minFilter = ss.m_magFilter = ( filter == D3DTEXF_LINEAR && !bDepth ) ? D3DTEXF_LINEAR : D3DTEXF_POINT;
	ss.m_mipFilter = D3DTEXF_NONE;
	ss.m_maxAniso = 1;
	ss.m_minLOD = 0;

	id<MTLTexture> view = srcTex;
	if ( src->m_nSamples == 1 && src->m_nMips > 1 )
	{
		view = [srcTex newTextureViewWithPixelFormat:srcTex.pixelFormat textureType:MTLTextureType2D levels:NSMakeRange( srcMip, 1 ) slices:NSMakeRange( srcSlice, 1 )];
	}
	else if ( src->m_type == MTLTextureTypeCube )
	{
		view = [srcTex newTextureViewWithPixelFormat:srcTex.pixelFormat textureType:MTLTextureType2D levels:NSMakeRange( 0, 1 ) slices:NSMakeRange( srcSlice, 1 )];
	}
	[enc setFragmentTexture:view atIndex:0];
	[enc setFragmentSamplerState:GetSamplerState( this, ss, false ) atIndex:0];
	[enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
	[enc endEncoding];
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Present
// ------------------------------------------------------------------------------------------------------------------------------ //

// The blit of a finished frame into a drawable, done by either way of presenting
static void EncodePresentBlit( id<MTLCommandBuffer> cb, id<MTLRenderPipelineState> pipeline, id<MTLSamplerState> nearest, id<MTLSamplerState> linear,
	id<MTLTexture> src, id<CAMetalDrawable> drawable )
{
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	rp.colorAttachments[0].texture = drawable.texture;
	rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
	rp.colorAttachments[0].storeAction = MTLStoreActionStore;

	id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
	[enc setRenderPipelineState:pipeline];
	QuadParams qp;
	memset( &qp, 0, sizeof( qp ) );
	qp.dst[0] = -1; qp.dst[1] = 1; qp.dst[2] = 1; qp.dst[3] = -1;
	qp.src[0] = 0; qp.src[1] = 0; qp.src[2] = 1; qp.src[3] = 1;
	[enc setVertexBytes:&qp length:sizeof( qp ) atIndex:0];

	// Nearest when the sizes match, as ToGL's blit to the window
	bool bScales = drawable.texture.width != src.width || drawable.texture.height != src.height;
	[enc setFragmentTexture:src atIndex:0];
	[enc setFragmentSamplerState:bScales ? linear : nearest atIndex:0];
	[enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
	[enc endEncoding];
	[cb presentDrawable:drawable];
}

// With vsync off, frames are handed to a thread of their own that waits for
// drawables and shows the newest frame whenever the display takes one, so
// the game never waits for the window. A frame is copied into one of a few
// textures first, as the backbuffer is drawn into again at once.
class CMtlPresenter
{
public:
	CMtlPresenter( CMtlDevice *dev, id<MTLRenderPipelineState> pipeline, id<MTLSamplerState> nearest, id<MTLSamplerState> linear ) :
		m_dev( dev ), m_pipeline( pipeline ), m_nearest( nearest ), m_linear( linear ), m_state( std::make_shared<State>() )
	{
		m_thread = std::thread( [this] { ThreadMain(); } );
	}

	~CMtlPresenter()
	{
		{
			std::lock_guard<std::mutex> lock( m_state->m_mutex );
			m_state->m_bQuit = true;
		}
		m_state->m_cv.notify_all();
		m_thread.join();
	}

	// Copies the frame in the current command buffer; returns the slot to
	// publish once that command buffer is committed, or -1 to drop the frame.
	int CopyFrame( id<MTLTexture> src )
	{
		int slot = -1;
		{
			std::lock_guard<std::mutex> lock( m_state->m_mutex );
			for ( int i = 0; i < kSlots; i++ )
			{
				if ( m_state->m_slots[i] == kFree )
				{
					slot = i;
					m_state->m_slots[i] = kWriting;
					break;
				}
			}
		}
		if ( slot < 0 )
			return -1;

		__strong id<MTLTexture> &frame = m_frames[slot];
		if ( !frame || frame.width != src.width || frame.height != src.height || frame.pixelFormat != src.pixelFormat )
		{
			MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:src.pixelFormat width:src.width height:src.height mipmapped:NO];
			desc.usage = MTLTextureUsageShaderRead;
			desc.storageMode = MTLStorageModePrivate;
			frame = [m_dev->m_device newTextureWithDescriptor:desc];
		}
		id<MTLBlitCommandEncoder> blit = m_dev->BlitEncoder();
		[blit copyFromTexture:src sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake( 0, 0, 0 ) sourceSize:MTLSizeMake( src.width, src.height, 1 )
			toTexture:frame destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake( 0, 0, 0 )];
		return slot;
	}

	// The copy's command buffer is committed: the present thread's command
	// buffers, committed later to the same queue, run after it.
	void Publish( int slot )
	{
		{
			std::lock_guard<std::mutex> lock( m_state->m_mutex );
			if ( m_state->m_nLatest >= 0 )
				m_state->m_slots[m_state->m_nLatest] = kFree;		// never shown; a newer frame replaces it
			m_state->m_slots[slot] = kLatest;
			m_state->m_nLatest = slot;
		}
		m_state->m_cv.notify_one();
	}

private:
	enum { kSlots = 4 };
	enum ESlot { kFree, kWriting, kLatest, kPresenting };

	// Shared with the completion handlers, which may run after the presenter is gone
	struct State
	{
		std::mutex m_mutex;
		std::condition_variable m_cv;
		ESlot m_slots[kSlots] = { kFree, kFree, kFree, kFree };
		int m_nLatest = -1;
		bool m_bQuit = false;
	};

	void ThreadMain()
	{
		ThreadSetDebugName( "ToMetal present" );
		std::shared_ptr<State> state = m_state;
		for ( ;; )
		{
			int slot;
			id<MTLTexture> frame;
			{
				std::unique_lock<std::mutex> lock( state->m_mutex );
				state->m_cv.wait( lock, [&] { return state->m_bQuit || state->m_nLatest >= 0; } );
				if ( state->m_bQuit )
					return;
				slot = state->m_nLatest;
				state->m_nLatest = -1;
				state->m_slots[slot] = kPresenting;
				frame = m_frames[slot];
			}

			@autoreleasepool
			{
				id<CAMetalDrawable> drawable = [m_dev->m_layer nextDrawable];
				if ( !drawable )
				{
					std::lock_guard<std::mutex> lock( state->m_mutex );
					state->m_slots[slot] = kFree;
					continue;
				}
				id<MTLCommandBuffer> cb = [m_dev->m_queue commandBuffer];
				EncodePresentBlit( cb, m_pipeline, m_nearest, m_linear, frame, drawable );
				[cb addCompletedHandler:^( id<MTLCommandBuffer> ) {
					std::lock_guard<std::mutex> lock( state->m_mutex );
					state->m_slots[slot] = kFree;
				}];
				[cb commit];
			}
		}
	}

	CMtlDevice *m_dev;
	id<MTLRenderPipelineState> m_pipeline;
	id<MTLSamplerState> m_nearest, m_linear;
	id<MTLTexture> m_frames[kSlots];
	std::shared_ptr<State> m_state;
	std::thread m_thread;
};

static void DeletePresenter( CMtlPresenter *pPresenter )
{
	delete pPresenter;
}

void CMtlDevice::Present( IDirect3DSurface9 *pBackBuffer, bool bVSync )
{
	MtlDumpPresent( this );

	if ( m_layer.displaySyncEnabled != bVSync )
	{
		m_layer.displaySyncEnabled = bVSync;
	}

	// The present blit's pipeline and samplers, made here so the present
	// thread needn't touch the caches
	if ( !m_presentPipeline )
	{
		MtlRenderPassKey key;
		memset( &key, 0, sizeof( key ) );
		key.m_color[0] = m_layer.pixelFormat;
		key.m_nSamples = 1;
		m_presentPipeline = UtilPipeline( this, "blit_ps", key, true, false );

		MtlSamplerState ss;
		memset( &ss, 0, sizeof( ss ) );
		ss.m_addressU = ss.m_addressV = ss.m_addressW = D3DTADDRESS_CLAMP;
		ss.m_mipFilter = D3DTEXF_NONE;
		ss.m_maxAniso = 1;
		ss.m_minFilter = ss.m_magFilter = D3DTEXF_POINT;
		m_presentNearest = GetSamplerState( this, ss, false );
		ss.m_minFilter = ss.m_magFilter = D3DTEXF_LINEAR;
		m_presentLinear = GetSamplerState( this, ss, false );
	}

	int nPublish = -1;
	CGSize size = m_layer.drawableSize;
	if ( pBackBuffer && pBackBuffer->m_tex && size.width >= 1 && size.height >= 1 && m_presentPipeline )
	{
		CMtlTex *src = pBackBuffer->m_tex;
		id<MTLTexture> srcTex = ( src->m_nSamples > 1 ) ? ResolvedTexture( src ) : src->m_tex;
		FlushPass();

		if ( bVSync )
		{
			// Vsync just turned on: stop the present thread first, dropping
			// the frame it hasn't shown, so it can't show after this one
			if ( m_pPresenter )
			{
				DeletePresenter( m_pPresenter );
				m_pPresenter = NULL;
			}

			// Paced by the display: wait for its drawable here
			id<CAMetalDrawable> drawable = [m_layer nextDrawable];
			if ( drawable )
			{
				EncodePresentBlit( CommandBuffer(), m_presentPipeline, m_presentNearest, m_presentLinear, srcTex, drawable );
			}
		}
		else
		{
			if ( !m_pPresenter )
			{
				m_pPresenter = new CMtlPresenter( this, m_presentPipeline, m_presentNearest, m_presentLinear );
			}
			nPublish = m_pPresenter->CopyFrame( srcTex );
			FlushPass();
		}
		src->m_nLastUseSerial = m_nSerial;
	}

	// Pace the CPU: at most MTL_MAX_FRAMES_IN_FLIGHT frames queued
	dispatch_semaphore_t sem = m_frameSemaphore;
	[CommandBuffer() addCompletedHandler:^( id<MTLCommandBuffer> cb ) {
		dispatch_semaphore_signal( sem );
	}];
	Submit( false );
	if ( nPublish >= 0 )
	{
		m_pPresenter->Publish( nPublish );
	}
	dispatch_semaphore_wait( sem, DISPATCH_TIME_FOREVER );

	m_nFrame++;
	m_nCurUploadChunk = -1;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Occlusion queries
// ------------------------------------------------------------------------------------------------------------------------------ //

static void RecycleQuerySlots( CMtlDevice *dev, CMtlQuery *q )
{
	for ( size_t i = 0; i < q->m_slots.size(); i++ )
	{
		dev->m_retiredVisibilitySlots.push_back( std::make_pair( q->m_nEndSerial, q->m_slots[i] ) );
	}
	q->m_slots.clear();

	// Slots the GPU is done with can be handed out again
	size_t n = 0;
	for ( size_t i = 0; i < dev->m_retiredVisibilitySlots.size(); i++ )
	{
		if ( !dev->IsBusy( dev->m_retiredVisibilitySlots[i].first ) )
			dev->m_freeVisibilitySlots.push_back( dev->m_retiredVisibilitySlots[i].second );
		else
			dev->m_retiredVisibilitySlots[n++] = dev->m_retiredVisibilitySlots[i];
	}
	dev->m_retiredVisibilitySlots.resize( n );
}

void CMtlDevice::BeginOcclusion( CMtlQuery *pQuery )
{
	if ( m_pActiveQuery && m_pActiveQuery != pQuery )
	{
		EndOcclusion( m_pActiveQuery );
	}
	RecycleQuerySlots( this, pQuery );
	pQuery->m_bActive = true;
	m_pActiveQuery = pQuery;

	if ( m_renderEncoder && !m_bPassDirty && !m_freeVisibilitySlots.empty() )
	{
		uint slot = m_freeVisibilitySlots.back();
		m_freeVisibilitySlots.pop_back();
		( (uint64*)[m_visibilityBuffer contents] )[slot] = 0;
		pQuery->m_slots.push_back( slot );
		[m_renderEncoder setVisibilityResultMode:MTLVisibilityResultModeCounting offset:slot * sizeof( uint64 )];
	}
}

void CMtlDevice::EndOcclusion( CMtlQuery *pQuery )
{
	if ( m_pActiveQuery == pQuery )
	{
		if ( m_renderEncoder && !m_bPassDirty )
		{
			[m_renderEncoder setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
		}
		m_pActiveQuery = NULL;
	}
	pQuery->m_bActive = false;
	pQuery->m_nEndSerial = m_nSerial;
}

uint CMtlDevice::OcclusionResult( CMtlQuery *pQuery )
{
	uint64 total = 0;
	const uint64 *p = (const uint64*)[m_visibilityBuffer contents];
	for ( size_t i = 0; i < pQuery->m_slots.size(); i++ )
	{
		total += p[ pQuery->m_slots[i] ];
	}
	return (uint)MIN( total, (uint64)0xFFFFFFFF );
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Releases
// ------------------------------------------------------------------------------------------------------------------------------ //

void CMtlDevice::ReleasedTexture( IDirect3DBaseTexture9 *pTex )
{
	for ( int i = 0; i < MTL_SAMPLER_COUNT; i++ )
	{
		if ( m_samplers[i].m_pTexture == pTex )
		{
			m_samplers[i].m_pTexture = NULL;
		}
	}
}

void CMtlDevice::ReleasedMtlTex( CMtlTex *pTex )
{
	// A surface of this tex may still be bound as a target
	bool bTarget = false;
	for ( int i = 0; i < MTL_MAX_RENDER_TARGETS; i++ )
	{
		if ( m_pRenderTargets[i] && m_pRenderTargets[i]->m_tex == pTex )
			bTarget = true;
	}
	if ( m_pDepthStencil && m_pDepthStencil->m_tex == pTex )
		bTarget = true;
	if ( bTarget )
	{
		FlushPass();
	}
}

void CMtlDevice::ReleasedSurface( IDirect3DSurface9 *pSurf )
{
	for ( int i = 0; i < MTL_MAX_RENDER_TARGETS; i++ )
	{
		if ( m_pRenderTargets[i] == pSurf )
		{
			FlushPass();
			m_pRenderTargets[i] = NULL;
		}
	}
	if ( m_pDepthStencil == pSurf )
	{
		FlushPass();
		m_pDepthStencil = NULL;
	}
}

void CMtlDevice::ReleasedShader( CMtlShader *pShader )
{
	if ( m_pVertexShader && m_pVertexShader->m_shader == pShader )
		m_pVertexShader = NULL;
	if ( m_pPixelShader && m_pPixelShader->m_shader == pShader )
		m_pPixelShader = NULL;
}

void CMtlDevice::ReleasedVertexBuffer( IDirect3DVertexBuffer9 *pBuf )
{
	for ( int i = 0; i < MTL_MAX_STREAMS; i++ )
	{
		if ( m_streams[i].m_vtxBuffer == pBuf )
			m_streams[i].m_vtxBuffer = NULL;
	}
}

void CMtlDevice::ReleasedIndexBuffer( IDirect3DIndexBuffer9 *pBuf )
{
	if ( m_pIndices == pBuf )
		m_pIndices = NULL;
}

void CMtlDevice::ReleasedVertexDecl( IDirect3DVertexDeclaration9 *pDecl )
{
	if ( m_pVertDecl == pDecl )
		m_pVertDecl = NULL;
}

void CMtlDevice::ReleasedQuery( CMtlQuery *pQuery )
{
	if ( m_pActiveQuery == pQuery )
	{
		EndOcclusion( pQuery );
	}
	RecycleQuerySlots( this, pQuery );
}
