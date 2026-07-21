#pragma once

#include <Common/Common.hpp>

#include <Dota2/CBasePattern.hpp>
#include <Dota2/CHook_Loader.hpp>

class CDOTAInput;
class CUserCmd;

class IAndromedaClient
{
public:
	virtual void OnRender() = 0;
	virtual void OnCreateMove( CDOTAInput* pCDOTAInput , CUserCmd* pCUserCmd ) = 0;
};

class CAndromedaClient final : public IAndromedaClient
{
public:
	auto OnInit() -> void;

public:
	auto SetCameraDistance( float Distance ) -> void;
	auto RenderLastHitMarkers() -> void;
	auto RenderEnemyVisionWarning() -> void;

public:
	virtual void OnRender() override;
	virtual void OnCreateMove( CDOTAInput* pCDOTAInput , CUserCmd* pCUserCmd ) override;

private:
	CBasePattern dota_camera_distance = { XorStr( "dota_camera_distance" ) , XorStr( "F3 0F 11 05 ? ? ? ? E8 ? ? ? ? 48 8D 0D ? ? ? ? F3 0F 11 05 ? ? ? ? E8 ? ? ? ? 48 8D 0D ? ? ? ? F3 0F 11 05 ? ? ? ? E8" ) , XorStr( CLIENT_DLL ) , 0 , eBasePatternSearchType::SEARCH_TYPE_MOV_PTR };
	CBasePattern dota_camera_fog_end = { XorStr( "dota_camera_fog_end" ) , XorStr( "F3 0F 11 05 ? ? ? ? E8 ? ? ? ? 48 8D 0D ? ? ? ? F3 0F 11 05 ? ? ? ? E8 ? ? ? ? F3 0F 11 05 ? ? ? ? 48 83 C4 ? C3" ) , XorStr( CLIENT_DLL ) , 0 , eBasePatternSearchType::SEARCH_TYPE_MOV_PTR };
	CBasePattern dota_camera_farplane = { XorStr( "dota_camera_farplane" ) , XorStr( "F3 0F 11 05 ? ? ? ? 48 83 C4 ? C3" ) , XorStr( CLIENT_DLL ) , 0 , eBasePatternSearchType::SEARCH_TYPE_MOV_PTR };
	CBasePattern dota_npc_get_current_vision_range = { XorStr( "dota_npc_get_current_vision_range" ) , XorStr( "40 53 48 83 EC 20 48 8B 01 48 8B D9 FF 90 F0 04 00 00 84 C0 74 ? 48 8B 03 48 8B CB FF 90 D0 0C 00 00 84 C0 75 ?" ) , XorStr( CLIENT_DLL ) };
};

auto GetAndromedaClient() -> CAndromedaClient*;
