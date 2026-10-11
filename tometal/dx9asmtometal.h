//------------------------------------------------------------------------------
// dx9asmtometal.h
//
// Translates Direct3D 9 shader bytecode (vs/ps 2.0, 2.x and 3.0) into Metal
// Shading Language. Derived from ToGL's dx9asmtogl2, which emitted GLSL: the
// instruction translation is the same, while declarations, inputs, outputs,
// constants and texture sampling follow Metal's rules.
//------------------------------------------------------------------------------

#ifndef DX9_ASM_TO_METAL_H
#define DX9_ASM_TO_METAL_H

#include "tier1/utlbuffer.h"
#include "tier1/utlstring.h"

#define MAX_SHADER_CONSTANTS	512

#define MAX_DECLARED_OUTPUTS	32
#define MAX_DECLARED_INPUTS	32

// Option bits
#define D3DToMetal_OptionSpew	0x80000000

// Code for which component of the "dummy" address register is needed by an instruction
#define ARL_DEST_NONE		-1
#define ARL_DEST_X			 0
#define ARL_DEST_Y			 1
#define ARL_DEST_Z			 2
#define ARL_DEST_W			 3

// Interpolants passed from vertex to pixel shaders, as bits of a mask.
#define D3DTOMETAL_INTERP_TEXCOORD( n )	( 1u << ( n ) )			// TEXCOORD0..15
#define D3DTOMETAL_INTERP_COLOR( n )	( 1u << ( 16 + ( n ) ) )	// COLOR0..1

// Metal buffer indices the generated functions use. Vertex streams occupy
// buffer indices 0 to 4 in vertex functions.
#define D3DTOMETAL_VS_CONSTANT_BUFFER	16	// constant float4 vc[256]
#define D3DTOMETAL_VS_MISC_BUFFER		17	// D3DToMetalVSMisc
#define D3DTOMETAL_PS_CONSTANT_BUFFER	0	// constant float4 pc[224]
#define D3DTOMETAL_PS_MISC_BUFFER		1	// D3DToMetalPSMisc

// Preprocessor macro the generated pixel functions are compiled with: the
// D3DCMPFUNC of the alpha test, or 0 (the default) for none.
#define D3DTOMETAL_ALPHA_TEST_MACRO		"D3DTOMETAL_ALPHA_TEST_FUNC"

// Macros the generated pixel functions are compiled with to write color 0 in
// the other sRGB space from the view the pass draws the target through:
// 1 encodes to sRGB, 2 decodes for an sRGB view to encode back, 0 (the
// default) leaves it. With SHADER_BLEND 1 the function blends with the
// framebuffer itself, by BLEND_SRC and BLEND_DST (D3DBLEND_ZERO to
// D3DBLEND_SRCALPHASAT) and BLEND_OP (a D3DBLENDOP).
#define D3DTOMETAL_SRGB_WRITE_MACRO		"D3DTOMETAL_SRGB_WRITE"
#define D3DTOMETAL_SHADER_BLEND_MACRO	"D3DTOMETAL_SHADER_BLEND"
#define D3DTOMETAL_BLEND_SRC_MACRO		"D3DTOMETAL_BLEND_SRC"
#define D3DTOMETAL_BLEND_DST_MACRO		"D3DTOMETAL_BLEND_DST"
#define D3DTOMETAL_BLEND_OP_MACRO		"D3DTOMETAL_BLEND_OP"

// Layout of the buffer bound at D3DTOMETAL_VS_MISC_BUFFER. Must match the
// struct emitted by the translator.
struct D3DToMetalVSMisc
{
	float	m_vScreen[4];			// 1/viewport width, 1/viewport height, width, height
	float	m_ClipPlanes[2][4];		// user clip planes in clip space
	int		m_nClipPlaneEnable;		// bit n: clip plane n enabled
	int		m_nPad[3];
	int		m_IntConsts[16][4];		// i0..i15
	int		m_BoolConsts[16];		// b0..b15
};

// Layout of the buffer bound at D3DTOMETAL_PS_MISC_BUFFER.
struct D3DToMetalPSMisc
{
	float	m_flAlphaRef;			// alpha test reference, 0..1
	int		m_nPad[3];
	int		m_IntConsts[16][4];		// i0..i15
	int		m_BoolConsts[16];		// b0..b15
};

