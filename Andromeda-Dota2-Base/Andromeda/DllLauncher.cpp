#include "DllLauncher.hpp"

#include <string>
#include <winternl.h>

#include <Common/CrashLog.hpp>
#include <Common/Helpers/StringHelper.hpp>

#include <Dota2/CHook_Loader.hpp>
#include <Dota2/CSDK_Loader.hpp>
#include <Dota2/SDK/CFunctionList.hpp>

#include <AndromedaClient/CAndromedaClient.hpp>

static CDllLauncher g_CDllLauncher{};

auto CDllLauncher::OnDllMain( LPVOID lpReserved , HINSTANCE hInstace ) -> void
{
	if ( lpReserved )
	{
		ManualMapParam_t* pParam = reinterpret_cast<ManualMapParam_t*>( lpReserved );

		if ( pParam )
		{
			m_DllDir = pParam->DllPath;
			m_DllDir += "\\";
			m_DllDir = m_DllDir.substr( 0 , m_DllDir.find_last_of( '\\' ) + 1 );
		}
	}
	else
	{
		char szDllDir[MAX_PATH];

		GetModuleFileNameA( hInstace , szDllDir , MAX_PATH );

		m_DllDir = szDllDir;
		m_DllDir = m_DllDir.substr( 0 , m_DllDir.find_last_of( '\\' ) );
		m_DllDir += '\\';
	}

	m_hDllImage = hInstace;

	m_SizeofImage = GetSizeOfImageInternal();
	m_BaseOfCode = GetBaseOfCodeInternal();

	char szGameFile[MAX_PATH] = { 0 };
	GetModuleFileNameA( 0 , szGameFile , MAX_PATH );

	m_Dota2Dir = szGameFile;
	m_Dota2Dir = m_Dota2Dir.substr( 0 , m_Dota2Dir.find_last_of( "\\/" ) );
	m_Dota2Dir += '\\';

	memset( szGameFile , 0 , MAX_PATH );

	CreateThread( 0 , 0 , StartCheatTheard , lpReserved , 0 , 0 );
}

auto CDllLauncher::OnDestroy() -> void
{
	if ( !m_bDestroyed )
	{
		GetDevLog()->Destroy();
		GetHook_Loader()->DestroyHooks();
		GetCrashLog()->DestroyVectorExceptionHandler();
		
		m_bDestroyed = true;
	}
}

auto WINAPI CDllLauncher::StartCheatTheard( LPVOID lpThreadParameter ) -> DWORD
{
	GetDevLog()->Init();
	GetCrashLog()->InitVectorExceptionHandler();

	DEV_LOG( "[+] StartCheatThread initialized. DLL Dir: %s\n" , ansi_to_utf8( GetDllDir() ).c_str() );
	DEV_LOG( "[+] Dota 2 Dir: %s\n" , ansi_to_utf8( GetDota2Dir() ).c_str() );

	DEV_LOG( "[+] Initializing MinHook...\n" );
	if ( !GetHook_Loader()->InitalizeMH() )
	{
		DEV_LOG( "[error] Hook_Loader::InitalizeMH failed!\n" );
		return 0;
	}
	DEV_LOG( "[+] MinHook initialized successfully.\n" );

	DEV_LOG( "[+] Installing first hook set...\n" );
	if ( !GetHook_Loader()->InstallFirstHook() )
	{
		DEV_LOG( "[error] Hook_Loader::InstallFirstHook failed!\n" );
		return 0;
	}
	DEV_LOG( "[+] First hook set installed.\n" );

	DEV_LOG( "[+] Initializing FunctionList...\n" );
	if ( !GetFunctionList()->OnInit() )
	{
		DEV_LOG( "[error] FunctionList::OnInit failed!\n" );
		return 0;
	}
	DEV_LOG( "[+] FunctionList initialized.\n" );

	DEV_LOG( "[+] Loading SDK...\n" );
	if ( !GetSDK_Loader()->LoadSDK() )
	{
		DEV_LOG( "[error] CSDK_Loader::LoadSDK failed!\n" );
		return 0;
	}
	DEV_LOG( "[+] SDK loaded.\n" );

	DEV_LOG( "[+] Installing second hook set...\n" );
	if ( !GetHook_Loader()->InstallSecondHook() )
	{
		DEV_LOG( "[error] Hook_Loader::InstallSecondHook failed!\n" );
		return 0;
	}
	DEV_LOG( "[+] Second hook set installed.\n" );

	DEV_LOG( "[+] Initializing AndromedaClient...\n" );
	GetAndromedaClient()->OnInit();
	DEV_LOG( "[+] AndromedaClient initialized successfully.\n" );

	return 0;
}

auto GetDllDir()->std::string&
{
	return GetDllLauncher()->m_DllDir;
}

auto GetDota2Dir() -> std::string&
{
	return GetDllLauncher()->m_Dota2Dir;
}

auto GetDllLauncher() -> CDllLauncher*
{
	return &g_CDllLauncher;
}
