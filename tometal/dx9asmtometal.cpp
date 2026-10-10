//------------------------------------------------------------------------------
// dx9asmtometal.cpp
//
// Translates Direct3D 9 shader bytecode into Metal Shading Language.
// Derived from ToGL's dx9asmtogl2.cpp; see dx9asmtometal.h.
//------------------------------------------------------------------------------
#include "tier0/platform.h"
#include "tier0/dbg.h"
#include "tier1/strtools.h"
#include "tier1/utlbuffer.h"
#include "tometal/dxabstract_types.h"
#include "materialsystem/IShader.h"
#include "dx9asmtometal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define strcat_s( a, b, c ) V_strcat( a, c, b )

// Set when the shader being translated uses something the translator doesn't
// support; TranslateShader then returns false instead of a program.
static thread_local bool s_bTranslationFailed;
static thread_local int s_nTranslationErrorLine;

static void TranslationErrorAt( int nLine )
{
	if ( !s_bTranslationFailed )
	{
		s_nTranslationErrorLine = nLine;
	}
	s_bTranslationFailed = true;
}
#define TranslationError() TranslationErrorAt( __LINE__ )

int D3DToMetal::GetLastErrorLine()
{
	return s_nTranslationErrorLine;
}

#define DST_REGISTER		0
#define SRC_REGISTER		1

#define UNDECLARED_OUTPUT	0xFFFFFFFF
#define UNDECLARED_INPUT	0xFFFFFFFF


static const char *g_szVecZeros[] = { NULL, "0.0", "vec2( 0.0, 0.0 )", "vec3( 0.0, 0.0, 0.0 )", "vec4( 0.0, 0.0, 0.0, 0.0 )" };
static const char *g_szVecOnes[] = { NULL, "1.0", "vec2( 1.0, 1.0 )", "vec3( 1.0, 1.0, 1.0 )", "vec4( 1.0, 1.0, 1.0, 1.0 )" };
static const char *g_szDefaultSwizzle = "xyzw";
static const char *g_szDefaultSwizzleStrings[] = { "x", "y", "z", "w" };

static const char *g_pAtomicTempVarName = "atomic_temp_var";

int __cdecl SortInts( const int *a, const int *b )
{
	if ( *a < *b )
		return -1;
	else if ( *a > *b )
		return 1;
	else
		return 0;
}

void StripExtraTrailingZeros( char *pStr )
{
	int len = (int)V_strlen( pStr );
	while ( len >= 2 && pStr[len-1] == '0' && pStr[len-2] != '.' )
	{
		pStr[len-1] = 0;
		--len;
	}
}

void D3DToMetal::PrintToBufWithIndents( CUtlBuffer &buf, const char *pFormat, ... )
{
	va_list marker;
	va_start( marker, pFormat );

	char szTemp[1024];
	V_vsnprintf( szTemp, sizeof( szTemp ), pFormat, marker );
	va_end( marker );

	PrintIndentation( (char*)buf.Base(), buf.Size() );
	strcat_s( (char*)buf.Base(), buf.Size(), szTemp );
}

void PrintToBuf( CUtlBuffer &buf, const char *pFormat, ... )
{
	va_list marker;
	va_start( marker, pFormat );
	
	char szTemp[1024];
	V_vsnprintf( szTemp, sizeof( szTemp ), pFormat, marker );
	va_end( marker );

	strcat_s( (char*)buf.Base(), buf.Size(), szTemp );
}

void PrintToBuf( char *pOut, int nOutSize, const char *pFormat, ... )
{
	int nStrlen = V_strlen( pOut );
	pOut += nStrlen;
	nOutSize -= nStrlen;
	
	va_list marker;
	va_start( marker, pFormat );
	V_vsnprintf( pOut, nOutSize, pFormat, marker );
	va_end( marker );
}

// Return the number of letters following the dot.
// Returns 4 if there is no dot.
// (So "r0.xy" returns 2 and "r0" returns 4).
int GetNumWriteMaskEntries( const char *pParam )
{
	const char *pDot = strchr( pParam, '.' );
	if ( pDot )
		return V_strlen( pDot + 1 );
	else
		return 4;
}

const char* GetSwizzleDot( const char *pParam )
{
	const char *pDot = strrchr( pParam, '.' );

	const char *pSquareClose = strrchr( pParam, ']' );

	if ( pSquareClose )
	{
		// The test against ']' catches cases like, so we point to the last dot vc[int(va_r.x) + 29].x
		if ( pDot && ( pSquareClose < pDot  ) )
			return pDot;
		else
			return NULL;
	}

	// Make sure the next character is a valid swizzle since we want to treat strings like vec4( gl_Normal, 0.0 ) as a whole param name.
	if ( pDot && ( ( *(pDot+1) == 'x' ) || ( *(pDot+1) == 'y' ) || ( *(pDot+1) == 'z' ) || ( *(pDot+1) == 'w' ) ||
				   ( *(pDot+1) == 'r' ) || ( *(pDot+1) == 'g' ) || ( *(pDot+1) == 'b' ) || ( *(pDot+1) == 'z' ) ) )
	{
		return pDot;
	}

	return NULL;
}

int GetNumSwizzleComponents( const char *pParam )
{
	// Special scalar output which won't accept a swizzle
	if ( !V_stricmp( pParam, "gl_FogFragCoord" ) )
		return 1;

	// Special scalar output which won't accept a swizzle
	if ( !V_stricmp( pParam, "gl_FragDepth" ) )
		return 1;	
	
	// Special scalar output which won't accept a swizzle
	if ( !V_stricmp( pParam, "a0" ) )
		return 1;
	
	const char *pDot = GetSwizzleDot( pParam );
	if ( pDot )
	{
		pDot++; // Step over the dot

		int nNumSwizzleComponents = 0;
		while ( ( *pDot == 'x' ) || ( *pDot == 'y' ) || ( *pDot == 'z' ) || ( *pDot == 'w' ) ||
				( *pDot == 'r' ) || ( *pDot == 'g' ) || ( *pDot == 'b' ) || ( *pDot == 'z' ) )
		{
			nNumSwizzleComponents++;
			pDot++;
		}

		return nNumSwizzleComponents;
	}

	return 0;
}

char GetSwizzleComponent( const char *pParam, int n )
{
	Assert( n < 4 );

	const char *pDot = GetSwizzleDot( pParam );
	if ( pDot )
	{
		++pDot;
		int nComponents = (int)V_strlen( pDot );
		Assert( nComponents > 0 );

		if ( n < nComponents )
			return pDot[n];
		else
			return pDot[nComponents-1];
	}

	return g_szDefaultSwizzle[n];
}

// Replace the parameter name and leave the swizzle intact.
// So "somevar.xyz" becomes "othervar.xyz".
void ReplaceParamName( const char *pSrc, const char *pNewParamName, char *pOut, int nOutLen )
{
	// Start with the new parameter name.
	V_strncpy( pOut, pNewParamName, nOutLen );

	// Now add the swizzle if necessary.
	const char *pDot = GetSwizzleDot( pSrc );
	if ( pDot )
	{
		V_strncat( pOut, pDot, nOutLen );
	}
}

void GetParamNameWithoutSwizzle( const char *pParam, char *pOut, int nOutLen )
{
	char *pParamStart = (char *) pParam;
    const char *pParamEnd = GetSwizzleDot( pParam );        // dot followed by valid swizzle characters
	bool bAbsWrapper = false;
    
	// Check for abs() or -abs() wrapper and strip it off during the fixup
	if ( !V_strncmp( pParam, "abs(", 4 ) || !V_strncmp( pParam, "-abs(", 5 ) )
	{
		const char *pOpenParen = strchr( pParam, '(' );		// FIRST opening paren
		const char *pClosingParen = strrchr( pParam, ')' ); // LAST closing paren

		Assert ( pOpenParen && pClosingParen );

		pParamStart = (char *) pOpenParen;
		pParamStart++;
		bAbsWrapper = true;
        
        if ( !pParamEnd )
        {
            pParamEnd = pClosingParen;
        }
    }
  	
    if ( pParamEnd )
    {
        int nToCopy = MIN( nOutLen-1, pParamEnd - pParamStart );
        memcpy( pOut, pParamStart, nToCopy );
        pOut[nToCopy] = 0;
    
    }
    else
    {
        V_strncpy( pOut, pParamStart, nOutLen );
    }
    
}

bool DoParamNamesMatch( const char *pParam1, const char *pParam2 )
{
	char szTemp[2][256];
	GetParamNameWithoutSwizzle( pParam1, szTemp[0], sizeof( szTemp[0] ) );
	GetParamNameWithoutSwizzle( pParam2, szTemp[1], sizeof( szTemp[1] ) );
	return ( V_stricmp( szTemp[0], szTemp[1] ) == 0 );
}



// Extract the n'th component of the swizzle mask.
// If n would exceed the length of the swizzle mask, then it looks up into "xyzw".
void WriteParamWithSingleMaskEntry( const char *pParam, int n, char *pOut, int nOutLen )
{
	bool bCloseParen = false;
	if ( !V_strncmp( pParam, "-abs(", 5 ) )
	{
		V_strcpy( pOut, "-abs(" );
		bCloseParen = true;
		
		pOut += 5; nOutLen -= 5;
	}
	else if ( !V_strncmp( pParam, "abs(", 4 ) )
	{
		V_strcpy( pOut, "abs(" );
		bCloseParen = true;
		
		pOut += 4; nOutLen -= 4;
	}
	
	GetParamNameWithoutSwizzle( pParam, pOut, nOutLen );
	PrintToBuf( pOut, nOutLen, "." );
	PrintToBuf( pOut, nOutLen, "%c", GetSwizzleComponent( pParam, n ) );

	if ( bCloseParen )
	{
		PrintToBuf( pOut, nOutLen, ")" );
	}
}


float uint32ToFloat( uint32 dw )
{
	return *((float*)&dw);
}

CUtlString EnsureNumSwizzleComponents( const char *pSrcRegisterName, int nComponents )
{
	int nExisting = GetNumSwizzleComponents( pSrcRegisterName );
	if ( nExisting == nComponents )
		return pSrcRegisterName;

	bool bAbsWrapper = false; // Parameter wrapped in an abs()
	bool bAbsNegative = false; // -abs()
	char szSrcRegister[128];
	V_strncpy( szSrcRegister, pSrcRegisterName, sizeof(szSrcRegister) );

	// Check for abs() or -abs() wrapper and strip it off during the fixup
	if ( !V_strncmp( pSrcRegisterName, "abs(", 4 ) || !V_strncmp( pSrcRegisterName, "-abs(", 5 ) )
	{
		bAbsWrapper = true;
		bAbsNegative = pSrcRegisterName[0] == '-';

		const char *pOpenParen = strchr( pSrcRegisterName, '(' );		// FIRST opening paren
		const char *pClosingParen = strrchr( pSrcRegisterName, ')' ); // LAST closing paren

		Assert ( pOpenParen && pClosingParen );	// If we start with abs( and don't get both parens, something is very wrong

		// Copy out just the register name with no abs()
		int nRegNameLength = pClosingParen - pOpenParen - 1;
		V_strncpy( szSrcRegister, pOpenParen+1, nRegNameLength + 1 ); // Kind of a weird function...copy more than you need and slam the last char to NULL-terminate
	}

	char szReg[256];
	GetParamNameWithoutSwizzle( szSrcRegister, szReg, sizeof( szReg ) );
	if ( nComponents == 0 )
		return szReg;

	PrintToBuf( szReg, sizeof( szReg ), "." );
	if ( nExisting > nComponents )
	{
		// DX ASM will sometimes have statements like "NRM r0.xyz, r1.yzww", where it just doesn't use the last part of r1. So we won't either.
		for ( int i=0; i < nComponents; i++ )
		{
			PrintToBuf( szReg, sizeof( szReg ), "%c", GetSwizzleComponent( szSrcRegister, i ) );
		}
	}
	else
	{
		if ( nExisting == 0 )
		{
			// We've got something like r0 and need N more components, so add as much of "xyzw" is needed.
			for ( int i=0; i < nComponents; i++ )
				PrintToBuf( szReg, sizeof( szReg ), "%c", g_szDefaultSwizzle[i] );
		}
		else
		{
			// We've got something like r0.x and need N more components, so replicate the X so it looks like r0.xxx
			V_strncpy( szReg, szSrcRegister, sizeof( szReg ) );
			char cLast = szSrcRegister[ V_strlen( szSrcRegister ) - 1 ];
			for ( int i=nExisting; i < nComponents; i++ )
			{
				PrintToBuf( szReg, sizeof( szReg ), "%c", cLast );
			}
		}
	}

	if ( bAbsWrapper )
	{
		char szTemp[128];
		V_strncpy( szTemp, szReg, sizeof(szTemp) );
		V_snprintf( szReg, sizeof( szReg ), "%sabs(%s)", bAbsNegative ? "-" : "", szTemp ) ;
	}

	return szReg;	
}

static bool IsFloatNaN( float f )
{
	const uint nBits = *reinterpret_cast<uint*>(&f);
	const uint nExponent = ( nBits >> 23 ) & 0xFF;

	return ( nExponent == 255 );
}

static inline bool EqualTol( double a, double b, double t )
{
	return fabs( a - b ) <= ( ( MAX( fabs( a ), fabs( b ) ) + 1.0 ) * t );
}

// Originally written by Bruce Dawson, see:
// See http://randomascii.wordpress.com/2012/03/08/float-precisionfrom-zero-to-100-digits-2/
// This class represents a very limited high-precision number with 'count' 32-bit
// unsigned elements.
template <int count>
struct HighPrec
{
	typedef unsigned T;
	typedef unsigned long long Product_t;
	static const int kWordShift = 32;
	HighPrec()
	{
		memset(m_data, 0, sizeof(m_data));
		m_nLowestNonZeroIndex = ARRAYSIZE(m_data);
	}

	// Insert the bits from value into m_data, shifted in from the bottom (least
	// significant end) by the specified number of bits. A shift of zero or less
	// means that none of the bits will be shifted in. A shift of one means that
	// the high bit of value will be in the bottom of the last element of m_data -
	// the least significant bit. A shift of kWordShift means that value will be
	// in the least significant element of m_data, and so on.
	void InsertLowBits(T value, int shiftAmount)
	{
		if (shiftAmount <= 0)
			return;

		int subShift = shiftAmount & (kWordShift - 1);
		int bigShift = shiftAmount / kWordShift;
		Product_t result = (Product_t)value << subShift;
		T resultLow = (T)result;
		T resultHigh = result >> kWordShift;

		// Use an unsigned type so that negative numbers will become large,
		// which makes the range checking below simpler.
		unsigned highIndex = ARRAYSIZE(m_data) - 1 - bigShift;
		// Write the results to the data array. If the index is too large
		// then that means that the data was shifted off the edge.
		if ( (highIndex < ARRAYSIZE(m_data)) && ( resultHigh ) )
		{
			m_data[highIndex] |= resultHigh;
			m_nLowestNonZeroIndex = MIN( m_nLowestNonZeroIndex, highIndex );
		}

		if ( ( highIndex + 1 < ARRAYSIZE(m_data)) && ( resultLow ) )
		{
			m_data[highIndex + 1] |= resultLow;
			m_nLowestNonZeroIndex = MIN( m_nLowestNonZeroIndex, highIndex + 1 );
		}
	}

	// Insert the bits from value into m_data, shifted in from the top (most
	// significant end) by the specified number of bits. A shift of zero or less
	// means that none of the bits will be shifted in. A shift of one means that
	// the low bit of value will be in the top of the first element of m_data -
	// the most significant bit. A shift of kWordShift means that value will be
	// in the most significant element of m_data, and so on.
	void InsertTopBits(T value, int shiftAmount)
	{
		InsertLowBits(value, (ARRAYSIZE(m_data) + 1) * kWordShift - shiftAmount);
	}

	// Return true if all elements of m_data are zero.
	bool IsZero() const
	{
		bool bIsZero = ( m_nLowestNonZeroIndex == ARRAYSIZE(m_data) );

#ifdef DEBUG
		for (int i = 0; i < ARRAYSIZE(m_data); ++i)
		{
			if (m_data[i])
			{
				Assert( !bIsZero );
				return false;
			}
		}
		Assert( bIsZero );
#endif

		return bIsZero;
	}

