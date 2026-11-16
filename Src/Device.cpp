#include "Device.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

std::string TrimString2(std::string str) {
    const std::string whiteSpaces = " \t\n\r\f\v";
    // Remove leading whitespace
    size_t first_non_space = str.find_first_not_of(whiteSpaces);
    str.erase(0, first_non_space);
    // Remove trailing whitespace
    size_t last_non_space = str.find_last_not_of(whiteSpaces);
    str.erase(last_non_space + 1);
    return str;
}


void DeviceProcess(std::unordered_map<int, int>& deviceTransferTime, const std::string &fileName,
                    std::unordered_set<int>& accessibleSatellite) {
    // CSV format:
    // n, m
    // n lines of satellite IDs
    // m lines of device ID and transfer time
    deviceTransferTime.clear();
    accessibleSatellite.clear();
    std::cout << "1111\n";
    std::ifstream csvData(fileName, std::ios::in);
    if (!csvData.is_open()) {
        std::cout << "Error: opening data file fail" << std::endl;
        exit(1);
    } else {
		// Clear data from the previous round.
        deviceTransferTime.clear(); 
		// Device transfer times are read below.
        accessibleSatellite.clear();
        
        int n, m;
        std::string line;
        std::istringstream sin;
        std::vector<std::string> words;
        std::string word;

        getline(csvData, line);
        words.clear();
        sin.clear();
        sin.str(line);
        while (std::getline(sin, word, ',')) {
            words.push_back(word);
        }
        n = std::stoi(TrimString2(words[0]));
        m = std::stoi(TrimString2(words[1]));

        for (int i = 0; i < n; ++i) {
            getline(csvData, line);
            words.clear();
            sin.clear();
            sin.str(line);
            while (std::getline(sin, word, ',')) {
                words.push_back(word);
            }
            accessibleSatellite.insert(std::stoi(TrimString2(words[0])));
        }

        for (int j = 0; j < m; ++j) {
            getline(csvData, line);
            words.clear();
            sin.clear();
            sin.str(line);
            while (std::getline(sin, word, ',')) {
                words.push_back(word);
            }
            int dev_id = std::stoi(TrimString2(words[0]));
            int tranfer_time = std::stoi(TrimString2(words[1]));
            deviceTransferTime[dev_id] = tranfer_time;
        }
        csvData.close();
    }
}
