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

	auto GetMinimumPhysicalAttackDamage( int MinimumRawDamage , C_DOTA_BaseNPC* pTarget , CEntityIdentity* pTargetIdentity ) -> int
	{
		const auto RawDamage = static_cast<float>( ( std::max )( 0 , MinimumRawDamage ) );
		const auto Armor = pTarget->m_flPhysicalArmorValue();
		const auto ArmorMultiplier = 1.f - ( 0.06f * Armor ) / ( 1.f + 0.06f * std::abs( Armor ) );

		auto UnitMultiplier = 1.f;
		const auto szUnitName = pTargetIdentity->DesingerName().String();

		if ( szUnitName && std::strstr( szUnitName , "siege" ) )
			UnitMultiplier = 0.5f;

		return ( std::max )( 0 , static_cast<int>( std::floor( RawDamage * ArmorMultiplier * UnitMultiplier ) ) );
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
	const auto MinimumRawDamage = ( std::max )( 0 , pLocalHero->GetDamageMin() );
	const auto DisplaySize = ImGui::GetIO().DisplaySize;
	auto pDrawList = ImGui::GetBackgroundDrawList();
	auto EntityCount = 0;
	auto CreepCount = 0;
	auto TargetCount = 0;
	auto OnScreenCount = 0;
	auto RedCount = 0;
	auto YellowCount = 0;
	auto GreenCount = 0;
	auto DrawnCount = 0;

	for ( auto EntityIndex = 0; EntityIndex < MAX_TOTAL_ENTITIES; ++EntityIndex )
	{
		auto pEntity = pEntitySystem->GetBaseEntity<C_BaseEntity>( EntityIndex );

		if ( !pEntity )
			continue;

		++EntityCount;

		if ( pEntity == pLocalHero || !IsClassOrDerivedFrom( pEntity , XorStr( "C_DOTA_BaseNPC_Creep" ) ) )
			continue;

		++CreepCount;

		auto pIdentity = pEntity->pEntityIdentity();

		if ( !pIdentity || ( pIdentity->m_flags() & DORMANT_ENTITY_FLAG ) != 0 )
			continue;

		auto pCreep = reinterpret_cast<C_DOTA_BaseNPC*>( pEntity );

		if ( pCreep->m_lifeState() != 0 || pCreep->m_iHealth() <= 0 || pCreep->m_iTeamNum() == LocalTeam )
			continue;

		++TargetCount;

		const auto AttackDamage = GetMinimumPhysicalAttackDamage( MinimumRawDamage , pCreep , pIdentity );
		auto pSceneNode = pCreep->m_pGameSceneNode();

		if ( !pSceneNode )
			continue;

		auto MarkerPosition = pSceneNode->m_vecAbsOrigin();

		if ( !std::isfinite( MarkerPosition.m_x ) || !std::isfinite( MarkerPosition.m_y ) || !std::isfinite( MarkerPosition.m_z ) )
			continue;

		MarkerPosition.m_z += ( std::max )( 100.f , static_cast<float>( pCreep->m_iHealthBarOffset() ) + 15.f );

		ImVec2 ScreenPosition{};

		if ( !Math::WorldToScreen( MarkerPosition , ScreenPosition ) )
			continue;

		if ( ScreenPosition.x < -16.f || ScreenPosition.y < -16.f || ScreenPosition.x > DisplaySize.x + 16.f || ScreenPosition.y > DisplaySize.y + 16.f )
			continue;

		++OnScreenCount;

		const auto CreepHealth = pCreep->m_iHealth();
		const auto YellowMargin = ( std::max )( 10 , static_cast<int>( std::ceil( static_cast<float>( AttackDamage ) * 0.2f ) ) );
		const auto YellowThreshold = AttackDamage + YellowMargin;
		auto MarkerColor = IM_COL32( 230 , 65 , 65 , 245 );

		if ( AttackDamage > 0 && CreepHealth <= AttackDamage )
		{
			MarkerColor = IM_COL32( 80 , 225 , 95 , 245 );
			++GreenCount;
		}
		else if ( AttackDamage > 0 && CreepHealth <= YellowThreshold )
		{
			MarkerColor = IM_COL32( 245 , 195 , 45 , 245 );
			++YellowCount;
		}
		else
		{
			++RedCount;
		}

		ScreenPosition.y -= 6.f;

		const ImVec2 LeftPoint( ScreenPosition.x - 6.f , ScreenPosition.y + 8.f );
		const ImVec2 RightPoint( ScreenPosition.x + 6.f , ScreenPosition.y + 8.f );
		const ImVec2 BottomPoint( ScreenPosition.x , ScreenPosition.y + 15.f );
		const ImVec2 TextSize = ImGui::CalcTextSize( XorStr( "LH" ) );
		const ImVec2 TextPosition( ScreenPosition.x - TextSize.x * 0.5f , ScreenPosition.y - TextSize.y * 0.5f );

		pDrawList->AddCircleFilled( ScreenPosition , 12.f , IM_COL32( 10 , 10 , 10 , 245 ) , 24 );
		pDrawList->AddCircleFilled( ScreenPosition , 9.f , MarkerColor , 24 );
		pDrawList->AddTriangleFilled( LeftPoint , RightPoint , BottomPoint , IM_COL32( 10 , 10 , 10 , 245 ) );
		pDrawList->AddTriangleFilled( ImVec2( LeftPoint.x + 2.f , LeftPoint.y ) , ImVec2( RightPoint.x - 2.f , RightPoint.y ) , ImVec2( BottomPoint.x , BottomPoint.y - 3.f ) , MarkerColor );
		pDrawList->AddText( TextPosition , IM_COL32( 10 , 10 , 10 , 255 ) , XorStr( "LH" ) );
		++DrawnCount;
	}

	static auto NextDiagnosticTime = 0ull;
	const auto CurrentTime = GetTickCount64();

	if ( CurrentTime >= NextDiagnosticTime )
	{
		DEV_LOG( "[LastHitMarker] total_min_damage=%i entities=%i creeps=%i targets=%i on_screen=%i red=%i yellow=%i green=%i drawn=%i\n" , MinimumRawDamage , EntityCount , CreepCount , TargetCount , OnScreenCount , RedCount , YellowCount , GreenCount , DrawnCount );
		NextDiagnosticTime = CurrentTime + 3000ull;
	}
}

auto CAndromedaClient::OnCreateMove( CDOTAInput* pCDOTAInput , CUserCmd* pCUserCmd ) -> void
{

}

auto GetAndromedaClient() -> CAndromedaClient*
{
	return &g_CAndromedaClient;
}