	// Divide by div and return the remainder, from 0 to div-1.
	// Standard long-division algorithm.
	T DivReturnRemainder(T divisor)
	{
		T remainder = 0;

#ifdef DEBUG
		for (uint j = 0; j < m_nLowestNonZeroIndex; ++j)
		{
			Assert( m_data[j] == 0 );
		}
#endif
		
		int nNewLowestNonZeroIndex = ARRAYSIZE(m_data);
		for (int i = m_nLowestNonZeroIndex; i < ARRAYSIZE(m_data); ++i)
		{
			Product_t dividend = ((Product_t)remainder << kWordShift) + m_data[i];
			Product_t result = dividend / divisor;
			remainder = T(dividend % divisor);

			m_data[i] = T(result);

			if ( ( result ) && ( nNewLowestNonZeroIndex == ARRAYSIZE(m_data) ) )
				nNewLowestNonZeroIndex = i;
		}
		m_nLowestNonZeroIndex = nNewLowestNonZeroIndex;

		return remainder;
	}

	// The individual 'digits' (32-bit unsigned integers actually) that
	// make up the number. The most-significant digit is in m_data[0].
	T m_data[count];
	
	uint m_nLowestNonZeroIndex;
};

union Double_t
{
	Double_t(double num = 0.0f) : f(num) {}
	// Portable extraction of components.
	bool Negative() const { return (i >> 63) != 0; }
	int64_t RawMantissa() const { return i & ((1LL << 52) - 1); }
	int64_t RawExponent() const { return (i >> 52) & 0x7FF; }

	int64_t i;
	double f;
};

static uint PrintDoubleInt( char *pBuf, uint nBufSize, double f, uint nMinChars )
{
	static const char *pDigits = "00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899";

	Assert( !nMinChars || ( ( nMinChars % 6 ) == 0 ) );

	char *pLastChar = pBuf + nBufSize - 1;
	char *pDst = pLastChar;
	*pDst-- = '\0';

	// Put the double in our magic union so we can grab the components.
	union Double_t num(f);

	// Get the character that represents the sign.
	// Check for NaNs or infinity.
	if (num.RawExponent() == 2047)
	{
		TranslationError();
	}

	// Adjust for the exponent bias.
	int exponentValue = int(num.RawExponent() - 1023);
	// Add the implied one to the mantissa.
	uint64_t mantissaValue = (1ll << 52) + num.RawMantissa();
	// Special-case for denormals - no special exponent value and
	// no implied one.
	if (num.RawExponent() == 0)
	{
		exponentValue = -1022;
		mantissaValue = num.RawMantissa();
	}
	uint32_t mantissaHigh = mantissaValue >> 32;
	uint32_t mantissaLow = mantissaValue & 0xFFFFFFFF;

	// The first bit of the mantissa has an implied value of one and this can
	// be shifted 1023 positions to the left, so that's 1024 bits to the left
	// of the binary point, or 32 32-bit words for the integer part.
	HighPrec<32> intPart;
	// When our exponentValue is zero (a number in the 1.0 to 2.0 range)
	// we have a 53-bit mantissa and the implied value of the highest bit
	// is 1. We need to shift 12 bits in from the bottom to get that 53rd bit
	// into the ones spot in the integral portion.
	// To complicate it a bit more we have to insert the mantissa as two parts.
	intPart.InsertLowBits(mantissaHigh, 12 + exponentValue);
	intPart.InsertLowBits(mantissaLow, 12 + exponentValue - 32);

	bool bAnyDigitsLeft;
	do
	{
		uint remainder = intPart.DivReturnRemainder( 1000000 ); // 10^6
		uint origRemainer = remainder; (void)origRemainer;

		bAnyDigitsLeft = !intPart.IsZero();

		if ( bAnyDigitsLeft )
		{
			uint n = remainder % 100U; remainder /= 100U; *reinterpret_cast<uint16*>(pDst - 1) = reinterpret_cast<const uint16*>(pDigits)[n]; 
			n = remainder % 100U; remainder /= 100U; *reinterpret_cast<uint16*>(pDst - 1 - 2) = reinterpret_cast<const uint16*>(pDigits)[n]; 
			Assert( remainder < 100U );
			*reinterpret_cast<uint16*>(pDst - 1 - 4) = reinterpret_cast<const uint16*>(pDigits)[remainder]; 
			pDst -= 6;
		}
		else
		{
			uint n = remainder % 100U; remainder /= 100U; *reinterpret_cast<uint16*>(pDst - 1) = reinterpret_cast<const uint16*>(pDigits)[n]; --pDst; if ( ( n >= 10 ) || ( remainder ) ) --pDst;
			if ( remainder )
			{
				n = remainder % 100U; remainder /= 100U; *reinterpret_cast<uint16*>(pDst - 1) = reinterpret_cast<const uint16*>(pDigits)[n]; --pDst; if ( ( n >= 10 ) || ( remainder ) ) --pDst;

				if ( remainder )
				{
					Assert( remainder < 100U );
					*reinterpret_cast<uint16*>(pDst - 1) = reinterpret_cast<const uint16*>(pDigits)[remainder]; --pDst; if ( remainder >= 10 ) --pDst;
				}
			}
		}

	} while ( bAnyDigitsLeft );

	uint l = pLastChar - pDst;
	
	while ( ( l - 1 ) < nMinChars )
	{
		*pDst-- = '0';
		l++;
	}
	
	Assert( (int)l == ( pLastChar - pDst ) );

	Assert( l <= nBufSize );
			
	memmove( pBuf, pDst + 1, l );
	return l - 1;
}

// FloatToString is equivalent to sprintf( "%.12f" ), but doesn't have any dependencies on the current locale setting.
// Unfortunately, high accuracy radix conversion is actually pretty tricky to do right. 
// Most importantly, this function has the same max roundtrip (IEEE->ASCII->IEEE) error as the MS CRT functions and can reliably handle extremely large inputs.
static void FloatToString( char *pBuf, uint nBufSize, double fConst )
{
	char *pEnd = pBuf + nBufSize;
	char *pDst = pBuf;

	double flVal = fConst;
	if ( IsFloatNaN( flVal ) )
	{
		flVal = 0;
	}

	if ( flVal < 0.0f )
	{
		*pDst++ = '-';
		flVal = -flVal;
	}

	double flInt;
	double flFract = modf( flVal, &flInt );

	flFract = floor( flFract * 1000000000000.0 + .5 );

	if ( !flInt )
	{
		*pDst++ = '0';
	}
	else
	{
		uint l = PrintDoubleInt( pDst, pEnd - pDst, flInt, 0 );
		pDst += l;
	}

	*pDst++ = '.';
	if ( !flFract )
	{
		*pDst++ = '0';
		*pDst++ = '\0';
	}
	else
	{
		uint l = PrintDoubleInt( pDst, pEnd - pDst, flFract, 12 );
		pDst += l;
					
		StripExtraTrailingZeros( pBuf );	// Turn 1.00000 into 1.0
	}
}


// Definitions shared by every generated program. GLSL-style names used by the
// instruction translation map onto their Metal equivalents here, so the
// translated instructions can stay close to ToGL's.
static const char *g_pMSLPrelude =
	"#include <metal_stdlib>\n"
	"using namespace metal;\n"
	"typedef float2 vec2;\n"
	"typedef float3 vec3;\n"
	"typedef float4 vec4;\n"
	"#define lessThan( a, b ) ( ( a ) < ( b ) )\n"
	"#define greaterThanEqual( a, b ) ( ( a ) >= ( b ) )\n"
	"// D3D9 pow, log and rsq operate on the absolute value of their source.\n"
	"template < typename T > inline T d3d_pow( T a, T b ) { return pow( abs( a ), b ); }\n"
	"#define texture2D( s, c ) s##_t.sample( s##_s, ( c ) )\n"
	"#define texture2DBias( s, c, b ) s##_t.sample( s##_s, ( c ), bias( b ) )\n"
	"#define texture2DLod( s, c, l ) s##_t.sample( s##_s, ( c ), level( l ) )\n"
	"#define texture2DProj( s, c ) s##_t.sample( s##_s, ( c ).xy / ( c ).w )\n"
	"#define shadow2D( s, c ) vec4( s##_t.sample_compare( s##_s, ( c ).xy, ( c ).z ) )\n"
	"#define shadow2DLod( s, c, l ) vec4( s##_t.sample_compare( s##_s, ( c ).xy, ( c ).z, level( l ) ) )\n"
	"#define texture3D( s, c ) s##_t.sample( s##_s, ( c ) )\n"
	"#define textureCube( s, c ) s##_t.sample( s##_s, ( c ) )\n"
	"#define textureCubeBias( s, c, b ) s##_t.sample( s##_s, ( c ), bias( b ) )\n"
	"#define texture3DLod( s, c, l ) s##_t.sample( s##_s, ( c ), level( l ) )\n"
	"#define textureCubeLod( s, c, l ) s##_t.sample( s##_s, ( c ), level( l ) )\n"
	"#define texture2DGrad( s, c, dx, dy ) s##_t.sample( s##_s, ( c ), gradient2d( ( dx ), ( dy ) ) )\n"
	"\n";

static const char *g_pVSMiscStruct =
	"struct VSMisc\n"
	"{\n"
	"\tfloat4 vcscreen;\n"
	"\tfloat4 clipPlane[2];\n"
	"\tint clipEnable;\n"
	"\tint pad[3];\n"
	"\tint4 ints[16];\n"
	"\tint bools[16];\n"
	"};\n\n";

static const char *g_pPSMiscStruct =
	"struct PSMisc\n"
	"{\n"
	"\tfloat alphaRef;\n"
	"\tint pad[3];\n"
	"\tint4 ints[16];\n"
	"\tint bools[16];\n"
	"};\n\n"
	"#ifndef D3DTOMETAL_ALPHA_TEST_FUNC\n"
	"#define D3DTOMETAL_ALPHA_TEST_FUNC 0\n"
	"#endif\n\n";

D3DToMetal::D3DToMetal()
{
}

uint32 D3DToMetal::GetNextToken( void )
{
	uint32 dwToken = *m_pdwNextToken;
	m_pdwNextToken++;
	return dwToken;
}

void D3DToMetal::SkipTokens( uint32 numToSkip )
{
	m_pdwNextToken += numToSkip;
}

uint32 D3DToMetal::Opcode( uint32 dwToken )
{
	return ( dwToken & D3DSI_OPCODE_MASK );
}

uint32 D3DToMetal::OpcodeSpecificData (uint32 dwToken)
{
	return ( ( dwToken & D3DSP_OPCODESPECIFICCONTROL_MASK ) >> D3DSP_OPCODESPECIFICCONTROL_SHIFT );
}

uint32 D3DToMetal::TextureType ( uint32 dwToken )
{
	return ( dwToken & D3DSP_TEXTURETYPE_MASK ); // Note this one doesn't shift due to weird D3DSAMPLER_TEXTURE_TYPE enum
}

// Print the MSL intrinsic corresponding to particular instruction
bool D3DToMetal::OpenIntrinsic( uint32 inst, char* buff, int nBufLen, uint32 destDimension, uint32 nArgumentDimension )
{
	// Some intrinsics need type conversion, which we do in this routine
	// As a result, the caller must sometimes close both parentheses, not just one
	bool bDoubleClose = false;

	if ( nArgumentDimension == 0 )
	{
		nArgumentDimension = 4;
	}

	switch ( inst )
	{
		case D3DSIO_DP3:
		case D3DSIO_DP4:
			if ( destDimension == 1 )
			{
				V_snprintf( buff, nBufLen, "dot( " );
			}
			else
			{
				if ( !destDimension )
					destDimension = 4;
				V_snprintf( buff, nBufLen, "vec%d( dot( ", destDimension );
				bDoubleClose = true;
			}
			break;
		case D3DSIO_MIN:
			V_snprintf( buff, nBufLen, "min( " );
			break;
		case D3DSIO_MAX:
			V_snprintf( buff, nBufLen, "max( " );
			break;
		case D3DSIO_SLT:
			if ( nArgumentDimension == 1 )
			{
				V_snprintf( buff, nBufLen, "float( " ); // lessThan doesn't have a scalar version
			}
			else
			{
				Assert( nArgumentDimension > 1 );
				V_snprintf( buff, nBufLen, "vec%d( lessThan( ", nArgumentDimension );
				bDoubleClose = true;
			}
			break;
		case D3DSIO_SGE:
			if ( nArgumentDimension == 1 )
			{
				V_snprintf( buff, nBufLen, "float( " ); // greaterThanEqual doesn't have a scalar version
			}
			else
			{
				Assert( nArgumentDimension > 1 );
				V_snprintf( buff, nBufLen, "vec%d( greaterThanEqual( ", nArgumentDimension );
				bDoubleClose = true;
			}
			break;
		case D3DSIO_DST:
			V_snprintf( buff, nBufLen, "dst( " );
			break;
		case D3DSIO_POW:
			V_snprintf( buff, nBufLen, "d3d_pow( " );
			break;
		case D3DSIO_CRS:
			V_snprintf( buff, nBufLen, "cross( " );
			break;
		default:
			TranslationError();
			break;
	}

	return bDoubleClose;
}

const char* GetOperatorString( uint32 inst )
{
	if ( inst == D3DSIO_ADD )
		return "+";
	else if ( inst == D3DSIO_SUB )
		return "-";
	else if ( inst == D3DSIO_MUL )
		return "*";

	Error( "GetOperatorString: unknown operator" );
	return "zzzz";
}

// Print ASM opcode
void D3DToMetal::PrintOpcode( uint32 inst, char* buff, int nBufLen )
{
	switch ( inst )
	{
		case D3DSIO_TEXDEPTH:
			V_snprintf( buff, nBufLen, "texdepth" );
			break;
		default:
			TranslationError();
			break;
	}
}

uint32 D3DToMetal::GetRegType( uint32 dwRegToken )
{
	return ( ( dwRegToken & D3DSP_REGTYPE_MASK2 ) >> D3DSP_REGTYPE_SHIFT2 ) | ( ( dwRegToken & D3DSP_REGTYPE_MASK ) >> D3DSP_REGTYPE_SHIFT );
}

void D3DToMetal::PrintIndentation( char *pBuf, int nBufLen )
{
	for( int i=0; i<m_NumIndentTabs; i++ )
	{
		strcat_s( pBuf, nBufLen, "\t" );
	}
}

CUtlString D3DToMetal::GetParameterString( uint32 dwToken, uint32 dwSourceOrDest, bool bForceScalarSource, int *pARLDestReg )
{
	char szTemp[1024];
	PrintParameterToString( dwToken, dwSourceOrDest, szTemp, sizeof( szTemp ), bForceScalarSource, pARLDestReg );
	return szTemp;
}

// If the register happens to end with ".xyzw", then this strips off the mask.
void SimplifyFourParamRegister( char *pRegister )
{
	int nLen = V_strlen( pRegister );
	if ( nLen > 5 && V_strcmp( &pRegister[nLen-5], ".xyzw" ) == 0 )
		pRegister[nLen-5] = 0;
}

// This returns 0 for x, 1 for y, 2 for z, and 3 for w.
int GetSwizzleComponentVectorIndex( char chMask )
{
	if ( chMask == 'x' )
		return 0;
	else if ( chMask == 'y' )
		return 1;
	else if ( chMask == 'z' )
		return 2;
	else if ( chMask == 'w' )
		return 3;

	Error( "GetSwizzleComponentVectorIndex( '%c' ) - invalid parameter.\n", chMask );
	return 0;
}

