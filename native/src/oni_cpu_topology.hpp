#pragma once
// Execution-only core count. See docs/settings.md for topology sources.
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#if defined(__linux__)
#include <sched.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace ma::oni::cpu {
// Count cores from OS topology, rather than assuming two logical CPUs per
// core (SMT may be disabled or the CPU may have mixed core types).
inline unsigned physical_core_count() noexcept {
    try {
#if defined(__linux__)
        cpu_set_t affinity; CPU_ZERO(&affinity);
        if (sched_getaffinity(0, sizeof(affinity), &affinity) != 0) return 1;
        std::set<std::string> cores;
        for (unsigned cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
            if (!CPU_ISSET(cpu, &affinity)) continue;
            const auto path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/";
            std::string siblings;
            std::ifstream current(path + "core_cpus_list");
            if (!(current >> siblings)) {
                std::ifstream legacy(path + "thread_siblings_list");
                if (!(legacy >> siblings)) return 1;
            }
            cores.insert(std::move(siblings));
        }
        return std::clamp(static_cast<unsigned>(cores.size()), 1U, 256U);
#elif defined(_WIN32)
        DWORD bytes = 0;
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) return 1;
        std::vector<std::byte> buffer(bytes);
        if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &bytes)) return 1;
        DWORD_PTR allowed = 0, system = 0;
        const bool single_group = GetActiveProcessorGroupCount() == 1 &&
            GetProcessAffinityMask(GetCurrentProcess(), &allowed, &system) && allowed != 0;
        unsigned cores = 0;
        for (std::size_t at = 0; at < bytes;) {
            if (bytes - at < offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor.GroupMask) + sizeof(GROUP_AFFINITY)) return 1;
            const auto* item = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + at);
            if (item->Size == 0 || item->Size > bytes - at) return 1;
            if (item->Relationship == RelationProcessorCore &&
                (!single_group || (item->Processor.GroupMask[0].Mask & allowed) != 0)) ++cores;
            at += item->Size;
        }
        return std::clamp(cores, 1U, 256U);
#endif
    } catch (...) { }
    // Without topology, one worker avoids treating SMT threads as cores.
    return 1;
}
}
