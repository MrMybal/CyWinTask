#pragma once

#include <future>
#include <string>
#include <vector>

struct HardwareField {
    std::string group;
    std::string label;
    std::string value;
};

// Inventory is read once in the background; no hardware calls occur in a UI frame.
class HardwareInventory {
public:
    HardwareInventory();
    void poll();
    bool ready() const { return ready_; }
    const std::vector<HardwareField>& fields() const { return fields_; }
private:
    static std::vector<HardwareField> collect();
    std::future<std::vector<HardwareField>> pending_;
    std::vector<HardwareField> fields_;
    bool ready_ {};
};