// MSL, like GLSL, needs the # of src masks to match the dest write mask.
//
// So this:
//		r0.xy = r1 + r2;
// becomes:
//		r0.xy = r1.xy + r2.xy;
//
// Also, and this is the trickier one: MSL reads the source registers from their first component on
// whereas D3D reads them as referenced in the dest register mask!
//
//		So this code in D3D:
//			r0.yz = c0.x + c1.wxyz
//		Really means:
//			r0.y = c0.x + c1.x
//			r0.z = c0.x + c1.y
//		So we translate it to this in MSL:
//			r0.yz = c0.xx + c1.xy
//
CUtlString D3DToMetal::FixSwizzle( const char *pDestRegisterName, const char *pSrcRegisterName )
{
	bool bAbsWrapper = false; // Parameter wrapped in an abs()
	bool bAbsNegative = false; // -abs()
	char szSrcRegister[128];
	V_strncpy( szSrcRegister, pSrcRegisterName, sizeof(szSrcRegister) );

	// Check for abs() or -abs() wrapper and strip it off during the fixup
	if ( !V_strncmp( pSrcRegisterName, "abs(", 4 ) || !V_strncmp( pSrcRegisterName, "-abs(", 5 ) )
	{
		bAbsWrapper = true;
		bAbsNegative = pSrcRegisterName[0] == '-';

		const char *pOpenParen = strchr( pSrcRegisterName, '(' );		// FIRST opening paren
		const char *pClosingParen = strrchr( pSrcRegisterName, ')' ); // LAST closing paren

		Assert ( pOpenParen && pClosingParen );	// If we start with abs( and don't get both parens, something is very wrong

		// Copy out just the register name with no abs()
		int nRegNameLength = pClosingParen - pOpenParen - 1;
		V_strncpy( szSrcRegister, pOpenParen+1, nRegNameLength + 1 ); // Kind of a weird function...copy more than you need and slam the last char to NULL-terminate
	}

	int nSwizzlesInDest = GetNumSwizzleComponents( pDestRegisterName );
	if ( nSwizzlesInDest == 0 )
		nSwizzlesInDest = 4;

	char szFixedSrcRegister[128];
	GetParamNameWithoutSwizzle( szSrcRegister, szFixedSrcRegister, sizeof( szFixedSrcRegister ) );
	V_strncat( szFixedSrcRegister, ".", sizeof( szFixedSrcRegister ) );
	for ( int i=0; i < nSwizzlesInDest; i++ )
	{
		char chDestWriteMask = GetSwizzleComponent( pDestRegisterName, i );
		int nVectorIndex = GetSwizzleComponentVectorIndex( chDestWriteMask );

		char ch[2];
		ch[0] = GetSwizzleComponent( szSrcRegister, nVectorIndex );
		ch[1] = 0;
		V_strncat( szFixedSrcRegister, ch, sizeof( szFixedSrcRegister ) );
	}

	SimplifyFourParamRegister( szFixedSrcRegister );

	if ( bAbsWrapper )
	{
		char szTempSrcRegister[128];
		V_strncpy( szTempSrcRegister, szFixedSrcRegister, sizeof(szTempSrcRegister) );
		V_snprintf( szFixedSrcRegister, sizeof( szFixedSrcRegister ), "%sabs(%s)", bAbsNegative ? "-" : "", szTempSrcRegister ) ;
	}

	return szFixedSrcRegister;
}

// Weird encoding...bits are split apart in the dwToken
inline uint32 GetRegTypeFromToken( uint32 dwToken )
{
	return ( ( dwToken & D3DSP_REGTYPE_MASK2 ) >> D3DSP_REGTYPE_SHIFT2 ) | ( ( dwToken & D3DSP_REGTYPE_MASK ) >> D3DSP_REGTYPE_SHIFT );
}

void D3DToMetal::FlagIndirectRegister( uint32 dwToken, int *pARLDestReg )
{
	if ( !pARLDestReg )
		return;

	switch ( dwToken & D3DVS_SWIZZLE_MASK & D3DVS_X_W )
	{
		case D3DVS_X_X:
			*pARLDestReg = ARL_DEST_X;
			break;
		case D3DVS_X_Y:
			*pARLDestReg = ARL_DEST_Y;
			break;
		case D3DVS_X_Z:
			*pARLDestReg = ARL_DEST_Z;
			break;
		case D3DVS_X_W:
			*pARLDestReg = ARL_DEST_W;
			break;
	}
}

// The interpolant bit for a D3D input or output semantic.
uint32 D3DToMetal::InterpolantForUsage( uint32 dwUsage, uint32 dwUsageIndex )
{
	if ( dwUsage == D3DDECLUSAGE_TEXCOORD )
	{
		if ( dwUsageIndex >= 16 )
			TranslationError();
		return D3DTOMETAL_INTERP_TEXCOORD( dwUsageIndex );
	}
	if ( dwUsage == D3DDECLUSAGE_COLOR )
	{
		if ( dwUsageIndex >= 2 )
			TranslationError();
		return D3DTOMETAL_INTERP_COLOR( dwUsageIndex );
	}
	TranslationError(); // Not supported yet, but can be if we need it.
	return 0;
}

//------------------------------------------------------------------------------
// PrintParameterToString()
//
// Helper function which prints the MSL representation of passed Parameter dwToken
// to string. Token defines parameter details. The dwSourceOrDest parameter says
// whether or not this is a source or destination register
//------------------------------------------------------------------------------
void D3DToMetal::PrintParameterToString ( uint32 dwToken, uint32 dwSourceOrDest, char *pRegisterName, int nBufLen, bool bForceScalarSource, int *pARLDestReg )
{
	char buff[32];
	bool bAllowWriteMask = true;
	bool bAllowSwizzle = true;

	uint32 dwRegNum = dwToken & D3DSP_REGNUM_MASK;

	uint32 dwRegType, dwSwizzle;
	uint32 dwSrcModifier = D3DSPSM_NONE;

	// Clear string to zero length
	pRegisterName[0] = 0;

	dwRegType = GetRegTypeFromToken( dwToken );

	// If this is a source register
	if ( dwSourceOrDest == SRC_REGISTER )
	{
		dwSrcModifier = dwToken & D3DSP_SRCMOD_MASK;

		// If there are any source modifiers, check to see if they're at
		// least partially "prefix" and prepend appropriately
		if ( dwSrcModifier != D3DSPSM_NONE )
		{
			switch ( dwSrcModifier )
			{
				// These four start with just minus... (some may result in "postfix" notation as well later on)
				case D3DSPSM_NEG:							 // negate
					strcat_s( pRegisterName, nBufLen, "-" );
					break;
				case D3DSPSM_BIASNEG:						// bias and negate
				case D3DSPSM_SIGNNEG:						// sign and negate
				case D3DSPSM_X2NEG:						  // *2 and negate
					TranslationError();
					strcat_s( pRegisterName, nBufLen, "-" );
					break;
				case D3DSPSM_COMP:							// complement
					TranslationError();
					strcat_s( pRegisterName, nBufLen, "1-" );
					break;
				case D3DSPSM_ABS:							 // abs()
					strcat_s( pRegisterName, nBufLen, "abs(" );
					break;
				case D3DSPSM_ABSNEG:						 // -abs()
					strcat_s( pRegisterName, nBufLen, "-abs(" );
					break;
				case D3DSPSM_NOT:							 // for predicate register: "!p0"
					TranslationError();
					strcat_s( pRegisterName, nBufLen, "!" );
					break;
			}
		}
	}

	// Register name (from type and number)
	switch ( dwRegType )
	{
		case D3DSPR_TEMP:
			V_snprintf( buff, sizeof( buff ), "r%d", dwRegNum );
			strcat_s( pRegisterName, nBufLen, buff );
			m_dwTempUsageMask |= 0x00000001 << dwRegNum;			// Keep track of the use of this temp
			break;
		case D3DSPR_INPUT:
			if ( !m_bVertexShader && ( dwSourceOrDest == SRC_REGISTER ) )
			{
				if ( m_dwMajorVersion == 3 )
				{
					V_snprintf( buff, sizeof( buff ), "oTempT%d", dwRegNum );
				}
				else
				{
					// ps_2_x v0 and v1 are the interpolated colors
					if ( dwRegNum > 1 )
						TranslationError();
					V_snprintf( buff, sizeof( buff ), "in_color%d", dwRegNum );
					m_nPSInputMask |= D3DTOMETAL_INTERP_COLOR( dwRegNum );
				}
				strcat_s( pRegisterName, nBufLen, buff );
			}
			else
			{
				V_snprintf( buff, sizeof( buff ), "v%d", dwRegNum );
				strcat_s( pRegisterName, nBufLen, buff );
			}
			break;
		case D3DSPR_CONST:
			if ( m_bConstantRegisterDefined[dwRegNum] )
			{
				// Put defined constants into their own namespace "d"
				V_snprintf( buff, sizeof( buff ), "%s%d", m_bVertexShader ? "vd" : "pd", dwRegNum );
				strcat_s( pRegisterName, nBufLen, buff );
			}
			else if ( dwToken & D3DSHADER_ADDRESSMODE_MASK )  // Indirect addressing (e.g. skinning in a vertex shader)
			{
				if ( !m_bVertexShader )  // No indirect addressing in PS, this shouldn't happen
				{
					TranslationError();
				}

				// Any register may be read, so the device must upload all of them.
				m_nHighestRegister = DXABSTRACT_VS_PARAM_SLOTS - 1;

				// Index into single vc[] register array with relative addressing
				int nDstReg = -1;
				FlagIndirectRegister( GetNextToken(), &nDstReg );
				if ( pARLDestReg )
					*pARLDestReg = nDstReg;

				Assert( nDstReg != ARL_DEST_NONE );
				int nSrcSwizzle = 'x';
				if ( nDstReg == ARL_DEST_Y )
					nSrcSwizzle = 'y';
				else if ( nDstReg == ARL_DEST_Z )
					nSrcSwizzle = 'z';
				else if ( nDstReg == ARL_DEST_W )
					nSrcSwizzle = 'w';
				V_snprintf( buff, sizeof( buff ), "vc[int(va_r.%c) + %d]", nSrcSwizzle, dwRegNum );

				strcat_s( pRegisterName, nBufLen, buff );

				// Must allow swizzling, otherwise this example doesn't compile right: mad r3.xyz, c27[a0.w].w, r3, r7
			}
			else // Direct addressing of constant array
			{
				m_nHighestRegister = MAX( m_nHighestRegister, dwRegNum );
				Assert( m_nHighestRegister < DXABSTRACT_VS_PARAM_SLOTS );

				V_snprintf( buff, sizeof( buff ), "%s[%d]", m_bVertexShader ? "vc" : "pc", dwRegNum );
				strcat_s( pRegisterName, nBufLen, buff );
			}
			break;
		case D3DSPR_ADDR: // aliases to D3DSPR_TEXTURE
			if ( m_bVertexShader )
			{
				Assert( dwRegNum == 0 );

				V_snprintf( buff, sizeof( buff ), "va_r" );
			}
			else // D3DSPR_TEXTURE in the pixel shader: an interpolated texture coordinate
			{
				V_snprintf( buff, sizeof( buff ), "oT%d", dwRegNum );
				m_nPSInputMask |= D3DTOMETAL_INTERP_TEXCOORD( dwRegNum );
			}
			strcat_s( pRegisterName, nBufLen, buff );
			break;
		case D3DSPR_RASTOUT: // vertex shader oPos
			Assert( m_bVertexShader );
			Assert( m_dwMajorVersion == 2 );
			switch( dwRegNum )
			{
				case D3DSRO_POSITION:
					strcat_s( pRegisterName, nBufLen, "vTempPos" );
					m_bDeclareVSOPos = true;
				break;

				case D3DSRO_FOG:
					strcat_s( pRegisterName, nBufLen, "gl_FogFragCoord" );
					m_bDeclareVSOFog = true;
				break;

				default:
					printf( "\nD3DSPR_RASTOUT: dwRegNum is %08x and token is %08x", dwRegNum, dwToken );
					TranslationError();
				break;
			}
			break;
		case D3DSPR_ATTROUT:
			Assert( m_bVertexShader );
			Assert( m_dwMajorVersion == 2 );

			if ( dwRegNum > 1 )
			{
				TranslationError();
				break;
			}
			V_snprintf( buff, sizeof( buff ), "out_color%d", dwRegNum );
			m_dwColorOutMask |= ( 1 << dwRegNum );

			strcat_s( pRegisterName, nBufLen, buff );
			break;
		case D3DSPR_TEXCRDOUT: // aliases to D3DSPR_OUTPUT
			if ( m_bVertexShader )
			{
				if ( m_nVSPositionOutput == (int32) dwRegNum )
				{
					V_snprintf( buff, sizeof( buff ), "vTempPos" ); // This output is the position
				}
				else if ( m_dwMajorVersion == 3 )
				{
					V_snprintf( buff, sizeof( buff ), "oTempT%d", dwRegNum );
				}
				else
				{
					V_snprintf( buff, sizeof( buff ), "oT%d", dwRegNum );
				}

				m_dwTexCoordOutMask |= ( 0x00000001 << dwRegNum );
			}
			else
			{
				TranslationError();
				V_snprintf( buff, sizeof( buff ), "oC%d", dwRegNum );
			}
			strcat_s( pRegisterName, nBufLen, buff );
			break;
		case D3DSPR_CONSTINT:
			V_snprintf( buff, sizeof( buff ), "i%d", dwRegNum );	// Loops use these
			strcat_s( pRegisterName, nBufLen, buff );
			m_dwConstIntUsageMask |= 0x00000001 << dwRegNum;		// Keep track of the use of this integer constant
			break;
		case D3DSPR_COLOROUT:
			V_snprintf( buff, sizeof( buff ), "gl_FragData[%d]", dwRegNum );
			strcat_s( pRegisterName, nBufLen, buff );
			m_bOutputColorRegister[dwRegNum] = true;
			break;
		case D3DSPR_DEPTHOUT:
			V_snprintf( buff, sizeof( buff ), "gl_FragDepth" );
			strcat_s( pRegisterName, nBufLen, buff );
			m_bOutputDepthRegister = true;
			break;
		case D3DSPR_SAMPLER:
			V_snprintf( buff, sizeof( buff ), "sampler%d", dwRegNum );
			strcat_s( pRegisterName, nBufLen, buff );
			break;
		case D3DSPR_CONST2:
		case D3DSPR_CONST3:
		case D3DSPR_CONST4:
			TranslationError();
			break;
		case D3DSPR_CONSTBOOL:
			V_snprintf( buff, sizeof( buff ), m_bVertexShader ? "b%d" : "fb%d", dwRegNum );
			strcat_s( pRegisterName, nBufLen, buff );
			m_dwConstBoolUsageMask |= 0x00000001 << dwRegNum;		// Keep track of the use of this bool constant
			break;
		case D3DSPR_MISCTYPE:
			Assert( dwRegNum == 0 ); // So far, we know that MISC[0] is vPos, but we don't know about any other MISC registers
			if ( dwRegNum != 0 )
				TranslationError();
			V_snprintf( buff, sizeof( buff ), "gl_FragCoord" );
			strcat_s( pRegisterName, nBufLen, buff );
			m_bUsesFragCoord = true;
			break;
		case D3DSPR_LOOP:
		case D3DSPR_TEMPFLOAT16:
		case D3DSPR_LABEL:
		case D3DSPR_PREDICATE:
			TranslationError();
			break;
	}

	// If this is a dest register
	if ( dwSourceOrDest == DST_REGISTER )
	{
		//
		// Write masks
		//
		// If some (not all, not none) of the write masks are set, we should include them
		//
		if ( bAllowWriteMask && ( !((dwToken & D3DSP_WRITEMASK_ALL) == D3DSP_WRITEMASK_ALL) || ((dwToken & D3DSP_WRITEMASK_ALL) == 0x00000000) ) )
		{
			// Put the dot on there
			strcat_s( pRegisterName, nBufLen, "." );

			// Optionally put on the x, y, z or w
			if ( dwToken & D3DSP_WRITEMASK_0 )
			{
				strcat_s( pRegisterName, nBufLen, "x" );
			}
			if ( dwToken & D3DSP_WRITEMASK_1 )
			{
				strcat_s( pRegisterName, nBufLen, "y" );
			}
			if ( dwToken & D3DSP_WRITEMASK_2 )
			{
				strcat_s( pRegisterName, nBufLen, "z" );
			}
			if ( dwToken & D3DSP_WRITEMASK_3 )
			{
				strcat_s( pRegisterName, nBufLen, "w" );
			}
		}
	}
	else // must be a source register
	{
		if ( bAllowSwizzle ) // relative addressing hard-codes the swizzle on a0.x
		{
			uint32 dwXSwizzle, dwYSwizzle, dwZSwizzle, dwWSwizzle;

			// Mask out the swizzle modifier
			dwSwizzle = dwToken & D3DVS_SWIZZLE_MASK;

			// If there are any swizzles at all, tack on the appropriate notation
			if ( dwSwizzle != D3DVS_NOSWIZZLE )
			{
				// Separate out the two-bit codes for each component swizzle
				dwXSwizzle = dwSwizzle & D3DVS_X_W;
				dwYSwizzle = dwSwizzle & D3DVS_Y_W;
				dwZSwizzle = dwSwizzle & D3DVS_Z_W;
				dwWSwizzle = dwSwizzle & D3DVS_W_W;

				// Put on the dot
				strcat_s( pRegisterName, nBufLen, "." );

				// See where X comes from
				switch ( dwXSwizzle )
				{
					case D3DVS_X_X:
						strcat_s( pRegisterName, nBufLen, "x" );
						break;
					case D3DVS_X_Y:
						strcat_s( pRegisterName, nBufLen, "y" );
						break;
					case D3DVS_X_Z:
						strcat_s( pRegisterName, nBufLen, "z" );
						break;
					case D3DVS_X_W:
						strcat_s( pRegisterName, nBufLen, "w" );
						break;
				}

				if ( !bForceScalarSource )
				{
					// If the source of the remaining components are aren't
					// identical to the source of x, continue with swizzle
					if ( ((dwXSwizzle >> D3DVS_SWIZZLE_SHIFT) != (dwYSwizzle >> (D3DVS_SWIZZLE_SHIFT + 2))) ||	// X and Y sources match?
					   ((dwXSwizzle >> D3DVS_SWIZZLE_SHIFT) != (dwZSwizzle >> (D3DVS_SWIZZLE_SHIFT + 4))) ||	// X and Z sources match?
					   ((dwXSwizzle >> D3DVS_SWIZZLE_SHIFT) != (dwWSwizzle >> (D3DVS_SWIZZLE_SHIFT + 6))))	  // X and W sources match?
					{
						// Use either 1 or 4 components in a swizzle, so just plow on through the rest
						switch ( dwYSwizzle )
						{
							case D3DVS_Y_X:
								strcat_s( pRegisterName, nBufLen, "x" );
								break;
							case D3DVS_Y_Y:
								strcat_s( pRegisterName, nBufLen, "y" );
								break;
							case D3DVS_Y_Z:
								strcat_s( pRegisterName, nBufLen, "z" );
								break;
							case D3DVS_Y_W:
								strcat_s( pRegisterName, nBufLen, "w" );
								break;
						}

						switch ( dwZSwizzle )
						{
							case D3DVS_Z_X:
								strcat_s( pRegisterName, nBufLen, "x" );
								break;
							case D3DVS_Z_Y:
								strcat_s( pRegisterName, nBufLen, "y" );
								break;
							case D3DVS_Z_Z:
								strcat_s( pRegisterName, nBufLen, "z" );
								break;
							case D3DVS_Z_W:
								strcat_s( pRegisterName, nBufLen, "w" );
								break;
						}

						switch ( dwWSwizzle )
						{
							case D3DVS_W_X:
								strcat_s( pRegisterName, nBufLen, "x" );
								break;
							case D3DVS_W_Y:
								strcat_s( pRegisterName, nBufLen, "y" );
								break;
							case D3DVS_W_Z:
								strcat_s( pRegisterName, nBufLen, "z" );
								break;
							case D3DVS_W_W:
								strcat_s( pRegisterName, nBufLen, "w" );
								break;
						}
					}
				} // end !bForceScalarSource
			}
			else // dwSwizzle == D3DVS_NOSWIZZLE
			{
				// MOVA reads a scalar
				if ( bForceScalarSource )
				{
					strcat_s( pRegisterName, nBufLen, ".x" );
				}
			}
		} // bAllowSwizzle

		// If there are any source modifiers, check to see if they're at
		// least partially "postfix" and tack them on as appropriate
		if ( dwSrcModifier != D3DSPSM_NONE )
		{
			switch ( dwSrcModifier )
			{
				case D3DSPSM_BIAS:							// bias
				case D3DSPSM_BIASNEG:						// bias and negate
				case D3DSPSM_SIGN:							// sign
				case D3DSPSM_SIGNNEG:						// sign and negate
				case D3DSPSM_X2:							  // *2
				case D3DSPSM_X2NEG:						  // *2 and negate
				case D3DSPSM_DZ:							  // divide through by z component
				case D3DSPSM_DW:							  // divide through by w component
					TranslationError();
					break;
				case D3DSPSM_ABS:							 // abs()
				case D3DSPSM_ABSNEG:						 // -abs()
					strcat_s( pRegisterName, nBufLen, ")" );
					break;
			}
		} // end postfix modifiers (really only ps.1.x)
	}
}

