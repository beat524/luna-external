#pragma once

#include <cstdint>
#include <string>

namespace ext::game
{
    // Every Roblox-specific value lives here. These change on every Roblox update,
    // so they are loaded from offsets.ini (next to the executable) instead of being
    // hard-coded. Numeric values are read as hex; globals may also be given as a
    // byte-pattern via the matching "<name>_sig" key.
    struct offsets
    {
        // Globals, resolved once at start-up (pattern or module-relative offset).
        std::uintptr_t fake_data_model = 0;  // address of the FakeDataModel global
        std::uintptr_t visual_engine = 0;    // address of the VisualEngine global
        std::uintptr_t task_scheduler = 0;   // address of the TaskScheduler global

        // TaskScheduler -> jobs -> RenderJob -> DataModel (fallback path).
        std::uintptr_t ts_job_start = 0;
        std::uintptr_t ts_job_end = 0;
        std::uintptr_t ts_job_name = 0;
        std::uintptr_t render_job_real_datamodel = 0;

        // FakeDataModel -> DataModel
        std::uintptr_t fake_to_datamodel = 0;

        // Instance
        std::uintptr_t instance_children_start = 0;    // std::vector begin
        std::uintptr_t instance_children_end = 0;      // std::vector end
        std::uintptr_t instance_name = 0;              // Name container pointer
        std::uintptr_t instance_name_offset = 0;       // string offset inside the Name container
        std::uintptr_t instance_class_descriptor = 0;  // ClassDescriptor*
        std::uintptr_t class_descriptor_name = 0;      // std::string inside ClassDescriptor

        // Player
        std::uintptr_t player_character = 0;
        std::uintptr_t player_team = 0;

        // Players service
        std::uintptr_t players_local_player = 0;

        // Humanoid
        std::uintptr_t humanoid_health = 0;
        std::uintptr_t humanoid_max_health = 0;

        // BasePart
        std::uintptr_t part_position = 0;  // direct Vector3 (older layout / fallback)
        std::uintptr_t part_size = 0;      // direct Vector3 (fallback)

        // BasePart -> Primitive indirection (modern Roblox). Takes priority when set.
        std::uintptr_t basepart_primitive = 0;
        std::uintptr_t primitive_position = 0;
        std::uintptr_t primitive_size = 0;

        // Workspace
        std::uintptr_t workspace_camera = 0;

        // VisualEngine
        std::uintptr_t view_matrix = 0;  // 4x4 float
        std::uintptr_t dimensions = 0;   // Vector2
    };

    bool load_offsets( const std::string& path );
    const offsets& get_offsets();
    bool offsets_valid();
}  // namespace ext::game
