#include "CInjector.h"

#include <BlackBone/Process/Process.h>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <sstream>
#include <vector>

namespace
{
	constexpr auto LOCAL_PLAYER_CONTROLLER_PATTERN =
		"E8 ? ? ? ? 48 89 44 24 ? 48 8B F8 48 85 C0 0F 84 ? ? ? ? 4C 89 AC 24 ? ? ? ? 48 8D 54 24";

	struct PeView_t
	{
		const IMAGE_NT_HEADERS64* pNtHeaders = nullptr;
		const IMAGE_SECTION_HEADER* pSections = nullptr;
	};

	auto GetPayloadPath( std::filesystem::path& PayloadPath ) -> bool
	{
		std::vector<WCHAR> ModulePath( 32768 );
		const auto PathLength = GetModuleFileNameW( nullptr , ModulePath.data() ,
			static_cast<DWORD>( ModulePath.size() ) );

		if ( !PathLength || PathLength >= ModulePath.size() )
			return false;

		PayloadPath = std::filesystem::path( ModulePath.data() ).replace_extension( L".dll" );
		return true;
	}

	auto ReadFileBytes( const std::filesystem::path& Path , std::vector<BYTE>& Data ) -> bool
	{
		std::ifstream File( Path , std::ios::binary | std::ios::ate );

		if ( !File )
			return false;

		const auto FileSize = static_cast<std::streamoff>( File.tellg() );

		if ( FileSize <= 0 || static_cast<unsigned long long>( FileSize ) > std::numeric_limits<size_t>::max() )
			return false;

		Data.resize( static_cast<size_t>( FileSize ) );
		File.seekg( 0 , std::ios::beg );
		File.read( reinterpret_cast<char*>( Data.data() ) , static_cast<std::streamsize>( Data.size() ) );

		return static_cast<size_t>( File.gcount() ) == Data.size();
	}

	auto GetPeView( const std::vector<BYTE>& Data , PeView_t& View ) -> bool
	{
		if ( Data.size() < sizeof( IMAGE_DOS_HEADER ) )
			return false;

		const auto pDosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>( Data.data() );

		if ( pDosHeader->e_magic != IMAGE_DOS_SIGNATURE || pDosHeader->e_lfanew <= 0 )
			return false;

		const auto NtOffset = static_cast<size_t>( pDosHeader->e_lfanew );

		if ( NtOffset > Data.size() || sizeof( IMAGE_NT_HEADERS64 ) > Data.size() - NtOffset )
			return false;

		const auto pNtHeaders = reinterpret_cast<const IMAGE_NT_HEADERS64*>( Data.data() + NtOffset );

		if ( pNtHeaders->Signature != IMAGE_NT_SIGNATURE ||
			pNtHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
			pNtHeaders->FileHeader.SizeOfOptionalHeader < sizeof( IMAGE_OPTIONAL_HEADER64 ) ||
			!pNtHeaders->FileHeader.NumberOfSections )
		{
			return false;
		}

		const auto SectionOffset = NtOffset + sizeof( DWORD ) + sizeof( IMAGE_FILE_HEADER ) +
			pNtHeaders->FileHeader.SizeOfOptionalHeader;
		const auto SectionBytes = static_cast<size_t>( pNtHeaders->FileHeader.NumberOfSections ) *
			sizeof( IMAGE_SECTION_HEADER );

		if ( SectionOffset > Data.size() || SectionBytes > Data.size() - SectionOffset )
			return false;

		View.pNtHeaders = pNtHeaders;
		View.pSections = reinterpret_cast<const IMAGE_SECTION_HEADER*>( Data.data() + SectionOffset );
		return true;
	}

	auto IsX64Dll( const PeView_t& View ) -> bool
	{
		return View.pNtHeaders->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
			( View.pNtHeaders->FileHeader.Characteristics & IMAGE_FILE_DLL ) != 0;
	}

	auto ParsePattern( const char* szPattern , std::vector<int>& Pattern ) -> bool
	{
		std::istringstream Stream( szPattern );
		std::string Token;

		while ( Stream >> Token )
		{
			if ( Token == "?" || Token == "??" )
			{
				Pattern.push_back( -1 );
				continue;
			}

			char* pEnd = nullptr;
			const auto Value = std::strtoul( Token.c_str() , &pEnd , 16 );

			if ( Token.size() != 2 || !pEnd || *pEnd || Value > 0xFF )
				return false;

			Pattern.push_back( static_cast<int>( Value ) );
		}

		return !Pattern.empty();
	}

