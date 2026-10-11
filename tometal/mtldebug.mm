//================ Copyright (c) Valve Corporation. All Rights Reserved. =================
//
// mtldebug.mm
//
// mtl_dumpframe <file>: writes every pass, clear, copy and draw of the next
// frame, with the state each one used, and the shaders' translations.
//
//==================================================================================================
#include "mtlinternal.h"
#include "tier1/convar.h"
#include "tier1/strtools.h"

#include <set>

#include "tier0/memdbgon.h"

FILE *g_pMtlDump;
static char s_dumpPath[ 512 ];
static bool s_bDumpRequested;
static std::set<uint32> s_dumpShaders;		// written out this frame, by id
static CUtlBuffer s_dumpShaderText( 0, 0, CUtlBuffer::TEXT_BUFFER );

CON_COMMAND( mtl_dumpframe, "Write the passes, draws and state of the next frame to a file: mtl_dumpframe <path>" )
{
	if ( args.ArgC() < 2 )
	{
		Msg( "usage: mtl_dumpframe <path>\n" );
		return;
	}
	V_strncpy( s_dumpPath, args[1], sizeof( s_dumpPath ) );
	s_bDumpRequested = true;
}

static const char *TexName( const CMtlTex *pTex )
{
	return ( pTex && pTex->m_pDebugLabel ) ? pTex->m_pDebugLabel : "?";
}

static const char *ShaderName( const CMtlShader *pShader )
{
	if ( !pShader )
		return "(none)";
	return pShader->m_pDebugLabel ? pShader->m_pDebugLabel : pShader->m_name;
}

// Keeps the translation of a shader the frame uses, for the end of the dump
static void DumpShader( CMtlShader *s )
{
	if ( !s || !s_dumpShaders.insert( s->m_nId ).second )
		return;
	D3DToMetal xlat;
	CUtlBuffer msl( 0, 0, CUtlBuffer::TEXT_BUFFER );
	D3DToMetalShaderInfo info;
	xlat.TranslateShader( s->m_pCode, &msl, &info, 0, s->m_nShadowMask, s->m_nCentroidMask, 0, ShaderName( s ) );
	s_dumpShaderText.Printf( "==== %s shader %u %s (highwater %u, samplers %x, types %x, shadow %x, interp %x)\n",
		s->m_bVertex ? "vertex" : "pixel", s->m_nId, ShaderName( s ), s->m_info.m_nHighWater, s->m_info.m_nSamplerMask, s->m_info.m_nSamplerTypes,
		s->m_info.m_nShadowSamplerMask, s->m_info.m_nInterpolantMask );
	s_dumpShaderText.Put( msl.Base(), msl.TellPut() );
	s_dumpShaderText.PutChar( '\n' );
}

static void DumpSurface( const char *pWhat, IDirect3DSurface9 *pSurf )
{
	if ( !pSurf || !pSurf->m_tex )
		return;
	const CMtlTex *t = pSurf->m_tex;
	fprintf( g_pMtlDump, " %s=%s(%s %ux%u mip%d face%d x%u)", pWhat, TexName( t ), t->m_pFormat->m_pName, t->MipWidth( pSurf->m_mip ), t->MipHeight( pSurf->m_mip ), pSurf->m_mip, pSurf->m_face, t->m_nSamples );
}

// Called at each present: ends a dump in progress, starts a requested one
void MtlDumpPresent( CMtlDevice *dev )
{
	if ( g_pMtlDump )
	{
		fprintf( g_pMtlDump, "present\n\n%.*s", (int)s_dumpShaderText.TellPut(), (const char*)s_dumpShaderText.Base() );
		s_dumpShaders.clear();
		s_dumpShaderText.Purge();
		fclose( g_pMtlDump );
		g_pMtlDump = NULL;
		Msg( "mtl_dumpframe: wrote %s\n", s_dumpPath );
	}
	if ( s_bDumpRequested )
	{
		s_bDumpRequested = false;
		g_pMtlDump = fopen( s_dumpPath, "w" );
		if ( !g_pMtlDump )
		{
			Warning( "mtl_dumpframe: can't write %s\n", s_dumpPath );
		}
	}
}

void MtlDumpPass( CMtlDevice *dev, MTLRenderPassDescriptor *rp )
{
	fprintf( g_pMtlDump, "pass %ux%u", dev->m_nPassWidth, dev->m_nPassHeight );
	for ( int i = 0; i < MTL_MAX_RENDER_TARGETS; i++ )
	{
		if ( rp.colorAttachments[i].texture )
		{
			char what[16];
			V_snprintf( what, sizeof( what ), "rt%d", i );
			DumpSurface( what, dev->m_pRenderTargets[i] );
			fprintf( g_pMtlDump, "[%s load=%d pf=%d]", rp.colorAttachments[i].texture.label ? [rp.colorAttachments[i].texture.label UTF8String] : "", (int)rp.colorAttachments[i].loadAction, (int)rp.colorAttachments[i].texture.pixelFormat );
		}
	}
	if ( rp.depthAttachment.texture )
	{
		DumpSurface( "depth", dev->m_pDepthStencil );
		fprintf( g_pMtlDump, "[load=%d clear=%g]", (int)rp.depthAttachment.loadAction, rp.depthAttachment.clearDepth );
	}
	fprintf( g_pMtlDump, " srgbwrite=%d\n", (int)dev->m_renderStates[D3DRS_SRGBWRITEENABLE] );
}

