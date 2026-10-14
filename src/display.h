#pragma once

struct DisplayConfig
{
    bool resolutionList = true;  // video options list the monitor's resolutions ([Display] ResolutionList)
    bool aboveDesktop = false;   // ... including those above the desktop resolution ([Display] AboveDesktop)
    bool matchRefresh = true;    // fullscreen at the Windows refresh rate, not 60 Hz ([Display] MatchRefreshRate)
    int refreshRate = 0;         // forced refresh rate in Hz, 0 = same as Windows ([Display] RefreshRate)
};

bool InstallDisplay(const DisplayConfig& cfg);
