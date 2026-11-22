#include "../src/HardwareInventory.h"

#include <chrono>
#include <cstdio>
#include <thread>

int main()
{
    HardwareInventory inventory;
    for (int attempt = 0; attempt < 200 && !inventory.ready(); ++attempt) {
        inventory.poll();
        if (!inventory.ready()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!inventory.ready() || inventory.fields().empty()) {
        std::fprintf(stderr, "Hardware inventory did not return fields\n");
        return 1;
    }
    std::printf("Hardware inventory returned %zu fields\n", inventory.fields().size());
    return 0;
}
