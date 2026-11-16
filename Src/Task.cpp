#include "Task.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

std::string TrimString1(std::string str) {
    const std::string whiteSpaces = " \t\n\r\f\v";
    // Remove leading whitespace
    size_t first_non_space = str.find_first_not_of(whiteSpaces);
    str.erase(0, first_non_space);
    // Remove trailing whitespace
    size_t last_non_space = str.find_last_not_of(whiteSpaces);
    str.erase(last_non_space + 1);
    return str;
}

void TimeWindowsProcess(std::vector<TimeWindows>& allTimewindows, const std::string &fileName, std::vector<Tasks> &allTasks,int &TaskEndTime, int &WindowsIndex) {
    // Each CSV row: TaskIndex, SatelliteIndex, DeviceIndex, StartTime, EndTime.
    std::ifstream csvData(fileName, std::ios::in);
    if (!csvData.is_open()) {
        std::cout << "Error: opening task file fail" << std::endl;
        exit(1);
    } else {
        std::string line;
        std::istringstream sin;
        std::vector<std::string> words;
        std::string word;
        getline(csvData, line);
        words.clear();
        sin.clear();
        sin.str(line);
        std::getline(sin, word, ',');
        words.push_back(word);

        int endtime = std::stoi(TrimString1(words[0]));
        TaskEndTime = endtime;
        // Read windows to be scheduled before endtime.

        while (getline(csvData, line)) {
            words.clear();
            sin.clear();
            sin.str(line);
            while (std::getline(sin, word, ',')) {
                words.push_back(word);
            }
            TimeWindows newForecast;
            int TaskIndex = std::stoi(TrimString1(words[0]));
            newForecast.WindowsId = WindowsIndex;
            newForecast.TaskIndex = TaskIndex;
            newForecast.SatelliteIndex = std::stoi(TrimString1(words[1]));
            newForecast.DeviceIndex = std::stoi(TrimString1(words[2]));
            newForecast.StartTime = std::stoi(TrimString1(words[3]));
            newForecast.EndTime = std::stoi(TrimString1(words[4]));
            // Task ID, satellite ID, device ID, start time, end time.
			
            // Store the current time window.
            allTimewindows.push_back(newForecast);
            
            // Keep task IDs aligned with vector indices.
            Tasks task;
            while((int)allTasks.size() <= TaskIndex){
                allTasks.push_back(task);
            }

            allTasks[TaskIndex].TaskId = TaskIndex;
            allTasks[TaskIndex].TWs.push_back(WindowsIndex);
            // Record this task's time-window ID.
            WindowsIndex ++;
        }   
        csvData.close();
    }
}