void MtlDumpClear( CMtlDevice *dev, DWORD nCount, DWORD nFlags, D3DCOLOR color, float z, DWORD stencil, bool bLoadAction )
{
	fprintf( g_pMtlDump, "clear flags=%x color=%08x z=%g stencil=%u rects=%u %s", nFlags, color, z, stencil, nCount, bLoadAction ? "(load action)" : "(drawn)" );
	DumpSurface( "rt0", dev->m_pRenderTargets[0] );
	DumpSurface( "depth", dev->m_pDepthStencil );
	fprintf( g_pMtlDump, " scissor=%d [%d %d %d %d]\n", (int)dev->m_renderStates[D3DRS_SCISSORTESTENABLE], (int)dev->m_scissorRect.left, (int)dev->m_scissorRect.top, (int)dev->m_scissorRect.right, (int)dev->m_scissorRect.bottom );
}

void MtlDumpCopy( const char *pWhat, IDirect3DSurface9 *pSrc, IDirect3DSurface9 *pDst )
{
	fprintf( g_pMtlDump, "%s", pWhat );
	DumpSurface( "src", pSrc );
	DumpSurface( "dst", pDst );
	fprintf( g_pMtlDump, "\n" );
}

void MtlDumpDraw( CMtlDevice *dev, D3DPRIMITIVETYPE type, uint nPrims, bool bDrawn )
{
	const DWORD *rs = dev->m_renderStates;
	CMtlShader *pVS = dev->m_pVertexShader ? dev->m_pVertexShader->m_shader : NULL;
	CMtlShader *pPS = dev->m_pPixelShader ? dev->m_pPixelShader->m_shader : NULL;
	DumpShader( pVS );
	DumpShader( pPS );

	uint8 srgbConvert = kMtlSRGBConvertNone;
	bool bSRGBInPass = dev->SRGBConversion( &srgbConvert );
	fprintf( g_pMtlDump, "draw %u%s prim=%d n=%u vs=%u:%s ps=%u:%s srgbconvert=%d%s\n", dev->m_nDrawCallIndex, bDrawn ? "" : " SKIPPED", (int)type, nPrims,
		pVS ? pVS->m_nId : 0, ShaderName( pVS ), pPS ? pPS->m_nId : 0, ShaderName( pPS ), srgbConvert, bSRGBInPass ? "" : " (reopen)" );
	fprintf( g_pMtlDump, "  blend=%d %d/%d op%d cwm=%x atest=%d func%d ref%d a2c=%d | z=%d zw=%d zf=%d | st=%d f%d ref%d rm%x wm%x ops%d/%d/%d | cull=%d fill=%d clip=%x srgbw=%d\n",
		(int)rs[D3DRS_ALPHABLENDENABLE], (int)rs[D3DRS_SRCBLEND], (int)rs[D3DRS_DESTBLEND], (int)rs[D3DRS_BLENDOP], (int)rs[D3DRS_COLORWRITEENABLE],
		(int)rs[D3DRS_ALPHATESTENABLE], (int)rs[D3DRS_ALPHAFUNC], (int)rs[D3DRS_ALPHAREF], (int)rs[D3DRS_ADAPTIVETESS_Y],
		(int)rs[D3DRS_ZENABLE], (int)rs[D3DRS_ZWRITEENABLE], (int)rs[D3DRS_ZFUNC],
		(int)rs[D3DRS_STENCILENABLE], (int)rs[D3DRS_STENCILFUNC], (int)rs[D3DRS_STENCILREF], (int)rs[D3DRS_STENCILMASK], (int)rs[D3DRS_STENCILWRITEMASK],
		(int)rs[D3DRS_STENCILFAIL], (int)rs[D3DRS_STENCILZFAIL], (int)rs[D3DRS_STENCILPASS],
		(int)rs[D3DRS_CULLMODE], (int)rs[D3DRS_FILLMODE], (int)rs[D3DRS_CLIPPLANEENABLE], (int)rs[D3DRS_SRGBWRITEENABLE] );
	const D3DVIEWPORT9 &vp = dev->m_viewport;
	fprintf( g_pMtlDump, "  vp=%u,%u %ux%u z%g-%g scissor=%d [%d %d %d %d] pass=%ux%u",
		(uint)vp.X, (uint)vp.Y, (uint)vp.Width, (uint)vp.Height, vp.MinZ, vp.MaxZ, (int)rs[D3DRS_SCISSORTESTENABLE],
		(int)dev->m_scissorRect.left, (int)dev->m_scissorRect.top, (int)dev->m_scissorRect.right, (int)dev->m_scissorRect.bottom, dev->m_nPassWidth, dev->m_nPassHeight );
	DumpSurface( "rt0", dev->m_pRenderTargets[0] );
	DumpSurface( "rt1", dev->m_pRenderTargets[1] );
	DumpSurface( "depth", dev->m_pDepthStencil );
	fprintf( g_pMtlDump, "\n" );

	const CMtlVertexDecl *pDecl = dev->m_pVertDecl ? dev->m_pVertDecl->m_decl : NULL;
	if ( pDecl )
	{
		fprintf( g_pMtlDump, "  decl:" );
		for ( uint i = 0; i < pDecl->m_nElements; i++ )
		{
			const D3DVERTEXELEMENT9 &e = pDecl->m_elements[i];
			fprintf( g_pMtlDump, " [s%d +%d t%d u%d.%d]", e.Stream, e.Offset, e.Type, e.Usage, e.UsageIndex );
		}
		for ( int s = 0; s < MTL_MAX_STREAMS; s++ )
		{
			if ( pDecl->m_nStreamMask & ( 1 << s ) )
				fprintf( g_pMtlDump, " stream%d(stride %u off %u %s)", s, dev->m_streams[s].m_stride, dev->m_streams[s].m_offset, dev->m_streams[s].m_vtxBuffer ? "" : "NULL" );
		}
		fprintf( g_pMtlDump, "\n" );
	}
	if ( pVS )
	{
		fprintf( g_pMtlDump, "  vsin:" );
		for ( int r = 0; r < 16; r++ )
		{
			if ( pVS->m_info.m_AttribMap[r] != 0xBB )
				fprintf( g_pMtlDump, " v%d=u%d.%d%s", r, pVS->m_info.m_AttribMap[r] >> 4, pVS->m_info.m_AttribMap[r] & 15, ( pDecl && pDecl->m_usageToElement[ pVS->m_info.m_AttribMap[r] ] != 0xFF ) ? "" : "(MISSING)" );
		}
		fprintf( g_pMtlDump, "\n" );
	}

	if ( pPS )
	{
		for ( int i = 0; i < MTL_SAMPLER_COUNT; i++ )
		{
			if ( !( pPS->m_info.m_nSamplerMask & ( 1 << i ) ) )
				continue;
			const MtlSamplerState &ss = dev->m_samplers[i].m_state;
			IDirect3DBaseTexture9 *pBase = dev->m_samplers[i].m_pTexture;
			const CMtlTex *t = pBase ? pBase->m_tex : NULL;
			uint nType = ( pPS->m_info.m_nSamplerTypes >> ( i * 2 ) ) & 3;
			fprintf( g_pMtlDump, "  s%d(type%u%s): ", i, nType, ( pPS->m_info.m_nShadowSamplerMask & ( 1 << i ) ) ? " shadow" : "" );
			if ( t )
			{
				uint nValid = 0;
				for ( uint k = 0; k < t->m_nFaces * t->m_nMips; k++ )
					nValid += t->m_pSliceValid[k] ? 1 : 0;
				fprintf( g_pMtlDump, "%s %s %ux%ux%u mips%u mtltype%d flags%x valid%u/%u",
					TexName( t ), t->m_pFormat->m_pName, t->m_nWidth, t->m_nHeight, t->m_nDepth, t->m_nMips, (int)t->m_type, t->m_nFlags, nValid, t->m_nFaces * t->m_nMips );
			}
			else
			{
				fprintf( g_pMtlDump, "NULL" );
			}
			fprintf( g_pMtlDump, " | addr%d,%d,%d filt%d/%d/%d aniso%d minlod%d srgb%d border%08x\n",
				ss.m_addressU, ss.m_addressV, ss.m_addressW, ss.m_minFilter, ss.m_magFilter, ss.m_mipFilter, ss.m_maxAniso, ss.m_minLOD, ss.m_srgb, ss.m_borderColor );
		}
		uint n = MIN( pPS->m_info.m_nHighWater, (uint)MTL_PS_CONSTANT_COUNT );
		fprintf( g_pMtlDump, "  pc[%u]:", n );
		for ( uint c = 0; c < n; c++ )
		{
			const float *f = dev->m_psConstants[c];
			fprintf( g_pMtlDump, " c%u(%g %g %g %g)", c, f[0], f[1], f[2], f[3] );
		}
		fprintf( g_pMtlDump, "\n  pb:" );
		for ( int b = 0; b < 16; b++ )
			fprintf( g_pMtlDump, "%d", dev->m_psMisc.m_BoolConsts[b] );
		fprintf( g_pMtlDump, " vb:" );
		for ( int b = 0; b < 16; b++ )
			fprintf( g_pMtlDump, "%d", dev->m_vsMisc.m_BoolConsts[b] );
		fprintf( g_pMtlDump, "\n" );
	}
}
