//================ Copyright (c) 1996-2012 Valve Corporation. All Rights Reserved. =================
//
// dxabstract.h
//
// The Direct3D 9 interface ToMetal implements on Metal. It has the same
// classes and methods as ToGL's header of the same name, including the
// extensions shaderapidx9 uses, so the shader API compiles against either.
// The implementation keeps its Metal state in the objects declared opaquely
// below.
//
//==================================================================================================
#ifndef DXABSTRACT_H
#define DXABSTRACT_H

#ifdef DX_TO_GL_ABSTRACTION

#include "togl/rendermechanism.h"

#include "tier0/platform.h"
#include "tier0/dbg.h"
#include "tier1/interface.h"
#include "tier1/utlmap.h"
#include "tier1/utlvector.h"

class CCommand;

// Metal objects behind the D3D ones (tometal/mtlinternal.h)
struct CMtlTex;
struct CMtlBuffer;
struct CMtlShader;
struct CMtlQuery;
struct CMtlVertexDecl;
class CMtlDevice;

// Same as ToGL's; the shader API's shader-pair cache uses it.
struct GLMShaderPairInfo
{
	int		m_status;		// -1 means req'd index was out of bounds (loop stop..)  0 means not present.  1 means present/active.

	char	m_vsName[ 128 ];
	int		m_vsStaticIndex;
	int		m_vsDynamicIndex;

	char	m_psName[ 128 ];
	int		m_psStaticIndex;
	int		m_psDynamicIndex;
};

// Connects ToMetal to the engine's interfaces; the shader API calls these
// where it would call ToGLConnectLibraries and ToGLDisconnectLibraries.
TOMETAL_INTERFACE void ToMetalConnectLibraries( CreateInterfaceFn factory );
TOMETAL_INTERFACE void ToMetalDisconnectLibraries();

TOMETAL_INTERFACE void toglGetClientRect( VD3DHWND hWnd, RECT *destRect );

// Draw calls and primitives issued since startup; never reset.
TOMETAL_INTERFACE void tometalGetDrawTotals( uint64 *pDraws, uint64 *pPrimitives );

struct TOMETAL_CLASS IUnknown
{
	int	m_refcount[2];
	bool m_mark;

	IUnknown()
	{
		m_refcount[0] = 1;
		m_refcount[1] = 0;
		m_mark = false;
	};

	virtual	~IUnknown()
	{
	};

	void	AddRef( int which=0, const char *comment = NULL )
	{
		Assert( which >= 0 );
		Assert( which < 2 );
		m_refcount[which]++;
	};

	ULONG	Release( int which=0, const char *comment = NULL )
	{
		Assert( which >= 0 );
		Assert( which < 2 );

		m_refcount[which]--;
		if ( (!m_refcount[0]) && (!m_refcount[1]) )
		{
			delete this;
			return 0;
		}
		return m_refcount[0];
	};

	void	SetMark( bool markValue, const char *comment=NULL )
	{
		m_mark = markValue;
	}
};

// ------------------------------------------------------------------------------------------------------------------------------ //
// INTERFACES
// ------------------------------------------------------------------------------------------------------------------------------ //

struct TOMETAL_CLASS IDirect3DResource9 : public IUnknown
{
	IDirect3DDevice9	*m_device;		// parent device
	D3DRESOURCETYPE		m_restype;

	DWORD SetPriority(DWORD PriorityNew);
};

struct TOMETAL_CLASS IDirect3DBaseTexture9 : public IDirect3DResource9						// "A Texture.."
{
	D3DSURFACE_DESC			m_descZero;			// desc of top level.
	CMtlTex					*m_tex;				// a CMtlTex can represent all forms of tex

	virtual					~IDirect3DBaseTexture9();
	D3DRESOURCETYPE	TOMETALMETHODCALLTYPE GetType();
	DWORD TOMETALMETHODCALLTYPE GetLevelCount();
	HRESULT TOMETALMETHODCALLTYPE GetLevelDesc(UINT Level,D3DSURFACE_DESC *pDesc);
};

