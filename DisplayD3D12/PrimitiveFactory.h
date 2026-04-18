/*
	PrimitiveFactory.h - D3D12 version
	(c)2024 Palestar
*/

#ifndef PRIMITIVE_FACTORY_D3D12_H
#define PRIMITIVE_FACTORY_D3D12_H

#include "Debug/Assert.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

#include <malloc.h>

//---------------------------------------------------------------------------------------

class BasePrimitiveFactory12
{
public:
	class KeyConflict {};

	BasePrimitiveFactory12( const PrimitiveKey &key );
	virtual ~BasePrimitiveFactory12();

	virtual DevicePrimitive *		create() = 0;
	virtual void				release() = 0;

	static BasePrimitiveFactory12 *	findFactory( const PrimitiveKey &key );

private:
	PrimitiveKey				m_Key;
};

//---------------------------------------------------------------------------------------

template<class T>
class PrimitiveFactory12 : public BasePrimitiveFactory12
{
public:
	class PrimitiveProxy : public T
	{
	public:
		PrimitiveProxy( PrimitiveFactory12 * pFactory ) : m_pFactory( pFactory )
		{}

		void recache()
		{
			m_pFactory->m_Cache.insert( this );
			release();
		}
	private:
		PrimitiveFactory12 *	m_pFactory;
	};

	PrimitiveFactory12() : BasePrimitiveFactory12( T::staticPrimitiveKey() )
	{}
	virtual ~PrimitiveFactory12()
	{
		release();
	}

	#pragma warning(push)
	#pragma warning(disable:4316)
	DevicePrimitive * create()
	{
		List< T * >::Iterator primitive = m_Cache.head();
		if ( primitive.valid() )
		{
			DevicePrimitive * pPrimitive = *primitive;
			ASSERT( pPrimitive );
			m_Cache.remove( primitive );
			return( pPrimitive );
		}
#if defined(_MSC_VER)
		const size_t alignment = alignof(PrimitiveProxy);
		if ( alignment >= 16 )
		{
			void * mem = _aligned_malloc( sizeof(PrimitiveProxy), alignment );
			if ( mem == NULL )
				return nullptr;
			PrimitiveProxy * p = ::new (mem) PrimitiveProxy( this );
			return p;
		}
		else
		{
			return( new PrimitiveProxy( this ) );
		}
#else
		return( new PrimitiveProxy( this ) );
#endif
	}
	#pragma warning(pop)

	void release()
	{
		List< T * >::Iterator primitive = m_Cache.head();
		while( primitive.valid() )
		{
#if defined(_MSC_VER)
			const size_t alignment = alignof(PrimitiveProxy);
			if ( alignment >= 16 )
			{
				PrimitiveProxy * p = static_cast<PrimitiveProxy *>( *primitive );
				p->~PrimitiveProxy();
				_aligned_free( p );
			}
			else
			{
				delete *primitive;
			}
#else
			delete *primitive;
#endif
			primitive++;
		}
		m_Cache.release();
	}

private:
	List< T * >		m_Cache;
	friend class PrimitiveProxy;
};

//---------------------------------------------------------------------------------------

#define IMPLEMENT_PRIMITIVE_FACTORY_D3D12( thisClass )				\
	static PrimitiveFactory12<thisClass> g_##thisClass##Factory;

//---------------------------------------------------------------------------------------

#endif

//---------------------------------------------------------------------------------------
// EOF