// What the device needs to know about a translated shader.
struct D3DToMetalShaderInfo
{
	bool	m_bVertexShader;
	uint32	m_nMajorVersion;
	uint32	m_nMinorVersion;

	uint32	m_nSamplerMask;			// bit n: sampler n is used
	uint32	m_nSamplerTypes;		// 2 bits per sampler: SAMPLER_TYPE_2D, _CUBE, _3D or _UNUSED
	uint32	m_nShadowSamplerMask;	// samplers declared as depth textures sampled with a comparison

	uint32	m_nHighWater;			// float constant registers referenced: c0..c(n-1)

	uint8	m_AttribMap[16];		// vertex shaders: (usage << 4) | usage index of each input vN, 0xBB if unused

	uint32	m_nInterpolantMask;		// vertex shaders: interpolants written; pixel shaders: interpolants read
	uint32	m_nFragDataMask;		// pixel shaders: color outputs written (bit n: oCn)
	bool	m_bWritesDepth;			// pixel shaders: writes oDepth
};

class D3DToMetal
{
private:
	// Pointers for dwToken stream management
	uint32* m_pdwBaseToken;
	uint32* m_pdwNextToken;

	// Vertex shader or pixel shader, and version (necessary because some opcodes alias)
	bool m_bVertexShader;
	uint32 m_dwMinorVersion;
	uint32 m_dwMajorVersion;

	bool	m_bSpew;

	// Counter for dealing with nested loops
	int m_nLoopDepth;

	// Various scratch temps needed to handle mis-matches in instruction sets between D3D and Metal
	bool m_bDeclareAddressReg;
	bool m_bNeedsSinCosDeclarations;

	// Keep track of which vs outputs are used so we can declare them
	bool m_bDeclareVSOPos;
	bool m_bDeclareVSOFog;
	uint32 m_dwTexCoordOutMask;
	uint32 m_dwColorOutMask;		// vs_2_x oD0/oD1

	int32 m_nVSPositionOutput;

	// Mask of interpolants (by usage index) which need centroid decoration
	uint32 m_nCentroidMask;

	// Pixel shader inputs read, as D3DTOMETAL_INTERP_* bits
	uint32 m_nPSInputMask;

	// Keep track of which temps are used so they can be declared
	uint32 m_dwTempUsageMask;
	bool m_bOutputColorRegister[4];
	bool m_bOutputDepthRegister;
	bool m_bUsesFragCoord;

	// Declaration of integer and bool constants
	uint32 m_dwConstIntUsageMask;
	uint32 m_dwConstBoolUsageMask;

	uint32 m_dwDefConstIntUsageMask;
	uint32 m_dwDefConstIntIterCount[32];

	// Did we use atomic_temp_var?
	bool m_bUsedAtomicTempVar;

	// Track constants so we know how to declare them
	bool m_bConstantRegisterDefined[MAX_SHADER_CONSTANTS];

	// Track sampler types when declared so we can properly decorate TEX instructions
	uint32 m_dwSamplerTypes[32];

	// Track sampler usage
	uint32 m_dwSamplerUsageMask;

	// Track shadow sampler usage
	int m_nShadowDepthSamplerMask;

	// Track attribute references
	// init to 0xFFFFFFFF (unhit)
	// index by (dwRegToken & D3DSP_REGNUM_MASK) in VS DCL insns
	// fill with (usage<<4) | (usage index).
	uint32 m_dwAttribMap[16];
	uint32 m_dwInputRegisterMask;	// vertex shaders: vN declared

	// Register high water mark
	uint32 m_nHighestRegister;

	// Indentation for readability
	int m_NumIndentTabs;

	// Output buffers.
	CUtlBuffer *m_pBufHeaderCode;
	CUtlBuffer *m_pBufAttribCode;
	CUtlBuffer *m_pBufParamCode;
	CUtlBuffer *m_pBufALUCode;

	// The semantic attached to each oN (vs_3_0) or vN (ps_3_0) register.
	uint32 m_DeclaredOutputs[MAX_DECLARED_OUTPUTS];
	uint32 m_DeclaredInputs[MAX_DECLARED_INPUTS];