uint32 D3DToMetal::MaintainAttributeMap( uint32 dwToken, uint32 dwRegToken )
{
	// Check that this reg index has not been used before - if it has, let Houston know
	uint dwRegIndex = dwRegToken & D3DSP_REGNUM_MASK;
	if ( dwRegIndex >= 16 )
	{
		TranslationError();
		return 0;
	}

	if ( m_dwAttribMap[ dwRegIndex ] == 0xFFFFFFFF )
	{
		// semantic/usage in the higher nibble
		// usage index in the low nibble

		uint usage		= dwToken & D3DSP_DCL_USAGE_MASK;
		uint usageindex	= ( dwToken & D3DSP_DCL_USAGEINDEX_MASK ) >> D3DSP_DCL_USAGEINDEX_SHIFT;

		m_dwAttribMap[ dwRegIndex ] = ( usage << 4 ) | usageindex;

		// avoid writing 0xBB since runtime code uses that for an 'unused' marker
		if ( m_dwAttribMap[ dwRegIndex ] == 0xBB )
		{
			TranslationError();
		}
	}
	else
	{
		TranslationError();
	}

	return dwRegIndex;
}

void D3DToMetal::Handle_DCL()
{
	uint32 dwToken = GetNextToken();		// What kind of dcl is this...
	uint32 dwRegToken = GetNextToken();		// Look ahead to register token

	uint32 dwUsage = ( dwToken & D3DSP_DCL_USAGE_MASK );
	uint32 dwUsageIndex = ( dwToken & D3DSP_DCL_USAGEINDEX_MASK ) >> D3DSP_DCL_USAGEINDEX_SHIFT;

	uint32 dwRegNum = dwRegToken & D3DSP_REGNUM_MASK;
	uint32 nRegType = GetRegTypeFromToken( dwRegToken );

	if ( m_bVertexShader )
	{
		// If this is an output, remember the index (what the ASM code calls o0, o1, o2..) and the semantic.
		// When GetParameterString( DST_REGISTER ) hits this one, we'll return "oTempTN".
		// At the end of the function, they're stored into the output struct by semantic.
		if ( ( m_dwMajorVersion >= 3 ) && ( nRegType == D3DSPR_OUTPUT ) )
		{
			if ( dwRegNum >= MAX_DECLARED_OUTPUTS || m_DeclaredOutputs[dwRegNum] != UNDECLARED_OUTPUT )
			{
				TranslationError();
				return;
			}

			Assert( dwToken != UNDECLARED_OUTPUT );
			m_DeclaredOutputs[dwRegNum] = dwToken;

			// Flag which o# output register maps to the position
			if ( dwUsage == D3DDECLUSAGE_POSITION )
			{
				m_nVSPositionOutput = dwRegNum;
				m_bDeclareVSOPos = true;
			}
		}
		else if ( nRegType == D3DSPR_SAMPLER )
		{
			// Vertex texturing isn't supported (ToGL didn't support it either); CS:GO doesn't use it
			TranslationError();
		}
		else
		{
			Assert( nRegType == D3DSPR_INPUT );
			if ( nRegType != D3DSPR_INPUT )
				TranslationError();

			MaintainAttributeMap( dwToken, dwRegToken );
			m_dwInputRegisterMask |= 1 << dwRegNum;
		}
	}
	else // Pixel shader
	{
		// If the register is a sampler, the dcl has a dimension decorator that we have to save for subsequent TEX instructions
		if ( nRegType == D3DSPR_SAMPLER )
		{
			int nRegNum = dwRegToken & D3DSP_REGNUM_MASK;
			switch ( TextureType( dwToken ) )
			{
				default:
				case D3DSTT_UNKNOWN:
				case D3DSTT_2D:
					m_dwSamplerTypes[nRegNum] = SAMPLER_TYPE_2D;
					break;
				case D3DSTT_CUBE:
					m_dwSamplerTypes[nRegNum] = SAMPLER_TYPE_CUBE;
					break;
				case D3DSTT_VOLUME:
					m_dwSamplerTypes[nRegNum] = SAMPLER_TYPE_3D;
					break;
			}

			// Track sampler declarations
			m_dwSamplerUsageMask |= 1 << nRegNum;
		}
		else if ( ( m_dwMajorVersion == 3 ) && ( nRegType == D3DSPR_INPUT ) )
		{
			Assert( m_DeclaredInputs[dwRegNum] == UNDECLARED_INPUT );
			m_DeclaredInputs[dwRegNum] = dwToken;
			m_nPSInputMask |= InterpolantForUsage( dwUsage, dwUsageIndex );
		}
		else if ( nRegType == D3DSPR_TEXTURE )
		{
			// ps_2_x texture coordinate register tN
			m_nPSInputMask |= D3DTOMETAL_INTERP_TEXCOORD( dwRegNum );
		}
		else if ( nRegType == D3DSPR_INPUT )
		{
			// ps_2_x color register vN
			if ( dwRegNum > 1 )
				TranslationError();
			m_nPSInputMask |= D3DTOMETAL_INTERP_COLOR( dwRegNum );
		}
		else
		{
			// No need to declare anything (D3DSPR_MISCTYPE: VPOS)
		}
	}
}

void D3DToMetal::Handle_DEFIB( uint32 instruction )
{
	Assert( ( instruction == D3DSIO_DEFI ) || ( instruction == D3DSIO_DEFB ) );

	// which register is being defined
	uint32 dwToken = GetNextToken();

	uint32 nRegNum = dwToken & D3DSP_REGNUM_MASK;

	uint32 regType = GetRegTypeFromToken( dwToken );

	if ( regType == D3DSPR_CONSTINT )
	{
		m_dwDefConstIntUsageMask |= ( 1 << nRegNum );

		uint x = GetNextToken();
		uint y = GetNextToken();
		uint z = GetNextToken();
		uint w = GetNextToken();
		NOTE_UNUSED(y); NOTE_UNUSED(z);	NOTE_UNUSED(w);

		Assert( nRegNum < 32 );
		if ( nRegNum < 32 )
		{
			m_dwDefConstIntIterCount[nRegNum] = x;
		}
	}
	else
	{
		TranslationError();
	}
}

void D3DToMetal::Handle_DEF()
{
	// Which register is being defined
	uint32 dwToken = GetNextToken();

	// Note that this constant was explicitly defined
	m_bConstantRegisterDefined[dwToken & D3DSP_REGNUM_MASK] = true;
	CUtlString sParamName = GetParameterString( dwToken, DST_REGISTER, false, NULL );

	PrintIndentation( (char*)m_pBufParamCode->Base(), m_pBufParamCode->Size() );
	PrintToBuf( *m_pBufParamCode, "vec4 %s = vec4( ", sParamName.String() );

	// Run through the 4 floats
	for ( int i=0; i < 4; i++ )
	{
		float fConst = uint32ToFloat( GetNextToken() );

		char szTemp[1024];

		FloatToString( szTemp, sizeof( szTemp ), fConst );

		PrintToBuf( *m_pBufParamCode, i != 3 ? "%s, " : "%s", szTemp ); // end with comma-space
	}

	PrintToBuf( *m_pBufParamCode, " );\n" );
}

void D3DToMetal::Handle_MAD( uint32 nInstruction )
{
	uint32 nDestToken = GetNextToken();
	CUtlString sParam1 = GetParameterString( nDestToken, DST_REGISTER, false, NULL );
	int nARLComp0 = ARL_DEST_NONE;
	CUtlString sParam2 = GetParameterString( GetNextToken(), SRC_REGISTER, false, &nARLComp0 );
	int nARLComp1 = ARL_DEST_NONE;
	CUtlString sParam3 = GetParameterString( GetNextToken(), SRC_REGISTER, false, &nARLComp1 );
	int nARLComp2 = ARL_DEST_NONE;
	CUtlString sParam4 = GetParameterString( GetNextToken(), SRC_REGISTER, false, &nARLComp2 );

	sParam2 = FixSwizzle( sParam1, sParam2 );
	sParam3 = FixSwizzle( sParam1, sParam3 );
	sParam4 = FixSwizzle( sParam1, sParam4 );
	PrintToBufWithIndents( *m_pBufALUCode, "%s = %s * %s + %s;\n", sParam1.String(), sParam2.String(), sParam3.String(), sParam4.String() );

	// If the _SAT instruction modifier is used, then do a saturate here.
	if ( nDestToken & D3DSPDM_SATURATE )
	{
		int nComponents = GetNumSwizzleComponents( sParam1.String() );
		if ( nComponents == 0 )
			nComponents = 4;

		PrintToBufWithIndents( *m_pBufALUCode, "%s = clamp( %s, %s, %s );\n", sParam1.String(), sParam1.String(), g_szVecZeros[nComponents], g_szVecOnes[nComponents] );
	}
}

void D3DToMetal::Handle_DP2ADD()
{
	char pDestReg[64], pSrc0Reg[64], pSrc1Reg[64], pSrc2Reg[64];
	uint32 nDestToken = GetNextToken();
	PrintParameterToString( nDestToken, DST_REGISTER, pDestReg, sizeof( pDestReg ), false, NULL );
	PrintParameterToString( GetNextToken(), SRC_REGISTER, pSrc0Reg, sizeof( pSrc0Reg ), false, NULL );
	PrintParameterToString( GetNextToken(), SRC_REGISTER, pSrc1Reg, sizeof( pSrc1Reg ), false, NULL );
	PrintParameterToString( GetNextToken(), SRC_REGISTER, pSrc2Reg, sizeof( pSrc2Reg ), false, NULL );

	// We should only be assigning to a single component of the dest.
	Assert( GetNumSwizzleComponents( pDestReg ) == 1 );
	Assert( GetNumSwizzleComponents( pSrc2Reg ) == 1 );

	// This is a 2D dot product, so we only want two entries from the middle components.
	CUtlString sArg0 = EnsureNumSwizzleComponents( pSrc0Reg, 2 );
	CUtlString sArg1 = EnsureNumSwizzleComponents( pSrc1Reg, 2 );

	PrintToBufWithIndents( *m_pBufALUCode, "%s = dot( %s, %s ) + %s;\n", pDestReg, sArg0.String(), sArg1.String(), pSrc2Reg );

	// If the _SAT instruction modifier is used, then do a saturate here.
	if ( nDestToken & D3DSPDM_SATURATE )
	{
		int nComponents = GetNumSwizzleComponents( pDestReg );
		if ( nComponents == 0 )
			nComponents = 4;

		PrintToBufWithIndents( *m_pBufALUCode, "%s = clamp( %s, %s, %s );\n", pDestReg, pDestReg, g_szVecZeros[nComponents], g_szVecOnes[nComponents] );
	}
}

void D3DToMetal::Handle_SINCOS()
{
	char pDestReg[64], pSrc0Reg[64];
	PrintParameterToString( GetNextToken(), DST_REGISTER, pDestReg, sizeof( pDestReg ), false, NULL );
	PrintParameterToString( GetNextToken(), SRC_REGISTER, pSrc0Reg, sizeof( pSrc0Reg ), true, NULL );
	m_bNeedsSinCosDeclarations = true;

	CUtlString sDest( pDestReg );
	CUtlString sArg0 = EnsureNumSwizzleComponents( pSrc0Reg, 1 );// Ensure input is scalar
	CUtlString sResult( "vSinCosTmp.xy" );			// Always going to populate this
	sResult = FixSwizzle( sDest, sResult );			// Make sure we match the desired output reg

	PrintToBufWithIndents( *m_pBufALUCode, "vSinCosTmp.z = %s * %s;\n", sArg0.String(), sArg0.String() );

	PrintToBufWithIndents( *m_pBufALUCode, "vSinCosTmp.xy = vSinCosTmp.zz * scA.xy + scA.wz;\n" );
	PrintToBufWithIndents( *m_pBufALUCode, "vSinCosTmp.xy = vSinCosTmp.xy * vSinCosTmp.zz + scB.xy;\n" );
	PrintToBufWithIndents( *m_pBufALUCode, "vSinCosTmp.xy = vSinCosTmp.xy * vSinCosTmp.zz + scB.wz;\n" );

	PrintToBufWithIndents( *m_pBufALUCode, "vSinCosTmp.x = vSinCosTmp.x * %s;\n", sArg0.String() );

	PrintToBufWithIndents( *m_pBufALUCode, "vSinCosTmp.xy = vSinCosTmp.xy * vSinCosTmp.xx;\n" );
	PrintToBufWithIndents( *m_pBufALUCode, "vSinCosTmp.xy = vSinCosTmp.xy + vSinCosTmp.xy;\n" );
	PrintToBufWithIndents( *m_pBufALUCode, "vSinCosTmp.x = -vSinCosTmp.x + scB.z;\n" );

	PrintToBufWithIndents( *m_pBufALUCode, "%s = %s;\n", sDest.String(), sResult.String() );

	if ( m_dwMajorVersion < 3 )
	{
		// Eat two more tokens since D3D defines Taylor series constants that we won't need
		// Only valid for pixel and vertex shader version earlier than 3_0
		// (http://msdn.microsoft.com/en-us/library/windows/hardware/ff569710(v=vs.85).aspx)
		SkipTokens( 2 );
	}
}

