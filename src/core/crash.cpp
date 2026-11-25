#include "crash.hpp"

#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace ext::crash
{
    namespace
    {
        // The console window cannot be scrolled when the exe is launched from
        // Explorer, so every diagnostic ends up in a file next to the exe.
        wchar_t g_log_path[ MAX_PATH ]{};
        volatile LONG g_handling = 0;

        void build_log_path()
        {
            if ( g_log_path[ 0 ] )
                return;

            if ( !GetModuleFileNameW( nullptr, g_log_path, MAX_PATH ) )
            {
                wcsncpy_s( g_log_path, L"crash.txt", _TRUNCATE );
                return;
            }

            wchar_t* slash = wcsrchr( g_log_path, L'\\' );
            if ( slash )
                slash[ 1 ] = L'\0';
            else
                g_log_path[ 0 ] = L'\0';

            wcscat_s( g_log_path, L"crash.txt" );
        }

        // Resolves an address to "module+0xRVA". Returns false for non-image
        // addresses (heap, stack, JIT).
        bool describe_address( const void* address, char* out, const std::size_t out_size )
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if ( !VirtualQuery( address, &mbi, sizeof( mbi ) ) || mbi.Type != MEM_IMAGE )
                return false;

            const auto base = static_cast< const unsigned char* >( mbi.AllocationBase );
            wchar_t module_path[ MAX_PATH ]{};
            if ( !GetModuleFileNameW( reinterpret_cast< HMODULE >( const_cast< unsigned char* >( base ) ),
                                      module_path, MAX_PATH ) )
                return false;

            const wchar_t* name = wcsrchr( module_path, L'\\' );
            name = name ? name + 1 : module_path;

            char narrow[ MAX_PATH ]{};
            WideCharToMultiByte( CP_ACP, 0, name, -1, narrow, MAX_PATH, nullptr, nullptr );

            const auto rva = reinterpret_cast< std::uintptr_t >( address ) - reinterpret_cast< std::uintptr_t >( base );
            std::snprintf( out, out_size, "%s+0x%llX", narrow, static_cast< unsigned long long >( rva ) );
            return true;
        }

        char g_report[ 32768 ];
        std::size_t g_report_used = 0;

        void append( const char* format, ... )
        {
            if ( g_report_used >= sizeof( g_report ) - 1 )
                return;

            va_list args;
            va_start( args, format );
            const int written = std::vsnprintf( g_report + g_report_used, sizeof( g_report ) - g_report_used, format, args );
            va_end( args );

            if ( written > 0 )
                g_report_used += static_cast< std::size_t >( written );
            if ( g_report_used > sizeof( g_report ) - 1 )
                g_report_used = sizeof( g_report ) - 1;
        }

        void flush_report( const char* reason )
        {
            build_log_path();

            const HANDLE file = CreateFileW( g_log_path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                             FILE_ATTRIBUTE_NORMAL, nullptr );
            if ( file != INVALID_HANDLE_VALUE )
            {
                SYSTEMTIME now{};
                GetLocalTime( &now );

                char header[ 256 ];
                const int header_length = std::snprintf(
                    header, sizeof( header ), "\n===== %s  %04u-%02u-%02u %02u:%02u:%02u =====\n", reason,
                    now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond );
                DWORD written = 0;
                WriteFile( file, header, static_cast< DWORD >( header_length ), &written, nullptr );
                WriteFile( file, g_report, static_cast< DWORD >( g_report_used ), &written, nullptr );
                CloseHandle( file );
            }

            // Keep a copy on stdout as well, in case the console is visible.
            std::fputs( g_report, stdout );
            std::fflush( stdout );
        }

        void describe_exception( EXCEPTION_POINTERS* info )
        {
            g_report_used = 0;

            const EXCEPTION_RECORD* record = info && info->ExceptionRecord ? info->ExceptionRecord : nullptr;
            const CONTEXT* context = info ? info->ContextRecord : nullptr;

            if ( !record )
            {
                append( "no exception record\n" );
                return;
            }

            char where[ MAX_PATH + 32 ] = "unknown";
            describe_address( record->ExceptionAddress, where, sizeof( where ) );

            append( "exception : 0x%08lX  (%s)\n", record->ExceptionCode, where );

            switch ( record->ExceptionCode )
            {
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
                append( "diagnosis : integer divide by zero\n" );
                break;
            case EXCEPTION_ACCESS_VIOLATION:
                if ( record->NumberParameters >= 2 )
                    append( "diagnosis : access violation %s at 0x%llX\n",
                            record->ExceptionInformation[ 0 ] ? "WRITE" : "READ",
                            static_cast< unsigned long long >( record->ExceptionInformation[ 1 ] ) );
                break;
            case 0xC0000409:
                append( "diagnosis : fastfail / abort (subcode 0x%llX)\n",
                        record->NumberParameters >= 1
                            ? static_cast< unsigned long long >( record->ExceptionInformation[ 0 ] )
                            : 0ull );
                break;
            default:
                break;
            }

            if ( !context )
                return;

            char rip[ MAX_PATH + 32 ] = "unknown";
            describe_address( reinterpret_cast< const void* >( context->Rip ), rip, sizeof( rip ) );

            append( "thread    : %lu\n", GetCurrentThreadId() );
            append( "rip       : 0x%llX  (%s)\n", static_cast< unsigned long long >( context->Rip ), rip );
            append( "rsp       : 0x%llX   rbp: 0x%llX\n",
                    static_cast< unsigned long long >( context->Rsp ),
                    static_cast< unsigned long long >( context->Rbp ) );
            append( "rax       : 0x%llX   rbx: 0x%llX   rcx: 0x%llX   rdx: 0x%llX\n",
                    static_cast< unsigned long long >( context->Rax ),
                    static_cast< unsigned long long >( context->Rbx ),
                    static_cast< unsigned long long >( context->Rcx ),
                    static_cast< unsigned long long >( context->Rdx ) );
            append( "rsi       : 0x%llX   rdi: 0x%llX   r8 : 0x%llX   r9 : 0x%llX\n",
                    static_cast< unsigned long long >( context->Rsi ),
                    static_cast< unsigned long long >( context->Rdi ),
                    static_cast< unsigned long long >( context->R8 ),
                    static_cast< unsigned long long >( context->R9 ) );

            // Walk the faulting thread's stack and print every slot that points
            // into a loaded image. ReadProcessMemory is used so an unmapped page
            // cannot fault the handler itself.
            append( "stack     :\n" );

            const auto* cursor = reinterpret_cast< const unsigned char* >( context->Rsp );
            const auto* stop = reinterpret_cast< const unsigned char* >( context->Rsp + 0x8000 );
            int printed = 0;

            for ( ; cursor < stop && printed < 64; cursor += sizeof( std::uintptr_t ) )
            {
                std::uintptr_t value = 0;
                if ( !ReadProcessMemory( GetCurrentProcess(), cursor, &value, sizeof( value ), nullptr ) )
                    break;
                if ( !value )
                    continue;

                char described[ MAX_PATH + 32 ]{};
                if ( !describe_address( reinterpret_cast< const void* >( value ), described, sizeof( described ) ) )
                    continue;

                append( "  0x%llX  %s\n", static_cast< unsigned long long >( value ), described );
                ++printed;
            }

            if ( printed == 0 )
                append( "  (no image addresses on the stack)\n" );
        }

        LONG WINAPI on_unhandled_exception( EXCEPTION_POINTERS* info )
        {
            if ( InterlockedExchange( &g_handling, 1 ) != 0 )
                return EXCEPTION_CONTINUE_SEARCH;

            describe_exception( info );
            flush_report( "unhandled exception" );

            // Let Windows create a minidump / WER report as usual.
            return EXCEPTION_CONTINUE_SEARCH;
        }

        void on_terminate()
        {
            if ( InterlockedExchange( &g_handling, 1 ) == 0 )
            {
                g_report_used = 0;
                append( "std::terminate called (no active exception handler)\n" );
                flush_report( "terminate" );
            }
            std::abort();
        }
    }  // namespace

    void install()
    {
        build_log_path();
        SetUnhandledExceptionFilter( on_unhandled_exception );
        std::set_terminate( on_terminate );
    }
}  // namespace ext::crash
