//================ Copyright (c) Valve Corporation. All Rights Reserved. =================
//
// mtlinternal.h
//
// ToMetal's private declarations: the Metal objects behind the D3D9 ones, and
// the device that turns D3D9 state into Metal pipelines and passes.
//
// Coordinate conventions: Metal and D3D9 agree on clip space depth (0..1), on
// Y pointing down in framebuffers and textures, and on which winding is
// clockwise, so no Y flip or depth remap is applied. The one difference is
// the pixel centre: D3D9 puts it on integer coordinates, Metal half a pixel
// further on, which the translated vertex shaders correct (see vcscreen).
//
//==================================================================================================
#ifndef MTLINTERNAL_H
#define MTLINTERNAL_H

// Objective-C's BOOL is bool; Source's (and D3D's) is int, and it appears in
// the D3D9 method signatures and structs. Apple's headers are read with their
// BOOL renamed, so BOOL means int everywhere in ToMetal, as in the shader API.
#define BOOL OBJC_BOOL
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>
#undef BOOL

#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <memory>

#include "togl/rendermechanism.h"
#include "tier0/threadtools.h"
#include "materialsystem/IShader.h"
#include "dx9asmtometal.h"

class ILauncherMgr;
class CMtlPresenter;

#define MTL_SAMPLER_COUNT		16
#define MTL_MAX_RENDER_TARGETS	4
#define MTL_MAX_STREAMS			D3D_MAX_STREAMS
#define MTL_DUMMY_VERTEX_BUFFER_INDEX	MTL_MAX_STREAMS		// a zero vertex for inputs the declaration lacks
#define MTL_VS_CONSTANT_COUNT	DXABSTRACT_VS_PARAM_SLOTS
#define MTL_PS_CONSTANT_COUNT	224
#define MTL_MAX_FRAMES_IN_FLIGHT	3

// ------------------------------------------------------------------------------------------------------------------------------ //
// Formats
// ------------------------------------------------------------------------------------------------------------------------------ //

enum EMtlConvert
{
	kMtlConvertNone,
	kMtlConvertR8G8B8,		// 24 bit BGR -> BGRA8
	kMtlConvertR5G6B5,		// -> BGRA8
	kMtlConvertX1R5G5B5,	// -> BGRA8
	kMtlConvertA1R5G5B5,	// -> BGRA8
	kMtlConvertA4R4G4B4,	// -> BGRA8
};

struct MtlFormatDesc
{
	D3DFORMAT			m_d3dFormat;
	const char			*m_pName;
	MTLPixelFormat		m_mtlFormat;		// storage
	MTLPixelFormat		m_mtlFormatSRGB;	// view used when sampling or rendering with sRGB on, or Invalid
	uint				m_nBlockSize;		// 1, or 4 for block compressed formats
	uint				m_nD3DBytesPerBlock;	// what the D3D side locks and writes, per texel or block
	uint				m_nMtlBytesPerBlock;	// what Metal stores
	EMtlConvert			m_eConvert;
	bool				m_bSwizzle;
	MTLTextureSwizzleChannels m_swizzle;
	bool				m_bDepth;
	bool				m_bStencil;
};

const MtlFormatDesc *MtlGetFormatDesc( D3DFORMAT format );
void MtlConvertRow( const MtlFormatDesc *pDesc, const uint8 *pSrc, uint8 *pDst, uint nTexels );

// ------------------------------------------------------------------------------------------------------------------------------ //
// Textures
// ------------------------------------------------------------------------------------------------------------------------------ //

enum EMtlTexFlags
{
	kMtlTexRenderable	= 0x01,		// render target or depth stencil
	kMtlTexDepth		= 0x02,
	kMtlTexStencil		= 0x04,
	kMtlTexMultisampled	= 0x08,
	kMtlTexSRGB			= 0x10,		// may be viewed as sRGB
};

// CPU copy of one face/mip, kept while locked; kept afterwards for slices
// that are updated after their first upload, so the next lock needn't read
// back.
struct MtlSliceBacking
{
	uint8			*m_pData;
	uint			m_nPitch;		// bytes per row (or row of blocks), D3D layout
	uint			m_nSlicePitch;	// bytes per depth slice, D3D layout
	bool			m_bLocked;
	bool			m_bReadOnly;
	bool			m_bPartial;		// the lock covers part of the slice
	bool			m_bKeep;		// the slice was locked again after its first upload
	uint			m_x, m_y, m_z, m_w, m_h, m_d;	// locked box, in texels
};

