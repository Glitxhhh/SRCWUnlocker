#include "Reflect.h"
#include "Helpers.h"
#include <cctype>

namespace Reflect
{
    static std::unordered_map<std::string, SDK::UClass*> classCache;
    static std::unordered_map<std::string, SDK::UFunction*> funcCache;
    static std::unordered_map<std::string, int32_t> propOffsetCache;

    // UObject::ProcessEvent located by signature at startup (see InitGlobals). The SDK's own
    // UObject::ProcessEvent calls through a vtable index hardcoded in a generated header, which
    // can shift between builds; we only fall back to it if the signature isn't found.
    static void* g_ProcessEventFn = nullptr;
    void ProcessEvent(SDK::UObject* obj, SDK::UFunction* fn, void* parms)
    {
        CallCount++;
        LastCall = fn;
        CurrentCall = fn;
        if (TraceCalls && fn) {
            // Logged (and flushed to SRCW.log) BEFORE the call, so if the game hangs inside it the last
            // line names the culprit. Includes the first bytes of the parameter block (the arguments).
            std::cout << "[Call #" << CallCount.load() << "] " << fn->GetFullName() << "  args=";
            const uint8_t* pb = static_cast<const uint8_t*>(parms);
            int n = pb ? (fn->Size < 16 ? fn->Size : 16) : 0;
            static const char* hex = "0123456789abcdef";
            for (int i = 0; i < n; i++) std::cout << hex[pb[i] >> 4] << hex[pb[i] & 15] << (i + 1 < n ? " " : "");
            std::cout << "\n";
        }
        if (g_ProcessEventFn) reinterpret_cast<void(*)(SDK::UObject*, SDK::UFunction*, void*)>(g_ProcessEventFn)(obj, fn, parms);
        else obj->ProcessEvent(fn, parms);
        CurrentCall = nullptr;
    }

    void ClearCache() { classCache.clear(); funcCache.clear(); propOffsetCache.clear(); }

    SDK::UClass* FindClass(const char* className)
    {
        auto it = classCache.find(className);
        if (it != classCache.end()) return it->second;
        SDK::UClass* cls = SDK::UObject::FindClassFast(className);
        if (cls) classCache[className] = cls;
        return cls;
    }

    SDK::UObject* FindCDO(const char* className)
    {
        SDK::UClass* cls = FindClass(className);
        return cls ? cls->DefaultObject : nullptr;
    }

    SDK::UObject* FindInstance(const char* className)
    {
        SDK::UClass* cls = FindClass(className);
        if (!cls) return nullptr;
        for (int i = 0; i < SDK::UObject::GObjects->Num(); i++)
        {
            SDK::UObject* obj = SDK::UObject::GObjects->GetByIndex(i);
            if (!obj || obj->IsDefaultObject()) continue;
            if (obj->IsA(cls)) return obj;
        }
        return nullptr;
    }

    SDK::UFunction* FindFunction(const char* className, const char* funcName)
    {
        std::string key = std::string(className) + "::" + funcName;
        auto it = funcCache.find(key);
        if (it != funcCache.end()) return it->second;
        SDK::UClass* cls = FindClass(className);
        if (!cls) return nullptr;
        SDK::UFunction* func = cls->GetFunction(className, funcName);
        if (func) funcCache[key] = func;
        return func;
    }

    SDK::FProperty* FindProperty(SDK::UStruct* structType, const char* propName)
    {
        if (!structType) return nullptr;
        for (SDK::FField* field = structType->ChildProperties; field; field = field->Next)
        {
            if (field->Name.ToString() == propName)
                return static_cast<SDK::FProperty*>(field);
        }
        if (structType->Super)
            return FindProperty(structType->Super, propName);
        return nullptr;
    }

    int32_t FindPropertyOffset(SDK::UStruct* structType, const char* propName)
    {
        if (!structType) return -1;
        std::string key = structType->GetName() + "." + propName;
        auto it = propOffsetCache.find(key);
        if (it != propOffsetCache.end()) return it->second;
        SDK::FProperty* prop = FindProperty(structType, propName);
        if (!prop) return -1;
        propOffsetCache[key] = prop->Offset;
        return prop->Offset;
    }