void D3DToMetal::Handle_LRP( uint32 nInstruction )
{
	uint32 nDestToken = GetNextToken();
	CUtlString sDest = GetParameterString( nDestToken, DST_REGISTER, false, NULL );
	int nARLComp0 = ARL_DEST_NONE;
	CUtlString sParam0 = GetParameterString( GetNextToken(), SRC_REGISTER, false, &nARLComp0 );
	int nARLComp1 = ARL_DEST_NONE;
	CUtlString sParam1 = GetParameterString( GetNextToken(), SRC_REGISTER, false, &nARLComp1 );
	int nARLComp2 = ARL_DEST_NONE;
	CUtlString sParam2 = GetParameterString( GetNextToken(), SRC_REGISTER, false, &nARLComp2 );

	sParam0 = FixSwizzle( sDest, sParam0 );
	sParam1 = FixSwizzle( sDest, sParam1 );
	sParam2 = FixSwizzle( sDest, sParam2 );

	// dest = src0 * (src1 - src2) + src2;
	PrintToBufWithIndents( *m_pBufALUCode, "%s = %s * ( %s - %s ) + %s;\n", sDest.String(), sParam0.String(), sParam1.String(), sParam2.String(), sParam2.String() );

	// If the _SAT instruction modifier is used, then do a saturate here.
	if ( nDestToken & D3DSPDM_SATURATE )
	{
		int nComponents = GetNumSwizzleComponents( sDest.String() );
		if ( nComponents == 0 )
			nComponents = 4;

		PrintToBufWithIndents( *m_pBufALUCode, "%s = clamp( %s, %s, %s );\n", sDest.String(), sDest.String(), g_szVecZeros[nComponents], g_szVecOnes[nComponents] );
	}
}

void D3DToMetal::Handle_TEX( uint32 dwToken, bool bIsTexLDL )
{
	char pDestReg[64], pSrc0Reg[64], pSrc1Reg[64];
	PrintParameterToString( GetNextToken(), DST_REGISTER, pDestReg, sizeof( pDestReg ), false, NULL );
	PrintParameterToString( GetNextToken(), SRC_REGISTER, pSrc0Reg, sizeof( pSrc0Reg ), false, NULL );

	uint32 dwSrc1Token = GetNextToken();
	PrintParameterToString( dwSrc1Token, SRC_REGISTER, pSrc1Reg, sizeof( pSrc1Reg ), false, NULL );

	// The sampler is a bare name; macros paste its texture and sampler arguments together.
	char szSampler[64];
	GetParamNameWithoutSwizzle( pSrc1Reg, szSampler, sizeof( szSampler ) );

	uint32 nSamplerIndex = dwSrc1Token & D3DSP_REGNUM_MASK;
	Assert( nSamplerIndex < ARRAYSIZE( m_dwSamplerTypes ) );
	if ( nSamplerIndex >= 16 )
	{
		TranslationError();
		return;
	}

	// The fetched value is a vec4; FixSwizzle selects the components the destination writes.
	char szResult[256];
	uint32 nTexLdType = ( bIsTexLDL ? 0 : ( OpcodeSpecificData( dwToken ) << D3DSP_OPCODESPECIFICCONTROL_SHIFT ) );

	uint32 nSamplerType = m_dwSamplerTypes[nSamplerIndex];
	if ( nSamplerType == SAMPLER_TYPE_2D )
	{
		const bool bIsShadowSampler = ( ( 1 << ( (int) nSamplerIndex ) ) & m_nShadowDepthSamplerMask ) != 0;

		if ( bIsTexLDL )
		{
			CUtlString sCoordVar = EnsureNumSwizzleComponents( pSrc0Reg, bIsShadowSampler ? 3 : 2 );

			// Strip out the W component of the pSrc0Reg and pass that as the LOD.
			char szLOD[128], szExtra[8];
			GetParamNameWithoutSwizzle( pSrc0Reg, szLOD, sizeof( szLOD ) );
			V_snprintf( szExtra, sizeof( szExtra ), ".%c", GetSwizzleComponent( pSrc0Reg, 3 ) );
			V_strncat( szLOD, szExtra, sizeof( szLOD ) );

			V_snprintf( szResult, sizeof( szResult ), "%s( %s, %s, %s )", bIsShadowSampler ? "shadow2DLod" : "texture2DLod", szSampler, sCoordVar.String(), szLOD );
		}
		else if ( bIsShadowSampler )
		{
			// .z is meant to contain the object depth, while .xy contains the 2D tex coords
			CUtlString sCoordVar3D = EnsureNumSwizzleComponents( pSrc0Reg, 3 );
			V_snprintf( szResult, sizeof( szResult ), "shadow2D( %s, %s )", szSampler, sCoordVar3D.String() );
		}
		else if ( nTexLdType == D3DSI_TEXLD_PROJECT )
		{
			// This projective case is after the shadow case intentionally, due to the way that "projective"
			// loads are overloaded in our D3D shaders for shadow lookups.
			CUtlString s4DProjCoords = EnsureNumSwizzleComponents( pSrc0Reg, 4 );
			V_snprintf( szResult, sizeof( szResult ), "texture2DProj( %s, %s )", szSampler, s4DProjCoords.String() );
		}
		else if ( nTexLdType == D3DSI_TEXLD_BIAS )
		{
			CUtlString sCoordVar = EnsureNumSwizzleComponents( pSrc0Reg, 2 );
			char szBias[128], szExtra[8];
			GetParamNameWithoutSwizzle( pSrc0Reg, szBias, sizeof( szBias ) );
			V_snprintf( szExtra, sizeof( szExtra ), ".%c", GetSwizzleComponent( pSrc0Reg, 3 ) );
			V_strncat( szBias, szExtra, sizeof( szBias ) );
			V_snprintf( szResult, sizeof( szResult ), "texture2DBias( %s, %s, %s )", szSampler, sCoordVar.String(), szBias );
		}
		else
		{
			CUtlString sCoordVar = EnsureNumSwizzleComponents( pSrc0Reg, 2 );
			V_snprintf( szResult, sizeof( szResult ), "texture2D( %s, %s )", szSampler, sCoordVar.String() );
		}
	}
	else if ( nSamplerType == SAMPLER_TYPE_3D )
	{
		CUtlString sCoordVar = EnsureNumSwizzleComponents( pSrc0Reg, 3 );
		if ( bIsTexLDL )
		{
			char szLOD[128], szExtra[8];
			GetParamNameWithoutSwizzle( pSrc0Reg, szLOD, sizeof( szLOD ) );
			V_snprintf( szExtra, sizeof( szExtra ), ".%c", GetSwizzleComponent( pSrc0Reg, 3 ) );
			V_strncat( szLOD, szExtra, sizeof( szLOD ) );
			V_snprintf( szResult, sizeof( szResult ), "texture3DLod( %s, %s, %s )", szSampler, sCoordVar.String(), szLOD );
		}
		else
		{
			V_snprintf( szResult, sizeof( szResult ), "texture3D( %s, %s )", szSampler, sCoordVar.String() );
		}
	}
	else if ( nSamplerType == SAMPLER_TYPE_CUBE )
	{
		CUtlString sCoordVar = EnsureNumSwizzleComponents( pSrc0Reg, 3 );
		if ( bIsTexLDL )
		{
			char szLOD[128], szExtra[8];
			GetParamNameWithoutSwizzle( pSrc0Reg, szLOD, sizeof( szLOD ) );
			V_snprintf( szExtra, sizeof( szExtra ), ".%c", GetSwizzleComponent( pSrc0Reg, 3 ) );
			V_strncat( szLOD, szExtra, sizeof( szLOD ) );
			V_snprintf( szResult, sizeof( szResult ), "textureCubeLod( %s, %s, %s )", szSampler, sCoordVar.String(), szLOD );
		}
		else if ( nTexLdType == D3DSI_TEXLD_BIAS )
		{
			char szBias[128], szExtra[8];
			GetParamNameWithoutSwizzle( pSrc0Reg, szBias, sizeof( szBias ) );
			V_snprintf( szExtra, sizeof( szExtra ), ".%c", GetSwizzleComponent( pSrc0Reg, 3 ) );
			V_strncat( szBias, szExtra, sizeof( szBias ) );
			V_snprintf( szResult, sizeof( szResult ), "textureCubeBias( %s, %s, %s )", szSampler, sCoordVar.String(), szBias );
		}
		else
		{
			V_snprintf( szResult, sizeof( szResult ), "textureCube( %s, %s )", szSampler, sCoordVar.String() );
		}
	}
	else
	{
		TranslationError();
		return;
	}

	// Sample into a vec4 temp, then assign the components the destination's write mask selects
	// (D3D applies the destination mask to the fetched value).
	PrintToBufWithIndents( *m_pBufALUCode, "%s = %s;\n", g_pAtomicTempVarName, szResult );
	m_bUsedAtomicTempVar = true;
	CUtlString sFetched = FixSwizzle( pDestReg, g_pAtomicTempVarName );
	PrintToBufWithIndents( *m_pBufALUCode, "%s = %s;\n", pDestReg, sFetched.String() );
}

void D3DToMetal::StrcatToHeaderCode( const char *pBuf )
{
	strcat_s( (char*)m_pBufHeaderCode->Base(), m_pBufHeaderCode->Size(), pBuf );
}

void D3DToMetal::StrcatToALUCode( const char *pBuf )
{
	PrintIndentation( (char*)m_pBufALUCode->Base(), m_pBufALUCode->Size() );

	strcat_s( (char*)m_pBufALUCode->Base(), m_pBufALUCode->Size(), pBuf );
}

void D3DToMetal::StrcatToParamCode( const char *pBuf )
{
	strcat_s( (char*)m_pBufParamCode->Base(), m_pBufParamCode->Size(), pBuf );
}

void D3DToMetal::StrcatToAttribCode( const char *pBuf )
{
	strcat_s( (char*)m_pBufAttribCode->Base(), m_pBufAttribCode->Size(), pBuf );
}

// texldd dst, coord, sampler, ddx, ddy: a fetch with explicit gradients.
void D3DToMetal::Handle_TexLDD( uint32 nInstruction )
{
	char pDestReg[64], pSrc0Reg[64], pSrc1Reg[64], pSrc2Reg[64], pSrc3Reg[64];
	PrintParameterToString( GetNextToken(), DST_REGISTER, pDestReg, sizeof( pDestReg ), false, NULL );
	PrintParameterToString( GetNextToken(), SRC_REGISTER, pSrc0Reg, sizeof( pSrc0Reg ), false, NULL );
	uint32 dwSrc1Token = GetNextToken();
	PrintParameterToString( dwSrc1Token, SRC_REGISTER, pSrc1Reg, sizeof( pSrc1Reg ), false, NULL );
	PrintParameterToString( GetNextToken(), SRC_REGISTER, pSrc2Reg, sizeof( pSrc2Reg ), false, NULL );
	PrintParameterToString( GetNextToken(), SRC_REGISTER, pSrc3Reg, sizeof( pSrc3Reg ), false, NULL );

	uint32 nSamplerIndex = dwSrc1Token & D3DSP_REGNUM_MASK;
	if ( nSamplerIndex >= 16 || m_dwSamplerTypes[nSamplerIndex] != SAMPLER_TYPE_2D || ( ( 1 << nSamplerIndex ) & m_nShadowDepthSamplerMask ) )
	{
		TranslationError(); // Only 2D gradient fetches are used
		return;
	}

	char szSampler[64];
	GetParamNameWithoutSwizzle( pSrc1Reg, szSampler, sizeof( szSampler ) );
	CUtlString sCoord = EnsureNumSwizzleComponents( pSrc0Reg, 2 );
	CUtlString sDdx = EnsureNumSwizzleComponents( pSrc2Reg, 2 );
	CUtlString sDdy = EnsureNumSwizzleComponents( pSrc3Reg, 2 );

	PrintToBufWithIndents( *m_pBufALUCode, "%s = texture2DGrad( %s, %s, %s, %s );\n", g_pAtomicTempVarName, szSampler, sCoord.String(), sDdx.String(), sDdy.String() );
	m_bUsedAtomicTempVar = true;
	CUtlString sFetched = FixSwizzle( pDestReg, g_pAtomicTempVarName );
	PrintToBufWithIndents( *m_pBufALUCode, "%s = %s;\n", pDestReg, sFetched.String() );
}

void D3DToMetal::Handle_TexCoord()
{
	// texcoord / texcrd are ps_1_x instructions
	TranslationError();
	GetNextToken();
	if ( ( m_dwMajorVersion == 1 ) && ( m_dwMinorVersion == 4 ) && ( !m_bVertexShader ) )
	{
		GetNextToken();
	}
}

void D3DToMetal::Handle_BREAKC( uint32 dwToken )
{
	uint nComparison = ( dwToken & D3DSHADER_COMPARISON_MASK ) >> D3DSHADER_COMPARISON_SHIFT;

	const char *pComparison = "?";
	switch ( nComparison )
	{
	case D3DSPC_GT: pComparison = ">"; break;
	case D3DSPC_EQ: pComparison = "=="; break;
	case D3DSPC_GE: pComparison = ">="; break;
	case D3DSPC_LT: pComparison = "<"; break;
	case D3DSPC_NE: pComparison = "!="; break;
	case D3DSPC_LE: pComparison = "<="; break;
	default:
		TranslationError();
	}

	char src0[256];
	uint32 src0Token = GetNextToken();
	PrintParameterToString( src0Token, SRC_REGISTER, src0, sizeof( src0 ), false, NULL );

	char src1[256];
	uint32 src1Token = GetNextToken();
	PrintParameterToString( src1Token, SRC_REGISTER, src1, sizeof( src1 ), false, NULL );

	PrintToBufWithIndents( *m_pBufALUCode, "if (%s %s %s) break;\n", src0, pComparison, src1 );
}

void D3DToMetal::HandleBinaryOp( uint32 nInstruction )
{
	uint32 nDestToken = GetNextToken();
	CUtlString sParam1 = GetParameterString( nDestToken, DST_REGISTER, false, NULL );
	int nARLComp0 = ARL_DEST_NONE;
	CUtlString sParam2 = GetParameterString( GetNextToken(), SRC_REGISTER, false, &nARLComp0 );
	int nARLComp1 = ARL_DEST_NONE;
	CUtlString sParam3 = GetParameterString( GetNextToken(), SRC_REGISTER, false, &nARLComp1 );

	// Since DP3 and DP4 have a scalar as the dest and vectors as the src, don't screw with the swizzle specifications.
	if ( nInstruction == D3DSIO_DP3 )
	{
		sParam2 = EnsureNumSwizzleComponents( sParam2, 3 );
		sParam3 = EnsureNumSwizzleComponents( sParam3, 3 );
	}
	else if ( nInstruction == D3DSIO_DP4 )
	{
		sParam2 = EnsureNumSwizzleComponents( sParam2, 4 );
		sParam3 = EnsureNumSwizzleComponents( sParam3, 4 );
	}
	else if ( nInstruction == D3DSIO_DST )
	{
		m_bUsesDSTInstruction = true;
		sParam2 = EnsureNumSwizzleComponents( sParam2, 4 );
		sParam3 = EnsureNumSwizzleComponents( sParam3, 4 );
	}
	else if ( nInstruction == D3DSIO_CRS )
	{
		// cross() takes vec3s and the destination is .xyz
		sParam2 = EnsureNumSwizzleComponents( FixSwizzle( sParam1, sParam2 ), 3 );
		sParam3 = EnsureNumSwizzleComponents( FixSwizzle( sParam1, sParam3 ), 3 );
	}
	else
	{
		sParam2 = FixSwizzle( sParam1, sParam2 );
		sParam3 = FixSwizzle( sParam1, sParam3 );
	}

	char buff[256];
	if ( nInstruction == D3DSIO_ADD || nInstruction == D3DSIO_SUB || nInstruction == D3DSIO_MUL )
	{
		// These all look like x = y op z
		PrintToBufWithIndents( *m_pBufALUCode, "%s = %s %s %s;\n", sParam1.String(), sParam2.String(), GetOperatorString( nInstruction ), sParam3.String() );
	}
	else
	{
		int nDestComponents = GetNumSwizzleComponents( sParam1.String() );
		int nSrcComponents = GetNumSwizzleComponents( sParam2.String() );

		// All remaining instructions can use intrinsics like dot() and cross().
		bool bDoubleClose = OpenIntrinsic( nInstruction, buff, sizeof( buff ), nDestComponents, nSrcComponents );

		if ( ( nSrcComponents == 1 ) && ( nInstruction == D3DSIO_SGE ) )
		{
			PrintToBufWithIndents( *m_pBufALUCode, "%s = %s%s >= %s );\n", sParam1.String(), buff, sParam2.String(), sParam3.String() );
		}
		else if ( ( nSrcComponents == 1 ) && ( nInstruction == D3DSIO_SLT ) )
		{
			PrintToBufWithIndents( *m_pBufALUCode, "%s = %s%s < %s );\n", sParam1.String(), buff, sParam2.String(), sParam3.String() );
		}
		else
		{
			PrintToBufWithIndents( *m_pBufALUCode, "%s = %s%s, %s %s;\n", sParam1.String(), buff, sParam2.String(), sParam3.String(), bDoubleClose ? ") )" : ")" );
		}
	}

	// If the _SAT instruction modifier is used, then do a saturate here.
	if ( nDestToken & D3DSPDM_SATURATE )
	{
		int nComponents = GetNumSwizzleComponents( sParam1.String() );
		if ( nComponents == 0 )
			nComponents = 4;

		PrintToBufWithIndents( *m_pBufALUCode, "%s = clamp( %s, %s, %s );\n", sParam1.String(), sParam1.String(), g_szVecZeros[nComponents], g_szVecOnes[nComponents] );
	}
}

