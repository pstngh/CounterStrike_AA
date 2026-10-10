//========= Copyright (C) 1996-2013, Valve Corporation, All rights reserved. ============//
//
// Purpose: Helper for UI components - used for binding to both Scaleform ActionScript and 
//          Panorama Javascript.
//
// $NoKeywords: $
//=============================================================================//

#ifndef UICOMPONENT_COMMON_H
#define UICOMPONENT_COMMON_H
#ifdef _WIN32
#pragma once
#endif

#if defined ( PANORAMA_ENABLE )
#define USE_PANORAMA_BINDINGS
#endif // PANORAMA_ENABLE

#ifndef CSGO_PORT
// In Source1 the pointers_to_members pragma is used within Scaleform and VGUI headers to ensure member function pointers
// are maximum size. This affects the panorama javascript bindings (see uijsregistration.h).
// We also add the pragma here so that we always assume the larger size pointers in source 1 for consistency, regardless
// of the order of includes. 
#define MEMBER_FUNCPTRS_MAXSIZE
#pragma pointers_to_members( full_generality, virtual_inheritance )
#endif

#ifdef USE_PANORAMA_BINDINGS

#include "panorama/uijsregistration.h"
#include "panorama/iuiengine.h"
#define DECLARE_PANORAMA_JSREGISTERFUNC void JSRegisterFunc();
#else
#define DECLARE_PANORAMA_JSREGISTERFUNC
#endif

#include "bannedwords.h"

class IUiComponentGlobalInstanceBase
{
public:
	virtual void Tick() {}
	virtual void Shutdown() = 0;
	virtual void InstallScaleformBindings( int iSlot ) = 0;
	virtual void InstallPanoramaBindings() = 0;
	virtual void ShutdownComponentApiDef(int iSlot) = 0;
};

template < class T >
class CUiComponentGlobalInstanceHelper : public IUiComponentGlobalInstanceBase
{
protected:
	static T* & InternalInstancePtr() { static T* s_pInstance = NULL; return s_pInstance; }

public:
	static T* GetInstance()
	{
		if ( !InternalInstancePtr() )
		{
			InternalInstancePtr() = new T();
		}
		return InternalInstancePtr();
	}

	void Shutdown() OVERRIDE
	{
		T *pInstance = InternalInstancePtr();
		Assert( pInstance && (pInstance == this) );
		if ( pInstance && (pInstance == this) )
		{
			InternalInstancePtr() = NULL;
			delete pInstance;
		}
	}
};

#define UI_COMPONENT_DECLARE_GLOBAL_INSTANCE_ONLY( classname ) \
	protected: \
		friend class CUiComponentGlobalInstanceHelper< classname >; \
		void InstallScaleformBindings( int iSlot ) OVERRIDE; \
		void InstallPanoramaBindings( ) OVERRIDE; \
		void ShutdownComponentApiDef( int iSlot ) OVERRIDE; \
		classname(); \
		~classname(); \
	private: \
		classname( const classname & other ); \
		classname & operator = ( const classname & other ); \
		DECLARE_PANORAMA_JSREGISTERFUNC


#define SF_COMPONENT_EVENT_NAME( category, eventname ) "ScaleformComponent_" #category "_" #eventname
#define SF_COMPONENT_EVENT_OBJECT_NAME( category, eventname ) g_ScaleformComponent_##category##_##eventname##_event
#define SF_COMPONENT_DECLARE_EVENT0( category, eventname )
#define SF_COMPONENT_DECLARE_EVENT1( category, eventname, param1_name, param1 )
#define SF_COMPONENT_DECLARE_EVENT2(category, eventname, param1_name, param1, param2_name, param2)
#define SF_COMPONENT_DECLARE_EVENT3(category, eventname, param1_name, param1, param2_name, param2, param3_name, param3)
#define SF_COMPONENT_DECLARE_EVENT4(category, eventname, param1_name, param1, param2_name, param2, param3_name, param3, param4_name, param4)
#define SF_COMPONENT_DECLARE_EVENT5(category, eventname, param1_name, param1, param2_name, param2, param3_name, param3, param4_name, param4, param5_name, param5)
#define SF_COMPONENT_DEFINE_EVENT(category, eventname)
#define SF_COMPONENT_BROADCAST_EVENT( category, eventname, ... )


