#pragma once

#include <Common/Common.hpp>

#include "CBaseTypes.hpp"
#include "CHandle.hpp"
#include "Color_t.hpp"
#include "CUtlMemory.hpp"
#include "CUtlString.hpp"
#include "CUtlSymbol.hpp"
#include "CUtlSymbolLarge.hpp"
#include "CUtlVector.hpp"
#include "CStrongHandle.hpp"
#include "CUtlStringToken.hpp"

#include <Dota2/SDK/Math/Math.hpp>
#include <Dota2/SDK/Update/Offsets.hpp>
#include <Dota2/SDK/CSchemaOffset.hpp>
#include <Dota2/SDK/Interface/CShemaSystemSDK.hpp>

class IHandleEntity
{
public:
	virtual ~IHandleEntity() {}
};

class CEntityIdentity
{
public:
	SCHEMA_OFFSET_CUSTOM( pBaseEntity , 0x0 , C_BaseEntity* );
	SCHEMA_OFFSET_CUSTOM( Handle , 0x10 , CHandle );

public:
	SCHEMA_OFFSET( "CEntityIdentity" , "m_name" , Name , CUtlSymbolLarge );
	SCHEMA_OFFSET( "CEntityIdentity" , "m_designerName" , DesingerName , CUtlSymbolLarge );
	SCHEMA_OFFSET( "CEntityIdentity" , "m_flags" , m_flags , uint32 );

private:
	PAD( 0x70 );
};

class CEntityInstance : public IHandleEntity
{
public:
	auto GetSchemaClassBinding() -> CSchemaClassBinding*
	{
		CSchemaClassBinding* pBinding = nullptr;

		VirtualFn( void )( CEntityInstance* , CSchemaClassBinding** );
		vget< Fn >( this , SDK::VMT_Index::CSchemaSystem::SchemaClassInfo )( this , &pBinding );

		return pBinding;
	}

public:
	SCHEMA_OFFSET( "CEntityInstance" , "m_pEntity" , pEntityIdentity , CEntityIdentity* );
};

class CGameSceneNode
{
public:
	SCHEMA_OFFSET( "CGameSceneNode" , "m_vecAbsOrigin" , m_vecAbsOrigin , Vector3 );
};

class C_BaseEntity : public CEntityInstance
{
public:
	SCHEMA_OFFSET( "C_BaseEntity" , "m_pGameSceneNode" , m_pGameSceneNode , CGameSceneNode* );
	SCHEMA_OFFSET( "C_BaseEntity" , "m_iMaxHealth" , m_iMaxHealth , int32 );
	SCHEMA_OFFSET( "C_BaseEntity" , "m_iHealth" , m_iHealth , int32 );
	SCHEMA_OFFSET( "C_BaseEntity" , "m_lifeState" , m_lifeState , uint8 );
	SCHEMA_OFFSET( "C_BaseEntity" , "m_iTeamNum" , m_iTeamNum , uint8 );
	SCHEMA_OFFSET( "C_BaseEntity" , "m_iTaggedAsVisibleByTeam" , m_iTaggedAsVisibleByTeam , int32 );
};


class C_BaseModelEntity : public C_BaseEntity
{
public:

};

class C_DOTA_BaseNPC : public C_BaseModelEntity
{
public:
	auto GetDamageMin() -> int
	{
		VirtualFn( int )( C_DOTA_BaseNPC* );
		return vget< Fn >( this , SDK::VMT_Index::C_DOTA_BaseNPC::GetDamageMin )( this );
	}

	SCHEMA_OFFSET( "C_DOTA_BaseNPC" , "m_iDamageMin" , m_iDamageMin , int32 );
	SCHEMA_OFFSET( "C_DOTA_BaseNPC" , "m_iDamageMax" , m_iDamageMax , int32 );
	SCHEMA_OFFSET( "C_DOTA_BaseNPC" , "m_iDamageBonus" , m_iDamageBonus , int32 );
	SCHEMA_OFFSET( "C_DOTA_BaseNPC" , "m_iHealthBarOffset" , m_iHealthBarOffset , int32 );
	SCHEMA_OFFSET( "C_DOTA_BaseNPC" , "m_flPhysicalArmorValue" , m_flPhysicalArmorValue , float32 );
	SCHEMA_OFFSET( "C_DOTA_BaseNPC" , "m_iDayTimeVisionRange" , m_iDayTimeVisionRange , int32 );
	SCHEMA_OFFSET( "C_DOTA_BaseNPC" , "m_iNightTimeVisionRange" , m_iNightTimeVisionRange , int32 );
};

class C_DOTA_BaseNPC_Hero : public C_DOTA_BaseNPC
{
public:
};

class CBasePlayerController : public C_BaseEntity
{
public:

};

class C_DOTAPlayerController : public CBasePlayerController
{
public:
	SCHEMA_OFFSET( "C_DOTAPlayerController" , "m_hAssignedHero" , m_hAssignedHero , CHandle ); // C_DOTA_BaseNPC_Hero
};

class C_DOTACameraBounds : public C_BaseEntity
{
public:
	SCHEMA_OFFSET( "C_DOTACameraBounds" , "m_vecBoundsMin" , m_vecBoundsMin , Vector3 );
	SCHEMA_OFFSET( "C_DOTACameraBounds" , "m_vecBoundsMax" , m_vecBoundsMax , Vector3 );
};
