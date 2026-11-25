#include <windows.h>

#include <iostream>
#include <string>

#include "core/crash.hpp"
#include "core/process.hpp"
#include "features/features.hpp"
#include "game/offsets.hpp"
#include "game/roblox.hpp"
#include "overlay/overlay.hpp"

int main( int argc, char** argv )
{
    ext::crash::install();

    bool dump_only = false;
    for ( int i = 1; i < argc; ++i )
        if ( std::string( argv[ i ] ) == "--dump" )
            dump_only = true;

    std::cout << "[*] waiting for RobloxPlayerBeta.exe...\n";
    while ( !ext::g_process.attach( L"RobloxPlayerBeta.exe" ) )
        Sleep( 1000 );

    std::cout << "[+] attached  pid=" << ext::g_process.pid() << "  base=0x" << std::hex << ext::g_process.base()
              << std::dec << "\n";

    // Offsets are resolved after attaching, because signature scans need the process.
    if ( !ext::game::load_offsets( "offsets.ini" ) )
        std::cout << "[!] offsets.ini not found next to the exe (using defaults)\n";

    if ( dump_only )
    {
        ext::game::probe();
        ext::game::dump();
        return 0;
    }

    if ( !ext::game::offsets_valid() )
        std::cout << "[!] offsets.ini is incomplete - ESP/aimbot will do nothing until it is filled in\n";

    if ( !ext::overlay::init() )
    {
        std::cout << "[!] overlay init failed\n";
        return 1;
    }

    ext::overlay::run();
    ext::overlay::shutdown();
    return 0;
}