void D3DToMetal::WriteCmp( const char *pDestReg, const char *pSrc0Reg, const char *pSrc1Reg, const char *pSrc2Reg )
{
	int nWriteMaskEntries = GetNumWriteMaskEntries( pDestReg );
	for ( int i=0; i < nWriteMaskEntries; i++ )
	{
		char params[4][256];
		WriteParamWithSingleMaskEntry( pDestReg, i, params[0], sizeof( params[0] ) );
		WriteParamWithSingleMaskEntry( pSrc0Reg, i, params[1], sizeof( params[1] ) );
		WriteParamWithSingleMaskEntry( pSrc1Reg, i, params[2], sizeof( params[2] ) );
		WriteParamWithSingleMaskEntry( pSrc2Reg, i, params[3], sizeof( params[3] ) );

		PrintToBufWithIndents( *m_pBufALUCode, "%s = ( %s >= 0.0 ) ? %s : %s;\n", params[0], params[1], params[2], params[3] );
	}
}

void D3DToMetal::Handle_CMP()
{
	// In Direct3D, result = (src0 >= 0.0) ? src1 : src2
	char pDestReg[64], pSrc0Reg[64], pSrc1Reg[64], pSrc2Reg[64];
	uint32 nDestToken = GetNextToken();
	PrintParameterToString( nDestToken, DST_REGISTER, pDestReg, sizeof( pDestReg ), false, NULL );
	PrintParameterToString( GetNextToken(), SRC_REGISTER, pSrc0Reg, sizeof( pSrc0Reg ), false, NULL );
	PrintParameterToString( GetNextToken(), SRC_REGISTER, pSrc1Reg, sizeof( pSrc1Reg ), false, NULL );
	PrintParameterToString( GetNextToken(), SRC_REGISTER, pSrc2Reg, sizeof( pSrc2Reg ), false, NULL );

	// These are a tricky case.. we have to expand it out into multiple statements.
	char szDestBase[256];
	GetParamNameWithoutSwizzle( pDestReg, szDestBase, sizeof( szDestBase ) );

	V_strncpy( pSrc0Reg, FixSwizzle( pDestReg, pSrc0Reg ), sizeof( pSrc0Reg ) );
	V_strncpy( pSrc1Reg, FixSwizzle( pDestReg, pSrc1Reg ), sizeof( pSrc1Reg ) );
	V_strncpy( pSrc2Reg, FixSwizzle( pDestReg, pSrc2Reg ), sizeof( pSrc2Reg ) );

	// The dest register may also be a source:
	//		CMP r0.xy, r0.xx, r1, r2
	// would generate this:
	//		r0.x = (r0.x >= 0) ? r1.x : r2.x;
	//		r0.y = (r0.x >= 0) ? r1.x : r2.x;
	// But the first line changes r0.x and thus screws the atomicity of the CMP instruction for the second line.
	// So we assign r0 to a temporary first and then write to the temporary.
	PrintToBufWithIndents( *m_pBufALUCode, "%s = %s;\n", g_pAtomicTempVarName, szDestBase );

	char szTempVar[256];
	ReplaceParamName( pDestReg, g_pAtomicTempVarName, szTempVar, sizeof( szTempVar ) );
	WriteCmp( szTempVar, pSrc0Reg, pSrc1Reg, pSrc2Reg );

	PrintToBufWithIndents( *m_pBufALUCode, "%s = %s;\n", szDestBase, g_pAtomicTempVarName );
	m_bUsedAtomicTempVar = true;

	// If the _SAT instruction modifier is used, then do a saturate here.
	if ( nDestToken & D3DSPDM_SATURATE )
	{
		int nComponents = GetNumSwizzleComponents( pDestReg );
		if ( nComponents == 0 )
			nComponents = 4;

		PrintToBufWithIndents( *m_pBufALUCode, "%s = clamp( %s, %s, %s );\n", pDestReg, pDestReg, g_szVecZeros[nComponents], g_szVecOnes[nComponents] );
	}
}

void D3DToMetal::Handle_NRM()
{
	char pDestReg[64];
	char pSrc0Reg[64];
	PrintParameterToString( GetNextToken(), DST_REGISTER, pDestReg, sizeof( pDestReg ), false, NULL );
	int nARLSrcComp = ARL_DEST_NONE;
	PrintParameterToString( GetNextToken(), SRC_REGISTER, pSrc0Reg, sizeof( pSrc0Reg ), false, &nARLSrcComp );

	CUtlString sSrc = EnsureNumSwizzleComponents( pSrc0Reg, 3 );

	// D3D normalizes the .xyz of the source; write the components the destination asks for.
	int nDestComponents = GetNumSwizzleComponents( pDestReg );
	if ( nDestComponents == 0 || nDestComponents == 3 )
	{
		PrintToBufWithIndents( *m_pBufALUCode, "%s = normalize( %s );\n", nDestComponents == 0 ? CUtlString( CUtlString( pDestReg ) + ".xyz" ).String() : pDestReg, sSrc.String() );
	}
	else
	{
		PrintToBufWithIndents( *m_pBufALUCode, "%s.xyz = normalize( %s );\n", g_pAtomicTempVarName, sSrc.String() );
		m_bUsedAtomicTempVar = true;
		CUtlString sNormalized = FixSwizzle( pDestReg, g_pAtomicTempVarName );
		PrintToBufWithIndents( *m_pBufALUCode, "%s = %s;\n", pDestReg, sNormalized.String() );
	}
}

void D3DToMetal::Handle_UnaryOp( uint32 nInstruction )
{
	uint32 nDestToken = GetNextToken();
	CUtlString sParam1 = GetParameterString( nDestToken, DST_REGISTER, false, NULL );
	CUtlString sParam2 = GetParameterString( GetNextToken(), SRC_REGISTER, false, NULL );
	sParam2 = FixSwizzle( sParam1, sParam2 );

	if ( nInstruction == D3DSIO_MOV )
	{
		PrintToBufWithIndents( *m_pBufALUCode, "%s = %s;\n", sParam1.String(), sParam2.String() );
	}
	else if ( nInstruction == D3DSIO_RSQ )
	{
		PrintToBufWithIndents( *m_pBufALUCode, "%s = rsqrt( abs( %s ) );\n", sParam1.String(), sParam2.String() );
	}
	else if ( nInstruction == D3DSIO_RCP )
	{
		PrintToBufWithIndents( *m_pBufALUCode, "%s = 1.0 / %s;\n", sParam1.String(), sParam2.String() );
	}
	else if ( nInstruction == D3DSIO_EXP )
	{
		PrintToBufWithIndents( *m_pBufALUCode, "%s = exp2( %s );\n", sParam1.String(), sParam2.String() );
	}
	else if ( nInstruction == D3DSIO_FRC )
	{
		PrintToBufWithIndents( *m_pBufALUCode, "%s = fract( %s );\n", sParam1.String(), sParam2.String() );
	}
	else if ( nInstruction == D3DSIO_LOG )	// d3d 'log' is log base 2 of the absolute value
	{
		PrintToBufWithIndents( *m_pBufALUCode, "%s = log2( abs( %s ) );\n", sParam1.String(), sParam2.String() );
	}
	else if ( nInstruction == D3DSIO_ABS )
	{
		PrintToBufWithIndents( *m_pBufALUCode, "%s = abs( %s );\n", sParam1.String(), sParam2.String() );
	}
	else if ( nInstruction == D3DSIO_MOVA )
	{
		// mova rounds to the nearest integer
		m_bDeclareAddressReg = true;
		PrintToBufWithIndents( *m_pBufALUCode, "%s = floor( %s + 0.5 );\n", sParam1.String(), sParam2.String() );
	}
	else if ( nInstruction == D3DSIO_DSX )
	{
		PrintToBufWithIndents( *m_pBufALUCode, "%s = dfdx( %s );\n", sParam1.String(), sParam2.String() );
	}
	else if ( nInstruction == D3DSIO_DSY )
	{
		PrintToBufWithIndents( *m_pBufALUCode, "%s = dfdy( %s );\n", sParam1.String(), sParam2.String() );
	}
	else
	{
		TranslationError();
	}

	// If the _SAT instruction modifier is used, then do a saturate here.
	if ( nDestToken & D3DSPDM_SATURATE )
	{
		int nComponents = GetNumSwizzleComponents( sParam1.String() );
		if ( nComponents == 0 )
		{
			nComponents = 4;
		}

		PrintToBufWithIndents( *m_pBufALUCode, "%s = clamp( %s, %s, %s );\n", sParam1.String(), sParam1.String(), g_szVecZeros[nComponents], g_szVecOnes[nComponents] );
	}
}

// Texture and sampler arguments of the pixel function, one pair per sampler used.
void D3DToMetal::WriteSamplerParameters( CUtlBuffer &buf )
{
	for ( int i=0; i < 16; i++ )
	{
		if ( !( m_dwSamplerUsageMask & ( 1 << i ) ) )
			continue;

		const char *pTextureType = NULL;
		if ( m_dwSamplerTypes[i] == SAMPLER_TYPE_2D )
		{
			pTextureType = ( ( ( 1 << i ) & m_nShadowDepthSamplerMask ) != 0 ) ? "depth2d<float>" : "texture2d<float>";
		}
		else if ( m_dwSamplerTypes[i] == SAMPLER_TYPE_3D )
		{
			pTextureType = "texture3d<float>";
		}
		else if ( m_dwSamplerTypes[i] == SAMPLER_TYPE_CUBE )
		{
			pTextureType = "texturecube<float>";
		}
		else
		{
			TranslationError();
			continue;
		}

		buf.Printf( ",\n\t%s sampler%d_t [[ texture( %d ) ]], sampler sampler%d_s [[ sampler( %d ) ]]", pTextureType, i, i, i, i );
	}
}

static void WriteInterpolantMembers( CUtlBuffer &buf, uint32 nMask, uint32 nCentroidMask, bool bPixelInput )
{
	for ( int i = 0; i < 16; i++ )
	{
		if ( nMask & D3DTOMETAL_INTERP_TEXCOORD( i ) )
		{
			const bool bCentroid = bPixelInput && ( nCentroidMask & ( 1 << i ) );
			buf.Printf( "\tfloat4 oT%d [[ user( oT%d )%s ]];\n", i, i, bCentroid ? ", centroid_perspective" : "" );
		}
	}
	for ( int i = 0; i < 2; i++ )
	{
		if ( nMask & D3DTOMETAL_INTERP_COLOR( i ) )
		{
			buf.Printf( "\tfloat4 color%d [[ user( color%d ) ]];\n", i, i );
		}
	}
}

// Stores the vertex shader's output registers into the output struct.
void D3DToMetal::WriteVertexOutputAssignments( CUtlBuffer &buf, uint32 nOutputMask )
{
	if ( m_dwMajorVersion == 3 )
	{
		for ( int i=0; i < ARRAYSIZE( m_DeclaredOutputs ); i++ )
		{
			if ( m_DeclaredOutputs[i] == UNDECLARED_OUTPUT )
				continue;

			if ( ( m_dwTexCoordOutMask & ( 1 << i ) ) == 0 )
				continue;

			if ( (int32)i == m_nVSPositionOutput )
				continue;

			uint32 dwToken = m_DeclaredOutputs[i];

			uint32 dwUsage = ( dwToken & D3DSP_DCL_USAGE_MASK );
			uint32 dwUsageIndex = ( dwToken & D3DSP_DCL_USAGEINDEX_MASK ) >> D3DSP_DCL_USAGEINDEX_SHIFT;

			if ( dwUsage == D3DDECLUSAGE_COLOR )
			{
				// Vertex colors are clamped, as D3D9 hardware did
				buf.Printf( "\tout.color%d = saturate( oTempT%d );\n", dwUsageIndex, i );
			}
			else if ( dwUsage == D3DDECLUSAGE_TEXCOORD )
			{
				buf.Printf( "\tout.oT%d = oTempT%d;\n", dwUsageIndex, i );
			}
			else
			{
				TranslationError(); // Not supported yet, but can be if we need it.
			}
		}
	}
	else
	{
		for ( int i=0; i<16; i++ )
		{
			if ( m_dwTexCoordOutMask & ( 1 << i ) )
			{
				buf.Printf( "\tout.oT%d = oT%d;\n", i, i );
			}
		}
		for ( int i=0; i<2; i++ )
		{
			if ( m_dwColorOutMask & ( 1 << i ) )
			{
				buf.Printf( "\tout.color%d = saturate( out_color%d );\n", i, i );
			}
		}
	}
}

// Reads the pixel shader's input registers from the input struct.
void D3DToMetal::WritePixelInputAssignments()
{
	if ( m_dwMajorVersion == 3 )
	{
		for ( int i=0; i < ARRAYSIZE( m_DeclaredInputs ); i++ )
		{
			if ( m_DeclaredInputs[i] == UNDECLARED_INPUT )
				continue;

			uint32 dwToken = m_DeclaredInputs[i];

			uint32 dwUsage = ( dwToken & D3DSP_DCL_USAGE_MASK );
			uint32 dwUsageIndex = ( dwToken & D3DSP_DCL_USAGEINDEX_MASK ) >> D3DSP_DCL_USAGEINDEX_SHIFT;

			if ( dwUsage == D3DDECLUSAGE_COLOR )
			{
				PrintToBufWithIndents( *m_pBufAttribCode, "vec4 oTempT%d = in.color%d;\n", i, dwUsageIndex );
			}
			else if ( dwUsage == D3DDECLUSAGE_TEXCOORD )
			{
				PrintToBufWithIndents( *m_pBufAttribCode, "vec4 oTempT%d = in.oT%d;\n", i, dwUsageIndex );
			}
		}
	}
	else
	{
		for ( int i = 0; i < 16; i++ )
		{
			if ( m_nPSInputMask & D3DTOMETAL_INTERP_TEXCOORD( i ) )
				PrintToBufWithIndents( *m_pBufAttribCode, "vec4 oT%d = in.oT%d;\n", i, i );
		}
		for ( int i = 0; i < 2; i++ )
		{
			if ( m_nPSInputMask & D3DTOMETAL_INTERP_COLOR( i ) )
				PrintToBufWithIndents( *m_pBufAttribCode, "vec4 in_color%d = in.color%d;\n", i, i );
		}
	}
}

