/**
	@file DisplayEffect.h
	@brief This is the base class for any rendering effects (e.g. Bloom, Blur, Depth of field, etc..)

	(c)2009 Palestar Inc
	@author Richard Lyle @date 9/5/2010 2:50:46 PM
*/

#ifndef DISPLAYEFFECT_H
#define DISPLAYEFFECT_H

#include "Factory/FactoryTypes.h"
#include "Standard/WeakReference.h"
#include "MedusaDll.h"

class DisplayDevice;		// forward declare

//---------------------------------------------------------------------------------------------------

class DLL DisplayEffect : public Widget
{
public:
	DECLARE_WIDGET_CLASS();

	//! Types
	typedef Reference< DisplayEffect >		Ref;
	typedef WeakReference< DisplayEffect >	WeakRef;

	// Construction
	DisplayEffect() 
	{}
	virtual ~DisplayEffect()
	{}

	//! Interface
	virtual bool				preRender( DisplayDevice * pDevice ) = 0;		// This is invoked before we render our scene
	virtual bool				postRender( DisplayDevice * pDevice ) = 0;		// This is invoked after the scene and all passes have been rendered.
	virtual void				release() = 0;									// when called the effect should release all allocated resources

	// Called by the device immediately before it tears down its descriptor
	// heaps.  D3D12 effects override to return their RTV/SRV slots back to
	// the device heaps (otherwise the slots leak) AND null out any cached
	// device pointer so a later destructor doesn't try to free into a dead
	// heap.  Default no-op for backends (D3D9) that don't track explicit
	// descriptor heaps.
	//
	// IMPORTANT: definition lives in DisplayEffect.cpp, not inline here.
	// `class DLL` marks this `__declspec(dllimport)` for consumers across the
	// DLL boundary (e.g. DisplayD3D12.dll → Medusa.dll).  A derived class
	// that doesn't override (DisplayEffectBlur, etc.) emits a vtable slot
	// referencing `__imp_?onDeviceShutdown@DisplayEffect@@UEAAXXZ`, which the
	// linker expects to find as an exported symbol from Medusa.dll.  An
	// inline body in the header doesn't generate an exported definition —
	// the .cpp body does.
	virtual void				onDeviceShutdown();
};

#endif

//---------------------------------------------------------------------------------------------------
//EOF
