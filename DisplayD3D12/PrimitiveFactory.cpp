/*
	PrimitiveFactory.cpp - D3D12 version
	(c)2024 Palestar
*/

#include "PrimitiveFactory.h"
#include <map>

//---------------------------------------------------------------------------------------------------

static std::map< PrimitiveKey, BasePrimitiveFactory12 * > & getFactoryHash()
{
	static std::map< PrimitiveKey, BasePrimitiveFactory12 * > s_Hash;
	return s_Hash;
}

BasePrimitiveFactory12::BasePrimitiveFactory12( const PrimitiveKey & key ) : m_Key( key )
{
	std::map< PrimitiveKey, BasePrimitiveFactory12 * > & hash = getFactoryHash();
	if ( hash.find( key ) != hash.end() )
		return;		// already registered, don't throw - DX9 factory may have registered it
	hash[ key ] = this;
}

BasePrimitiveFactory12::~BasePrimitiveFactory12()
{
	std::map< PrimitiveKey, BasePrimitiveFactory12 * > & hash = getFactoryHash();
	std::map< PrimitiveKey, BasePrimitiveFactory12 * >::iterator it = hash.find( m_Key );
	if ( it != hash.end() && it->second == this )
		hash.erase( it );
}

BasePrimitiveFactory12 * BasePrimitiveFactory12::findFactory( const PrimitiveKey & key )
{
	std::map< PrimitiveKey, BasePrimitiveFactory12 * > & hash = getFactoryHash();
	std::map< PrimitiveKey, BasePrimitiveFactory12 * >::iterator it = hash.find( key );
	if ( it != hash.end() )
		return it->second;
	return NULL;
}

//---------------------------------------------------------------------------------------------------
// EOF