void D3DToMetal::Handle_DeclarativeNonDclOp( uint32 nInstruction )
{
	char buff[128];
	uint32 dwToken = GetNextToken();
	PrintParameterToString( dwToken, DST_REGISTER, buff, sizeof( buff ), false, NULL );

	if ( nInstruction == D3DSIO_TEXKILL )
	{
		// TEXKILL is supposed to discard the pixel if any of the src register's X, Y, or Z components are less than zero.
		// We have to translate it to something like:
		//   if ( r0.x < 0.0 || r0.y < 0.0 )
		//        discard_fragment();
		char c[3];
		c[0] = GetSwizzleComponent( buff, 0 );
		c[1] = GetSwizzleComponent( buff, 1 );
		c[2] = GetSwizzleComponent( buff, 2 );

		// Get the unique components.
		char cUnique[3];
		cUnique[0] = c[0];

		int nUnique = 1;
		if ( c[1] != c[0] )
			cUnique[nUnique++] = c[1];

		if ( c[2] != c[1] && c[2] != c[0] )
			cUnique[nUnique++] = c[2];

		// Get the src register base name.
		char szBase[256];
		GetParamNameWithoutSwizzle( buff, szBase, sizeof( szBase ) );

		PrintToBufWithIndents( *m_pBufALUCode, "if ( %s.%c < 0.0 ", szBase, cUnique[0] );
		for ( int i=1; i < nUnique; i++ )
		{
			PrintToBuf( *m_pBufALUCode, "|| %s.%c < 0.0 ", szBase, cUnique[i] );
		}
		PrintToBuf( *m_pBufALUCode, ")\n" );
		PrintToBufWithIndents( *m_pBufALUCode, "{\n" );
		PrintToBufWithIndents( *m_pBufALUCode, "\tdiscard_fragment();\n" );
		PrintToBufWithIndents( *m_pBufALUCode, "}\n" );
	}
	else
	{
		TranslationError();
	}
}

// This information can't be gleaned from the bytecode, so it comes from the
// shader's name, as in ToGL. Returns a mask, since several interpolants can be
// tagged with centroid.
uint32 D3DToMetalCentroidMaskFromName( bool bPixelShader, const char *pName )
{
	if ( !pName )
		return 0;
	
	// Important: The centroid bitflags must match between all linked vertex/pixel shaders!
	if ( bPixelShader )
	{
		if ( V_stristr( pName, "lightmappedgeneric_ps" ) || V_strstr( pName, "worldtwotextureblend_ps" ) )
		{
			return (0x01 << 2) | (0x01 << 3); // iterators 2 and 3
		}
		else if ( V_stristr( pName, "lightmappedreflective_ps" ) )
		{
			return (0x01 << 6) | (0x01 << 7); // iterators 6 and 7
		}
		else if ( V_stristr( pName, "water_ps" ) )
		{
			return 0xE0;
		}
		else if ( V_stristr( pName, "shadow_ps" ) )
		{
			return 0xE;
		}
		else if ( V_stristr( pName, "ShatteredGlass_ps" ) )
		{
			return 0xC;
		}
		else if ( V_stristr( pName, "WorldVertexAlpha_ps" ) || V_stristr( pName, "WorldVertexTransition_ps" ) )
		{
			// These pixel shaders want centroid but shouldn't be used
			Assert(0);
			return 0;
		}
		else if ( V_stristr( pName, "flashlight_ps" ) )
		{
			return 0xC;
		}
	}
	else // vertex shader
	{
		// Vertex shaders also
		if ( V_stristr( pName, "lightmappedgeneric_vs" ) )
		{
			return (0x01 << 2) | (0x01 << 3); // iterators 2 and 3
		}
		else if ( V_stristr( pName, "lightmappedreflective_vs" ) )
		{
			return (0x01 << 6) | (0x01 << 7); // iterators 6 and 7
		}
		else if ( V_stristr( pName, "water_vs" ) )
		{
			return 0xE0;
		}
		else if ( V_stristr( pName, "shadow_vs" ) )
		{
			return 0xE;
		}
		else if ( V_stristr( pName, "ShatteredGlass_vs" ) )
		{
			return 0xC;
		}
		else if ( V_stristr( pName, "flashlight_vs" ) )
		{
			return 0xC;
		}
	}
	
	// This shader doesn't have any centroid iterators
	return 0;
}


// As above: which samplers read shadow depth maps can't be gleaned from the bytecode.
int D3DToMetalShadowDepthSamplerMaskFromName( const char *pName )
{
	if ( !pName )
		return 0;	
	
	if ( V_stristr( pName, "water_ps" ) )
	{
		return (1<<7);
	}
	else if ( V_stristr( pName, "infected_ps" ) )
	{
		return (1<<1);
	}
	else if ( V_stristr( pName, "phong_ps" ) )
	{
		return (1<<4) | (1<<15);
	}
	else if ( V_stristr( pName, "vertexlit_and_unlit_generic_bump_ps" ) )
	{
		return (1<<8) | (1<<15);
	}
	else if ( V_stristr( pName, "vertexlit_and_unlit_generic_ps" ) )
	{
		return (1<<8) | (1<<15);
	}
	else if ( V_stristr( pName, "eye_refract_ps" ) )
	{
		return (1<<6);
	}
	else if ( V_stristr( pName, "eyes_flashlight_ps" ) )
	{
		return (1<<4);
	}
	else if ( V_stristr( pName, "worldtwotextureblend_ps" ) ) 
	{
		return (1<<7);
	}
	else if ( V_stristr( pName, "teeth_flashlight_ps" ) ) 
	{
		return (1<<2);
	}
	else if ( V_stristr( pName, "flashlight_ps" ) ) // substring of above, make sure this comes last!!
	{
		return (1<<7);
	}
	else if ( V_stristr( pName, "lightmappedgeneric_ps" ) )
	{
		return (1<<15);
	}
	else if ( V_stristr( pName, "character_ps" ) )
	{
		return (1 << 8);
	}
	// This shader doesn't have a shadow depth map sampler
	return 0;
}