struct TOMETAL_CLASS IDirect3DTexture9 : public IDirect3DBaseTexture9							// "Texture 2D"
{
	IDirect3DSurface9 *m_surfZero;				// surf of top level.
	virtual ~IDirect3DTexture9();
	HRESULT TOMETALMETHODCALLTYPE LockRect(UINT Level,D3DLOCKED_RECT* pLockedRect,CONST RECT* pRect,DWORD Flags);
	HRESULT TOMETALMETHODCALLTYPE UnlockRect(UINT Level);
	HRESULT TOMETALMETHODCALLTYPE GetSurfaceLevel(UINT Level,IDirect3DSurface9** ppSurfaceLevel);
};

struct TOMETAL_CLASS IDirect3DCubeTexture9 : public IDirect3DBaseTexture9						// "Texture Cube Map"
{
	IDirect3DSurface9		*m_surfZero[6];			// surfs of top level.
	virtual ~IDirect3DCubeTexture9();
	HRESULT TOMETALMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES FaceType,UINT Level,IDirect3DSurface9** ppCubeMapSurface);
	HRESULT TOMETALMETHODCALLTYPE GetLevelDesc(UINT Level,D3DSURFACE_DESC *pDesc);
};

struct TOMETAL_CLASS IDirect3DVolumeTexture9 : public IDirect3DBaseTexture9					// "Texture 3D"
{
	IDirect3DSurface9		*m_surfZero;			// surf of top level.
	D3DVOLUME_DESC			m_volDescZero;			// volume desc top level
	virtual ~IDirect3DVolumeTexture9();
	HRESULT TOMETALMETHODCALLTYPE LockBox(UINT Level,D3DLOCKED_BOX* pLockedVolume,CONST D3DBOX* pBox,DWORD Flags);
	HRESULT TOMETALMETHODCALLTYPE UnlockBox(UINT Level);
	HRESULT	TOMETALMETHODCALLTYPE GetLevelDesc( UINT level, D3DVOLUME_DESC *pDesc );
};

// A "D3D surface" is a tex, a face, and a mip. Surfaces made by
// CreateRenderTarget, CreateDepthStencilSurface and CreateOffscreenPlainSurface
// own their tex (m_restype D3DRTYPE_SURFACE); the others refer to a texture's.
struct TOMETAL_CLASS IDirect3DSurface9 : public IDirect3DResource9
{
	virtual ~IDirect3DSurface9();
	HRESULT TOMETALMETHODCALLTYPE LockRect(D3DLOCKED_RECT* pLockedRect,CONST RECT* pRect,DWORD Flags);
	HRESULT TOMETALMETHODCALLTYPE UnlockRect();
	HRESULT TOMETALMETHODCALLTYPE GetDesc(D3DSURFACE_DESC *pDesc);

	D3DSURFACE_DESC			m_desc;
	CMtlTex					*m_tex;
	int						m_face;
	int						m_mip;
};

struct TOMETAL_CLASS IDirect3D9 : public IUnknown
{
	virtual	~IDirect3D9();

	UINT	TOMETALMETHODCALLTYPE GetAdapterCount();

	HRESULT TOMETALMETHODCALLTYPE GetDeviceCaps				(UINT Adapter,D3DDEVTYPE DeviceType,D3DCAPS9* pCaps);
	HRESULT TOMETALMETHODCALLTYPE GetAdapterIdentifier			(UINT Adapter,DWORD Flags,D3DADAPTER_IDENTIFIER9* pIdentifier);
	HRESULT TOMETALMETHODCALLTYPE CheckDeviceFormat			(UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT AdapterFormat,DWORD Usage,D3DRESOURCETYPE RType,D3DFORMAT CheckFormat);
	UINT	TOMETALMETHODCALLTYPE GetAdapterModeCount			(UINT Adapter,D3DFORMAT Format);
	HRESULT TOMETALMETHODCALLTYPE EnumAdapterModes				(UINT Adapter,D3DFORMAT Format,UINT Mode,D3DDISPLAYMODE* pMode);
	HRESULT TOMETALMETHODCALLTYPE CheckDeviceType				(UINT Adapter,D3DDEVTYPE DevType,D3DFORMAT AdapterFormat,D3DFORMAT BackBufferFormat,BOOL bWindowed);
	HRESULT TOMETALMETHODCALLTYPE GetAdapterDisplayMode		(UINT Adapter,D3DDISPLAYMODE* pMode);
	HRESULT TOMETALMETHODCALLTYPE CheckDepthStencilMatch		(UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT AdapterFormat,D3DFORMAT RenderTargetFormat,D3DFORMAT DepthStencilFormat);
	HRESULT TOMETALMETHODCALLTYPE CheckDeviceMultiSampleType	(UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT SurfaceFormat,BOOL Windowed,D3DMULTISAMPLE_TYPE MultiSampleType,DWORD* pQualityLevels);

