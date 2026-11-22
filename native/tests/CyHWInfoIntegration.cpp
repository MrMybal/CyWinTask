#include <cyhwinfo/cpu.hpp>
#include <cyhwinfo/gpu.hpp>
#include <cyhwinfo/gpu_telemetry.hpp>
#include <cyhwinfo/gpu_processes.hpp>
#include <cyhwinfo/memory.hpp>
#include <cyhwinfo/motherboard.hpp>
#include <cyhwinfo/sensors.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

namespace {
template<class T> void append(std::vector<char>& data, T value)
{
    const auto* bytes = reinterpret_cast<const char*>(&value);
    data.insert(data.end(), bytes, bytes + sizeof(value));
}
void appendString(std::vector<char>& data, const char* value)
{
    const auto size = static_cast<std::uint32_t>(std::strlen(value));
    append(data, size);
    data.insert(data.end(), value, value + size);
}
void send(std::vector<char> frame)
{
    const auto size = static_cast<std::uint32_t>(frame.size());
    std::cout.write(reinterpret_cast<const char*>(&size), sizeof(size));
    std::cout.write(frame.data(), frame.size());
    std::cout.flush();
}
int mockBridge()
{
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
    std::vector<char> ready;
    append<std::uint8_t>(ready, 1);
    append<std::uint32_t>(ready, 6);
    send(std::move(ready));
    while (std::cin.get() == 'S') {
        std::vector<char> sample;
        append<std::uint8_t>(sample, 2);
        append<std::uint8_t>(sample, 0); // elevated
        append<std::uint8_t>(sample, 1); // PawnIO installed
        append<std::uint8_t>(sample, 1); // PawnIO accessible
        append<std::uint8_t>(sample, 1); // CPU enabled
        appendString(sample, "");
        append<std::uint32_t>(sample, 1);
        append<std::uint8_t>(sample, 0); // temperature
        append<std::uint8_t>(sample, 0); // LHM source
        appendString(sample, "demo-cpu");
        appendString(sample, "Demo CPU");
        appendString(sample, "demo-zero");
        appendString(sample, "Synthetic zero");
        append<std::uint8_t>(sample, 1);
        append<double>(sample, 0.0);
        for (int group = 0; group < 2; ++group) {
            append<std::uint8_t>(sample, 1);
            appendString(sample, "");
            append<std::uint32_t>(sample, 0);
        }
        send(std::move(sample));
    }
    return 0;
}
}

int main(int argc, char** argv)
{
    if (argc == 4 && std::strcmp(argv[1], "1") == 0) return mockBridge();
    const auto cpus = cyhwinfo::cpu::enumerate();
    const auto memory = cyhwinfo::memory::inventory();
    const auto board = cyhwinfo::motherboard::inventory();
    const auto gpus = cyhwinfo::gpu::enumerate();
    if (!cpus || cpus->empty() || !memory || !board || !gpus) return 1;
    std::cout << "PASS inventory components\n";

    std::size_t gpu_samples = 0;
    std::size_t gpu_loads = 0, gpu_temperatures = 0, gpu_power = 0, gpu_clocks = 0, gpu_vram = 0;
    for (const auto& gpu : *gpus) {
        auto sampler = cyhwinfo::gpu::Sampler::open(gpu.id);
        if (!sampler) continue;
        const auto sample = sampler->sample();
        if (!sample) continue;
        if (sample->temperature_celsius.value &&
            (!std::isfinite(*sample->temperature_celsius.value) ||
             *sample->temperature_celsius.value < -30 || *sample->temperature_celsius.value > 150)) return 2;
        gpu_loads += sample->utilization_percent.value.has_value();
        gpu_temperatures += sample->temperature_celsius.value.has_value();
        gpu_power += sample->power_watts.value.has_value();
        gpu_clocks += sample->frequency_hz.value.has_value();
        gpu_vram += sample->dedicated_memory_used_bytes.value.has_value();
        ++gpu_samples;
    }
    std::cout << "PASS GPU telemetry samples: " << gpu_samples << "; load=" << gpu_loads
              << "; temperature=" << gpu_temperatures << "; power=" << gpu_power
              << "; clock=" << gpu_clocks << "; VRAM=" << gpu_vram << '\n';

    std::size_t matched_gpu_adapters = 0, gpu_processes = 0;
    for (const auto& gpu : *gpus) {
        auto sampler = cyhwinfo::gpu::ProcessSampler::open(gpu);
        if (!sampler) continue;
        const auto sample = sampler->sample();
        if (!sample || !sample->adapter_matched) continue;
        ++matched_gpu_adapters;
        gpu_processes += sample->processes.size();
    }
    std::cout << "PASS WDDM GPU process counters: adapters=" << matched_gpu_adapters
              << "; process samples=" << gpu_processes << '\n';

    cyhwinfo::sensors::Options options{};
    options.cpu = true;
    options.motherboard = true;
    options.memory = true;
    auto sampler = cyhwinfo::sensors::Sampler::open(options);
    if (!sampler) {
        std::cout << "Advanced collector unavailable; native inventory and GPU telemetry continue\n";
    } else {
        auto sample = sampler->sample();
        if (!sample || !sample->cpu || !sample->motherboard || !sample->memory) return 3;
        std::size_t readings = 0;
        for (const auto* group : {&sample->cpu, &sample->motherboard, &sample->memory}) {
            for (const auto& sensor : (*group)->sensors) {
                if (sensor.reading.value.has_value()) {
                    if (!std::isfinite(*sensor.reading.value)) return 4;
                    ++readings;
                }
            }
        }
        std::cout << "PASS advanced sensor readings: " << readings
                  << "; PawnIO installed=" << sample->pawnio_installed
                  << "; accessible=" << sample->pawnio_accessible << '\n';
    }
    options.bridge_executable = L"Z:\\CyWinTask-No-Sensor-Bridge.exe";
    if (cyhwinfo::sensors::Sampler::open(options)) return 5;
    if (!cyhwinfo::memory::usage()) return 6;
    std::cout << "PASS missing collector fallback\n";
    wchar_t executable[MAX_PATH] {};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return 7;
    options.bridge_executable = executable;
    auto synthetic = cyhwinfo::sensors::Sampler::open(options);
    if (!synthetic) return 8;
    auto zero = synthetic->sample();
    if (!zero || !zero->pawnio_accessible || !zero->cpu || zero->cpu->sensors.size() != 1
        || !zero->cpu->sensors[0].reading.value.has_value()
        || *zero->cpu->sensors[0].reading.value != 0.0) return 9;
    std::cout << "PASS accessible driver protocol and zero reading (synthetic)\n";
    return 0;
}