    static SDK::FProperty* FindFirstParam(SDK::UFunction* func)
    {
        for (SDK::FField* field = func->ChildProperties; field; field = field->Next)
        {
            auto* prop = static_cast<SDK::FProperty*>(field);
            if (!(prop->PropertyFlags & static_cast<uint64_t>(SDK::EPropertyFlags::ReturnParm)))
                return prop;
        }
        return nullptr;
    }

    static SDK::FProperty* FindReturnParam(SDK::UFunction* func)
    {
        for (SDK::FField* field = func->ChildProperties; field; field = field->Next)
        {
            auto* prop = static_cast<SDK::FProperty*>(field);
            if (prop->PropertyFlags & static_cast<uint64_t>(SDK::EPropertyFlags::ReturnParm))
                return prop;
        }
        return nullptr;
    }

    static SDK::FProperty* FindNthParam(SDK::UFunction* func, int n)
    {
        int count = 0;
        for (SDK::FField* field = func->ChildProperties; field; field = field->Next)
        {
            auto* prop = static_cast<SDK::FProperty*>(field);
            if (!(prop->PropertyFlags & static_cast<uint64_t>(SDK::EPropertyFlags::ReturnParm)))
            {
                if (count == n) return prop;
                count++;
            }
        }
        return nullptr;
    }