	HRESULT TOMETALMETHODCALLTYPE CreateDevice					(UINT Adapter,D3DDEVTYPE DeviceType,VD3DHWND hFocusWindow,DWORD BehaviorFlags,D3DPRESENT_PARAMETERS* pPresentationParameters,IDirect3DDevice9** ppReturnedDeviceInterface);
};

struct TOMETAL_CLASS IDirect3DVertexDeclaration9 : public IUnknown
{
	IDirect3DDevice9		*m_device;
	CMtlVertexDecl			*m_decl;

	virtual					~IDirect3DVertexDeclaration9();
};

struct TOMETAL_CLASS IDirect3DQuery9 : public IDirect3DResource9
{
	D3DQUERYTYPE			m_type;		// D3DQUERYTYPE_OCCLUSION or D3DQUERYTYPE_EVENT
	CMtlQuery				*m_query;

	virtual					~IDirect3DQuery9();

	HRESULT					Issue(DWORD dwIssueFlags);
	HRESULT					GetData(void* pData,DWORD dwSize,DWORD dwGetDataFlags);
};

struct TOMETAL_CLASS IDirect3DVertexBuffer9 : public IDirect3DResource9
{
	CMtlBuffer				*m_vtxBuffer;
	D3DVERTEXBUFFER_DESC	m_vtxDesc;		// to satisfy GetDesc

	virtual					~IDirect3DVertexBuffer9();
	HRESULT					Lock(UINT OffsetToLock,UINT SizeToLock,void** ppbData,DWORD Flags);
	HRESULT					Unlock();
	void					UnlockActualSize( uint nActualSize, const void *pActualData = NULL );
};

struct TOMETAL_CLASS IDirect3DIndexBuffer9 : public IDirect3DResource9
{
	CMtlBuffer				*m_idxBuffer;
	D3DINDEXBUFFER_DESC		m_idxDesc;		// to satisfy GetDesc

	virtual					~IDirect3DIndexBuffer9();

	HRESULT					Lock(UINT OffsetToLock,UINT SizeToLock,void** ppbData,DWORD Flags);
	HRESULT					Unlock();
	void					UnlockActualSize( uint nActualSize, const void *pActualData = NULL );

	HRESULT					GetDesc(D3DINDEXBUFFER_DESC *pDesc);
};

struct TOMETAL_CLASS IDirect3DPixelShader9 : public IDirect3DResource9
{
	CMtlShader				*m_shader;

	virtual					~IDirect3DPixelShader9();
};

struct TOMETAL_CLASS IDirect3DVertexShader9 : public IDirect3DResource9
{
	CMtlShader				*m_shader;

	virtual					~IDirect3DVertexShader9();
};

typedef class CUtlMemory<D3DMATRIX> CD3DMATRIXAllocator;
typedef class CUtlVector<D3DMATRIX, CD3DMATRIXAllocator> CD3DMATRIXStack;

struct TOMETAL_CLASS ID3DXMatrixStack //: public IUnknown
{
	int	m_refcount[2];
	bool m_mark;
	CD3DMATRIXStack	m_stack;
	int						m_stackTop;	// top of stack is at the highest index, this is that index.  push increases, pop decreases.

	ID3DXMatrixStack();
	void  AddRef( int which=0, char *comment = NULL );
	ULONG Release( int which=0, char *comment = NULL );

	HRESULT	Create( void );

