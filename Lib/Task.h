#pragma once

#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

struct TimeWindows {
    int WindowsId;
    int TaskIndex;
    int SatelliteIndex;
    int DeviceIndex;
    int StartTime;
    int EndTime;
};

struct Tasks{
    int TaskId;
    std::vector<int> TWs;
};

void TimeWindowsProcess(std::vector<TimeWindows>& AllTimewindows, const std::string &file_name, std::vector<Tasks> &AllTasks,int &TaskEndTime, int &);