struct CMtlTex
{
	CMtlTex();
	~CMtlTex();

	id<MTLTexture>		m_tex;				// storage; rendered to, copied and uploaded to
	id<MTLTexture>		m_texRenderSRGB;	// sRGB view for rendering with D3DRS_SRGBWRITEENABLE, nil if the format has none
	id<MTLTexture>		m_texSample;		// sampled when D3DSAMP_SRGBTEXTURE is off (applies the format's swizzle)
	id<MTLTexture>		m_texSampleSRGB;	// sampled when D3DSAMP_SRGBTEXTURE is on, nil if the format has no sRGB variant
	id<MTLTexture>		m_texResolved;		// single sample copy of a multisampled target, made on demand
	const MtlFormatDesc	*m_pFormat;
	D3DFORMAT			m_d3dFormat;
	MTLTextureType		m_type;
	uint				m_nFlags;			// EMtlTexFlags
	uint				m_nWidth, m_nHeight, m_nDepth;
	uint				m_nMips;
	uint				m_nFaces;
	uint				m_nSamples;
	MtlSliceBacking		*m_pBacking;		// m_nFaces * m_nMips entries, allocated on first lock
	uint8				*m_pSliceValid;		// per slice: the texture holds data (else it's zero)
	uint64				m_nLastUseSerial;	// the last command buffer that read or wrote m_tex
	uint64				m_nLastWriteSerial;	// the last command buffer that wrote it
	char				*m_pDebugLabel;

	uint MipWidth( uint mip ) const { return MAX( 1u, m_nWidth >> mip ); }
	uint MipHeight( uint mip ) const { return MAX( 1u, m_nHeight >> mip ); }
	uint MipDepth( uint mip ) const { return MAX( 1u, m_nDepth >> mip ); }
};

// ------------------------------------------------------------------------------------------------------------------------------ //
// Buffers
// ------------------------------------------------------------------------------------------------------------------------------ //

struct CMtlBuffer
{
	id<MTLBuffer>		m_buf;
	uint				m_nSize;
	uint64				m_nLastUseSerial;
	bool				m_bDynamic;
	bool				m_bIndex32;
	// Locked ranges written through a staging copy because the buffer was busy
	uint8				*m_pStaging;
	uint				m_nLockOffset, m_nLockSize;
};

// ------------------------------------------------------------------------------------------------------------------------------ //
// Vertex declarations
// ------------------------------------------------------------------------------------------------------------------------------ //

struct CMtlVertexDecl
{
	uint				m_nElements;
	D3DVERTEXELEMENT9	m_elements[ MAX_D3DVERTEXELEMENTS ];
	MTLVertexFormat		m_formats[ MAX_D3DVERTEXELEMENTS ];
	uint8				m_usageToElement[ 256 ];		// (usage << 4) | usage index -> element, 0xFF if absent
	uint				m_nStreamMask;
	uint32				m_nHash;
};

// ------------------------------------------------------------------------------------------------------------------------------ //
// Shaders
// ------------------------------------------------------------------------------------------------------------------------------ //

struct CMtlShader
{
	CMtlShader();
	~CMtlShader();

	bool				m_bVertex;
	bool				m_bValid;			// translated; draws using an invalid shader are skipped
	uint32				*m_pCode;			// D3D9 bytecode, kept so vertex shaders can be retranslated with extra outputs
	uint				m_nCodeDWords;
	char				m_name[ 128 ];
	char				*m_pDebugLabel;
	int32				m_nShadowMask;
	uint32				m_nCentroidMask;
	D3DToMetalShaderInfo m_info;
	uint32				m_nId;				// unique, for pipeline keys

	// Compiled functions. Vertex shaders: by extra interpolant mask. Pixel
	// shaders: by alpha test function.
	std::unordered_map< uint32, id<MTLFunction> > m_functions;
	std::mutex			m_mutex;
};