	auto CountPatternMatches( const std::vector<BYTE>& Data , const PeView_t& View ,
		const std::vector<int>& Pattern , size_t& MatchOffset ) -> size_t
	{
		if ( Pattern.size() > Data.size() )
			return 0;

		size_t Anchor = 0;

		while ( Anchor < Pattern.size() && Pattern[Anchor] < 0 )
			++Anchor;

		if ( Anchor == Pattern.size() )
			return 0;

		size_t Matches = 0;

		for ( WORD SectionIndex = 0; SectionIndex < View.pNtHeaders->FileHeader.NumberOfSections; ++SectionIndex )
		{
			const auto& Section = View.pSections[SectionIndex];

			if ( ( Section.Characteristics & IMAGE_SCN_MEM_EXECUTE ) == 0 )
				continue;

			const auto RawStart = static_cast<size_t>( Section.PointerToRawData );
			const auto RawSize = static_cast<size_t>( Section.SizeOfRawData );

			if ( RawStart > Data.size() || RawSize > Data.size() - RawStart || Pattern.size() > RawSize )
				continue;

			const auto LastOffset = RawStart + RawSize - Pattern.size();

			for ( size_t Offset = RawStart; Offset <= LastOffset; ++Offset )
			{
				if ( Data[Offset + Anchor] != Pattern[Anchor] )
					continue;

				bool Found = true;

				for ( size_t Index = 0; Index < Pattern.size(); ++Index )
				{
					if ( Pattern[Index] >= 0 && Data[Offset + Index] != Pattern[Index] )
					{
						Found = false;
						break;
					}
				}

				if ( Found )
				{
					MatchOffset = Offset;
					++Matches;

					if ( Matches > 1 )
						return Matches;
				}
			}
		}

		return Matches;
	}

	auto FileOffsetToRva( const PeView_t& View , size_t FileOffset , DWORD& Rva ) -> bool
	{
		for ( WORD Index = 0; Index < View.pNtHeaders->FileHeader.NumberOfSections; ++Index )
		{
			const auto& Section = View.pSections[Index];
			const auto RawStart = static_cast<unsigned long long>( Section.PointerToRawData );
			const auto RawEnd = RawStart + Section.SizeOfRawData;

			if ( FileOffset >= RawStart && FileOffset < RawEnd )
			{
				Rva = Section.VirtualAddress + static_cast<DWORD>( FileOffset - RawStart );
				return true;
			}
		}

		return false;
	}

	auto IsExecutableRva( const PeView_t& View , DWORD Rva ) -> bool
	{
		for ( WORD Index = 0; Index < View.pNtHeaders->FileHeader.NumberOfSections; ++Index )
		{
			const auto& Section = View.pSections[Index];

			if ( ( Section.Characteristics & IMAGE_SCN_MEM_EXECUTE ) == 0 )
				continue;

			const auto SectionStart = static_cast<unsigned long long>( Section.VirtualAddress );
			const auto SectionSize = static_cast<unsigned long long>(
				std::max( Section.Misc.VirtualSize , Section.SizeOfRawData ) );
			const auto SectionEnd = SectionStart + SectionSize;

			if ( Rva >= SectionStart && Rva < SectionEnd )
				return true;
		}

		return false;
	}

	auto CountAsciiOccurrences( const std::vector<BYTE>& Data , const char* szText ) -> size_t
	{
		const auto TextLength = std::strlen( szText );

		if ( !TextLength || TextLength > Data.size() )
			return 0;

		size_t Count = 0;
		auto SearchStart = Data.cbegin();

		while ( SearchStart != Data.cend() )
		{
			const auto Match = std::search( SearchStart , Data.cend() , szText , szText + TextLength );

			if ( Match == Data.cend() )
				break;

			++Count;
			SearchStart = Match + TextLength;
		}

		return Count;
	}

