#include "CAndromedaClient.hpp"
#include "CAndromedaGUI.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <ImGui/imgui.h>

#include <Dota2/SDK/SDK.hpp>
#include <Dota2/SDK/Interface/CGameEntitySystem.hpp>
#include <Dota2/SDK/Interface/IVEngineClient2.hpp>
#include <Dota2/SDK/Math/Math.hpp>

#include <AndromedaClient/Fonts/CFontManager.hpp>
#include <AndromedaClient/GUI/CAndromedaMenu.hpp>
#include <AndromedaClient/Settings/Settings.hpp>

static CAndromedaClient g_CAndromedaClient{};

namespace
{
	constexpr auto DORMANT_ENTITY_FLAG = 1u << 7;

	auto IsClassOrDerivedFrom( CEntityInstance* pEntity , const char* szClassName ) -> bool
	{
		if ( !pEntity || !szClassName )
			return false;

		auto pBinding = pEntity->GetSchemaClassBinding();

		for ( auto Depth = 0; pBinding && Depth < 32; ++Depth )
		{
			const auto szBindingName = pBinding->m_bindingName();

			if ( szBindingName && std::strcmp( szBindingName , szClassName ) == 0 )
				return true;

			auto pBaseClass = pBinding->m_baseClass();
			pBinding = pBaseClass ? pBaseClass->m_classInfo() : nullptr;
		}

		return false;
	}

	auto IsResolvedHandleValid( CHandle Handle , C_BaseEntity* pEntity ) -> bool
	{
		if ( !Handle.IsValid() || !pEntity )
			return false;

		auto pIdentity = pEntity->pEntityIdentity();
		return pIdentity && pIdentity->Handle().m_Index == Handle.m_Index;
	}

	auto GetMinimumPhysicalAttackDamage( C_DOTA_BaseNPC_Hero* pHero , C_DOTA_BaseNPC* pTarget , CEntityIdentity* pTargetIdentity ) -> int
	{
		const auto RawDamage = static_cast<float>( std::max( 0 , pHero->m_iDamageMin() + pHero->m_iDamageBonus() ) );
		const auto Armor = pTarget->m_flPhysicalArmorValue();
		const auto ArmorMultiplier = 1.f - ( 0.06f * Armor ) / ( 1.f + 0.06f * std::abs( Armor ) );

		auto UnitMultiplier = 1.f;
		const auto szUnitName = pTargetIdentity->DesingerName().String();

		if ( szUnitName && std::strstr( szUnitName , "siege" ) )
			UnitMultiplier = 0.5f;

		return std::max( 0 , static_cast<int>( std::floor( RawDamage * ArmorMultiplier * UnitMultiplier ) ) );
	}
}

auto CAndromedaClient::OnInit() -> void
{
	auto LocalPLayer = -1;
	auto Width = 0;
	auto Height = 0;

	SDK::Interfaces::EngineToClient()->GetLocalPlayer( LocalPLayer );
	SDK::Interfaces::EngineToClient()->GetScreenSize( Width , Height );

	DEV_LOG( "%i , %i , %i\n" , LocalPLayer , Width , Height );

	if ( dota_camera_distance.Search() )
		DEV_LOG( "[dota_camera_distance] Found !\n" );

	if ( dota_camera_fog_end.Search() )
		DEV_LOG( "[dota_camera_fog_end] Found !\n" );

	if ( dota_camera_farplane.Search() )
		DEV_LOG( "[dota_camera_farplane] Found !\n" );

	if ( Math::Init() )
		DEV_LOG( "[WorldToScreen] Found !\n" );

}

auto CAndromedaClient::SetCameraDistance( float Distance ) -> void
{
	static float* force_dota_camera_distance = reinterpret_cast<float*>( dota_camera_distance.GetFunction() );
	static float* force_dota_camera_fog_end = reinterpret_cast<float*>( dota_camera_fog_end.GetFunction() );
	static float* force_dota_camera_farplane = reinterpret_cast<float*>( dota_camera_farplane.GetFunction() );

	if( force_dota_camera_distance )
		*force_dota_camera_distance = Distance;

	if ( force_dota_camera_fog_end )
		*force_dota_camera_fog_end = 10000.f;

	if ( force_dota_camera_farplane )
		*force_dota_camera_farplane = 10000.f;
}

