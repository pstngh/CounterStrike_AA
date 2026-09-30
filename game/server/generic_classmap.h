//========= Copyright © 1996-2009, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef ASW_GENERIC_CLASSMAP_H
#define ASW_GENERIC_CLASSMAP_H
#ifdef _WIN32
#pragma once
#endif

template <class T>
class CGenericClassmap
{
public:
	typedef T *( *DISPATCHFUNCTION )( void );

				CGenericClassmap() { }
				~CGenericClassmap() { }

	void		Add( const char *mapname, const char *classname, int size, DISPATCHFUNCTION factory = 0 );
	char const	*Lookup( const char *classname );
	T			*CreateInstance( const char *mapname );
	int			GetClassSize( const char *classname );

private:
	class classentry_t
	{
	public:
		classentry_t()
		{
			mapname[ 0 ] = 0;
			factory = 0;
			size = -1;
		}

		char const *GetMapName() const
		{
			return mapname;
		}

		void SetMapName( char const *newname )
		{
			Q_strncpy( mapname, newname, sizeof( mapname ) );
		}

		DISPATCHFUNCTION	factory;
		int					size;
	private:
		char				mapname[ 40 ];
	};

	CUtlDict< classentry_t, unsigned short > m_ClassDict;
};

template <class T>
void CGenericClassmap< T >::Add( const char *mapname, const char *classname, int size, DISPATCHFUNCTION factory )
{
	const char *map = Lookup( classname );
	if ( map && !Q_strcasecmp( mapname, map ) )
		return;

	if ( map )
	{
		int index = m_ClassDict.Find( classname );
		Assert( index != m_ClassDict.InvalidIndex() );
		m_ClassDict.RemoveAt( index );
	}

	classentry_t element;
	element.SetMapName( mapname );
	element.factory = factory;
	element.size = size;
	m_ClassDict.Insert( classname, element );
}

template <class T>
const char *CGenericClassmap< T >::Lookup( const char *classname )
{
	unsigned short index;
	static classentry_t lookup; 

	index = m_ClassDict.Find( classname );
	if ( index == m_ClassDict.InvalidIndex() )
		return NULL;

	lookup = m_ClassDict.Element( index );
	return lookup.GetMapName();
}

template <class T>
T *CGenericClassmap< T >::CreateInstance( const char *mapname )
{
	int c = m_ClassDict.Count();
	int i;

	for ( i = 0; i < c; i++ )
	{
		classentry_t *lookup = &m_ClassDict[ i ];
		if ( !lookup )
			continue;

		if ( Q_stricmp( lookup->GetMapName(), mapname ) )
			continue;

		if ( !lookup->factory )
		{
#if defined( _DEBUG )
			Msg( "No factory for %s/%s\n", lookup->GetMapName(), m_ClassDict.GetElementName( i ) );
#endif
			continue;
		}

		return ( *lookup->factory )();
	}

	return NULL;
}

template <class T>
int CGenericClassmap< T >::GetClassSize( const char *classname )
{
	int c = m_ClassDict.Count();
	int i;

	for ( i = 0; i < c; i++ )
	{
		classentry_t *lookup = &m_ClassDict[ i ];
		if ( !lookup )
			continue;

		if ( Q_strcmp( lookup->GetMapName(), classname ) )
			continue;

		return lookup->size;
	}

	return -1;
}

#endif // ASW_GENERIC_CLASSMAP_H