	auto PrintTokenStatus() -> void
	{
		HANDLE hToken = nullptr;

		if ( !OpenProcessToken( GetCurrentProcess() , TOKEN_QUERY , &hToken ) )
		{
			PrintMessage( "[dry-run] [warn] Cannot query process token (error %lu)\n" , GetLastError() );
			return;
		}

		TOKEN_ELEVATION Elevation{};
		DWORD ReturnLength = 0;
		const auto HasElevation = GetTokenInformation( hToken , TokenElevation , &Elevation ,
			sizeof( Elevation ) , &ReturnLength ) != FALSE;

		LUID DebugPrivilege{};
		BOOL DebugEnabled = FALSE;

		if ( LookupPrivilegeValueA( nullptr , SE_DEBUG_NAME , &DebugPrivilege ) )
		{
			PRIVILEGE_SET Privileges{};
			Privileges.PrivilegeCount = 1;
			Privileges.Control = PRIVILEGE_SET_ALL_NECESSARY;
			Privileges.Privilege[0].Luid = DebugPrivilege;
			Privileges.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;
			PrivilegeCheck( hToken , &Privileges , &DebugEnabled );
		}

		PrintMessage( "[dry-run] [info] Elevated token: %s; SeDebugPrivilege enabled: %s\n" ,
			HasElevation && Elevation.TokenIsElevated ? "yes" : "no" , DebugEnabled ? "yes" : "no" );

		CloseHandle( hToken );
	}
}

auto CInjector::InitPaths() -> bool
{
	ZeroMemory( szDllFilePath , sizeof( szDllFilePath ) );
	ZeroMemory( szCurrentDir , sizeof( szCurrentDir ) );

	const auto PathLength = GetModuleFileNameA( nullptr , szDllFilePath , MAX_PATH );
	const auto CurrentDirLength = GetCurrentDirectoryA( MAX_PATH , szCurrentDir );

	if ( !PathLength || PathLength >= MAX_PATH || !CurrentDirLength || CurrentDirLength >= MAX_PATH )
		return false;

	auto pExtension = std::strrchr( szDllFilePath , '.' );

	if ( !pExtension )
		return false;

	return strcpy_s( pExtension , MAX_PATH - static_cast<size_t>( pExtension - szDllFilePath ) , ".dll" ) == 0;
}

auto CInjector::Init() -> bool
{
	if ( !InitPaths() )
		return false;

	if ( GetPrivileges() && FileExist( szDllFilePath ) )
		return true;

	return false;
}

