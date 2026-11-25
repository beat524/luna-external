#include "roblox.hpp"

#include "core/process.hpp"
#include "offsets.hpp"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace ext::game
{
    namespace
    {
        std::uintptr_t g_datamodel_override = 0;

        std::uintptr_t read_ptr( const std::uintptr_t address )
        {
            return g_process.read< std::uintptr_t >( address );
        }

        // MSVC std::string layout: buffer/pointer at +0x0, size at +0x10.
        std::string read_string( const std::uintptr_t address )
        {
            if ( !address )
                return {};

            const auto size = g_process.read< std::size_t >( address + 0x10 );
            if ( size == 0 || size > 512 )
                return {};

            std::uintptr_t data = address;
            if ( size >= 16 )
                data = read_ptr( address );
            if ( !data )
                return {};

            std::string result( size, '\0' );
            g_process.read_buffer( data, result.data(), size );
            return result;
        }

        std::string read_cstr( const std::uintptr_t address, const std::size_t max_len = 64 )
        {
            if ( !address )
                return {};
            char buffer[ 65 ]{};
            if ( !g_process.read_buffer( address, buffer, max_len ) )
                return {};
            return std::string( buffer );
        }

        bool printable( const std::string& text )
        {
            if ( text.empty() || text.size() > 48 )
                return false;
            for ( const unsigned char c : text )
                if ( c < 32 || c > 126 )
                    return false;
            return true;
        }

        // Job names are either an inline SSO std::string at +0x18 or a char*
        // stored there; accept whichever reads back as printable text.
        std::string read_job_name( const std::uintptr_t job, const std::uintptr_t name_offset )
        {
            const auto inline_name = read_cstr( job + name_offset );
            if ( printable( inline_name ) )
                return inline_name;

            const auto pointed_name = read_cstr( read_ptr( job + name_offset ) );
            if ( printable( pointed_name ) )
                return pointed_name;

            return {};
        }

        int g_children_mode = 0;  // 0 = inline begin/end, 1 = pointer to vector, 2 = swapped
        std::ofstream g_probe_log;

        // Writes to the console and to probe.txt, so a scrolled-away console
        // never loses the diagnostic.
        void plog( const char* format, ... )
        {
            char buffer[ 2048 ];
            va_list args;
            va_start( args, format );
            vsnprintf( buffer, sizeof( buffer ), format, args );
            va_end( args );

            std::fputs( buffer, stdout );
            if ( g_probe_log )
                g_probe_log << buffer;
        }

        // Three plausible ways a Roblox build can expose an instance's children.
        std::vector< std::uintptr_t > children_mode( const std::uintptr_t instance, const int mode )
        {
            std::vector< std::uintptr_t > result;
            const auto& o = get_offsets();
            if ( !instance )
                return result;

            std::uintptr_t begin = 0;
            std::uintptr_t end = 0;

            if ( mode == 1 )
            {
                // Children is a pointer to a container holding begin/end.
                const auto container = read_ptr( instance + o.instance_children_start );
                if ( !container )
                    return result;
                begin = read_ptr( container );
                end = read_ptr( container + 8 );
            }
            else if ( mode == 3 )
            {
                // Pointer to a container of 16-byte elements (pointer + control
                // block); the Instance pointer is the first qword of each.
                const auto container = read_ptr( instance + o.instance_children_start );
                if ( !container )
                    return result;
                begin = read_ptr( container );
                end = read_ptr( container + 8 );
            }
            else if ( mode == 2 )
            {
                // The two fields are the other way round.
                begin = read_ptr( instance + o.instance_children_end );
                end = read_ptr( instance + o.instance_children_start );
            }
            else
            {
                begin = read_ptr( instance + o.instance_children_start );
                end = read_ptr( instance + o.instance_children_end );
            }

            if ( !begin || !end || end <= begin )
                return result;

            const std::size_t stride = ( mode == 3 ) ? 16 : 8;
            const auto count = ( end - begin ) / stride;
            if ( count == 0 || count > 20000 )
                return result;

            result.resize( count );
            if ( stride == 8 )
            {
                if ( !g_process.read_buffer( begin, result.data(), count * sizeof( std::uintptr_t ) ) )
                    result.clear();
            }
            else
            {
                std::vector< std::uint8_t > raw( count * stride );
                if ( !g_process.read_buffer( begin, raw.data(), raw.size() ) )
                    return {};
                for ( std::size_t i = 0; i < count; ++i )
                    std::memcpy( &result[ i ], raw.data() + i * stride, sizeof( std::uintptr_t ) );
            }
            return result;
        }
    }  // namespace

    std::uintptr_t data_model()
    {
        if ( g_datamodel_override )
            return g_datamodel_override;

        const auto& o = get_offsets();
        const auto fake = read_ptr( o.fake_data_model );
        if ( !fake )
            return 0;
        return read_ptr( fake + o.fake_to_datamodel );
    }

    std::string class_name( const std::uintptr_t instance )
    {
        const auto& o = get_offsets();
        if ( !instance )
            return {};
        const auto descriptor = read_ptr( instance + o.instance_class_descriptor );
        if ( !descriptor )
            return {};
        return read_string( descriptor + o.class_descriptor_name );
    }

    std::string name( const std::uintptr_t instance )
    {
        const auto& o = get_offsets();
        if ( !instance )
            return {};

        // Modern layout: instance -> Name container pointer -> std::string at +Name.
        const auto container = read_ptr( instance + o.instance_name );
        if ( !container )
            return {};
        return read_string( container + o.instance_name_offset );
    }

    std::vector< std::uintptr_t > children( const std::uintptr_t instance )
    {
        return children_mode( instance, g_children_mode );
    }

    std::uintptr_t find_child( const std::uintptr_t parent, const std::string& child_name )
    {
        for ( const auto child : children( parent ) )
            if ( name( child ) == child_name )
                return child;
        return 0;
    }

    std::uintptr_t find_child_by_class( const std::uintptr_t parent, const std::string& cls )
    {
        for ( const auto child : children( parent ) )
            if ( class_name( child ) == cls )
                return child;
        return 0;
    }

    std::uintptr_t service( const std::string& cls )
    {
        const auto dm = data_model();
        if ( !dm )
            return 0;

        if ( const auto by_class = find_child_by_class( dm, cls ) )
            return by_class;

        // Services are named after their class, so fall back to a name match in
        // case class-name resolution is broken for this build.
        return find_child( dm, cls );
    }

    std::uintptr_t local_player()
    {
        const auto players = service( "Players" );
        if ( !players )
            return 0;
        return read_ptr( players + get_offsets().players_local_player );
    }

    std::uintptr_t player_character( const std::uintptr_t player )
    {
        if ( !player )
            return 0;
        return read_ptr( player + get_offsets().player_character );
    }

    Vec3 part_position( const std::uintptr_t part )
    {
        const auto& o = get_offsets();
        if ( !part )
            return {};

        // Modern layout: BasePart -> Primitive -> Vector3 position.
        if ( o.basepart_primitive && o.primitive_position )
        {
            const auto primitive = read_ptr( part + o.basepart_primitive );
            if ( !primitive )
                return {};
            return g_process.read< Vec3 >( primitive + o.primitive_position );
        }

        if ( !o.part_position )
            return {};
        return g_process.read< Vec3 >( part + o.part_position );
    }

    Vec3 part_size( const std::uintptr_t part )
    {
        const auto& o = get_offsets();
        if ( !part )
            return {};

        if ( o.basepart_primitive && o.primitive_size )
        {
            const auto primitive = read_ptr( part + o.basepart_primitive );
            if ( !primitive )
                return {};
            return g_process.read< Vec3 >( primitive + o.primitive_size );
        }

        if ( !o.part_size )
            return {};
        return g_process.read< Vec3 >( part + o.part_size );
    }

    float humanoid_health( const std::uintptr_t humanoid )
    {
        if ( !humanoid )
            return 0.0f;
        return g_process.read< float >( humanoid + get_offsets().humanoid_health );
    }

    float humanoid_max_health( const std::uintptr_t humanoid )
    {
        if ( !humanoid )
            return 0.0f;
        return g_process.read< float >( humanoid + get_offsets().humanoid_max_health );
    }

    bool view_matrix( Mat4& out )
    {
        const auto& o = get_offsets();
        const auto engine = read_ptr( o.visual_engine );
        if ( !engine )
            return false;
        return g_process.read_buffer( engine + o.view_matrix, &out, sizeof( out ) );
    }

    Vec2 dimensions()
    {
        const auto& o = get_offsets();
        const auto engine = read_ptr( o.visual_engine );
        if ( !engine )
            return {};
        return g_process.read< Vec2 >( engine + o.dimensions );
    }

    bool world_to_screen( const Vec3& world, Vec2& screen, bool* used_transposed )
    {
        Mat4 view;
        if ( !view_matrix( view ) )
            return false;

        const auto dim = dimensions();
        if ( dim.x <= 0.0f || dim.y <= 0.0f )
            return false;

        return ext::world_to_screen( view, world, dim, screen, used_transposed );
    }

    std::uintptr_t roblox_window()
    {
        return reinterpret_cast< std::uintptr_t >( FindWindowW( nullptr, L"Roblox" ) );
    }

    bool is_foreground()
    {
        const auto window = reinterpret_cast< HWND >( roblox_window() );
        return window && GetForegroundWindow() == window;
    }

    void dump()
    {        const auto& o = get_offsets();
        std::printf( "base             = 0x%llX\n", static_cast< unsigned long long >( g_process.base() ) );
        std::printf( "fake_data_model  = 0x%llX\n", static_cast< unsigned long long >( o.fake_data_model ) );
        std::printf( "visual_engine    = 0x%llX\n", static_cast< unsigned long long >( o.visual_engine ) );

        const auto dm = data_model();
        std::printf( "datamodel        = 0x%llX\n", static_cast< unsigned long long >( dm ) );
        if ( !dm )
        {
            std::printf( "  (null - check fake_data_model / fake_to_datamodel)\n" );
            return;
        }

        std::printf( "  class          = %s\n", class_name( dm ).c_str() );
        for ( const auto child : children( dm ) )
            std::printf( "  child 0x%llX  %-24s %s\n", static_cast< unsigned long long >( child ),
                         class_name( child ).c_str(), name( child ).c_str() );

        const auto players = find_child_by_class( dm, "Players" );
        const auto lp = local_player();
        std::printf( "players          = 0x%llX\n", static_cast< unsigned long long >( players ) );
        std::printf( "local_player     = 0x%llX %s\n", static_cast< unsigned long long >( lp ), name( lp ).c_str() );

        const auto character = player_character( lp );
        std::printf( "character        = 0x%llX %s\n", static_cast< unsigned long long >( character ),
                     name( character ).c_str() );
        for ( const auto child : children( character ) )
            std::printf( "  part  0x%llX  %-24s %s\n", static_cast< unsigned long long >( child ),
                         class_name( child ).c_str(), name( child ).c_str() );

        const auto dim = dimensions();
        std::printf( "viewport         = %.0f x %.0f\n", dim.x, dim.y );
    }

    void probe()
    {
        g_probe_log.open( "probe.txt", std::ios::trunc );

        const auto& o = get_offsets();
        const auto fdm = read_ptr( o.fake_data_model );
        const auto ve = read_ptr( o.visual_engine );
        const auto ve_fdm = ve ? read_ptr( ve + 0xaf0 ) : 0;  // VisualEngine::FakeDataModel

        plog( "external probe\n" );
        plog( "base=%p fake_global=%p fdm=%p ve=%p ve_fdm=%p\n", reinterpret_cast< void* >( g_process.base() ),
              reinterpret_cast< void* >( o.fake_data_model ), reinterpret_cast< void* >( fdm ),
              reinterpret_cast< void* >( ve ), reinterpret_cast< void* >( ve_fdm ) );

        struct entry
        {
            const char* label;
            std::uintptr_t ptr;
        };

        const entry entries[] = {
            { "*(fdm+0x1f8) current", fdm ? read_ptr( fdm + 0x1f8 ) : 0 },
            { "fdm+0x1f8 additive", fdm ? fdm + 0x1f8 : 0 },
            { "*(fdm+0x1f0)", fdm ? read_ptr( fdm + 0x1f0 ) : 0 },
            { "fdm itself", fdm },
            { "*(ve_fdm+0x1f8)", ve_fdm ? read_ptr( ve_fdm + 0x1f8 ) : 0 },
            { "ve_fdm itself", ve_fdm },
        };

        std::uintptr_t best = 0;
        const char* best_label = nullptr;
        std::uintptr_t fallback = 0;
        const char* fallback_label = nullptr;
        std::size_t fallback_kids = 0;
        int non_empty = 0;

        for ( const auto& e : entries )
        {
            const auto cls = class_name( e.ptr );
            const auto kids0 = children_mode( e.ptr, 0 );
            const auto kids1 = children_mode( e.ptr, 1 );
            const auto kids2 = children_mode( e.ptr, 2 );

            plog( "[probe] %-22s %p class='%s' kids inline=%llu ptrvec=%llu swap=%llu\n", e.label,
                  reinterpret_cast< void* >( e.ptr ), cls.c_str(), static_cast< unsigned long long >( kids0.size() ),
                  static_cast< unsigned long long >( kids1.size() ),
                  static_cast< unsigned long long >( kids2.size() ) );

            if ( cls == "DataModel" && !best )
            {
                best = e.ptr;
                best_label = e.label;
            }

            std::size_t most = kids0.size();
            if ( kids1.size() > most )
                most = kids1.size();
            if ( kids2.size() > most )
                most = kids2.size();

            if ( most > 0 )
            {
                ++non_empty;
                if ( most > fallback_kids )
                {
                    fallback_kids = most;
                    fallback = e.ptr;
                    fallback_label = e.label;
                }
            }
        }

        if ( best )
        {
            g_datamodel_override = best;
            plog( "[probe] resolved DataModel by class via '%s' -> %p\n", best_label,
                  reinterpret_cast< void* >( best ) );
        }
        else if ( non_empty == 1 && fallback )
        {
            g_datamodel_override = fallback;
            plog( "[probe] no 'DataModel' class; only '%s' has children -> %p\n", fallback_label,
                  reinterpret_cast< void* >( fallback ) );
        }
        else
        {
            plog( "[probe] no candidate produced a DataModel; trying TaskScheduler jobs\n" );
        }

        // Fallback: walk TaskScheduler jobs and take RenderJob's RealDataModel.
        if ( !g_datamodel_override && o.task_scheduler && o.ts_job_start && o.ts_job_end )
        {
            const auto ts = read_ptr( o.task_scheduler );
            const auto jobs_begin = ts ? read_ptr( ts + o.ts_job_start ) : 0;
            const auto jobs_end = ts ? read_ptr( ts + o.ts_job_end ) : 0;
            plog( "[probe] task_scheduler=%p jobs=%p..%p\n", reinterpret_cast< void* >( ts ),
                  reinterpret_cast< void* >( jobs_begin ), reinterpret_cast< void* >( jobs_end ) );

            if ( jobs_begin && jobs_end > jobs_begin && ( jobs_end - jobs_begin ) < 0x20000 )
            {
                for ( const std::uintptr_t stride : { static_cast< std::uintptr_t >( 0x10 ),
                                                      static_cast< std::uintptr_t >( 0x8 ) } )
                {
                    int shown = 0;
                    for ( auto slot = jobs_begin; slot + stride <= jobs_end && shown < 40; slot += stride )
                    {
                        const auto job = read_ptr( slot );
                        if ( !job )
                            continue;

                        const auto job_name = read_job_name( job, o.ts_job_name );
                        if ( job_name.empty() )
                            continue;

                        const auto dm_candidate = read_ptr( job + o.render_job_real_datamodel );
                        const auto cls = class_name( dm_candidate );
                        plog( "[probe]   job '%s' -> +0x1f0 %p class='%s'\n", job_name.c_str(),
                              reinterpret_cast< void* >( dm_candidate ), cls.c_str() );
                        ++shown;

                        if ( cls == "DataModel" && !g_datamodel_override )
                        {
                            g_datamodel_override = dm_candidate;
                            plog( "[probe] resolved DataModel via job '%s' -> %p\n", job_name.c_str(),
                                  reinterpret_cast< void* >( dm_candidate ) );
                        }
                    }
                    if ( shown > 0 )
                        break;
                }
            }
        }

        const auto dm = g_datamodel_override ? g_datamodel_override : data_model();
        plog( "[probe] dm=%p class='%s'\n", reinterpret_cast< void* >( dm ), class_name( dm ).c_str() );

        // Raw bytes, so the layout can be derived even if every guess is wrong.
        plog( "[probe] dm raw qwords:\n" );
        for ( std::uintptr_t off = 0; off <= 0xC0; off += 8 )
            plog( "    +0x%02llX = %p\n", static_cast< unsigned long long >( off ),
                  reinterpret_cast< void* >( read_ptr( dm + off ) ) );

        // Class descriptor diagnostics: dump it and try the usual string shapes.
        const auto desc = read_ptr( dm + o.instance_class_descriptor );
        plog( "[probe] class_descriptor=%p\n", reinterpret_cast< void* >( desc ) );
        for ( std::uintptr_t off = 0; off <= 0x60; off += 8 )
            plog( "    desc+0x%02llX = %p\n", static_cast< unsigned long long >( off ),
                  reinterpret_cast< void* >( read_ptr( desc + off ) ) );

        const auto s0 = read_cstr( desc );
        const auto s8 = read_cstr( desc + 0x8 );
        const auto s8p = read_cstr( read_ptr( desc + 0x8 ) );
        const auto s10 = read_cstr( desc + 0x10 );
        plog( "[probe] desc strings: +0x00='%s' +0x08='%s' *(+0x08)='%s' +0x10='%s'\n",
              printable( s0 ) ? s0.c_str() : "(x)", printable( s8 ) ? s8.c_str() : "(x)",
              printable( s8p ) ? s8p.c_str() : "(x)", printable( s10 ) ? s10.c_str() : "(x)" );

        // Which children layout actually works for this build?
        int chosen = -1;
        double chosen_ratio = 0.0;
        std::size_t chosen_valid = 0;

        for ( int mode = 0; mode < 4; ++mode )
        {
            const auto kids = children_mode( dm, mode );

            std::size_t valid = 0;
            for ( std::size_t i = 0; i < kids.size(); ++i )
            {
                const auto cls = class_name( kids[ i ] );
                const auto nm = name( kids[ i ] );
                if ( !cls.empty() || !nm.empty() )
                    ++valid;

                if ( i < 8 )
                    plog( "    [%llu] %p class='%s' name='%s'\n", static_cast< unsigned long long >( i ),
                          reinterpret_cast< void* >( kids[ i ] ), cls.c_str(), nm.c_str() );
            }

            const double ratio =
                kids.empty() ? 0.0 : static_cast< double >( valid ) / static_cast< double >( kids.size() );
            plog( "[probe] children mode %d: count=%llu valid=%llu ratio=%.2f\n", mode,
                  static_cast< unsigned long long >( kids.size() ), static_cast< unsigned long long >( valid ),
                  ratio );

            if ( !kids.empty() &&
                 ( chosen < 0 || ratio > chosen_ratio || ( ratio == chosen_ratio && valid > chosen_valid ) ) )
            {
                chosen = mode;
                chosen_ratio = ratio;
                chosen_valid = valid;
            }
        }

        if ( chosen >= 0 )
        {
            g_children_mode = chosen;
            plog( "[probe] using children mode %d\n", chosen );
        }
        else
        {
            plog( "[probe] no children layout produced valid instances\n" );
        }

        plog( "[probe] fdm raw qwords:\n" );
        for ( std::uintptr_t off = 0x1E0; off <= 0x210; off += 8 )
            plog( "    +0x%03llX = %p\n", static_cast< unsigned long long >( off ),
                  reinterpret_cast< void* >( read_ptr( fdm + off ) ) );

        plog( "probe written to probe.txt\n" );

        if ( g_probe_log )
        {
            g_probe_log.flush();
            g_probe_log.close();
        }
        std::fflush( stdout );
    }
}  // namespace ext::game