    bool CallStatic(const char* className, const char* funcName)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return false;
        uint8_t params[512] = {};
        ProcessEvent(cdo, func, func->Size > 0 ? params : nullptr);
        return true;
    }

    bool CallStaticBool(const char* className, const char* funcName, bool value)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return false;
        uint8_t params[512] = {};
        SDK::FProperty* p = FindFirstParam(func);
        if (p) *(bool*)(params + p->Offset) = value;
        ProcessEvent(cdo, func, params);
        return true;
    }

    bool CallStaticInt32(const char* className, const char* funcName, int32_t value)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return false;
        uint8_t params[512] = {};
        SDK::FProperty* p = FindFirstParam(func);
        if (p) *(int32_t*)(params + p->Offset) = value;
        ProcessEvent(cdo, func, params);
        return true;
    }

    bool CallStaticUInt8(const char* className, const char* funcName, uint8_t value)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return false;
        uint8_t params[512] = {};
        SDK::FProperty* p = FindFirstParam(func);
        if (p) *(uint8_t*)(params + p->Offset) = value;
        ProcessEvent(cdo, func, params);
        return true;
    }

    bool CallStaticFloat(const char* className, const char* funcName, float value)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return false;
        uint8_t params[512] = {};
        SDK::FProperty* p = FindFirstParam(func);
        if (p) *(float*)(params + p->Offset) = value;
        ProcessEvent(cdo, func, params);
        return true;
    }

    bool CallStaticUInt8Bool(const char* className, const char* funcName, uint8_t p1, bool p2)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return false;
        uint8_t params[512] = {};
        SDK::FProperty* param0 = FindNthParam(func, 0);
        SDK::FProperty* param1 = FindNthParam(func, 1);
        if (param0) *(uint8_t*)(params + param0->Offset) = p1;
        if (param1) *(bool*)(params + param1->Offset) = p2;
        ProcessEvent(cdo, func, params);
        return true;
    }

    bool CallStaticInt32Bool(const char* className, const char* funcName, int32_t p1, bool p2)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return false;
        uint8_t params[512] = {};
        SDK::FProperty* param0 = FindNthParam(func, 0);
        SDK::FProperty* param1 = FindNthParam(func, 1);
        if (param0) *(int32_t*)(params + param0->Offset) = p1;
        if (param1) *(bool*)(params + param1->Offset) = p2;
        ProcessEvent(cdo, func, params);
        return true;
    }

    bool CallStaticRaw(const char* className, const char* funcName, void* paramsIn, int32_t paramsSize)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return false;
        ProcessEvent(cdo, func, paramsIn);
        return true;
    }

    bool CallInstance(SDK::UObject* obj, const char* className, const char* funcName)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        if (!func || !obj) return false;
        uint8_t params[512] = {};
        ProcessEvent(obj, func, func->Size > 0 ? params : nullptr);
        return true;
    }

    bool CallInstanceBool(SDK::UObject* obj, const char* className, const char* funcName, bool value)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        if (!func || !obj) return false;
        uint8_t params[512] = {};
        SDK::FProperty* p = FindFirstParam(func);
        if (p) *(bool*)(params + p->Offset) = value;
        ProcessEvent(obj, func, params);
        return true;
    }

    bool CallInstanceUInt8Bool(SDK::UObject* obj, const char* className, const char* funcName, uint8_t p1, bool p2)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        if (!func || !obj) return false;
        uint8_t params[512] = {};
        SDK::FProperty* param0 = FindNthParam(func, 0);
        SDK::FProperty* param1 = FindNthParam(func, 1);
        if (param0) *(uint8_t*)(params + param0->Offset) = p1;
        if (param1) *(bool*)(params + param1->Offset) = p2;
        ProcessEvent(obj, func, params);
        return true;
    }

    int32_t CallStaticRetInt32(const char* className, const char* funcName)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return 0;
        uint8_t params[512] = {};
        ProcessEvent(cdo, func, params);
        SDK::FProperty* ret = FindReturnParam(func);
        if (!ret) return 0;
        return *(int32_t*)(params + ret->Offset);
    }

    uint8_t CallStaticUInt8RetUInt8(const char* className, const char* funcName, uint8_t param)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return 0;
        uint8_t params[512] = {};
        SDK::FProperty* p = FindFirstParam(func);
        if (p) *(uint8_t*)(params + p->Offset) = param;
        ProcessEvent(cdo, func, params);
        SDK::FProperty* ret = FindReturnParam(func);
        if (!ret) return 0;
        return *(uint8_t*)(params + ret->Offset);
    }

    bool CallStaticRetBool(const char* className, const char* funcName)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return false;
        uint8_t params[512] = {};
        ProcessEvent(cdo, func, params);
        SDK::FProperty* ret = FindReturnParam(func);
        if (!ret) return false;
        return *(bool*)(params + ret->Offset);
    }

    bool CallStaticUInt8RetBool(const char* className, const char* funcName, uint8_t param)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return false;
        uint8_t params[512] = {};
        SDK::FProperty* p = FindFirstParam(func);
        if (p) *(uint8_t*)(params + p->Offset) = param;
        ProcessEvent(cdo, func, params);
        SDK::FProperty* ret = FindReturnParam(func);
        if (!ret) return false;
        return *(bool*)(params + ret->Offset);
    }

    int32_t GetEnumNum(const char* enumName)
    {
        for (int i = 0; i < SDK::UObject::GObjects->Num(); i++)
        {
            SDK::UObject* obj = SDK::UObject::GObjects->GetByIndex(i);
            if (!obj) continue;
            if (obj->IsA(SDK::EClassCastFlags::Enum) && obj->GetName() == enumName)
            {
                auto* uenum = static_cast<SDK::UEnum*>(obj);
                for (int j = 0; j < uenum->Names.Num(); j++)
                {
                    std::string name = uenum->Names[j].Key().ToString();
                    size_t colonPos = name.rfind("::");
                    std::string valueName = (colonPos != std::string::npos) ? name.substr(colonPos + 2) : name;
                    if (valueName == "Num")
                        return (int32_t)uenum->Names[j].Value();
                }
                // Some enums terminate with "Max" instead of "Num" (e.g. ECarStatusType).
                for (int j = 0; j < uenum->Names.Num(); j++)
                {
                    std::string name = uenum->Names[j].Key().ToString();
                    size_t colonPos = name.rfind("::");
                    if (((colonPos != std::string::npos) ? name.substr(colonPos + 2) : name) == "Max")
                        return (int32_t)uenum->Names[j].Value();
                }
                if (uenum->Names.Num() > 1)
                    return (int32_t)uenum->Names[uenum->Names.Num() - 2].Value();
            }
        }
        std::cout << "[Reflect] Warning: enum '" << enumName << "' not found\n";
        return 0;
    }

    static std::string ToLower(const std::string& s)
    {
        std::string out = s;
        for (char& c : out) c = (char)tolower((unsigned char)c);
        return out;
    }

    int32_t ResolveEnumValueByName(const char* enumName, const std::string& valueName)
    {
        std::string target = ToLower(valueName);
        for (int i = 0; i < SDK::UObject::GObjects->Num(); i++)
        {
            SDK::UObject* obj = SDK::UObject::GObjects->GetByIndex(i);
            if (!obj) continue;
            if (obj->IsA(SDK::EClassCastFlags::Enum) && obj->GetName() == enumName)
            {
                auto* uenum = static_cast<SDK::UEnum*>(obj);
                for (int j = 0; j < uenum->Names.Num(); j++)
                {
                    std::string name = uenum->Names[j].Key().ToString();
                    size_t colonPos = name.rfind("::");
                    std::string valueOnly = (colonPos != std::string::npos) ? name.substr(colonPos + 2) : name;
                    if (ToLower(valueOnly) == target)
                        return (int32_t)uenum->Names[j].Value();
                }
                return -1;
            }
        }
        std::cout << "[Reflect] Warning: enum '" << enumName << "' not found while resolving '" << valueName << "'\n";
        return -1;
    }

    // -------------------------------------------------------------------------
    // Engine global discovery
    // -------------------------------------------------------------------------
    static const uint8_t* const* g_NameBlocks = nullptr;
    struct RawFString { wchar_t* Data; int32_t Num; int32_t Max; };

    // Replacement for the game's FName::AppendString, decoding straight from the
    // FNamePool we located. Matches the SDK's expected (const FName*, FString&) shape.
    static void CustomAppendString(const SDK::FName* name, SDK::FString& outRef)
    {
        auto& out = *reinterpret_cast<RawFString*>(&outRef);
        std::wstring s;
        uint32_t idx = (uint32_t)name->ComparisonIndex, number = name->Number;
        uint32_t block = idx >> 16;
        if (g_NameBlocks && block < 8192 && g_NameBlocks[block]) {
            const uint8_t* entry = g_NameBlocks[block] + (size_t)(idx & 0xFFFF) * 2;
            uint16_t hdr = *(const uint16_t*)entry;
            int len = hdr >> 6; bool wide = hdr & 1;
            for (int i = 0; i < len; i++)
                s.push_back(wide ? ((const wchar_t*)(entry + 2))[i] : (wchar_t)entry[2 + i]);
        }
        if (number > 0) s += L"_" + std::to_wstring(number - 1);
        if (!out.Data || out.Max <= 0) return;
        int32_t n = (int32_t)s.size(); if (n > out.Max - 1) n = out.Max - 1;
        memcpy(out.Data, s.c_str(), (size_t)n * sizeof(wchar_t));
        out.Data[n] = 0; out.Num = n + 1;
    }

    bool InitGlobals()
    {
        static void* objs = nullptr; static void* pool = nullptr;
        uintptr_t base = (uintptr_t)module;

        if (!objs) {
            if ((objs = FindGObjectsArray())) std::cout << "[Init] GObjects found by scan at RVA 0x" << std::hex << ((uintptr_t)objs - base) << std::dec << "\n";
            if (objs) SDK::UObject::GObjects.InitManually(objs);
        }
        if (!pool) {
            if ((pool = FindFNamePool())) {
                g_NameBlocks = reinterpret_cast<const uint8_t* const*>((uint8_t*)pool + 0x10);
                SDK::FName::InitManually((void*)&CustomAppendString);
                std::cout << "[Init] FNamePool found at RVA 0x" << std::hex << ((uintptr_t)pool - base) << std::dec << "\n";
            }
        }
        if (objs && pool && !g_ProcessEventFn) {
            // Prologue + stack-protector load of UObject::ProcessEvent; '?' wildcards the cookie address.
            g_ProcessEventFn = PatternScan("40 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 10 01 00 00 48 8D 6C 24 30 48 89 9D 38 01 00 00 48 8B 05 ? ? ? ? 48 33 C5 48 89 85 D0 00 00 00");
            if (g_ProcessEventFn) {
                // Cross-check: it should appear in a real UObject's vtable. Report the index so drift is visible.
                int idx = -1;
                for (int i = 0; i < 64 && idx < 0; i++) {
                    SDK::UObject* o = SDK::UObject::GObjects->GetByIndex(i);
                    if (!o) continue;
                    void** vt = *reinterpret_cast<void***>(o);
                    for (int j = 0; j < 0x200; j++) if (vt[j] == g_ProcessEventFn) { idx = j; break; }
                }
                std::cout << "[Init] UObject::ProcessEvent at RVA 0x" << std::hex << ((uintptr_t)g_ProcessEventFn - base) << ", vtable index 0x" << idx << std::dec
                          << (idx == SDK::Offsets::ProcessEventIdx ? " (matches SDK)\n" : " (DIFFERS from SDK - good thing we derive it)\n");
            } else std::cout << "[Init] ProcessEvent signature not found, falling back to SDK vtable index\n";
        }
        return objs && pool;
    }
}