	D3DXMATRIX* GetTop();
	void Push();
	void Pop();
	void LoadIdentity();
	void LoadMatrix( const D3DXMATRIX *pMat );
	void MultMatrix( const D3DXMATRIX *pMat );
	void MultMatrixLocal( const D3DXMATRIX *pMat );
	HRESULT ScaleLocal(FLOAT x, FLOAT y, FLOAT z);

	// Left multiply the current matrix with the computed rotation
	// matrix, counterclockwise about the given axis with the given angle.
	// (rotation is about the local origin of the object)
	HRESULT RotateAxisLocal(CONST D3DXVECTOR3* pV, FLOAT Angle);

	// Left multiply the current matrix with the computed translation
	// matrix. (transformation is about the local origin of the object)
	HRESULT TranslateLocal(FLOAT x, FLOAT y, FLOAT z);
};

typedef ID3DXMatrixStack* LPD3DXMATRIXSTACK;

struct TOMETAL_CLASS IDirect3DDevice9 : public IUnknown
{
	IDirect3DDevice9();
	virtual	~IDirect3DDevice9();

	// Create call invoked from IDirect3D9
	HRESULT	TOMETALMETHODCALLTYPE Create( IDirect3DDevice9Params *params );

	//
	// Basics
	//
	HRESULT TOMETALMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pPresentationParameters);
	HRESULT TOMETALMETHODCALLTYPE SetViewport(CONST D3DVIEWPORT9* pViewport);
	HRESULT TOMETALMETHODCALLTYPE GetViewport(D3DVIEWPORT9* pViewport);
	HRESULT TOMETALMETHODCALLTYPE BeginScene();
	HRESULT TOMETALMETHODCALLTYPE Clear(DWORD Count,CONST D3DRECT* pRects,DWORD Flags,D3DCOLOR Color,float Z,DWORD Stencil);
	HRESULT TOMETALMETHODCALLTYPE EndScene();
	HRESULT TOMETALMETHODCALLTYPE Present(CONST RECT* pSourceRect,CONST RECT* pDestRect,VD3DHWND hDestWindowOverride,CONST RGNDATA* pDirtyRegion);

	// textures
	HRESULT TOMETALMETHODCALLTYPE CreateTexture(UINT Width,UINT Height,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DTexture9** ppTexture,VD3DHANDLE* pSharedHandle, char *debugLabel=NULL);
	HRESULT TOMETALMETHODCALLTYPE CreateCubeTexture(UINT EdgeLength,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DCubeTexture9** ppCubeTexture,VD3DHANDLE* pSharedHandle, char *debugLabel=NULL);
	HRESULT TOMETALMETHODCALLTYPE CreateVolumeTexture(UINT Width,UINT Height,UINT Depth,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DVolumeTexture9** ppVolumeTexture,VD3DHANDLE* pSharedHandle, char *debugLabel=NULL);

	HRESULT TOMETALMETHODCALLTYPE SetTexture(DWORD Stage,IDirect3DBaseTexture9* pTexture);
	HRESULT TOMETALMETHODCALLTYPE SetTextureNonInline(DWORD Stage,IDirect3DBaseTexture9* pTexture) { return SetTexture( Stage, pTexture ); }

	HRESULT TOMETALMETHODCALLTYPE GetTexture(DWORD Stage,IDirect3DBaseTexture9** ppTexture);

	// render targets, color and depthstencil, surfaces, blit
	HRESULT TOMETALMETHODCALLTYPE CreateRenderTarget(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Lockable,IDirect3DSurface9** ppSurface,VD3DHANDLE* pSharedHandle, char *debugLabel=NULL);
	HRESULT TOMETALMETHODCALLTYPE SetRenderTarget(DWORD RenderTargetIndex,IDirect3DSurface9* pRenderTarget);
	HRESULT TOMETALMETHODCALLTYPE GetRenderTarget(DWORD RenderTargetIndex,IDirect3DSurface9** ppRenderTarget);

	HRESULT TOMETALMETHODCALLTYPE CreateOffscreenPlainSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DPOOL Pool,IDirect3DSurface9** ppSurface,VD3DHANDLE* pSharedHandle);

	HRESULT TOMETALMETHODCALLTYPE CreateDepthStencilSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Discard,IDirect3DSurface9** ppSurface,VD3DHANDLE* pSharedHandle);
	HRESULT TOMETALMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil);
	HRESULT TOMETALMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface);

	HRESULT TOMETALMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* pRenderTarget,IDirect3DSurface9* pDestSurface);
	HRESULT TOMETALMETHODCALLTYPE GetFrontBufferData(UINT iSwapChain,IDirect3DSurface9* pDestSurface);
	HRESULT TOMETALMETHODCALLTYPE StretchRect(IDirect3DSurface9* pSourceSurface,CONST RECT* pSourceRect,IDirect3DSurface9* pDestSurface,CONST RECT* pDestRect,D3DTEXTUREFILTERTYPE Filter);

	// pixel shaders
	HRESULT TOMETALMETHODCALLTYPE CreatePixelShader(CONST DWORD* pFunction,IDirect3DPixelShader9** ppShader, const char *pShaderName, char *debugLabel = NULL, const uint32 *pCentroidMask = NULL );

	HRESULT TOMETALMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* pShader);
	HRESULT TOMETALMETHODCALLTYPE SetPixelShaderNonInline(IDirect3DPixelShader9* pShader) { return SetPixelShader( pShader ); }

	HRESULT TOMETALMETHODCALLTYPE SetPixelShaderConstantF(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount);
	HRESULT TOMETALMETHODCALLTYPE SetPixelShaderConstantFNonInline(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount) { return SetPixelShaderConstantF( StartRegister, pConstantData, Vector4fCount ); }

	HRESULT TOMETALMETHODCALLTYPE SetPixelShaderConstantB(UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount);
	HRESULT TOMETALMETHODCALLTYPE SetPixelShaderConstantI(UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount);

	// vertex shaders
	HRESULT TOMETALMETHODCALLTYPE CreateVertexShader(CONST DWORD* pFunction,IDirect3DVertexShader9** ppShader, const char *pShaderName, char *debugLabel = NULL);

	HRESULT TOMETALMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* pShader);
	HRESULT TOMETALMETHODCALLTYPE SetVertexShaderNonInline(IDirect3DVertexShader9* pShader) { return SetVertexShader( pShader ); }

	HRESULT TOMETALMETHODCALLTYPE SetVertexShaderConstantF(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount);
	HRESULT TOMETALMETHODCALLTYPE SetVertexShaderConstantFNonInline(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount) { return SetVertexShaderConstantF( StartRegister, pConstantData, Vector4fCount ); }

	HRESULT TOMETALMETHODCALLTYPE SetVertexShaderConstantB(UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount);
	HRESULT TOMETALMETHODCALLTYPE SetVertexShaderConstantBNonInline(UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount) { return SetVertexShaderConstantB( StartRegister, pConstantData, BoolCount ); }

	HRESULT TOMETALMETHODCALLTYPE SetVertexShaderConstantI(UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount);
	HRESULT TOMETALMETHODCALLTYPE SetVertexShaderConstantINonInline(UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount) { return SetVertexShaderConstantI( StartRegister, pConstantData, Vector4iCount ); }

	// ToGL's shader-pair prelinking. Metal pipelines depend on more state than
	// the shader pair, so these do nothing; pipelines are cached on disk instead.
	HRESULT TOMETALMETHODCALLTYPE LinkShaderPair( IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps );
	HRESULT TOMETALMETHODCALLTYPE ValidateShaderPair( IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps );
	HRESULT TOMETALMETHODCALLTYPE QueryShaderPair( int index, GLMShaderPairInfo *infoOut );

	// vertex buffers
	HRESULT TOMETALMETHODCALLTYPE CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements,IDirect3DVertexDeclaration9** ppDecl);

	HRESULT TOMETALMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl);
	HRESULT TOMETALMETHODCALLTYPE SetVertexDeclarationNonInline(IDirect3DVertexDeclaration9* pDecl) { return SetVertexDeclaration( pDecl ); }

	HRESULT TOMETALMETHODCALLTYPE SetFVF(DWORD FVF);		// we might not be using these ?
	HRESULT TOMETALMETHODCALLTYPE GetFVF(DWORD* pFVF);

	HRESULT CreateVertexBuffer(UINT Length,DWORD Usage,DWORD FVF,D3DPOOL Pool,IDirect3DVertexBuffer9** ppVertexBuffer,VD3DHANDLE* pSharedHandle);

	HRESULT TOMETALMETHODCALLTYPE SetStreamSource(UINT StreamNumber,IDirect3DVertexBuffer9* pStreamData,UINT OffsetInBytes,UINT Stride);
	HRESULT SetStreamSourceNonInline(UINT StreamNumber,IDirect3DVertexBuffer9* pStreamData,UINT OffsetInBytes,UINT Stride) { return SetStreamSource( StreamNumber, pStreamData, OffsetInBytes, Stride ); }

	// index buffers
	HRESULT TOMETALMETHODCALLTYPE CreateIndexBuffer(UINT Length,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DIndexBuffer9** ppIndexBuffer,VD3DHANDLE* pSharedHandle);

	HRESULT TOMETALMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* pIndexData);
	HRESULT TOMETALMETHODCALLTYPE SetIndicesNonInline(IDirect3DIndexBuffer9* pIndexData) { return SetIndices( pIndexData ); }

	// State management.
	HRESULT TOMETALMETHODCALLTYPE SetRenderStateInline(D3DRENDERSTATETYPE State,DWORD Value) { return SetRenderState( State, Value ); }
	HRESULT TOMETALMETHODCALLTYPE SetRenderStateConstInline(D3DRENDERSTATETYPE State,DWORD Value) { return SetRenderState( State, Value ); }
	HRESULT TOMETALMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE State,DWORD Value);

	HRESULT TOMETALMETHODCALLTYPE SetSamplerState(DWORD Sampler,D3DSAMPLERSTATETYPE Type,DWORD Value);
	HRESULT TOMETALMETHODCALLTYPE SetSamplerStateNonInline(DWORD Sampler,D3DSAMPLERSTATETYPE Type,DWORD Value) { return SetSamplerState( Sampler, Type, Value ); }

	void TOMETALMETHODCALLTYPE SetSamplerStates(DWORD Sampler, DWORD AddressU, DWORD AddressV, DWORD AddressW, DWORD MinFilter, DWORD MagFilter, DWORD MipFilter );
	void TOMETALMETHODCALLTYPE SetSamplerStatesNonInline(DWORD Sampler, DWORD AddressU, DWORD AddressV, DWORD AddressW, DWORD MinFilter, DWORD MagFilter, DWORD MipFilter ) { SetSamplerStates( Sampler, AddressU, AddressV, AddressW, MinFilter, MagFilter, MipFilter ); }

	// Draw.
	HRESULT TOMETALMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType,UINT StartVertex,UINT PrimitiveCount);
	HRESULT TOMETALMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType,INT BaseVertexIndex,UINT MinVertexIndex,UINT NumVertices,UINT startIndex,UINT primCount);
	HRESULT TOMETALMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType,UINT MinVertexIndex,UINT NumVertices,UINT PrimitiveCount,CONST void* pIndexData,D3DFORMAT IndexDataFormat,CONST void* pVertexStreamZeroData,UINT VertexStreamZeroStride);

	// misc
	BOOL TOMETALMETHODCALLTYPE ShowCursor(BOOL bShow);
	HRESULT TOMETALMETHODCALLTYPE ValidateDevice(DWORD* pNumPasses);
	HRESULT TOMETALMETHODCALLTYPE SetMaterial(CONST D3DMATERIAL9* pMaterial);
	HRESULT TOMETALMETHODCALLTYPE LightEnable(DWORD Index,BOOL Enable);
	HRESULT TOMETALMETHODCALLTYPE SetScissorRect(CONST RECT* pRect);
	HRESULT TOMETALMETHODCALLTYPE CreateQuery(D3DQUERYTYPE Type,IDirect3DQuery9** ppQuery);
	HRESULT TOMETALMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* pCaps);
	HRESULT TOMETALMETHODCALLTYPE TestCooperativeLevel();
	HRESULT TOMETALMETHODCALLTYPE EvictManagedResources();
	HRESULT TOMETALMETHODCALLTYPE SetLight(DWORD Index,CONST D3DLIGHT9*);
	void TOMETALMETHODCALLTYPE SetGammaRamp(UINT iSwapChain,DWORD Flags,CONST D3DGAMMARAMP* pRamp);

	void TOMETALMETHODCALLTYPE SaveGLState();
	void TOMETALMETHODCALLTYPE RestoreGLState();

	HRESULT TOMETALMETHODCALLTYPE SetClipPlane(DWORD Index,CONST float* pPlane);

	//
	// **** FIXED FUNCTION STUFF - None of this stuff needs support in Metal.
	//
	HRESULT TOMETALMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE State,CONST D3DMATRIX* pMatrix);
	HRESULT TOMETALMETHODCALLTYPE SetTextureStageState(DWORD Stage,D3DTEXTURESTAGESTATETYPE Type,DWORD Value);

	// The device may be driven from one thread at a time; the shader API hands
	// it between the main thread and its render thread with these.
	void TOMETALMETHODCALLTYPE AcquireThreadOwnership( );
	void TOMETALMETHODCALLTYPE ReleaseThreadOwnership( );
	ThreadId_t TOMETALMETHODCALLTYPE GetCurrentOwnerThreadId() const { return m_nCurOwnerThreadId; }

	void TOMETALMETHODCALLTYPE SetMaxUsedVertexShaderConstantsHint( uint nMaxReg );
	void TOMETALMETHODCALLTYPE SetMaxUsedVertexShaderConstantsHintNonInline( uint nMaxReg ) { SetMaxUsedVertexShaderConstantsHint( nMaxReg ); }

	void DumpStatsToConsole( const CCommand *pArgs );

	// Metal side of the device
	CMtlDevice					*m_mtl;