//------------------------------------------------------------------------------
// TranslateShader()
//
// Parses the D3D9 token stream and writes an MSL program with one entry point:
// "dx9vs" for vertex shaders, "dx9ps" for pixel shaders.
//------------------------------------------------------------------------------
bool D3DToMetal::TranslateShader( uint32* code, CUtlBuffer *pBufMSL, D3DToMetalShaderInfo *pInfo, uint32 options,
	int32 nShadowDepthSamplerMask, uint32 nCentroidMask, uint32 nExtraOutputMask, const char *debugLabel )
{
	uint32 i, dwToken, nInstruction, nNumTokensToSkip;

	s_bTranslationFailed = false;

	m_bSpew = (options & D3DToMetal_OptionSpew) != 0;

	m_NumIndentTabs = 1; // start code indented one tab
	m_nLoopDepth = 0;

	// Generous buffers: ps_3_0 shaders with loops expand to a lot of text.
	CUtlBuffer bufHeader( 0, 100000, CUtlBuffer::TEXT_BUFFER );
	CUtlBuffer bufAttrib( 0, 100000, CUtlBuffer::TEXT_BUFFER );
	CUtlBuffer bufParam( 0, 100000, CUtlBuffer::TEXT_BUFFER );
	CUtlBuffer bufALU( 0, 1000000, CUtlBuffer::TEXT_BUFFER );
	m_pBufHeaderCode = &bufHeader;
	m_pBufAttribCode = &bufAttrib;
	m_pBufParamCode = &bufParam;
	m_pBufALUCode = &bufALU;
	((char*)bufHeader.Base())[0] = 0;
	((char*)bufAttrib.Base())[0] = 0;
	((char*)bufParam.Base())[0] = 0;
	((char*)bufALU.Base())[0] = 0;

	for ( i=0; i<MAX_SHADER_CONSTANTS; i++ )
	{
		m_bConstantRegisterDefined[i] = false;
	}

	// Track shadow sampler usage for proper declaration
	m_nShadowDepthSamplerMask = nShadowDepthSamplerMask;

	// Various flags set while parsing code to drive various declaration instructions
	m_bNeedsSinCosDeclarations = false;
	m_bDeclareAddressReg = false;
	m_bDeclareVSOPos = false;
	m_bDeclareVSOFog = false;
	m_dwTexCoordOutMask = 0x00000000;
	m_dwColorOutMask = 0x00000000;
	m_nVSPositionOutput = -1;
	m_bOutputColorRegister[0] = false;
	m_bOutputColorRegister[1] = false;
	m_bOutputColorRegister[2] = false;
	m_bOutputColorRegister[3] = false;
	m_bOutputDepthRegister = false;
	m_bUsesFragCoord = false;
	m_bUsesDSTInstruction = false;
	m_dwTempUsageMask = 0x00000000;
	m_dwSamplerUsageMask = 0x00000000;
	m_dwConstIntUsageMask = 0x00000000;
	m_dwDefConstIntUsageMask = 0x00000000;
	memset( m_dwDefConstIntIterCount, 0, sizeof( m_dwDefConstIntIterCount ) );
	m_dwConstBoolUsageMask = 0x00000000;
	m_nCentroidMask = nCentroidMask;
	m_nPSInputMask = 0;
	m_nHighestRegister = 0;
	m_dwInputRegisterMask = 0;

	m_bUsedAtomicTempVar = false;
	for ( int i=0; i < ARRAYSIZE( m_dwSamplerTypes ); i++ )
	{
		m_dwSamplerTypes[i] = SAMPLER_TYPE_UNUSED;
	}

	for ( int i=0; i < ARRAYSIZE( m_DeclaredOutputs ); i++ )
	{
		m_DeclaredOutputs[i] = UNDECLARED_OUTPUT;
	}

	for ( int i=0; i < ARRAYSIZE( m_DeclaredInputs ); i++ )
	{
		m_DeclaredInputs[i] = UNDECLARED_INPUT;
	}

	memset( m_dwAttribMap, 0xFF, sizeof(m_dwAttribMap) );

	m_pdwBaseToken = m_pdwNextToken = code;	 // Initialize dwToken pointers

	dwToken = GetNextToken();
	m_dwMajorVersion = D3DSHADER_VERSION_MAJOR( dwToken );
	m_dwMinorVersion = D3DSHADER_VERSION_MINOR( dwToken );
	m_bVertexShader = ( dwToken & 0xFFFF0000 ) != 0xFFFF0000;

	if ( m_dwMajorVersion < 2 )
	{
		// Shader model 1 isn't supported, as in ToGL
		return false;
	}

	if ( m_bSpew )
	{
		printf("\n************* translating shader %s", debugLabel ? debugLabel : "" );
	}

	// Loop until we hit the end dwToken...note that D3DPS_END() == D3DVS_END() so this works for either
	while ( dwToken != D3DPS_END() && !s_bTranslationFailed )
	{
		dwToken = GetNextToken();	// Get next dwToken in the stream
		nInstruction = Opcode( dwToken ); // Mask out the instruction opcode

		switch ( nInstruction )
		{
			// -- No arguments at all -----------------------------------------------
			case D3DSIO_NOP:
				// D3D compiler outputs NOPs when shader debugging/optimizations are disabled.
				break;

			case D3DSIO_PHASE:
			case D3DSIO_RET:
			case D3DSIO_ENDLOOP:
			case D3DSIO_BREAK:
				TranslationError();
				break;

			// -- "Declarative" non dcl ops ----------------------------------------
			case D3DSIO_TEXDEPTH:
			case D3DSIO_TEXKILL:
				Handle_DeclarativeNonDclOp( nInstruction );
				break;

			// -- Unsupported ops -------------------------------------------------
			case D3DSIO_BEM:
			case D3DSIO_TEXBEM:
			case D3DSIO_TEXBEML:
			case D3DSIO_TEXDP3:
			case D3DSIO_TEXDP3TEX:
			case D3DSIO_TEXM3x2DEPTH:
			case D3DSIO_TEXM3x2TEX:
			case D3DSIO_TEXM3x3:
			case D3DSIO_TEXM3x3PAD:
			case D3DSIO_TEXM3x3TEX:
			case D3DSIO_TEXM3x3VSPEC:
			case D3DSIO_TEXREG2AR:
			case D3DSIO_TEXREG2GB:
			case D3DSIO_TEXREG2RGB:
			case D3DSIO_LABEL:
			case D3DSIO_CALL:
			case D3DSIO_LOOP:
			case D3DSIO_BREAKP:
				TranslationError();
				break;

			case D3DSIO_DSX:
			case D3DSIO_DSY:
				Handle_UnaryOp( nInstruction );
				break;

			case D3DSIO_IFC:
			{
				static const char *s_szCompareStrings[ 7 ] =
				{
					"__INVALID__",
					">",
					"==",
					">=",
					"<",
					"!=",
					"<="
				};

				// Compare mode is encoded in instruction token
				uint32 dwCompareMode = OpcodeSpecificData( dwToken );

				Assert( ( dwCompareMode >= 1 ) && ( dwCompareMode <= 6 ) );
				if ( ( dwCompareMode < 1 ) || ( dwCompareMode > 6 ) )
				{
					TranslationError();
					break;
				}

				// Get left side of compare
				dwToken = GetNextToken();
				char szLeftSide[64];
				PrintParameterToString( dwToken, SRC_REGISTER, szLeftSide, sizeof( szLeftSide ), false, NULL );

				// Get right side of compare
				dwToken = GetNextToken();
				char szRightSide[64];
				PrintParameterToString( dwToken, SRC_REGISTER, szRightSide, sizeof( szRightSide ), false, NULL );

				PrintToBufWithIndents( *m_pBufALUCode, "if ( %s %s %s )\n", szLeftSide, s_szCompareStrings[dwCompareMode], szRightSide );
				StrcatToALUCode( "{\n" );
				m_NumIndentTabs++;

				break;
			}
			case D3DSIO_IF:
			{
				char buff[256];
				dwToken = GetNextToken();
				PrintParameterToString( dwToken, SRC_REGISTER, buff, sizeof( buff ), false, NULL );

				PrintToBufWithIndents( *m_pBufALUCode, "if ( %s )\n", buff );
				StrcatToALUCode( "{\n" );
				m_NumIndentTabs++;

				break;
			}

			case D3DSIO_ELSE:
				m_NumIndentTabs--;
				StrcatToALUCode( "}\n" );
				StrcatToALUCode( "else\n" );
				StrcatToALUCode( "{\n" );
				m_NumIndentTabs++;

				break;

			case D3DSIO_ENDIF:
				m_NumIndentTabs--;
				StrcatToALUCode( "}\n" );

				break;

			case D3DSIO_REP:
			{
				char buff[256];
				dwToken = GetNextToken();
				PrintParameterToString( dwToken, SRC_REGISTER, buff, sizeof( buff ), false, NULL );

				// In practice, this is the only form of for loop that will appear in DX asm
				PrintToBufWithIndents( *m_pBufALUCode, "for( int rep%d=0; rep%d < %s; rep%d++ )\n", m_nLoopDepth, m_nLoopDepth, buff, m_nLoopDepth );
				StrcatToALUCode( "{\n" );

				m_nLoopDepth++;
				m_NumIndentTabs++;

				break;
			}

			case D3DSIO_ENDREP:
				m_nLoopDepth--;
				m_NumIndentTabs--;
				StrcatToALUCode( "}\n" );

				break;

			case D3DSIO_NRM:
				Handle_NRM();
				break;

			case D3DSIO_MOVA:
				Handle_UnaryOp( nInstruction );
				break;

			// Unary operations
			case D3DSIO_MOV:
			case D3DSIO_RCP:
			case D3DSIO_RSQ:
			case D3DSIO_EXP:
			case D3DSIO_EXPP:
			case D3DSIO_LOG:
			case D3DSIO_LOGP:
			case D3DSIO_FRC:
			case D3DSIO_LIT:
			case D3DSIO_ABS:
				Handle_UnaryOp( nInstruction );
				break;

			// -- Unsupported binary ops ----------------------------------------
			case D3DSIO_TEXM3x3SPEC:
			case D3DSIO_M4x4:
			case D3DSIO_M4x3:
			case D3DSIO_M3x4:
			case D3DSIO_M3x3:
			case D3DSIO_M3x2:
			case D3DSIO_CALLNZ:
			case D3DSIO_SETP:
				TranslationError();
				break;

			case D3DSIO_BREAKC:
				Handle_BREAKC( dwToken );
				break;

			// Binary Operations
			case D3DSIO_ADD:
			case D3DSIO_SUB:
			case D3DSIO_MUL:
			case D3DSIO_DP3:
			case D3DSIO_DP4:
			case D3DSIO_MIN:
			case D3DSIO_MAX:
			case D3DSIO_DST:
			case D3DSIO_SLT:
			case D3DSIO_SGE:
			case D3DSIO_CRS:
			case D3DSIO_POW:
				HandleBinaryOp( nInstruction );
				break;

			// -- Ternary ops -------------------------------------------------
			case D3DSIO_DP2ADD:
				Handle_DP2ADD();
				break;
			case D3DSIO_LRP:
				Handle_LRP( nInstruction );
				break;
			case D3DSIO_SGN:
			case D3DSIO_CND:
				TranslationError();
				break;
			case D3DSIO_CMP:
				Handle_CMP();
				break;
			case D3DSIO_SINCOS:
				Handle_SINCOS();
				break;
			case D3DSIO_MAD:
				Handle_MAD( nInstruction );
				break;

			// -- Quaternary op ------------------------------------------------
			case D3DSIO_TEXLDD:
				Handle_TexLDD( nInstruction );
				break;

			// -- Special cases: texcoord vs texcrd	and	tex vs texld -----------
			case D3DSIO_TEXCOORD:
				Handle_TexCoord();
				break;

			case D3DSIO_TEX:
				Handle_TEX( dwToken, false );
				break;

			case D3DSIO_TEXLDL:
				Handle_TEX( nInstruction, true );
				break;

			case D3DSIO_DCL:
				Handle_DCL();
				break;

			case D3DSIO_DEFB:
			case D3DSIO_DEFI:
				Handle_DEFIB( nInstruction );
				break;

			case D3DSIO_DEF:
				Handle_DEF();
				break;

			case D3DSIO_COMMENT:
				// Using OpcodeSpecificData() can fail here since the comments can be longer than 0xff dwords
				nNumTokensToSkip = ( dwToken & 0x0fff0000 ) >> 16;
				SkipTokens( nNumTokensToSkip );
				break;

			case D3DSIO_END:
				break;

			default:
				TranslationError();
				break;
		}
	}

	if ( s_bTranslationFailed )
		return false;

	// Note that this constant packing expects .wzyx swizzles in case we ever use the SINCOS code in a ps_2_x shader
	//
	// The Microsoft documentation on this is all kinds of broken and, strangely, these numbers don't even
	// match the D3DSINCOSCONST1 and D3DSINCOSCONST2 constants used by the D3D assembly sincos instruction...
	if ( m_bNeedsSinCosDeclarations )
	{
		PrintIndentation( (char*)m_pBufParamCode->Base(), m_pBufParamCode->Size() );
		StrcatToParamCode( "vec4 scA = vec4( -1.55009923e-6, -2.17013894e-5, 0.00260416674, 0.00026041668 );\n" );
		PrintIndentation( (char*)m_pBufParamCode->Base(), m_pBufParamCode->Size() );
		StrcatToParamCode( "vec4 scB = vec4( -0.020833334, -0.125, 1.0, 0.5 );\n" );
		PrintIndentation( (char*)m_pBufParamCode->Base(), m_pBufParamCode->Size() );
		StrcatToParamCode( "vec3 vSinCosTmp = vec3( 0.0 );\n" );
	}

	// Integer and boolean constants come from the misc buffer, unless the shader defines them
	for( int i=0; i<16; i++ )
	{
		if ( ( m_dwConstIntUsageMask & ( 0x00000001 << i ) ) && !( m_dwDefConstIntUsageMask & ( 0x00000001 << i ) ) )
		{
			PrintToBufWithIndents( *m_pBufParamCode, "int i%d = misc.ints[%d].x;\n", i, i );
		}
	}

	for( int i=0; i<32; i++ )
	{
		if ( m_dwDefConstIntUsageMask & ( 0x00000001 << i ) )
		{
			PrintToBufWithIndents( *m_pBufParamCode, "const int i%d = %i;\n", i, m_dwDefConstIntIterCount[i] );
		}
	}

	for( int i=0; i<16; i++ )
	{
		if ( m_dwConstBoolUsageMask & ( 0x00000001 << i ) )
		{
			PrintToBufWithIndents( *m_pBufParamCode, m_bVertexShader ? "bool b%d = misc.bools[%d] != 0;\n" : "bool fb%d = misc.bools[%d] != 0;\n", i, i );
		}
	}
	if ( ( m_dwConstIntUsageMask | m_dwConstBoolUsageMask ) & 0xFFFF0000 )
	{
		TranslationError();
		return false;
	}

	if ( m_bDeclareAddressReg )
	{
		PrintIndentation( (char*)m_pBufParamCode->Base(), m_pBufParamCode->Size() );
		StrcatToParamCode( "vec4 va_r = vec4( 0.0 );\n" );
	}

	// Declare temps in Param code buffer. D3D9 leaves them undefined; zero them so results are deterministic.
	for( int i=0; i<32; i++ )
	{
		if ( m_dwTempUsageMask & ( 0x00000001 << i ) )
		{
			PrintIndentation( (char*)m_pBufParamCode->Base(), m_pBufParamCode->Size() );
			PrintToBuf( *m_pBufParamCode, "vec4 r%d = vec4( 0.0 );\n", i );
		}
	}

	if ( m_bUsedAtomicTempVar )
	{
		PrintToBufWithIndents( *m_pBufParamCode, "vec4 %s = vec4( 0.0 );\n", g_pAtomicTempVarName );
	}

	// Program text: prelude, helpers, structs, the entry point.
	CUtlBuffer &out = *pBufMSL;
	out.Clear();
	out.PutString( g_pMSLPrelude );
	if ( debugLabel )
	{
		char szLabel[256];
		V_strncpy( szLabel, debugLabel, sizeof( szLabel ) );
		for ( char *p = szLabel; *p; ++p )
		{
			if ( *p == '\n' || *p == '\r' )
				*p = ' ';
		}
		out.Printf( "// %s\n\n", szLabel );
	}

	if ( m_bUsesDSTInstruction )
	{
		out.PutString( "inline vec4 dst( vec4 src0, vec4 src1 ) { return vec4( 1.0f, src0.y * src1.y, src0.z, src1.w ); }\n\n" );
	}

	uint32 nInterpolantMask = 0;
	if ( m_bVertexShader )
	{
		// Interpolants written, by semantic
		if ( m_dwMajorVersion == 3 )
		{
			for ( int i = 0; i < ARRAYSIZE( m_DeclaredOutputs ); i++ )
			{
				if ( m_DeclaredOutputs[i] == UNDECLARED_OUTPUT || (int32)i == m_nVSPositionOutput || !( m_dwTexCoordOutMask & ( 1 << i ) ) )
					continue;
				uint32 dwUsage = ( m_DeclaredOutputs[i] & D3DSP_DCL_USAGE_MASK );
				uint32 dwUsageIndex = ( m_DeclaredOutputs[i] & D3DSP_DCL_USAGEINDEX_MASK ) >> D3DSP_DCL_USAGEINDEX_SHIFT;
				nInterpolantMask |= InterpolantForUsage( dwUsage, dwUsageIndex );
			}
		}
		else
		{
			nInterpolantMask = ( m_dwTexCoordOutMask & 0xFFFF ) | ( m_dwColorOutMask << 16 );
		}
		if ( s_bTranslationFailed )
			return false;

		out.PutString( g_pVSMiscStruct );

		out.PutString( "struct VSIn\n{\n" );
		for ( int i = 0; i < 16; i++ )
		{
			if ( m_dwInputRegisterMask & ( 1 << i ) )
				out.Printf( "\tfloat4 v%d [[ attribute( %d ) ]];\n", i, i );
		}
		out.PutString( "};\n\n" );

		out.PutString( "struct VSOut\n{\n\tfloat4 gl_Position [[ position ]];\n" );
		WriteInterpolantMembers( out, nInterpolantMask | nExtraOutputMask, 0, false );
		out.PutString( "\tfloat clipDist [[ clip_distance ]] [2];\n};\n\n" );

		out.Printf( "vertex VSOut dx9vs( VSIn in [[ stage_in ]],\n\tconstant float4 *vc [[ buffer( %d ) ]],\n\tconstant VSMisc &misc [[ buffer( %d ) ]] )\n{\n",
			D3DTOMETAL_VS_CONSTANT_BUFFER, D3DTOMETAL_VS_MISC_BUFFER );
		out.PutString( "\tVSOut out;\n" );
		for ( int i = 0; i < 16; i++ )
		{
			if ( m_dwInputRegisterMask & ( 1 << i ) )
				out.Printf( "\tvec4 v%d = in.v%d;\n", i, i );
		}
		out.PutString( "\tvec4 vTempPos = vec4( 0.0, 0.0, 0.0, 1.0 );\n" );
		if ( m_dwMajorVersion == 3 )
		{
			for ( int i = 0; i < 32; i++ )
			{
				if ( ( m_dwTexCoordOutMask & ( 1 << i ) ) && (int32)i != m_nVSPositionOutput )
					out.Printf( "\tvec4 oTempT%d = vec4( 0.0 );\n", i );
			}
		}
		else
		{
			for ( int i = 0; i < 16; i++ )
			{
				if ( m_dwTexCoordOutMask & ( 1 << i ) )
					out.Printf( "\tvec4 oT%d = vec4( 0.0 );\n", i );
			}
			for ( int i = 0; i < 2; i++ )
			{
				if ( m_dwColorOutMask & ( 1 << i ) )
					out.Printf( "\tvec4 out_color%d = vec4( 0.0 );\n", i );
			}
		}
		if ( m_bDeclareVSOFog )
		{
			out.PutString( "\tfloat gl_FogFragCoord = 0.0;\n" );
		}
	}
	else
	{
		nInterpolantMask = m_nPSInputMask;

		out.PutString( g_pPSMiscStruct );

		out.PutString( "struct PSIn\n{\n\tfloat4 gl_FragCoord [[ position ]];\n" );
		WriteInterpolantMembers( out, nInterpolantMask, m_nCentroidMask, true );
		out.PutString( "};\n\n" );

		out.PutString( "struct PSOut\n{\n" );
		bool bAnyColor = false;
		for ( int i = 0; i < 4; i++ )
		{
			if ( m_bOutputColorRegister[i] || i == 0 )
			{
				out.Printf( "\tfloat4 c%d [[ color( %d ) ]];\n", i, i );
				bAnyColor = true;
			}
		}
		(void)bAnyColor;
		if ( m_bOutputDepthRegister )
		{
			out.PutString( "\tfloat depth [[ depth( any ) ]];\n" );
		}
		out.PutString( "};\n\n" );

		out.Printf( "fragment PSOut dx9ps( PSIn in [[ stage_in ]],\n\tconstant float4 *pc [[ buffer( %d ) ]],\n\tconstant PSMisc &misc [[ buffer( %d ) ]]",
			D3DTOMETAL_PS_CONSTANT_BUFFER, D3DTOMETAL_PS_MISC_BUFFER );
		WriteSamplerParameters( out );
		out.PutString( " )\n{\n" );
		out.PutString( "\tPSOut out;\n" );
		out.PutString( "\tvec4 gl_FragCoord = in.gl_FragCoord;\n" );
		out.PutString( "\tvec4 gl_FragData[4] = { vec4( 0.0 ), vec4( 0.0 ), vec4( 0.0 ), vec4( 0.0 ) };\n" );
		if ( m_bOutputDepthRegister )
		{
			out.PutString( "\tfloat gl_FragDepth = in.gl_FragCoord.z;\n" );
		}

		WritePixelInputAssignments();
	}

	out.PutString( (char*)m_pBufAttribCode->Base() );
	out.PutString( (char*)m_pBufParamCode->Base() );
	out.PutString( (char*)m_pBufALUCode->Base() );

	if ( m_bVertexShader )
	{
		// User clip planes, in clip space, before the pixel centre adjustment.
		out.PutString( "\tout.clipDist[0] = ( misc.clipEnable & 1 ) ? dot( vTempPos, misc.clipPlane[0] ) : 1.0;\n" );
		out.PutString( "\tout.clipDist[1] = ( misc.clipEnable & 2 ) ? dot( vTempPos, misc.clipPlane[1] ) : 1.0;\n" );
		// D3D9 puts pixel centres on integer coordinates, Metal half a pixel further right and down.
		out.PutString( "\tvTempPos.xy += float2( misc.vcscreen.x, -misc.vcscreen.y ) * vTempPos.w;\n" );
		out.PutString( "\tout.gl_Position = vTempPos;\n" );
		WriteVertexOutputAssignments( out, nInterpolantMask );
		for ( int i = 0; i < 16; i++ )
		{
			if ( ( nExtraOutputMask & ~nInterpolantMask ) & D3DTOMETAL_INTERP_TEXCOORD( i ) )
				out.Printf( "\tout.oT%d = float4( 0.0 );\n", i );
		}
		for ( int i = 0; i < 2; i++ )
		{
			if ( ( nExtraOutputMask & ~nInterpolantMask ) & D3DTOMETAL_INTERP_COLOR( i ) )
				out.Printf( "\tout.color%d = float4( 0.0 );\n", i );
		}
	}
	else
	{
		// D3D9's fixed-function alpha test, compiled in for the comparison the
		// draw uses (a macro rather than a function constant, so each variant
		// is a function of its own in the pipeline archive). Without it the
		// shader has no discard, which keeps hidden surface removal on.
		out.PutString(
			"#if D3DTOMETAL_ALPHA_TEST_FUNC != 0\n"
			"\t{\n"
			"\t\tfloat a = gl_FragData[0].w;\n"
			"#if D3DTOMETAL_ALPHA_TEST_FUNC == 1\n"
			"\t\tbool bPass = false;\n"
			"#elif D3DTOMETAL_ALPHA_TEST_FUNC == 2\n"
			"\t\tbool bPass = a < misc.alphaRef;\n"
			"#elif D3DTOMETAL_ALPHA_TEST_FUNC == 3\n"
			"\t\tbool bPass = a == misc.alphaRef;\n"
			"#elif D3DTOMETAL_ALPHA_TEST_FUNC == 4\n"
			"\t\tbool bPass = a <= misc.alphaRef;\n"
			"#elif D3DTOMETAL_ALPHA_TEST_FUNC == 5\n"
			"\t\tbool bPass = a > misc.alphaRef;\n"
			"#elif D3DTOMETAL_ALPHA_TEST_FUNC == 6\n"
			"\t\tbool bPass = a != misc.alphaRef;\n"
			"#elif D3DTOMETAL_ALPHA_TEST_FUNC == 7\n"
			"\t\tbool bPass = a >= misc.alphaRef;\n"
			"#else\n"
			"\t\tbool bPass = true;\n"
			"#endif\n"
			"\t\tif ( !bPass )\n"
			"\t\t\tdiscard_fragment();\n"
			"\t}\n"
			"#endif\n" );
		for ( int i = 0; i < 4; i++ )
		{
			if ( m_bOutputColorRegister[i] || i == 0 )
				out.Printf( "\tout.c%d = gl_FragData[%d];\n", i, i );
		}
		if ( m_bOutputDepthRegister )
		{
			out.PutString( "\tout.depth = gl_FragDepth;\n" );
		}
	}
	out.PutString( "\treturn out;\n}\n" );

	if ( s_bTranslationFailed )
		return false;

	// Metadata for the device
	memset( pInfo, 0, sizeof( *pInfo ) );
	pInfo->m_bVertexShader = m_bVertexShader;
	pInfo->m_nMajorVersion = m_dwMajorVersion;
	pInfo->m_nMinorVersion = m_dwMinorVersion;
	pInfo->m_nSamplerMask = m_dwSamplerUsageMask;
	uint nSamplerTypes = 0;
	for ( int i = 0; i < 16; i++ )
	{
		nSamplerTypes |= ( m_dwSamplerTypes[i] & 3 ) << ( i * 2 );
		if ( ( m_dwSamplerUsageMask & ( 1 << i ) ) && m_dwSamplerTypes[i] == SAMPLER_TYPE_2D && ( m_nShadowDepthSamplerMask & ( 1 << i ) ) )
			pInfo->m_nShadowSamplerMask |= 1 << i;
	}
	pInfo->m_nSamplerTypes = nSamplerTypes;
	pInfo->m_nHighWater = m_nHighestRegister + 1;
	for ( int i = 0; i < 16; i++ )
	{
		pInfo->m_AttribMap[i] = ( m_dwAttribMap[i] == 0xFFFFFFFF ) ? 0xBB : (uint8)m_dwAttribMap[i];
	}
	pInfo->m_nInterpolantMask = nInterpolantMask;
	uint nFragDataMask = 0;
	for ( int i = 0; i < 4; i++ )
	{
		nFragDataMask |= m_bOutputColorRegister[ i ] ? ( 1 << i ) : 0;
	}
	pInfo->m_nFragDataMask = nFragDataMask;
	pInfo->m_bWritesDepth = m_bOutputDepthRegister;

	if ( m_bSpew )
	{
		printf("\n************* translation complete\n\n " );
	}

	return true;
}
