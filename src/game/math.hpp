#pragma once

#include <cmath>

namespace ext
{
    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        Vec3 operator+( const Vec3& other ) const { return { x + other.x, y + other.y, z + other.z }; }
        Vec3 operator-( const Vec3& other ) const { return { x - other.x, y - other.y, z - other.z }; }
        float length() const { return std::sqrt( x * x + y * y + z * z ); }
    };

    struct Mat4
    {
        float m[ 4 ][ 4 ]{};
    };

    inline float screen_distance( const Vec2& a, const Vec2& b )
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return std::sqrt( dx * dx + dy * dy );
    }

    inline bool on_screen( const Vec2& p, const Vec2& dimensions )
    {
        return std::isfinite( p.x ) && std::isfinite( p.y ) && p.x > -dimensions.x && p.x < dimensions.x * 2.0f &&
               p.y > -dimensions.y && p.y < dimensions.y * 2.0f;
    }

    // Roblox's view matrix is normally read with the translation in the LAST row:
    //   w = m[3][0..3] . point
    // This is the primary convention.
    inline bool project_primary( const Mat4& v, const Vec3& p, const Vec2& dims, Vec2& out )
    {
        const float w = v.m[ 3 ][ 0 ] * p.x + v.m[ 3 ][ 1 ] * p.y + v.m[ 3 ][ 2 ] * p.z + v.m[ 3 ][ 3 ];
        if ( w < 0.1f )
            return false;

        const float x = v.m[ 0 ][ 0 ] * p.x + v.m[ 0 ][ 1 ] * p.y + v.m[ 0 ][ 2 ] * p.z + v.m[ 0 ][ 3 ];
        const float y = v.m[ 1 ][ 0 ] * p.x + v.m[ 1 ][ 1 ] * p.y + v.m[ 1 ][ 2 ] * p.z + v.m[ 1 ][ 3 ];

        out.x = dims.x * 0.5f * ( 1.0f + x / w );
        out.y = dims.y * 0.5f * ( 1.0f - y / w );
        return std::isfinite( out.x ) && std::isfinite( out.y );
    }

    // Transposed layout (translation in the last column), kept as a fallback in
    // case a particular dump uses the other convention.
    inline bool project_transposed( const Mat4& v, const Vec3& p, const Vec2& dims, Vec2& out )
    {
        const float w = v.m[ 0 ][ 3 ] * p.x + v.m[ 1 ][ 3 ] * p.y + v.m[ 2 ][ 3 ] * p.z + v.m[ 3 ][ 3 ];
        if ( w < 0.1f )
            return false;

        const float x = v.m[ 0 ][ 0 ] * p.x + v.m[ 1 ][ 0 ] * p.y + v.m[ 2 ][ 0 ] * p.z + v.m[ 3 ][ 0 ];
        const float y = v.m[ 0 ][ 1 ] * p.x + v.m[ 1 ][ 1 ] * p.y + v.m[ 2 ][ 1 ] * p.z + v.m[ 3 ][ 1 ];

        out.x = dims.x * 0.5f * ( 1.0f + x / w );
        out.y = dims.y * 0.5f * ( 1.0f - y / w );
        return std::isfinite( out.x ) && std::isfinite( out.y );
    }

    // Tries the primary convention, falls back to the transposed one. Reports
    // which convention was used so the debug HUD can show it.
    inline bool world_to_screen( const Mat4& view, const Vec3& point, const Vec2& dimensions, Vec2& out,
                                 bool* used_transposed = nullptr )
    {
        if ( project_primary( view, point, dimensions, out ) && on_screen( out, dimensions ) )
        {
            if ( used_transposed )
                *used_transposed = false;
            return true;
        }

        Vec2 alt;
        if ( project_transposed( view, point, dimensions, alt ) && on_screen( alt, dimensions ) )
        {
            out = alt;
            if ( used_transposed )
                *used_transposed = true;
            return true;
        }
        return false;
    }
}  // namespace ext