private:
	IDirect3DDevice9( const IDirect3DDevice9& );
	IDirect3DDevice9& operator= ( const IDirect3DDevice9& );

	HRESULT CreateDefaultSurfaces();

	DWORD						m_nValidMarker;
	ThreadId_t					m_nCurOwnerThreadId;
public:
	IDirect3DDevice9Params		m_params;						// mirror of the creation inputs

	IDirect3DSurface9			*m_pDefaultColorSurface;		// default color surface.
	IDirect3DSurface9			*m_pDefaultDepthStencilSurface;	// queried by GetDepthStencilSurface.
};

// ------------------------------------------------------------------------------------------------------------------------------ //
// D3DX
// ------------------------------------------------------------------------------------------------------------------------------ //
struct ID3DXInclude
{
	virtual HRESULT Open(D3DXINCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes);
	virtual HRESULT Close(LPCVOID pData);
};
typedef ID3DXInclude* LPD3DXINCLUDE;


struct TOMETAL_CLASS ID3DXBuffer : public IUnknown
{
	void* GetBufferPointer();
	DWORD GetBufferSize();
};

typedef ID3DXBuffer* LPD3DXBUFFER;

class ID3DXConstantTable : public IUnknown
{
};
typedef ID3DXConstantTable* LPD3DXCONSTANTTABLE;

