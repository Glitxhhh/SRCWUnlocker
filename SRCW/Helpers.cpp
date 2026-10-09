#include "Helpers.h"
#include <cstring>

BYTE* PatternScan(const char* signature)
{
    static auto pattern_to_byte = [](const char* pattern) {
        auto bytes = std::vector<int>{}; auto start = const_cast<char*>(pattern); auto end = const_cast<char*>(pattern) + strlen(pattern);
        for (auto current = start; current < end; ++current) {
            if (*current == '?') { ++current; if (*current == '?') ++current; bytes.push_back(-1); }
            else { bytes.push_back(strtoul(current, &current, 16)); }
        }
        return bytes;
    };
    auto dosHeader = (PIMAGE_DOS_HEADER)module; auto ntHeaders = (PIMAGE_NT_HEADERS)((std::uint8_t*)module + dosHeader->e_lfanew);
    auto sizeOfImage = ntHeaders->OptionalHeader.SizeOfImage; auto patternBytes = pattern_to_byte(signature);
    auto scanBytes = reinterpret_cast<std::uint8_t*>(module); auto s = patternBytes.size(); auto d = patternBytes.data();
    for (auto i = 0ul; i < sizeOfImage - s; ++i) {
        bool found = true;
        for (auto j = 0ul; j < s; ++j) { if (scanBytes[i + j] != d[j] && d[j] != -1) { found = false; break; } }
        if (found) return &scanBytes[i];
    }
    return nullptr;
}

void Patch(BYTE* src, BYTE* dst, const ULONG64 size) { DWORD p; VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &p); memcpy_s(dst, size, src, size); VirtualProtect(dst, size, p, &p); }

bool Detour64(BYTE* src, BYTE* dst, const ULONG64 size) {
    if (size < 12) return false; DWORD p; VirtualProtect(src, size, PAGE_EXECUTE_READWRITE, &p);
    *(BYTE*)src = 0x48; *(BYTE*)(src + 1) = 0xB8; *(ULONG64*)(src + 2) = (ULONG64)dst; *(BYTE*)(src + 10) = 0xFF; *(BYTE*)(src + 11) = 0xE0;
    VirtualProtect(src, size, p, &p); return true;
}

BYTE* TrampHook64(BYTE* src, BYTE* dst, const ULONG64 size) {
    if (size < 12) return 0;
    BYTE* gateway = (BYTE*)VirtualAlloc(0, size + 12, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!gateway) return 0;
    memcpy_s(gateway, size, src, size);
    *(BYTE*)(gateway + size) = 0x48; *(BYTE*)(gateway + size + 1) = 0xB8;
    *(ULONG64*)((ULONG64)gateway + size + 2) = (ULONG64)src + size;
    *(BYTE*)(gateway + size + 10) = 0xFF; *(BYTE*)(gateway + size + 11) = 0xE0;
    Detour64(src, dst, size); return gateway;
}

BYTE* RemoveHook(BYTE* src, BYTE* orig, const ULONG64 size) {
    if (size < 12) return 0; DWORD p; VirtualProtect(orig, size, PAGE_EXECUTE_READWRITE, &p);
    memcpy_s(orig, size, src, size); VirtualProtect(orig, size, p, &p); VirtualFree(src, 0, MEM_RELEASE); return orig;
}

std::uintptr_t GetAddressFromInstruction(std::uintptr_t address, int instruction_size) {
    if (address == (std::uintptr_t)nullptr || instruction_size < 5) throw 0;
    return address + instruction_size + *(int*)(address + instruction_size - 4);
}

const void* VTHook(const void** vtable, const int index, const void* hook) {
    DWORD p; const auto* orig = vtable[index]; if (!orig || !hook || orig == hook) return orig;
    VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &p); vtable[index] = hook; VirtualProtect(&vtable[index], sizeof(void*), p, &p); return orig;
}

void* ShadowVT(void* instance) {
    if (ShadowVTMap.find((uint64_t)instance) != ShadowVTMap.end()) return nullptr;
    uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(instance); MEMORY_BASIC_INFORMATION mbi{}; size_t size{};
    while (VirtualQuery(reinterpret_cast<LPCVOID>(vtable[size]), &mbi, sizeof(mbi))) {
        if ((mbi.State != MEM_COMMIT) || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) || !vtable[size]) break;
        ++size;
    }
    uintptr_t* shadow = (uintptr_t*)VirtualAlloc(nullptr, size * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!shadow) return nullptr;
    memcpy(shadow, vtable, size * sizeof(uintptr_t)); *reinterpret_cast<uintptr_t**>(instance) = shadow; ShadowVTMap[(uint64_t)instance] = (uint64_t)shadow;
    for (auto it = ShadowVTMap.begin(); it != ShadowVTMap.end(); ) { if (IsBadReadPtr((void*)(it->first), 8)) { VirtualFree((void*)(it->second), 0, MEM_RELEASE); it = ShadowVTMap.erase(it); } else { ++it; } }
    return shadow;
}

// =============================================================================
// Runtime discovery of engine globals
//
// Every game update shifts the fixed offsets Dumper-7 bakes into Basic.hpp, so
// instead we recognise the data structures themselves in the module's writable
// memory by their internal consistency. This depends on the UE5.4 memory layout,
// not on any particular build's instruction bytes or addresses.
// =============================================================================
static bool IsReadableRange(const void* p, size_t n)
{
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (const uint8_t*)p + n <= (const uint8_t*)mbi.BaseAddress + mbi.RegionSize;
}

