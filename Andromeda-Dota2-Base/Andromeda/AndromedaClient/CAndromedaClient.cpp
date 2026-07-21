#include "CAndromedaClient.hpp"
#include "CAndromedaGUI.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

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
	constexpr auto DOTA_TEAM_RADIANT = 2;
	constexpr auto DOTA_TEAM_DIRE = 3;
	constexpr auto VISION_SOURCE_DISTANCE_PADDING = 96.f;

	enum class EVisionWarningSource : uint8_t
	{
		None,
		Hero,
		Ward,
		HeroAndWard,
	};

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

	auto IsObserverWard( CEntityInstance* pEntity , CEntityIdentity* pIdentity ) -> bool
	{
		if ( IsClassOrDerivedFrom( pEntity , XorStr( "CDOTA_NPC_Observer_Ward" ) ) ||
			IsClassOrDerivedFrom( pEntity , XorStr( "C_DOTA_NPC_Observer_Ward" ) ) )
		{
			return true;
		}

		const auto szUnitName = pIdentity ? pIdentity->DesingerName().String() : nullptr;
		return szUnitName && std::strcmp( szUnitName , XorStr( "npc_dota_observer_wards" ) ) == 0;
	}

	auto IsInsideVisionRange( const Vector3& SourcePosition , const Vector3& TargetPosition , int VisionRange ) -> bool
	{
		if ( VisionRange <= 0 )
			return false;

		const auto DeltaX = SourcePosition.m_x - TargetPosition.m_x;
		const auto DeltaY = SourcePosition.m_y - TargetPosition.m_y;
		const auto PaddedVisionRange = static_cast<float>( VisionRange ) + VISION_SOURCE_DISTANCE_PADDING;
		return DeltaX * DeltaX + DeltaY * DeltaY <= PaddedVisionRange * PaddedVisionRange;
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

	if ( dota_npc_get_current_vision_range.Search() )
		DEV_LOG( "[dota_npc_get_current_vision_range] Found !\n" );

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

	if ( Settings::Visuals::EnemyVisionWarning )
		RenderEnemyVisionWarning();

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

auto CAndromedaClient::RenderEnemyVisionWarning() -> void
{
	auto pEntitySystem = SDK::Interfaces::GameEntitySystem();

	if ( !pEntitySystem || !ImGui::GetCurrentContext() )
		return;

	auto pLocalController = CGameEntitySystem::GetLocalPlayerController();

	if ( !pLocalController )
		return;

	const auto LocalHeroHandle = pLocalController->m_hAssignedHero();
	auto pLocalHero = LocalHeroHandle.Get<C_DOTA_BaseNPC_Hero>();

	if ( !IsResolvedHandleValid( LocalHeroHandle , pLocalHero ) || pLocalHero->m_lifeState() != 0 )
		return;

	const auto LocalTeam = static_cast<int>( pLocalHero->m_iTeamNum() );
	const auto EnemyTeam = LocalTeam == DOTA_TEAM_RADIANT ? DOTA_TEAM_DIRE : LocalTeam == DOTA_TEAM_DIRE ? DOTA_TEAM_RADIANT : 0;

	if ( EnemyTeam == 0 )
		return;

	static auto CachedSource = EVisionWarningSource::None;
	static auto PreviousSource = EVisionWarningSource::None;
	static auto NextVisionCheckTime = 0ull;
	static auto NotificationEndTime = 0ull;
	static auto NextDiagnosticTime = 0ull;
	const auto CurrentTime = GetTickCount64();

	if ( CurrentTime >= NextVisionCheckTime )
	{
		NextVisionCheckTime = CurrentTime + 100ull;
		CachedSource = EVisionWarningSource::None;

		auto HeroInRange = false;
		auto WardInRange = false;
		auto HeroCount = 0;
		auto WardCount = 0;
		auto pLocalSceneNode = pLocalHero->m_pGameSceneNode();
		using GetCurrentVisionRangeFn = int( __fastcall* )( C_DOTA_BaseNPC* );
		auto GetCurrentVisionRange = reinterpret_cast<GetCurrentVisionRangeFn>( dota_npc_get_current_vision_range.GetFunction() );

		if ( pLocalSceneNode )
		{
			const auto LocalPosition = pLocalSceneNode->m_vecAbsOrigin();

			for ( auto EntityIndex = 0; EntityIndex < MAX_TOTAL_ENTITIES && !( HeroInRange && WardInRange ); ++EntityIndex )
			{
				auto pEntity = pEntitySystem->GetBaseEntity<C_BaseEntity>( EntityIndex );

				if ( !pEntity || pEntity == pLocalHero || pEntity->m_iTeamNum() != EnemyTeam ||
					!IsClassOrDerivedFrom( pEntity , XorStr( "C_DOTA_BaseNPC" ) ) )
				{
					continue;
				}

				auto pIdentity = pEntity->pEntityIdentity();
				const auto IsWard = IsObserverWard( pEntity , pIdentity );
				const auto IsHero = !IsWard && IsClassOrDerivedFrom( pEntity , XorStr( "C_DOTA_BaseNPC_Hero" ) );

				if ( !IsWard && !IsHero )
					continue;

				auto pUnit = reinterpret_cast<C_DOTA_BaseNPC*>( pEntity );

				if ( pUnit->m_lifeState() != 0 )
					continue;

				if ( IsWard )
					++WardCount;
				else
					++HeroCount;

				auto pSceneNode = pUnit->m_pGameSceneNode();

				if ( !pSceneNode )
					continue;

				const auto SourcePosition = pSceneNode->m_vecAbsOrigin();
				const auto VisionRange = GetCurrentVisionRange ? GetCurrentVisionRange( pUnit ) :
					( std::max )( pUnit->m_iDayTimeVisionRange() , pUnit->m_iNightTimeVisionRange() );

				if ( !IsInsideVisionRange( SourcePosition , LocalPosition , VisionRange ) )
					continue;

				if ( IsWard )
					WardInRange = true;
				else
					HeroInRange = true;
			}
		}

		if ( HeroInRange && WardInRange )
			CachedSource = EVisionWarningSource::HeroAndWard;
		else if ( WardInRange )
			CachedSource = EVisionWarningSource::Ward;
		else if ( HeroInRange )
			CachedSource = EVisionWarningSource::Hero;

		if ( CachedSource != EVisionWarningSource::None )
		{
			NotificationEndTime = CurrentTime + 750ull;

			if ( PreviousSource == EVisionWarningSource::None || PreviousSource != CachedSource )
				MessageBeep( MB_ICONEXCLAMATION );
		}

		PreviousSource = CachedSource;

		if ( CurrentTime >= NextDiagnosticTime )
		{
			DEV_LOG( "[EnemyVisionWarning] heroes=%i wards=%i hero_in_range=%i ward_in_range=%i source=%i vision_fn=%i\n" ,
				HeroCount , WardCount , HeroInRange , WardInRange , static_cast<int>( CachedSource ) , GetCurrentVisionRange != nullptr );
			NextDiagnosticTime = CurrentTime + 3000ull;
		}
	}

	if ( CachedSource == EVisionWarningSource::None && CurrentTime >= NotificationEndTime )
		return;

	std::string WarningText = XorStr( "ENEMY VISION" );

	if ( CachedSource == EVisionWarningSource::Hero )
		WarningText = XorStr( "ENEMY VISION: HERO" );
	else if ( CachedSource == EVisionWarningSource::Ward )
		WarningText = XorStr( "ENEMY VISION: OBSERVER WARD" );
	else if ( CachedSource == EVisionWarningSource::HeroAndWard )
		WarningText = XorStr( "ENEMY VISION: HERO + WARD" );

	const auto DisplaySize = ImGui::GetIO().DisplaySize;
	const auto TextSize = ImGui::CalcTextSize( WarningText.c_str() );
	const ImVec2 Padding( 20.f , 11.f );
	const ImVec2 BoxMin( DisplaySize.x * 0.5f - TextSize.x * 0.5f - Padding.x , 70.f );
	const ImVec2 BoxMax( DisplaySize.x * 0.5f + TextSize.x * 0.5f + Padding.x , 70.f + TextSize.y + Padding.y * 2.f );
	const ImVec2 TextPosition( DisplaySize.x * 0.5f - TextSize.x * 0.5f , BoxMin.y + Padding.y );
	auto pDrawList = ImGui::GetBackgroundDrawList();
	const auto Pulse = 0.75f + 0.25f * std::sin( static_cast<float>( CurrentTime ) * 0.008f );
	const auto Red = static_cast<int>( 220.f + 35.f * Pulse );

	pDrawList->AddRectFilled( BoxMin , BoxMax , IM_COL32( 35 , 8 , 8 , 225 ) , 7.f );
	pDrawList->AddRect( BoxMin , BoxMax , IM_COL32( Red , 55 , 45 , 255 ) , 7.f , 0 , 2.f );
	pDrawList->AddText( TextPosition , IM_COL32( 255 , 225 , 215 , 255 ) , WarningText.c_str() );
}

auto CAndromedaClient::OnCreateMove( CDOTAInput* pCDOTAInput , CUserCmd* pCUserCmd ) -> void
{

}

auto GetAndromedaClient() -> CAndromedaClient*
{
	return &g_CAndromedaClient;
}