TOMETAL_INTERFACE const char* D3DXGetPixelShaderProfile( IDirect3DDevice9 *pDevice );

TOMETAL_INTERFACE D3DXMATRIX* D3DXMatrixMultiply( D3DXMATRIX *pOut, CONST D3DXMATRIX *pM1, CONST D3DXMATRIX *pM2 );
TOMETAL_INTERFACE D3DXVECTOR3* D3DXVec3TransformCoord( D3DXVECTOR3 *pOut, CONST D3DXVECTOR3 *pV, CONST D3DXMATRIX *pM );

TOMETAL_INTERFACE HRESULT D3DXCreateMatrixStack( DWORD Flags, LPD3DXMATRIXSTACK* ppStack);
TOMETAL_INTERFACE void D3DXMatrixIdentity( D3DXMATRIX * );

TOMETAL_INTERFACE D3DXINLINE D3DXVECTOR3* D3DXVec3Subtract( D3DXVECTOR3 *pOut, CONST D3DXVECTOR3 *pV1, CONST D3DXVECTOR3 *pV2 )
{
	pOut->x = pV1->x - pV2->x;
	pOut->y = pV1->y - pV2->y;
	pOut->z = pV1->z - pV2->z;
	return pOut;
}

TOMETAL_INTERFACE D3DXINLINE D3DXVECTOR3* D3DXVec3Cross( D3DXVECTOR3 *pOut, CONST D3DXVECTOR3 *pV1, CONST D3DXVECTOR3 *pV2 )
{
	D3DXVECTOR3 v;

	v.x = pV1->y * pV2->z - pV1->z * pV2->y;
	v.y = pV1->z * pV2->x - pV1->x * pV2->z;
	v.z = pV1->x * pV2->y - pV1->y * pV2->x;

	*pOut = v;
	return pOut;
}

