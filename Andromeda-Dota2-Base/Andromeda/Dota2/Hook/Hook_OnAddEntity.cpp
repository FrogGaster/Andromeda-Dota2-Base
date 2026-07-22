#include "Hook_OnAddEntity.hpp"

#include <Dota2/SDK/Types/CEntityData.hpp>

auto Hook_OnAddEntity( CGameEntitySystem* pCGameEntitySystem , CEntityInstance* pInst , CHandle handle ) -> void
{
	if ( pInst )
	{
		auto pIdentity = pInst->pEntityIdentity();
		auto pBinding = pInst->GetSchemaClassBinding();
		const char* szBindingName = pBinding ? pBinding->m_bindingName() : "Unknown";
		DEV_LOG( "Hook_OnAddEntity: %p , %s\n" , pIdentity , szBindingName ? szBindingName : "Unknown" );
	}

	return OnAddEntity_o( pCGameEntitySystem , pInst , handle );
}
