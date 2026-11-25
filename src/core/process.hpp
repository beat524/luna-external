#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ext
{
    // Thin wrapper around a foreign (x64) process: attach, module base, typed
    // read/write, and a byte-pattern scanner over the main module.
    class process
    {
       public:
        bool attach( const std::wstring& exe_name );
        void detach();

        bool attached() const { return handle_ != nullptr; }
        std::uint32_t pid() const { return pid_; }
        std::uintptr_t base() const { return base_; }
        std::size_t module_size() const { return size_; }
        HANDLE handle() const { return handle_; }

        template < typename T >
        T read( const std::uintptr_t address ) const
        {
            T value{};
            if ( address )
                ReadProcessMemory( handle_, reinterpret_cast< LPCVOID >( address ), &value, sizeof( T ), nullptr );
            return value;
        }

        template < typename T >
        bool write( const std::uintptr_t address, const T& value ) const
        {
            return address && WriteProcessMemory( handle_, reinterpret_cast< LPVOID >( address ), &value, sizeof( T ),
                                                  nullptr );
        }

        bool read_buffer( std::uintptr_t address, void* buffer, std::size_t size ) const;
        bool write_buffer( std::uintptr_t address, const void* buffer, std::size_t size ) const;

        // Scan the main module for an IDA-style pattern, e.g. "48 8B 05 ? ? ? ?".
        // Returns an absolute address in the target, or 0 if not found.
        std::uintptr_t scan( const std::string& pattern ) const;

        // Treat a value as a module-relative offset (RVA) and return the absolute address.
        std::uintptr_t rebase( const std::uintptr_t rva ) const { return rva ? base_ + rva : 0; }

       private:
        static bool parse_pattern( const std::string& pattern, std::vector< std::int32_t >& bytes, std::string& mask );
        static std::size_t scan_region( const std::uint8_t* data, std::size_t size,
                                        const std::vector< std::int32_t >& bytes, const std::string& mask );

        HANDLE handle_ = nullptr;
        std::uint32_t pid_ = 0;
        std::uintptr_t base_ = 0;
        std::size_t size_ = 0;
    };

    extern process g_process;
}  // namespace ext
