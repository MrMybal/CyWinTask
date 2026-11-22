#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct TemperatureReading {
    std::string name;
    double current {}, minimum {}, maximum {};
};

class SensorLog {
public:
    bool chooseFile();
    void openFile(const std::filesystem::path& path);
    void refresh();
    void clear();
    bool enabled() const { return enabled_; }
    void setEnabled(bool enabled) { enabled_ = enabled; }
    const std::filesystem::path& path() const { return path_; }
    const std::string& error() const { return error_; }
    const std::vector<TemperatureReading>& readings() const { return readings_; }
private:
    std::filesystem::path path_;
    std::filesystem::file_time_type lastWrite_ {};
    std::uintmax_t lastSize_ {};
    bool enabled_ {};
    std::string error_;
    std::vector<TemperatureReading> readings_;
};
