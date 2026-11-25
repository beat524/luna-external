#include "process.hpp"

#include <TlHelp32.h>

#include <algorithm>
#include <cctype>

namespace ext
{
    process g_process;

    bool process::attach( const std::wstring& exe_name )
    {
        detach();

        const HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
        if ( snap == INVALID_HANDLE_VALUE )
            return false;

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof( entry );

        bool found = false;
        if ( Process32FirstW( snap, &entry ) )
        {
            do
            {
                if ( exe_name == entry.szExeFile )
                {
                    pid_ = entry.th32ProcessID;
                    found = true;
                    break;
                }
            } while ( Process32NextW( snap, &entry ) );
        }
        CloseHandle( snap );

        if ( !found )
            return false;

        handle_ = OpenProcess( PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid_ );
        if ( !handle_ )
            return false;

        const HANDLE modules = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_ );
        if ( modules == INVALID_HANDLE_VALUE )
            return false;

        MODULEENTRY32W module{};
        module.dwSize = sizeof( module );
        if ( Module32FirstW( modules, &module ) )
        {
            base_ = reinterpret_cast< std::uintptr_t >( module.modBaseAddr );
            size_ = module.modBaseSize;
        }
        CloseHandle( modules );

        return base_ != 0;
    }

    void process::detach()
    {
        if ( handle_ )
            CloseHandle( handle_ );
        handle_ = nullptr;
        pid_ = 0;
        base_ = 0;
        size_ = 0;
    }

    bool process::read_buffer( const std::uintptr_t address, void* buffer, const std::size_t size ) const
    {
        return address && ReadProcessMemory( handle_, reinterpret_cast< LPCVOID >( address ), buffer, size, nullptr );
    }

    bool process::write_buffer( const std::uintptr_t address, const void* buffer, const std::size_t size ) const
    {
        return address && WriteProcessMemory( handle_, reinterpret_cast< LPVOID >( address ), buffer, size, nullptr );
    }

    bool process::parse_pattern( const std::string& pattern, std::vector< std::int32_t >& bytes, std::string& mask )
    {
        bytes.clear();
        mask.clear();

        std::size_t i = 0;
        while ( i < pattern.size() )
        {
            while ( i < pattern.size() && std::isspace( static_cast< unsigned char >( pattern[ i ] ) ) )
                ++i;
            if ( i >= pattern.size() )
                break;

            const std::size_t start = i;
            while ( i < pattern.size() && !std::isspace( static_cast< unsigned char >( pattern[ i ] ) ) )
                ++i;

            const std::string token = pattern.substr( start, i - start );

            if ( token == "?" || token == "??" )
            {
                bytes.push_back( -1 );
                mask.push_back( '?' );
            }
            else
            {
                try
                {
                    bytes.push_back( static_cast< std::int32_t >( std::stoul( token, nullptr, 16 ) ) );
                }
                catch ( ... )
                {
                    return false;
                }
                mask.push_back( 'x' );
            }
        }
        return !bytes.empty();
    }

    std::size_t process::scan_region( const std::uint8_t* data, const std::size_t size,
                                      const std::vector< std::int32_t >& bytes, const std::string& mask )
    {
        constexpr std::size_t npos = static_cast< std::size_t >( -1 );
        if ( bytes.empty() || size < bytes.size() )
            return npos;

        for ( std::size_t i = 0; i <= size - bytes.size(); ++i )
        {
            bool match = true;
            for ( std::size_t j = 0; j < bytes.size(); ++j )
            {
                if ( mask[ j ] == 'x' && data[ i + j ] != static_cast< std::uint8_t >( bytes[ j ] ) )
                {
                    match = false;
                    break;
                }
            }
            if ( match )
                return i;
        }
        return npos;
    }

    std::uintptr_t process::scan( const std::string& pattern ) const
    {
        std::vector< std::int32_t > bytes;
        std::string mask;
        if ( !parse_pattern( pattern, bytes, mask ) )
            return 0;

        constexpr std::size_t chunk = 0x100000;  // 1 MiB
        const std::size_t overlap = bytes.size() > 0 ? bytes.size() - 1 : 0;
        std::vector< std::uint8_t > buffer( chunk + overlap );

        for ( std::size_t offset = 0; offset < size_; offset += chunk )
        {
            const std::size_t to_read = std::min( chunk + overlap, size_ - offset );
            if ( !ReadProcessMemory( handle_, reinterpret_cast< LPCVOID >( base_ + offset ), buffer.data(), to_read,
                                     nullptr ) )
                continue;

            const auto index = scan_region( buffer.data(), to_read, bytes, mask );
            if ( index != static_cast< std::size_t >( -1 ) )
                return base_ + offset + index;

            if ( to_read < chunk + overlap )
                break;
        }
        return 0;
    }
}  // namespace ext
