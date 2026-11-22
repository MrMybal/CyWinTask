#include "HardwareInventory.h"

#include <cyhwinfo/cpu.hpp>
#include <cyhwinfo/gpu.hpp>
#include <cyhwinfo/memory.hpp>
#include <cyhwinfo/motherboard.hpp>

#include <chrono>
#include <cstdio>

namespace {
std::string number(double value, const char* unit)
{
    char buffer[80] {};
    std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, unit);
    return buffer;
}
constexpr double gibibyte = 1073741824.0;
constexpr double mebibyte = 1048576.0;
}

HardwareInventory::HardwareInventory()
    : pending_(std::async(std::launch::async, [] { return collect(); })) {}

void HardwareInventory::poll()
{
    if (ready_ || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try { fields_ = pending_.get(); }
    catch (...) { fields_.clear(); }
    ready_ = true;
}

std::vector<HardwareField> HardwareInventory::collect()
{
    std::vector<HardwareField> result;
    const auto add = [&](const std::string& group, const char* label, const std::string& value) {
        if (!value.empty()) result.push_back({group, label, value});
    };
    if (const auto cpus = cyhwinfo::cpu::enumerate()) {
        for (const auto& cpu : *cpus) {
            const auto group = std::string("CPU ") + std::to_string(cpu.socket);
            add(group, "Model", cpu.model);
            add(group, "Physical cores", std::to_string(cpu.physical_cores));
            add(group, "Logical cores", std::to_string(cpu.logical_cores));
            for (const auto& core : cpu.cores) if (core.base_frequency_hz) {
                add(group, "Base frequency", number(*core.base_frequency_hz / 1e9, "GHz")); break;
            }
            for (const auto& core : cpu.cores) if (core.max_frequency_hz) {
                add(group, "Maximum frequency", number(*core.max_frequency_hz / 1e9, "GHz")); break;
            }
            for (const auto& core : cpu.cores) if (core.cache.l3_bytes) {
                add(group, "L3 cache", number(*core.cache.l3_bytes / mebibyte, "MiB")); break;
            }
        }
    }
    if (const auto board = cyhwinfo::motherboard::inventory()) {
        add("Motherboard", "Vendor", board->vendor.value_or(""));
        add("Motherboard", "Model", board->name.value_or(""));
        add("Motherboard", "Firmware", board->firmware_version.value_or(""));
    }
    if (const auto memory = cyhwinfo::memory::inventory()) {
        add("Memory", "Installed", number(memory->total_bytes / gibibyte, "GiB"));
        if (memory->slots) add("Memory", "Slots", std::to_string(*memory->slots));
        if (memory->modules) for (const auto& module : *memory->modules) {
            const auto group = std::string("Memory module ") + std::to_string(module.index);
            add(group, "Position", module.location.value_or(""));
            if (module.size_bytes) add(group, "Capacity", number(*module.size_bytes / gibibyte, "GiB"));
            add(group, "Type", module.type.value_or(""));
            if (module.configured_rate_mt_per_second)
                add(group, "Speed", std::to_string(*module.configured_rate_mt_per_second) + " MT/s");
        }
    }
    if (const auto gpus = cyhwinfo::gpu::enumerate()) for (const auto& gpu : *gpus) {
        const auto group = std::string("GPU ") + std::to_string(gpu.index);
        add(group, "Model", gpu.name);
        add(group, "Driver", gpu.driver_version.value_or(""));
        if (gpu.dedicated_memory_bytes)
            add(group, "Dedicated memory", number(*gpu.dedicated_memory_bytes / gibibyte, "GiB"));
        if (gpu.memory_bus_width_bits)
            add(group, "Memory bus", std::to_string(*gpu.memory_bus_width_bits) + " bits");
        if (gpu.max_frequency_hz)
            add(group, "Maximum core clock", number(*gpu.max_frequency_hz / 1e6, "MHz"));
        if (gpu.max_memory_frequency_hz)
            add(group, "Maximum memory clock", number(*gpu.max_memory_frequency_hz / 1e6, "MHz"));
        if (gpu.power_limit_watts)
            add(group, "Power limit", number(*gpu.power_limit_watts, "W"));
    }
    return result;
}
