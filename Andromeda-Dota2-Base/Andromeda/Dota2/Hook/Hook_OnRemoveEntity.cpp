#include "Hook_OnRemoveEntity.hpp"

#include <Dota2/SDK/Types/CEntityData.hpp>

auto Hook_OnRemoveEntity( CGameEntitySystem* pCGameEntitySystem , CEntityInstance* pInst , CHandle handle ) -> void
{
	if ( pInst )
	{
		auto pIdentity = pInst->pEntityIdentity();
		auto pBinding = pInst->GetSchemaClassBinding();
		const char* szBindingName = pBinding ? pBinding->m_bindingName() : "Unknown";
		DEV_LOG( "Hook_OnRemoveEntity: %p , %s\n" , pIdentity , szBindingName ? szBindingName : "Unknown" );
	}

	return OnRemoveEntity_o( pCGameEntitySystem , pInst , handle );
}