#define SF_COMPONENT_FUNCTION_NAME( fnname )
#define SF_DEFAULT_PARAMS_DECL
#define SF_DEFAULT_PARAMS_PASS
#define SF_COMPONENT_FUNCTION( returntype, fnname )
#define SF_COMPONENT_FUNCTION_IMPL( classname, fnname )
#define SF_COMPONENT_FUNCTION_DELEGATE( ptrcomponent, fnname )
#define SF_COMPONENT_API_DEF_BEGIN( componentclass )
#define SF_COMPONENT_FUNCTION_API_DEF( returntype, fnname, componentclass )
#define SF_COMPONENT_FUNCTION_API_DEF_NOPREFIX( returntype, fnname, componentclass )
#define SF_COMPONENT_API_DEF_END( componentclass )
#define SF_COMPONENT_API_INSTALL( iSlot, componentclass, ptrInstance )
#define SF_COMPONENT_API_REMOVE( iSlot, componentclass )
#define SF_INTEGRATION_XUID_PARAM( xuidVarName, iParam )
#define SF_INTEGRATION_ITEMID_PARAM( itemidVarName, iParam ) 
#define SF_COMPONENT_NOTIFY_FLASH( category, eventname )
#define SF_COMPONENT_FORWARD_EVENT( szEvent )
#define SF_COMPONENT_HOST_DECL()
#define SF_COMPONENT_HOST_IMAGE_CACHE_ENABLE( bEnable )
#define SF_COMPONENT_HOST_API_DEF()
#define SF_COMPONENT_GC_REG_JOB( gcclientclass, cjobclass, cmsgclass, gcmsgid, ccomponentclass, ccomponentfn )


#ifdef USE_PANORAMA_BINDINGS

