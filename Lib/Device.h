#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

void DeviceProcess(std::unordered_map<int, int>& device_transfer_time, const std::string &file_name,
                    std::unordered_set<int>& accessible_satellite);