TOMETAL_INTERFACE D3DXINLINE FLOAT D3DXVec3Dot( CONST D3DXVECTOR3 *pV1, CONST D3DXVECTOR3 *pV2 )
{
	return pV1->x * pV2->x + pV1->y * pV2->y + pV1->z * pV2->z;
}

TOMETAL_INTERFACE D3DXMATRIX* D3DXMatrixInverse( D3DXMATRIX *pOut, FLOAT *pDeterminant, CONST D3DXMATRIX *pM );

TOMETAL_INTERFACE D3DXMATRIX* D3DXMatrixTranspose( D3DXMATRIX *pOut, CONST D3DXMATRIX *pM );

TOMETAL_INTERFACE D3DXPLANE* D3DXPlaneNormalize( D3DXPLANE *pOut, CONST D3DXPLANE *pP);

TOMETAL_INTERFACE D3DXVECTOR4* D3DXVec4Transform( D3DXVECTOR4 *pOut, CONST D3DXVECTOR4 *pV, CONST D3DXMATRIX *pM );


TOMETAL_INTERFACE D3DXVECTOR4* D3DXVec4Normalize( D3DXVECTOR4 *pOut, CONST D3DXVECTOR4 *pV );

TOMETAL_INTERFACE D3DXMATRIX* D3DXMatrixTranslation( D3DXMATRIX *pOut, FLOAT x, FLOAT y, FLOAT z );

