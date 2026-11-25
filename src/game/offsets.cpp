#include "offsets.hpp"

#include "core/process.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>

namespace ext::game
{
    namespace
    {
        offsets g_offsets{};
        std::unordered_map< std::string, std::string > g_raw;

        std::string trim( std::string text )
        {
            const auto not_space = []( const unsigned char c ) { return !std::isspace( c ); };
            text.erase( text.begin(), std::find_if( text.begin(), text.end(), not_space ) );
            text.erase( std::find_if( text.rbegin(), text.rend(), not_space ).base(), text.end() );
            return text;
        }

        bool has( const std::string& key )
        {
            return g_raw.find( key ) != g_raw.end();
        }

        // Numeric values are always parsed as hex (0x prefix optional).
        std::uintptr_t number( const std::string& key )
        {
            const auto it = g_raw.find( key );
            if ( it == g_raw.end() )
                return 0;
            try
            {
                return static_cast< std::uintptr_t >( std::stoull( it->second, nullptr, 16 ) );
            }
            catch ( ... )
            {
                return 0;
            }
        }

        // Resolve a global: prefer a signature scan, fall back to a module-relative offset.
        std::uintptr_t resolve_global( const std::string& key )
        {
            if ( has( key + "_sig" ) )
            {
                const auto found = g_process.scan( g_raw[ key + "_sig" ] );
                if ( found )
                    return found;
            }
            return g_process.rebase( number( key ) );
        }
    }  // namespace

    bool load_offsets( const std::string& path )
    {
        std::ifstream file( path );
        if ( !file )
            return false;

        std::string line;
        while ( std::getline( file, line ) )
        {
            line = trim( line );
            if ( line.empty() || line[ 0 ] == '#' || line[ 0 ] == ';' || line[ 0 ] == '[' )
                continue;

            const auto equals = line.find( '=' );
            if ( equals == std::string::npos )
                continue;

            g_raw[ trim( line.substr( 0, equals ) ) ] = trim( line.substr( equals + 1 ) );
        }

        g_offsets.fake_data_model = resolve_global( "fake_data_model" );
        g_offsets.visual_engine = resolve_global( "visual_engine" );
        g_offsets.task_scheduler = resolve_global( "task_scheduler" );

        g_offsets.ts_job_start = number( "ts_job_start" );
        g_offsets.ts_job_end = number( "ts_job_end" );
        g_offsets.ts_job_name = number( "ts_job_name" );
        g_offsets.render_job_real_datamodel = number( "render_job_real_datamodel" );

        g_offsets.fake_to_datamodel = number( "fake_to_datamodel" );
        g_offsets.instance_children_start = number( "instance_children_start" );
        g_offsets.instance_children_end = number( "instance_children_end" );
        g_offsets.instance_name = number( "instance_name" );
        g_offsets.instance_name_offset = number( "instance_name_offset" );
        g_offsets.instance_class_descriptor = number( "instance_class_descriptor" );
        g_offsets.class_descriptor_name = number( "class_descriptor_name" );
        g_offsets.player_character = number( "player_character" );
        g_offsets.player_team = number( "player_team" );
        g_offsets.players_local_player = number( "players_local_player" );
        g_offsets.humanoid_health = number( "humanoid_health" );
        g_offsets.humanoid_max_health = number( "humanoid_max_health" );
        g_offsets.part_position = number( "part_position" );
        g_offsets.part_size = number( "part_size" );
        g_offsets.basepart_primitive = number( "basepart_primitive" );
        g_offsets.primitive_position = number( "primitive_position" );
        g_offsets.primitive_size = number( "primitive_size" );
        g_offsets.workspace_camera = number( "workspace_camera" );
        g_offsets.view_matrix = number( "view_matrix" );
        g_offsets.dimensions = number( "dimensions" );
        return true;
    }

    const offsets& get_offsets()
    {
        return g_offsets;
    }

    bool offsets_valid()
    {
        const auto& o = g_offsets;
        const bool parts_ok = ( o.basepart_primitive && o.primitive_position ) || o.part_position;
        return o.fake_data_model && o.fake_to_datamodel && o.instance_children_start && o.instance_children_end &&
               o.instance_name && o.instance_class_descriptor && o.class_descriptor_name && o.player_character &&
               o.players_local_player && o.visual_engine && o.view_matrix && o.dimensions && parts_ok;
    }
}  // namespace ext::game
