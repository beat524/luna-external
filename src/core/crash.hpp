#pragma once

namespace ext::crash
{
    // Installs an unhandled-exception filter and a std::terminate handler that
    // append a readable report to crash.txt next to the executable.
    void install();
}  // namespace ext::crash