// ------------------------------------------------------------------------------------------------------------------------------ //
// Queries
// ------------------------------------------------------------------------------------------------------------------------------ //

struct CMtlQuery
{
	D3DQUERYTYPE		m_type;
	bool				m_bStarted, m_bStopped;
	bool				m_bActive;			// occlusion: between Issue(BEGIN) and Issue(END)
	std::vector<uint>	m_slots;			// visibility result slots this query counted into
	uint64				m_nEndSerial;		// command buffer holding Issue(END)
	uint				m_nIssueStartDrawCallIndex, m_nIssueEndDrawCallIndex;
	ThreadId_t			m_nIssueStartThreadID, m_nIssueEndThreadID;
	uint				m_nIssueStartFrameIndex, m_nIssueEndFrameIndex;
};

// ------------------------------------------------------------------------------------------------------------------------------ //
// State
// ------------------------------------------------------------------------------------------------------------------------------ //

struct MtlSamplerState
{
	uint8	m_addressU, m_addressV, m_addressW;
	uint8	m_minFilter, m_magFilter, m_mipFilter;
	uint8	m_maxAniso;
	uint8	m_minLOD;
	uint8	m_compare;			// D3DSAMP_SHADOWFILTER
	uint8	m_srgb;
	uint8	m_pad[2];
	uint32	m_borderColor;
};

// Everything in a render pipeline besides the shaders
struct MtlBlendState
{
	uint8	m_bBlendEnable;
	uint8	m_srcBlend, m_dstBlend, m_blendOp;
	uint8	m_colorWriteMask;	// D3DCOLORWRITEENABLE bits
	uint8	m_bAlphaToCoverage;
	uint8	m_alphaTestFunc;	// 0 = off, else D3DCMPFUNC
	uint8	m_pad;
};

struct MtlDepthStencilKey
{
	uint8	m_bDepthEnable, m_bDepthWrite, m_depthFunc;
	uint8	m_bStencilEnable, m_stencilFunc, m_stencilFail, m_stencilZFail, m_stencilPass;
	uint8	m_stencilReadMask, m_stencilWriteMask;
	uint8	m_pad[2];
};

struct MtlRenderPassKey
{
	MTLPixelFormat	m_color[ MTL_MAX_RENDER_TARGETS ];
	MTLPixelFormat	m_depth;
	MTLPixelFormat	m_stencil;
	uint			m_nSamples;
};

// Everything in a render pipeline
struct MtlPipelineKey
{
	uint32				m_vsId, m_psId;
	uint32				m_nExtraOutputs;
	uint32				m_declHash;
	uint32				m_strides[ MTL_MAX_STREAMS ];
	MtlRenderPassKey	m_pass;
	MtlBlendState		m_blend;
	uint32				m_nPad;
};

// Tracks a command buffer's progress; serials only grow.
struct MtlFrameSync
{
	std::atomic<uint64>	m_nCompletedSerial;
};

// Transient upload space, recycled once the command buffer using it completes
struct MtlUploadChunk
{
	id<MTLBuffer>	m_buf;
	uint			m_nUsed;
	uint64			m_nSerial;
};

// ------------------------------------------------------------------------------------------------------------------------------ //
// Device
// ------------------------------------------------------------------------------------------------------------------------------ //

struct MtlSamplerBinding
{
	IDirect3DBaseTexture9	*m_pTexture;
	MtlSamplerState			m_state;
};

class CMtlDevice
{
public:
	CMtlDevice();
	~CMtlDevice();

	bool Init( IDirect3DDevice9 *pD3D, const D3DPRESENT_PARAMETERS *pParams );
	void Shutdown();

	// Command buffers and passes
	id<MTLCommandBuffer> CommandBuffer();
	id<MTLRenderCommandEncoder> RenderEncoder();	// opens the pass for the current targets
	id<MTLBlitCommandEncoder> BlitEncoder();
	void EndEncoders();
	void FlushPass();								// ends the encoders, applying clears still waiting for the pass
	void Submit( bool bWait );						// commits the current command buffer
	void WaitForSerial( uint64 nSerial );
	uint64 CurrentSerial() const { return m_nSerial; }
	uint64 CompletedSerial() const { return m_pSync->m_nCompletedSerial.load(); }
	bool IsBusy( uint64 nSerial ) const { return nSerial > CompletedSerial(); }