auto CAndromedaClient::OnRender() -> void
{
	if ( Settings::Visuals::LastHitMarker )
		RenderLastHitMarkers();

	if ( GetAndromedaGUI()->IsVisible() )
		GetAndromedaMenu()->OnRenderMenu();

	GetFontManager()->FirstInitFonts();
	GetFontManager()->m_VerdanaFont.DrawString( 1 , 1 , ImColor( 255 , 255 , 0 ) , FW1_LEFT , XorStr( CHEAT_NAME ) );
}

auto CAndromedaClient::RenderLastHitMarkers() -> void
{
	auto pEntitySystem = SDK::Interfaces::GameEntitySystem();

	if ( !pEntitySystem || !ImGui::GetCurrentContext() )
		return;

	auto pLocalController = CGameEntitySystem::GetLocalPlayerController();

	if ( !pLocalController )
		return;

	const auto LocalHeroHandle = pLocalController->m_hAssignedHero();
	auto pLocalHero = LocalHeroHandle.Get<C_DOTA_BaseNPC_Hero>();

	if ( !IsResolvedHandleValid( LocalHeroHandle , pLocalHero ) )
		return;

	const auto LocalTeam = pLocalHero->m_iTeamNum();
	const auto HighestEntityIndex = std::min( std::max( pEntitySystem->GetHighestEntityIndex() , 0 ) , MAX_TOTAL_ENTITIES - 1 );
	const auto DisplaySize = ImGui::GetIO().DisplaySize;
	auto pDrawList = ImGui::GetBackgroundDrawList();

	for ( auto EntityIndex = 0; EntityIndex <= HighestEntityIndex; ++EntityIndex )
	{
		auto pEntity = pEntitySystem->GetBaseEntity<C_BaseEntity>( EntityIndex );

		if ( !pEntity || pEntity == pLocalHero || !IsClassOrDerivedFrom( pEntity , XorStr( "C_DOTA_BaseNPC_Creep" ) ) )
			continue;

		auto pIdentity = pEntity->pEntityIdentity();

		if ( !pIdentity || ( pIdentity->m_flags() & DORMANT_ENTITY_FLAG ) != 0 )
			continue;

		auto pCreep = reinterpret_cast<C_DOTA_BaseNPC*>( pEntity );

		if ( pCreep->m_lifeState() != 0 || pCreep->m_iHealth() <= 0 || pCreep->m_iTeamNum() == LocalTeam )
			continue;

		const auto AttackDamage = GetMinimumPhysicalAttackDamage( pLocalHero , pCreep , pIdentity );

		if ( AttackDamage <= 0 || pCreep->m_iHealth() > AttackDamage )
			continue;

		auto pSceneNode = pCreep->m_pGameSceneNode();

		if ( !pSceneNode )
			continue;

		auto MarkerPosition = pSceneNode->m_vecAbsOrigin();

		if ( !std::isfinite( MarkerPosition.m_x ) || !std::isfinite( MarkerPosition.m_y ) || !std::isfinite( MarkerPosition.m_z ) )
			continue;

		MarkerPosition.m_z += std::max( 100.f , static_cast<float>( pCreep->m_iHealthBarOffset() ) + 15.f );

		ImVec2 ScreenPosition{};

		if ( !Math::WorldToScreen( MarkerPosition , ScreenPosition ) )
			continue;

		if ( ScreenPosition.x < -16.f || ScreenPosition.y < -16.f || ScreenPosition.x > DisplaySize.x + 16.f || ScreenPosition.y > DisplaySize.y + 16.f )
			continue;

		ScreenPosition.y -= 6.f;

		const ImVec2 LeftPoint( ScreenPosition.x - 7.f , ScreenPosition.y - 7.f );
		const ImVec2 RightPoint( ScreenPosition.x + 7.f , ScreenPosition.y - 7.f );
		const ImVec2 BottomPoint( ScreenPosition.x , ScreenPosition.y + 6.f );

		pDrawList->AddTriangleFilled( LeftPoint , RightPoint , BottomPoint , IM_COL32( 255 , 210 , 45 , 240 ) );
		pDrawList->AddTriangle( LeftPoint , RightPoint , BottomPoint , IM_COL32( 15 , 15 , 15 , 245 ) , 2.f );
	}
}

auto CAndromedaClient::OnCreateMove( CDOTAInput* pCDOTAInput , CUserCmd* pCUserCmd ) -> void
{

}

auto GetAndromedaClient() -> CAndromedaClient*
{
	return &g_CAndromedaClient;
}