	bool m_bUsesDSTInstruction;

private:
	// Utilities to aid in decoding token stream
	uint32 GetNextToken( void );
	void SkipTokens( uint32 numToSkip );
	uint32 Opcode( uint32 dwToken );
	uint32 OpcodeSpecificData( uint32 dwToken );
	uint32 TextureType ( uint32 dwToken );
	uint32 GetRegType( uint32 dwRegToken );

	// Write to the different buffers.
	void StrcatToHeaderCode( const char *pBuf );
	void StrcatToALUCode( const char *pBuf );
	void StrcatToParamCode( const char *pBuf );
	void StrcatToAttribCode( const char *pBuf );
	void PrintToBufWithIndents( CUtlBuffer &buf, const char *pFormat, ... );

	// Utilities for decoding tokens in to strings according to ASM syntax
	void PrintOpcode( uint32 inst, char* buff, int nBufLen );

	CUtlString GetParameterString( uint32 dwToken, uint32 dwSourceOrDest, bool bForceScalarSource, int *pARLDestReg );

	void PrintParameterToString ( uint32 dwToken, uint32 dwSourceOrDest, char *pRegisterName, int nBufLen, bool bForceScalarSource, int *pARLDestReg );

	void FlagIndirectRegister( uint32 dwToken, int *pARLDestReg );

	// Utilities for decoding tokens in to strings according to MSL syntax
	bool OpenIntrinsic( uint32 inst, char* buff, int nBufLen, uint32 destDimension, uint32 nArgumentDimension );
	void PrintIndentation( char *pBuf, int nBufLen );

	uint32 MaintainAttributeMap( uint32 dwToken, uint32 dwRegToken );

	CUtlString FixSwizzle( const char *pDestRegisterName, const char *pSrcRegisterName );
	void WriteCmp( const char *pDestReg, const char *pSrc0Reg, const char *pSrc1Reg, const char *pSrc2Reg );
	void WriteSamplerParameters( CUtlBuffer &buf );
	void WriteVertexOutputAssignments( CUtlBuffer &buf, uint32 nOutputMask );
	void WritePixelInputAssignments();

	void Handle_DCL();
	void Handle_DEF();
	void Handle_DEFIB( uint32 nInstruction );
	void Handle_MAD( uint32 nInstruction );
	void Handle_DP2ADD();
	void Handle_SINCOS();
	void Handle_LRP( uint32 nInstruction );
	void Handle_TEX( uint32 dwToken, bool bIsTexLDL );
	void Handle_TexLDD( uint32 nInstruction );
	void Handle_TexCoord();
	void Handle_UnaryOp( uint32 nInstruction );
	void Handle_BREAKC( uint32 dwToken );
	void HandleBinaryOp( uint32 nInstruction );
	void Handle_CMP();
	void Handle_NRM();
	void Handle_DeclarativeNonDclOp( uint32 nInstruction );

	uint32 InterpolantForUsage( uint32 dwUsage, uint32 dwUsageIndex );

public:
	D3DToMetal();

	// Translates one shader. nExtraOutputMask (vertex shaders only) adds
	// interpolant outputs the shader doesn't write, set to zero, so the vertex
	// function can feed a pixel function that reads them. Returns false if the
	// shader uses something the translator doesn't support.
	bool TranslateShader( uint32* code, CUtlBuffer *pBufMSL, D3DToMetalShaderInfo *pInfo, uint32 options,
		int32 nShadowDepthSamplerMask, uint32 nCentroidMask, uint32 nExtraOutputMask, const char *debugLabel );

	// The line of dx9asmtometal.cpp that rejected the last shader this thread
	// failed to translate.
	static int GetLastErrorLine();
};

// Which interpolants of a shader want centroid sampling, and which samplers of
// a pixel shader read shadow depth maps with a comparison. The bytecode doesn't
// say, so like ToGL these come from the shader's name.
uint32 D3DToMetalCentroidMaskFromName( bool bPixelShader, const char *pName );
int D3DToMetalShadowDepthSamplerMaskFromName( const char *pName );

#endif // DX9_ASM_TO_METAL_H