	// Transient memory, valid for the current command buffer
	id<MTLBuffer> Upload( const void *pData, uint nSize, uint nAlign, uint *pOffset );

	// Draw
	bool PrepareDraw( D3DPRIMITIVETYPE type );
	void Clear( DWORD nCount, const D3DRECT *pRects, DWORD nFlags, D3DCOLOR color, float z, DWORD stencil );
	void Draw( D3DPRIMITIVETYPE type, uint nStartVertex, uint nPrimCount );
	void DrawIndexed( D3DPRIMITIVETYPE type, int nBaseVertex, uint nMinVertex, uint nNumVertices, uint nStartIndex, uint nPrimCount );

	// Textures
	void UploadSlice( CMtlTex *pTex, uint face, uint mip, const MtlSliceBacking &backing, uint x, uint y, uint z, uint w, uint h, uint d );
	void ReadSlice( CMtlTex *pTex, uint face, uint mip, MtlSliceBacking &backing );
	void ZeroTexture( CMtlTex *pTex, uint nFirstMip );
	id<MTLTexture> ResolvedTexture( CMtlTex *pTex );
	void StretchRect( IDirect3DSurface9 *pSrc, const RECT *pSrcRect, IDirect3DSurface9 *pDst, const RECT *pDstRect, D3DTEXTUREFILTERTYPE filter );

	// Present
	void Present( IDirect3DSurface9 *pBackBuffer, bool bVSync );

	// Resources released by the D3D side
	void ReleasedTexture( IDirect3DBaseTexture9 *pTex );
	void ReleasedMtlTex( CMtlTex *pTex );
	void ReleasedSurface( IDirect3DSurface9 *pSurf );
	void ReleasedShader( CMtlShader *pShader );
	void ReleasedVertexBuffer( IDirect3DVertexBuffer9 *pBuf );
	void ReleasedIndexBuffer( IDirect3DIndexBuffer9 *pBuf );
	void ReleasedVertexDecl( IDirect3DVertexDeclaration9 *pDecl );
	void ReleasedQuery( CMtlQuery *pQuery );

	// Queries
	void BeginOcclusion( CMtlQuery *pQuery );
	void EndOcclusion( CMtlQuery *pQuery );
	uint OcclusionResult( CMtlQuery *pQuery );

	// Shaders
	id<MTLFunction> GetFunction( CMtlShader *pShader, uint32 nKey );

	id<MTLDevice>				m_device;
	id<MTLCommandQueue>			m_queue;
	CAMetalLayer				*m_layer;
	IDirect3DDevice9			*m_pD3D;

	// D3D state ---------------------------------------------------------------
	IDirect3DSurface9			*m_pRenderTargets[ MTL_MAX_RENDER_TARGETS ];
	IDirect3DSurface9			*m_pDepthStencil;
	IDirect3DVertexDeclaration9	*m_pVertDecl;
	D3DStreamDesc				m_streams[ MTL_MAX_STREAMS ];
	IDirect3DIndexBuffer9		*m_pIndices;
	IDirect3DVertexShader9		*m_pVertexShader;
	IDirect3DPixelShader9		*m_pPixelShader;
	MtlSamplerBinding			m_samplers[ MTL_SAMPLER_COUNT ];

	D3DVIEWPORT9				m_viewport;
	RECT						m_scissorRect;
	DWORD						m_renderStates[ 256 ];			// D3DRENDERSTATETYPE values below 256
	float						m_clipPlanes[ 2 ][ 4 ];

	float						m_vsConstants[ MTL_VS_CONSTANT_COUNT ][ 4 ];
	float						m_psConstants[ MTL_PS_CONSTANT_COUNT ][ 4 ];
	D3DToMetalVSMisc			m_vsMisc;
	D3DToMetalPSMisc			m_psMisc;
	bool						m_bVSConstantsDirty, m_bPSConstantsDirty;
	uint						m_nVSUploadedCount, m_nPSUploadedCount;	// registers in the bound constant buffers
	bool						m_bVSMiscDirty, m_bPSMiscDirty;
	uint						m_nMaxVSConstantHint;

