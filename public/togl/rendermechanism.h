#ifndef RENDERMECHANISM_H
#define RENDERMECHANISM_H

#if defined(DX_TO_GL_ABSTRACTION) && defined(USE_METAL)

// The shader API built for Metal (shaderapimetal) emulates D3D9 with ToMetal.
#undef PROTECTED_THINGS_ENABLE

#include "tier0/basetypes.h"
#include "tier0/platform.h"

// What ToGL's headers include ahead of the D3D types, which the shader API
// relies on and which must precede the types' Windows macros
#include "bitmap/imageformat.h"
#include "bitvec.h"
#include "tier1/checksum_md5.h"
#include "tier1/utlvector.h"
#include "tier1/convar.h"
#include <sys/stat.h>

#include "tometal/dxabstract_types.h"
#include "tometal/dxabstract.h"

#define	GLMPRINTF(args)
#define	GLMPRINTSTR(args)
#define	GLMPRINTTEXT(args)

#elif defined(DX_TO_GL_ABSTRACTION)

#undef PROTECTED_THINGS_ENABLE

#include <GL/gl.h>
#include <GL/glext.h>

#include "tier0/basetypes.h"
#include "tier0/platform.h"

#include "togl/glmdebug.h"
#include "togl/glbase.h"
#include "togl/glentrypoints.h"
#include "togl/glmdisplay.h"
#include "togl/glmdisplaydb.h"
#include "togl/glmgrbasics.h"
#include "togl/glmgrext.h"
#include "togl/cglmbuffer.h"
#include "togl/cglmtex.h"
#include "togl/cglmfbo.h"
#include "togl/cglmprogram.h"
#include "togl/cglmquery.h"
#include "togl/glmgr.h"
#include "togl/dxabstract_types.h"
#include "togl/dxabstract.h"

#else
	//USE_ACTUAL_DX
	#ifdef WIN32
			#include <windows.h>
			#include "../../dx9sdk/include/d3d9.h"
			#include "../../dx9sdk/include/d3dx9.h"
		typedef HWND VD3DHWND;
	#endif

	#define	GLMPRINTF(args)	
	#define	GLMPRINTSTR(args)
	#define	GLMPRINTTEXT(args)
#endif // defined(DX_TO_GL_ABSTRACTION)

#endif // RENDERMECHANISM_H