// Build an ortho projection matrix. (right-handed)
TOMETAL_INTERFACE D3DXMATRIX* D3DXMatrixOrthoOffCenterRH( D3DXMATRIX *pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn,FLOAT zf );

TOMETAL_INTERFACE D3DXMATRIX* D3DXMatrixPerspectiveRH( D3DXMATRIX *pOut, FLOAT w, FLOAT h, FLOAT zn, FLOAT zf );

TOMETAL_INTERFACE D3DXMATRIX* D3DXMatrixPerspectiveOffCenterRH( D3DXMATRIX *pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn, FLOAT zf );

// Transform a plane by a matrix.  The vector (a,b,c) must be normal.
// M should be the inverse transpose of the transformation desired.
TOMETAL_INTERFACE D3DXPLANE* D3DXPlaneTransform( D3DXPLANE *pOut, CONST D3DXPLANE *pP, CONST D3DXMATRIX *pM );

TOMETAL_INTERFACE IDirect3D9 *Direct3DCreate9(UINT SDKVersion);

TOMETAL_INTERFACE void D3DPERF_SetOptions( DWORD dwOptions );

TOMETAL_INTERFACE HRESULT D3DXCompileShader(
	LPCSTR                          pSrcData,
	UINT                            SrcDataLen,
	CONST D3DXMACRO*                pDefines,
	LPD3DXINCLUDE                   pInclude,
	LPCSTR                          pFunctionName,
	LPCSTR                          pProfile,
	DWORD                           Flags,
	LPD3DXBUFFER*                   ppShader,
	LPD3DXBUFFER*                   ppErrorMsgs,
	LPD3DXCONSTANTTABLE*            ppConstantTable);

// fake D3D usage constant for SRGB tex creation
#define D3DUSAGE_TEXTURE_SRGB (0x80000000L)

#endif // DX_TO_GL_ABSTRACTION

#endif // DXABSTRACT_H