auto CInjector::DryRun( const char* szProcessName ) -> bool
{
	PrintMessage( "[dry-run] No injection will be attempted and target memory will not be modified.\n" );

	std::filesystem::path PayloadPath;

	if ( !GetPayloadPath( PayloadPath ) )
	{
		PrintMessage( "[dry-run] [error] Cannot resolve executable and payload paths.\n" );
		return false;
	}

	std::vector<BYTE> PayloadData;
	PeView_t PayloadView{};

	if ( !ReadFileBytes( PayloadPath , PayloadData ) )
	{
		PrintMessage( "[dry-run] [error] Payload DLL not found or unreadable: %ls\n" , PayloadPath.c_str() );
		return false;
	}

	if ( !GetPeView( PayloadData , PayloadView ) || !IsX64Dll( PayloadView ) )
	{
		PrintMessage( "[dry-run] [error] Payload is not a valid x64 PE DLL: %ls\n" , PayloadPath.c_str() );
		return false;
	}

	if ( CountAsciiOccurrences( PayloadData , LOCAL_PLAYER_CONTROLLER_PATTERN ) != 1 )
	{
		PrintMessage( "[dry-run] [error] Payload does not contain exactly one expected local-player AOB.\n" );
		return false;
	}

	PrintMessage( "[dry-run] [ok] Payload: %ls (%llu bytes, x64 DLL, expected AOB embedded)\n" , PayloadPath.c_str() ,
		static_cast<unsigned long long>( PayloadData.size() ) );
	PrintTokenStatus();

	const auto PID = GetProcessIdByName( szProcessName );

	if ( !PID )
	{
		PrintMessage( "[dry-run] [error] Target process is not running: %s\n" , szProcessName );
		return false;
	}

	const auto hProcess = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION , FALSE , PID );

	if ( !hProcess )
	{
		PrintMessage( "[dry-run] [error] Cannot open target for query-only access (PID %lu, error %lu)\n" ,
			PID , GetLastError() );
		return false;
	}

	std::vector<WCHAR> ProcessPath( 32768 );
	DWORD ProcessPathLength = static_cast<DWORD>( ProcessPath.size() );
	const auto HasProcessPath = QueryFullProcessImageNameW( hProcess , 0 , ProcessPath.data() , &ProcessPathLength );
	const auto ProcessPathError = HasProcessPath ? ERROR_SUCCESS : GetLastError();
	CloseHandle( hProcess );

	if ( !HasProcessPath )
	{
		PrintMessage( "[dry-run] [error] Cannot query target image path (PID %lu, error %lu)\n" , PID , ProcessPathError );
		return false;
	}

	PrintMessage( "[dry-run] [ok] Target query access: %ls (PID %lu)\n" , ProcessPath.data() , PID );

	const std::filesystem::path TargetPath( ProcessPath.data() );
	const auto GameDirectory = TargetPath.parent_path().parent_path().parent_path();
	const auto ClientPath = GameDirectory / "dota" / "bin" / "win64" / "client.dll";
	std::vector<BYTE> ClientData;
	PeView_t ClientView{};

	if ( !ReadFileBytes( ClientPath , ClientData ) )
	{
		PrintMessage( "[dry-run] [error] Cannot read client.dll: %ls\n" , ClientPath.c_str() );
		return false;
	}

	if ( !GetPeView( ClientData , ClientView ) || !IsX64Dll( ClientView ) )
	{
		PrintMessage( "[dry-run] [error] client.dll is not a valid x64 PE DLL: %ls\n" , ClientPath.c_str() );
		return false;
	}

	PrintMessage( "[dry-run] [ok] On-disk game client: %ls (%llu bytes)\n" , ClientPath.c_str() ,
		static_cast<unsigned long long>( ClientData.size() ) );

	std::vector<int> Pattern;

	if ( !ParsePattern( LOCAL_PLAYER_CONTROLLER_PATTERN , Pattern ) )
	{
		PrintMessage( "[dry-run] [error] Internal AOB definition is invalid.\n" );
		return false;
	}

	size_t MatchOffset = 0;
	const auto MatchCount = CountPatternMatches( ClientData , ClientView , Pattern , MatchOffset );

	if ( MatchCount != 1 )
	{
		PrintMessage( "[dry-run] [error] Local-player AOB match count: %llu (expected 1)\n" ,
			static_cast<unsigned long long>( MatchCount ) );
		return false;
	}

	DWORD CallRva = 0;

	if ( !FileOffsetToRva( ClientView , MatchOffset , CallRva ) ||
		MatchOffset + 5 > ClientData.size() || ClientData[MatchOffset] != 0xE8 )
	{
		PrintMessage( "[dry-run] [error] AOB matched outside a valid call site.\n" );
		return false;
	}

	int32_t RelativeAddress = 0;
	std::memcpy( &RelativeAddress , ClientData.data() + MatchOffset + 1 , sizeof( RelativeAddress ) );
	const auto TargetRva = static_cast<long long>( CallRva ) + 5 + RelativeAddress;

	if ( TargetRva < 0 || TargetRva >= ClientView.pNtHeaders->OptionalHeader.SizeOfImage ||
		!IsExecutableRva( ClientView , static_cast<DWORD>( TargetRva ) ) )
	{
		PrintMessage( "[dry-run] [error] Resolved AOB target is outside executable client.dll sections.\n" );
		return false;
	}

	PrintMessage( "[dry-run] [ok] Local-player AOB: one match at file offset 0x%llX, target RVA 0x%llX\n" ,
		static_cast<unsigned long long>( MatchOffset ) , static_cast<unsigned long long>( TargetRva ) );
	PrintMessage( "[dry-run] [success] All checks passed. No process memory was modified.\n" );
	return true;
}

auto CInjector::GetPrivileges() -> bool
{
	HANDLE hToken = NULL;
	LUID luid;
	TOKEN_PRIVILEGES tp;

	OpenProcessToken( GetCurrentProcess() , TOKEN_ALL_ACCESS , &hToken );

	LookupPrivilegeValue( NULL , SE_DEBUG_NAME , &luid );

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	if ( AdjustTokenPrivileges( hToken , FALSE , &tp , sizeof( TOKEN_PRIVILEGES ) , NULL , NULL ) )
	{
		CloseHandle( hToken );
		return true;
	}

	return false;
}

