#pragma once

/**
 * Struct holding pointers to REAPER API functions resolved at plugin load time
 * via reaper_plugin_info_t::GetFunc().
 */
struct ReaperAPI
{
    int (*GetPlayState)() = nullptr;
    double (*GetPlayPosition)() = nullptr;
    double (*GetCursorPosition)() = nullptr;
    double (*TimeMap2_timeToQN)(void *proj, double tpos) = nullptr;
    double (*TimeMap2_GetDividedBpmAtTime)(void *proj, double time) = nullptr;
    double (*Master_GetTempo)() = nullptr;

    bool resolve(void *(*GetFunc)(const char *))
    {
        GetPlayState = reinterpret_cast<int (*)()>(GetFunc("GetPlayState"));
        GetPlayPosition = reinterpret_cast<double (*)()>(GetFunc("GetPlayPosition"));
        GetCursorPosition = reinterpret_cast<double (*)()>(GetFunc("GetCursorPosition"));
        TimeMap2_timeToQN = reinterpret_cast<double (*)(void *, double)>(GetFunc("TimeMap2_timeToQN"));
        TimeMap2_GetDividedBpmAtTime = reinterpret_cast<double (*)(void *, double)>(GetFunc("TimeMap2_GetDividedBpmAtTime"));
        Master_GetTempo = reinterpret_cast<double (*)()>(GetFunc("Master_GetTempo"));

        return GetPlayState && GetPlayPosition && GetCursorPosition &&
               TimeMap2_timeToQN && TimeMap2_GetDividedBpmAtTime && Master_GetTempo;
    }
};