#define PANORAMA_COMPONENT_API_DEF_BEGIN( componentclass ) \
void componentclass::JSRegisterFunc() \
{ \

#define PANORAMA_COMPONENT_FUNCTION_API_DEF_DOC( returntype, fnname, componentclass, argNames, description ) \
	panorama::RegisterJSMethod( #fnname, &componentclass::fnname, description, argNames );

#define PANORAMA_COMPONENT_FUNCTION_API_DEF( returntype, fnname, componentclass ) \
	PANORAMA_COMPONENT_FUNCTION_API_DEF_DOC( returntype, fnname, componentclass, "", "" )

#define PANORAMA_COMPONENT_FUNCTION_RAW_API_DEF_DOC( returntype, fnname, componentclass, description ) \
	panorama::RegisterJSMethodRaw( #fnname, &componentclass::fnname, description );

#define PANORAMA_COMPONENT_FUNCTION_RAW_API_DEF( returntype, fnname, componentclass ) \
	PANORAMA_COMPONENT_FUNCTION_RAW_API_DEF_DOC( returntype, fnname, componentclass, "" )

#define PANORAMA_COMPONENT_API_DEF_END( componentclass ) \
}; \

#define PANORAMA_COMPONENT_API_INSTALL( componentclass, ptrInstance, scriptVarName, description ) \
	do { \
		panorama::UIEngine()->StartRegisterJSScope( scriptVarName, description ); \
		CUtlDelegate< void() > del( ptrInstance, &componentclass::JSRegisterFunc ); \
		CUtlAbstractDelegate absDel = del.GetAbstractDelegate(); \
		panorama::UIEngine()->ExposeObjectTypeToJavaScript( #componentclass, absDel ); \
		panorama::UIEngine()->ExposeGlobalObjectToJavaScript(scriptVarName, ptrInstance, #componentclass, true); \
		panorama::UIEngine()->EndRegisterJSScope(); \
	} while (0)

#define PANORAMA_COMPONENT_API_REMOVE( componentclass )

#define PANORAMA_COMPONENT_EVENT_NAME( category, eventname ) PanoramaComponent_##category##_##eventname

#define PANORAMA_COMPONENT_BROADCAST_EVENT( category, eventname, ... ) \
	panorama::DispatchEvent(PANORAMA_COMPONENT_EVENT_NAME(category, eventname)(), NULL, ##__VA_ARGS__);

#define PANORAMA_COMPONENT_BROADCAST_EVENT_WITH_PARAMS( category, eventname, ... ) \
	panorama::DispatchEvent(PANORAMA_COMPONENT_EVENT_NAME(category, eventname)(), NULL, ##__VA_ARGS__);

#define PANORAMA_COMPONENT_DECLARE_EVENT0( category, eventname ) \
	DECLARE_PANORAMA_EVENT0(PANORAMA_COMPONENT_EVENT_NAME( category, eventname ))

#define PANORAMA_COMPONENT_DECLARE_EVENT1( category, eventname, param1 ) \
	DECLARE_PANORAMA_EVENT1(PANORAMA_COMPONENT_EVENT_NAME( category, eventname ), param1)

#define PANORAMA_COMPONENT_DECLARE_EVENT2(category, eventname, param1, param2) \
	DECLARE_PANORAMA_EVENT2(PANORAMA_COMPONENT_EVENT_NAME( category, eventname ), param1, param2)

#define PANORAMA_COMPONENT_DECLARE_EVENT3(category, eventname, param1, param2, param3) \
	DECLARE_PANORAMA_EVENT3(PANORAMA_COMPONENT_EVENT_NAME( category, eventname ), param1, param2, param3)

#define PANORAMA_COMPONENT_DECLARE_EVENT4(category, eventname, param1, param2, param3, param4) \
	DECLARE_PANORAMA_EVENT4(PANORAMA_COMPONENT_EVENT_NAME( category, eventname ), param1, param2, param3, param4)

#define PANORAMA_COMPONENT_DECLARE_EVENT5(category, eventname, param1, param2, param3, param4, param5) \
	DECLARE_PANORAMA_EVENT5(PANORAMA_COMPONENT_EVENT_NAME( category, eventname ), param1, param2, param3, param4, param5)

#define PANORAMA_COMPONENT_DEFINE_EVENT(category, eventname)\
	DEFINE_PANORAMA_EVENT(PANORAMA_COMPONENT_EVENT_NAME(category, eventname))

#define PANORAMA_COMPONENT_MARSHALL_HELPER_FUNCTION_API_DEF( returntype, fnname, componentclass ) \
	RegisterJSMethodWithMarshallHelper( #fnname, &componentclass::SF_COMPONENT_FUNCTION_NAME( fnname ) );

#define PANORAMA_COMPONENT_MARSHALL_HELPER_FUNCTION_API_DEF_NOPREFIX( returntype, fnname, componentclass ) \
	RegisterJSMethodWithMarshallHelper( #fnname, &componentclass::fnname );

// Helper class to allow the existing component functions to take a common object for param/return val handling
// Allows existing SF component code which expects multiple return types or to sometimes not return a value to remain unchanged and be
// called by new panorama js code if needed.
class CPanoramaMarshallHelper : public IUIMarshalHelper
{
public:
	virtual SFVALUEARRAY Params_GetArgs( SFPARAMS params ) OVERRIDE;
	virtual unsigned int Params_GetNumArgs( SFPARAMS params ) OVERRIDE;
	virtual bool Params_ArgIs( SFPARAMS params, unsigned int index, _ValueType v ) OVERRIDE;
	virtual SFVALUE Params_GetArg( SFPARAMS params, int index = 0 ) OVERRIDE;
	virtual _ValueType Params_GetArgType( SFPARAMS params, int index = 0 ) OVERRIDE;
	virtual double Params_GetArgAsNumber( SFPARAMS params, int index = 0 ) OVERRIDE;
	virtual bool Params_GetArgAsBool( SFPARAMS params, int index = 0 ) OVERRIDE;
	virtual const char* Params_GetArgAsString( SFPARAMS params, int index = 0 ) OVERRIDE;
	virtual const wchar_t* Params_GetArgAsStringW( SFPARAMS params, int index = 0 ) OVERRIDE;

	virtual void Params_DebugSpew( SFPARAMS params ) OVERRIDE;

	virtual void Params_SetResult( SFPARAMS params, SFVALUE value ) OVERRIDE;
	virtual void Params_SetResult( SFPARAMS params, int value ) OVERRIDE;
	virtual void Params_SetResult( SFPARAMS params, float value ) OVERRIDE;
	virtual void Params_SetResult( SFPARAMS params, bool value ) OVERRIDE;
	virtual void Params_SetResult( SFPARAMS params, const char* value, bool bMakeNewValue = true ) OVERRIDE;
	virtual void Params_SetResult( SFPARAMS params, const wchar_t* value, bool bMakeNewValue = true ) OVERRIDE;

	virtual SFVALUE Params_CreateNewObject( SFPARAMS params ) OVERRIDE;
	virtual SFVALUE Params_CreateNewString( SFPARAMS params, const char* value ) OVERRIDE;
	virtual SFVALUE Params_CreateNewString( SFPARAMS params, const wchar_t* value ) OVERRIDE;
	virtual SFVALUE Params_CreateNewArray( SFPARAMS params, int size = -1 ) OVERRIDE;
private:
	// Scratch storage for returning v8 string params. Grows to high water mark of param count. 
	CUtlVector< CUtlString > m_vecStringStorage;
};
CPanoramaMarshallHelper *GetPanoramaMarshallHelper( void );

template< typename ObjType >
void JSMethodCallTupleWithMarshallHelper( const v8::FunctionCallbackInfo<v8::Value>& args )
{
	v8::Isolate::Scope isolate_scope( args.GetIsolate() );
	v8::HandleScope handle_scope( args.GetIsolate() );

	ObjType *pPanel = panorama::GetThisPtrForJSCall<ObjType>( args.Holder() );
	if ( !pPanel )
		return;

	v8::Local<v8::Array> callbackArray = v8::Local<v8::Array>::Cast( args.Data() );

	panorama::HACKY_FUNC_PTR_CASTER< void ( ObjType::* )( IUIMarshalHelper* pui, SFPARAMS obj ) > caster;
	panorama::RestoreFuncPtr( caster, callbackArray, 0 );

	( pPanel->*caster.funcPtr )( GetPanoramaMarshallHelper(), ( SFPARAMS ) &args );
}

template < typename ObjType >
void RegisterJSMethodWithMarshallHelper( const char *pchMethodName, void ( ObjType::*mf )( IUIMarshalHelper* pui, SFPARAMS obj ), const char *pDesc = NULL )
{
	v8::Isolate::Scope isolate_scope( panorama::GetV8Isolate() );
	v8::HandleScope handle_scope( panorama::GetV8Isolate() );

	v8::Handle<v8::Array> callbackArray = v8::Array::New( panorama::GetV8Isolate(), V8_CALLBACK_ARRAY_SIZE );
	panorama::GetPtrToCallbackArray( mf, callbackArray );

	v8::Handle<v8::ObjectTemplate> objTemplate = panorama::UIEngine()->GetCurrentV8ObjectTemplateToSetup();
	objTemplate->Set( v8::String::NewFromUtf8( panorama::GetV8Isolate(), pchMethodName ), v8::FunctionTemplate::New( panorama::GetV8Isolate(), &JSMethodCallTupleWithMarshallHelper<ObjType>, callbackArray ) );

	int nEntry = panorama::UIEngine()->NewRegisterJSEntry( pchMethodName, panorama::RegisterJSEntryInfo_t::k_EMethod, pDesc, panorama::k_ERegisterJSTypeVoid );
	panorama::RegisterJSType_t pParamTypes[ 1 ] = { panorama::k_ERegisterJSTypeRawV8Args };
	panorama::UIEngine()->SetRegisterJSEntryParams( nEntry, 1, pParamTypes, NULL );
}

#else  // USE_PANORAMA_BINDINGS

#define PANORAMA_COMPONENT_API_DEF_BEGIN( componentclass )
#define PANORAMA_COMPONENT_FUNCTION_API_DEF( returntype, fnname, componentclass )
#define PANORAMA_COMPONENT_FUNCTION_API_DEF_DOC( returntype, fnname, componentclass, argNames, description )
#define PANORAMA_COMPONENT_FUNCTION_RAW_API_DEF( returntype, fnname, componentclass )
#define PANORAMA_COMPONENT_FUNCTION_RAW_API_DEF_DOC( returntype, fnname, componentclass, description )
#define PANORAMA_COMPONENT_API_DEF_END( componentclass )
#define PANORAMA_COMPONENT_API_INSTALL( componentclass, ptrInstance, scriptVarName, description )
#define PANORAMA_COMPONENT_API_REMOVE( componentclass )
#define PANORAMA_COMPONENT_EVENT_NAME( category, eventname ) 
#define PANORAMA_COMPONENT_BROADCAST_EVENT( category, eventname, ... )
#define PANORAMA_COMPONENT_BROADCAST_EVENT_WITH_PARAMS( category, eventname, ... )
#define PANORAMA_COMPONENT_DECLARE_EVENT0( category, eventname )
#define PANORAMA_COMPONENT_DECLARE_EVENT1( category, eventname, param1 )
#define PANORAMA_COMPONENT_DECLARE_EVENT2(category, eventname, param1, param2)
#define PANORAMA_COMPONENT_DECLARE_EVENT3(category, eventname, param1, param2, param3)
#define PANORAMA_COMPONENT_DECLARE_EVENT4(category, eventname, param1, param2, param3, param4)
#define PANORAMA_COMPONENT_DECLARE_EVENT5(category, eventname, param1, param2, param3, param4, param5)
#define PANORAMA_COMPONENT_DEFINE_EVENT(category, eventname)
#define PANORAMA_COMPONENT_MARSHALL_HELPER_FUNCTION_API_DEF( returntype, fnname, componentclass )
#define PANORAMA_COMPONENT_MARSHALL_HELPER_FUNCTION_API_DEF_NOPREFIX( returntype, fnname, componentclass )

#endif  // USE_PANORAMA_BINDINGS

// 7ls - revisit shutdown code
#define UI_COMPONENT_API_DEF_COMMON_DOC( componentclass, scriptVarName, description ) \
	void componentclass::InstallScaleformBindings(int iSlot) \
	{ \
		SF_COMPONENT_API_INSTALL(iSlot, componentclass, this, scriptVarName); \
	} \
	void componentclass::InstallPanoramaBindings() \
	{ \
		PANORAMA_COMPONENT_API_INSTALL(componentclass, this, #scriptVarName, description); \
	} \
	void componentclass::ShutdownComponentApiDef(int iSlot) \
	{ \
		SF_COMPONENT_API_REMOVE(iSlot, componentclass); \
		PANORAMA_COMPONENT_API_REMOVE(componentclass); \
	}

#define UI_COMPONENT_API_DEF_COMMON( componentclass, scriptVarName ) \
	UI_COMPONENT_API_DEF_COMMON_DOC( componentclass, scriptVarName, "" )

#define UI_COMPONENT_BROADCAST_EVENT( category, eventname, ... ) \
SF_COMPONENT_BROADCAST_EVENT( category, eventname, ##__VA_ARGS__ ) \
PANORAMA_COMPONENT_BROADCAST_EVENT( category, eventname, ##__VA_ARGS__ )

#define UI_COMPONENT_DECLARE_EVENT0( category, eventname ) \
PANORAMA_COMPONENT_DECLARE_EVENT0( category, eventname ) \
SF_COMPONENT_DECLARE_EVENT0( category, eventname )

#define UI_COMPONENT_DECLARE_EVENT1( category, eventname, param1_name, param1 ) \
PANORAMA_COMPONENT_DECLARE_EVENT1( category, eventname, param1 ) \
SF_COMPONENT_DECLARE_EVENT1( category, eventname, param1_name, param1 )

#define UI_COMPONENT_DECLARE_EVENT2( category, eventname, param1_name, param1, param2_name, param2 ) \
PANORAMA_COMPONENT_DECLARE_EVENT2( category, eventname, param1, param2 ) \
SF_COMPONENT_DECLARE_EVENT2( category, eventname, param1_name, param1, param2_name, param2 )

#define UI_COMPONENT_DECLARE_EVENT3( category, eventname, param1_name, param1, param2_name, param2, param3_name, param3 ) \
PANORAMA_COMPONENT_DECLARE_EVENT3( category, eventname, param1, param2, param3 ) \
SF_COMPONENT_DECLARE_EVENT3( category, eventname, param1_name, param1, param2_name, param2, param3_name, param3 )

#define UI_COMPONENT_DECLARE_EVENT4( category, eventname, param1_name, param1, param2_name, param2, param3_name, param3, param4_name, param4 ) \
PANORAMA_COMPONENT_DECLARE_EVENT4( category, eventname, param1, param2, param3, param4 ) \
SF_COMPONENT_DECLARE_EVENT4( category, eventname, param1_name, param1, param2_name, param2, param3_name, param3, param4_name, param4 )

#define UI_COMPONENT_DECLARE_EVENT5( category, eventname, param1_name, param1, param2_name, param2, param3_name, param3, param4_name, param4, param5_name, param5 ) \
PANORAMA_COMPONENT_DECLARE_EVENT5( category, eventname, param1, param2, param3, param4, param5 ) \
SF_COMPONENT_DECLARE_EVENT5( category, eventname, param1_name, param1, param2_name, param2, param3_name, param3, param4_name, param4, param5_name, param5 )

#define UI_COMPONENT_DEFINE_EVENT( category, eventname )\
PANORAMA_COMPONENT_DEFINE_EVENT( category, eventname )\
SF_COMPONENT_DEFINE_EVENT( category, eventname )


// Only register Panorama cplusplus event handlers if not using Scaleform
// (Otherwise handlers will be called twice) 
#if defined (USE_PANORAMA_BINDINGS )

#define UI_COMPONENT_REGISTER_EVENT_HANDLER(category, eventname, pObj, methodptr) \
	RegisterForUnhandledEvent( PANORAMA_COMPONENT_EVENT_NAME(category, eventname)(), pObj, methodptr );

#else

#define UI_COMPONENT_REGISTER_EVENT_HANDLER(category, eventname, pObj, methodptr)

#endif


// Registration of GC job handler for ui component
#define UI_COMPONENT_GC_REG_JOB( gcclientclass, cjobclass, cmsgclass, gcmsgid, ccomponentclass, ccomponentfn ) \
class cjobclass : public GCSDK::CGCClientJob \
{ \
public: \
	explicit cjobclass( GCSDK::CGCClient *pGCClient ) : GCSDK::CGCClientJob( pGCClient ) { } \
	virtual bool BYieldingRunJobFromMsg( GCSDK::IMsgNetPacket *pNetPacket ) \
				{ \
	GCSDK::CProtoBufMsg<cmsgclass> msg( pNetPacket ); \
	ccomponentclass::GetInstance()->ccomponentfn( msg.Body() ); \
	return true; \
				} \
}; \
GC_REG_CLIENT_JOB( cjobclass, gcmsgid );


// Function decl/impl helpers for ui component code that will be called by scaleform and/or javascript

// Convert keyvalues to a json formatted string
bool Helper_RecursiveKeyValuesToJSONString( const KeyValues* pKV, CUtlBuffer &outBuffer );

#endif // UICOMPONENT_COMMON_H