auto CInjector::GetProcessIdByName( const char* szProcName )->DWORD
{
	HANDLE hSnapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS , 0 );
	DWORD dwGetProcessID = 0;

	if ( hSnapshot != INVALID_HANDLE_VALUE )
	{
		PROCESSENTRY32 ProcEntry32 = { 0 };
		ProcEntry32.dwSize = sizeof( PROCESSENTRY32 );

		if ( Process32First( hSnapshot , &ProcEntry32 ) )
		{
			do
			{
				if ( _stricmp( ProcEntry32.szExeFile , szProcName ) == 0 )
				{
					dwGetProcessID = (DWORD)ProcEntry32.th32ProcessID;
					break;
				}
			} while ( Process32Next( hSnapshot , &ProcEntry32 ) );
		}

		CloseHandle( hSnapshot );
	}

	return dwGetProcessID;
}

auto CInjector::InjectManualMap( const char* szProcessName ) -> bool
{
	bool Result = false;
	DWORD PID = 0;

	DEV_LOG( "[info] Wait for start %s\n" , szProcessName );

	while ( !PID )
	{
		PID = GetProcessIdByName( szProcessName );
		Sleep( 100 );
	}

	m_hProcess = OpenProcess( PROCESS_ALL_ACCESS , FALSE , PID );

	if ( !m_hProcess )
	{
		DEV_LOG( "[-] inject code: #1\n" );
		return false;
	}

	// Read Dll File
	{
		auto hFile = CreateFileA( szDllFilePath , GENERIC_READ , 0 , NULL , OPEN_EXISTING , FILE_ATTRIBUTE_NORMAL , NULL );

		if ( !hFile )
		{
			DEV_LOG( "[-] inject code: #2\n" );
			CloseHandle( m_hProcess );
			return false;
		}

		auto dwLength = GetFileSize( hFile , NULL );

		if ( dwLength == INVALID_FILE_SIZE || dwLength == 0 )
		{
			DEV_LOG( "[-] inject code: #3\n" );

			CloseHandle( hFile );
			CloseHandle( m_hProcess );

			return false;
		}

		m_pDllFile = (PBYTE)HeapAlloc( GetProcessHeap() , 0 , dwLength );

		if ( !m_pDllFile )
		{
			DEV_LOG( "[-] inject code: #4\n" );

			CloseHandle( hFile );
			CloseHandle( m_hProcess );

			return false;
		}

		if ( ReadFile( hFile , m_pDllFile , dwLength , &m_DllFileSize , NULL ) == FALSE )
		{
			DEV_LOG( "[-] inject code: #5\n" );

			CloseHandle( hFile );
			CloseHandle( m_hProcess );

			return false;
		}

		if ( dwLength != m_DllFileSize )
		{
			DEV_LOG( "[-] inject code: #6\n" );

			CloseHandle( hFile );
			CloseHandle( m_hProcess );

			return false;
		}

		CloseHandle( hFile );
	}

	DllLoaderData_t LoaderData = { 0 };

	// Loader Data
	memcpy( LoaderData.DllPath , szCurrentDir , MAX_PATH );

	// Inject To Process
	{
		blackbone::Process CS2Process;
		CS2Process.Attach( m_hProcess );

		blackbone::CustomArgs_t Args;

		Args.push_back( &LoaderData , sizeof( DllLoaderData_t ) );

		auto pImage = CS2Process.mmap().MapImage( m_DllFileSize , m_pDllFile , false , blackbone::WipeHeader , nullptr , nullptr , &Args );

		if ( pImage )
			Result = true;
		else
			DEV_LOG( "[-] inject: %ws\n" , blackbone::Utils::GetErrorDescription( pImage.status ).c_str() );
	}

	// Free And Close
	{
		if ( m_pDllFile )
			HeapFree( GetProcessHeap() , 0 , m_pDllFile );

		CloseHandle( m_hProcess );
	}

	return Result;
}

auto GetInjector() -> CInjector*
{
	return CInjector::Instance();
}