static void GetImageRange(uintptr_t& lo, uintptr_t& hi)
{
    auto dos = (PIMAGE_DOS_HEADER)module;
    auto nt = (PIMAGE_NT_HEADERS)((uint8_t*)module + dos->e_lfanew);
    lo = (uintptr_t)module;
    hi = lo + nt->OptionalHeader.SizeOfImage;
}

bool IsGObjectsArrayValid(const void* candidate)
{
    if (!IsReadableRange(candidate, 0x20)) return false;
    auto a = (const uint8_t*)candidate;
    uintptr_t objects = *(const uintptr_t*)(a + 0x00);
    int32_t maxEl = *(const int32_t*)(a + 0x10), numEl = *(const int32_t*)(a + 0x14);
    int32_t maxCh = *(const int32_t*)(a + 0x18), numCh = *(const int32_t*)(a + 0x1C);

    if (!objects || numEl < 0x1000 || numEl > maxEl || maxEl > 0x4000000) return false;
    if (maxCh != (maxEl + 0xFFFF) / 0x10000 || numCh != (numEl + 0xFFFF) / 0x10000) return false;
    if (!IsReadableRange((void*)objects, (size_t)numCh * sizeof(void*))) return false;

    auto chunks = (const uintptr_t*)objects;
    for (int c = 0; c < numCh; c++) if (!chunks[c]) return false;

    // First chunk's items (FUObjectItem is 0x18 bytes, Object* first) must be real UObjects:
    // readable, vtable pointer inside the game image, non-null Class.
    uintptr_t lo, hi; GetImageRange(lo, hi);
    if (!IsReadableRange((void*)chunks[0], 0x18 * 128)) return false;
    int good = 0;
    for (int i = 0; i < 128; i++) {
        uintptr_t obj = *(const uintptr_t*)(chunks[0] + (size_t)i * 0x18);
        if (!obj) continue;
        if (!IsReadableRange((void*)obj, 0x28)) return false;
        uintptr_t vt = *(const uintptr_t*)obj, cls = *(const uintptr_t*)(obj + 0x10);
        if (vt < lo || vt >= hi || !cls || !IsReadableRange((void*)cls, 0x28)) return false;
        good++;
    }
    return good >= 64;
}

static bool IsFNamePoolValid(const uint8_t* a)
{
    if (!IsReadableRange(a, 0x20)) return false;
    uint32_t curBlock = *(const uint32_t*)(a + 0x08), cursor = *(const uint32_t*)(a + 0x0C);
    uintptr_t block0 = *(const uintptr_t*)(a + 0x10);
    if (curBlock >= 8192 || cursor == 0 || cursor > 0x20000 * 2 || !block0) return false;
    if (!IsReadableRange(a + 0x10, (size_t)(curBlock + 1) * sizeof(void*))) return false;
    if (!*(const uintptr_t*)(a + 0x10 + (size_t)curBlock * sizeof(void*))) return false;
    if (!IsReadableRange((void*)block0, 8)) return false;
    // Entry 0 is always "None": header = (len << 6) | (hash << 1) | isWide, ASCII chars follow.
    uint16_t hdr = *(const uint16_t*)block0;
    return (hdr >> 6) == 4 && !(hdr & 1) && memcmp((const uint8_t*)block0 + 2, "None", 4) == 0;
}

template<typename Pred>
static void* ScanWritableImage(Pred pred)
{
    uintptr_t lo, hi; GetImageRange(lo, hi);
    uintptr_t p = lo;
    while (p < hi) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((void*)p, &mbi, sizeof(mbi))) break;
        uintptr_t rs = (uintptr_t)mbi.BaseAddress, re = rs + mbi.RegionSize;
        if (re > hi) re = hi;
        bool rw = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                  (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY));
        if (rw) {
            for (uintptr_t a = (rs + 7) & ~7ull; a + 0x20 <= re; a += 8)
                if (pred((const uint8_t*)a)) return (void*)a;
        }
        p = re > p ? re : p + 0x1000;
    }
    return nullptr;
}

// Scan callbacks run on memory already known readable, so do cheap field checks first
// and only pay for the VirtualQuery-heavy full validation on the few survivors.
void* FindGObjectsArray()
{
    return ScanWritableImage([](const uint8_t* a) {
        int32_t maxEl = *(const int32_t*)(a + 0x10), numEl = *(const int32_t*)(a + 0x14);
        int32_t maxCh = *(const int32_t*)(a + 0x18), numCh = *(const int32_t*)(a + 0x1C);
        if (numEl < 0x1000 || numEl > maxEl || maxEl > 0x4000000) return false;
        if (maxCh != (maxEl + 0xFFFF) / 0x10000 || numCh != (numEl + 0xFFFF) / 0x10000) return false;
        if (!*(const uintptr_t*)a) return false;
        return IsGObjectsArrayValid(a);
    });
}

void* FindFNamePool()
{
    return ScanWritableImage([](const uint8_t* a) {
        uint32_t curBlock = *(const uint32_t*)(a + 0x08), cursor = *(const uint32_t*)(a + 0x0C);
        if (curBlock >= 8192 || cursor == 0 || !*(const uintptr_t*)(a + 0x10)) return false;
        return IsFNamePoolValid(a);
    });
}