	// Metal side --------------------------------------------------------------
	id<MTLCommandBuffer>		m_cmdBuf;
	id<MTLRenderCommandEncoder>	m_renderEncoder;
	id<MTLBlitCommandEncoder>	m_blitEncoder;
	uint64						m_nSerial;				// of m_cmdBuf
	std::shared_ptr<MtlFrameSync>	m_pSync;		// shared with the completion handlers, which can run after the device is gone
	std::vector< std::pair<uint64, id<MTLCommandBuffer> > >	m_inFlight;	// committed, maybe not finished
	dispatch_semaphore_t		m_frameSemaphore;

	bool						m_bPassDirty;			// targets or sRGB write changed: next draw opens a new pass
	bool						m_bPassHasDraws;
	MtlRenderPassKey			m_passKey;
	uint						m_nPassWidth, m_nPassHeight;
	// Clears that arrive before anything is drawn in a pass become load actions
	bool						m_bPendingClearColor, m_bPendingClearDepth, m_bPendingClearStencil;
	MTLClearColor				m_pendingClearColor;
	double						m_flPendingClearDepth;
	uint						m_nPendingClearStencil;

	// Encoder state shadowing, reset when an encoder opens
	id<MTLRenderPipelineState>	m_curPipeline;
	id<MTLDepthStencilState>	m_curDepthStencil;
	id<MTLBuffer>				m_curVertexBuffers[ MTL_MAX_STREAMS + 1 ];
	uint						m_curVertexOffsets[ MTL_MAX_STREAMS + 1 ];
	id<MTLTexture>				m_curTextures[ MTL_SAMPLER_COUNT ];
	id<MTLSamplerState>			m_curSamplers[ MTL_SAMPLER_COUNT ];
	bool						m_bDynamicStateDirty;	// viewport, scissor, raster state
	bool						m_bVSConstantsBound, m_bPSConstantsBound;
	uint						m_nCurStencilRef;

	// Caches
	std::unordered_map< uint64, id<MTLRenderPipelineState> >	m_pipelines;
	std::unordered_map< uint32, id<MTLDepthStencilState> >		m_depthStencilStates;
	std::unordered_map< uint64, id<MTLSamplerState> >			m_samplerStates;
	uint32						m_nNextShaderId;

	// Presenting: the blit into the drawable, and the thread that presents
	// with vsync off
	id<MTLRenderPipelineState>	m_presentPipeline;
	id<MTLSamplerState>			m_presentNearest, m_presentLinear;
	CMtlPresenter				*m_pPresenter;

	// Built-in programs: clears, blits, present
	id<MTLLibrary>				m_utilLibrary;
	std::unordered_map< uint64, id<MTLRenderPipelineState> >	m_utilPipelines;
	id<MTLDepthStencilState>	m_clearDepthStencil[ 4 ];		// bit 0: write depth, bit 1: write stencil

	// Dummies
	id<MTLBuffer>				m_zeroBuffer;			// zero vertex data
	id<MTLTexture>				m_dummyTex2D, m_dummyTexCube, m_dummyTex3D, m_dummyDepth;

	// Upload chunks
	std::vector<MtlUploadChunk>	m_uploadChunks;
	int							m_nCurUploadChunk;

	// Occlusion queries
	id<MTLBuffer>				m_visibilityBuffer;
	std::vector<uint>			m_freeVisibilitySlots;
	std::vector< std::pair<uint64, uint> >	m_retiredVisibilitySlots;	// serial, slot
	CMtlQuery					*m_pActiveQuery;

	// Buffers renamed by D3DLOCK_DISCARD, reused once the GPU is done with them
	std::unordered_map< uint, std::vector< std::pair<uint64, id<MTLBuffer> > > >	m_bufferPool;
	id<MTLBuffer> NewBuffer( uint nSize );
	void RetireBuffer( id<MTLBuffer> buf, uint64 nSerial );

	uint						m_nFrame;
	uint						m_nDrawCallIndex;
};

extern CMtlDevice *g_pMtlDevice;
extern ILauncherMgr *g_pLauncherMgr;

// Stats read by perf_log
extern std::atomic<uint64> g_nMtlTotalDraws;
extern std::atomic<uint64> g_nMtlTotalPrimitives;

#endif // MTLINTERNAL_H
