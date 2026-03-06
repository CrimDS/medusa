/*
	FactoryConcrete.h
	(c)2005 Palestar, Richard Lyle
*/

#ifndef FACTORY_CONCRETE_H
#define FACTORY_CONCRETE_H

#include "Debug/Trace.h"
#include "Reflection/TypeTemplate.h"
#include "Factory.h"

#include "MedusaDll.h"

#include <new>
#include <malloc.h>

//-------------------------------------------------------------------------------

template<class T>
class FactoryConcrete : public Factory
{
public:
	// Construction
	FactoryConcrete( const char * pName, Factory * a_pBase, bool a_bSerializeKey );

	// Factory interface
	virtual Widget *		createWidget();
	virtual Widget *		createWidget( const InStream &input );
	virtual bool			isAbstract();
};

//---------------------------------------------------------------------------------------------------

template<class T>
inline FactoryConcrete<T>::FactoryConcrete(const char * pName, Factory * a_pBase, bool a_bSerializeKey ) 
	: Factory( pName, a_pBase, a_bSerializeKey, TypeAbstract<T>::instance()->typeId() )
{}

// Disable C4316 warnings in these allocation helpers - we explicitly handle overaligned types
#pragma warning(push)
#pragma warning(disable:4316)

template<class T>
inline Widget * FactoryConcrete<T>::createWidget() 
{
#if defined(_MSC_VER)
	const size_t alignment = alignof(T);
	if ( alignment >= 16 )
	{
		void * mem = _aligned_malloc( sizeof(T), alignment );
		if ( mem == NULL )
			return nullptr;
		T * pWidget = ::new (mem) T();
		return pWidget;
	}
	else
	{
		T * pWidget = new T();
		return pWidget;
	}
#else
	T * pWidget = new T();
	return pWidget;
#endif
}

template<class T>
inline Widget * FactoryConcrete<T>::createWidget( const InStream &input ) 
{
#if defined(_MSC_VER)
	const size_t alignment = alignof(T);
	if ( alignment >= 16 )
	{
		void * mem = _aligned_malloc( sizeof(T), alignment );
		if ( mem == NULL )
			return nullptr;
		T * pWidget = ::new (mem) T();
		pWidget->read( input );
		return pWidget;
	}
	else
	{
		T * pWidget = new T();
		pWidget->read( input );
		return pWidget;
	}
#else
	T * pWidget = new T();
	pWidget->read( input );
	return pWidget;
#endif
}

#pragma warning(pop)


template<class T>
inline bool FactoryConcrete<T>::isAbstract() 
{
	return( false );
}


//-------------------------------------------------------------------------------



#endif

//-------------------------------------------------------------------------------
// EOF
