#include "Reflect.h"
#include <iostream>
#include <cstdlib>

namespace Reflect
{
    // --- Caches for fast repeated lookups ---
    static std::unordered_map<std::string, SDK::UClass*> classCache;
    static std::unordered_map<std::string, SDK::UFunction*> funcCache;
    static std::unordered_map<std::string, int32> propOffsetCache;

    void ClearCache()
    {
        classCache.clear();
        funcCache.clear();
        propOffsetCache.clear();
    }

    // =========================================================================
    // Class / Object Finding
    // =========================================================================

    SDK::UClass* FindClass(const char* className)
    {
        auto it = classCache.find(className);
        if (it != classCache.end())
            return it->second;

        SDK::UClass* cls = SDK::UObject::FindClassFast(className);
        if (cls)
            classCache[className] = cls;

        return cls;
    }

    SDK::UObject* FindCDO(const char* className)
    {
        SDK::UClass* cls = FindClass(className);
        if (!cls) return nullptr;
        return cls->ClassDefaultObject;
    }

    SDK::UObject* FindInstance(const char* className)
    {
        SDK::UClass* cls = FindClass(className);
        if (!cls) return nullptr;

        for (int i = 0; i < SDK::UObject::GObjects->Num(); i++)
        {
            SDK::UObject* obj = SDK::UObject::GObjects->GetByIndex(i);
            if (!obj || obj->IsDefaultObject())
                continue;
            if (obj->IsA(cls))
                return obj;
        }

        return nullptr;
    }

    // =========================================================================
    // Function Finding
    // =========================================================================

    SDK::UFunction* FindFunction(const char* className, const char* funcName)
    {
        std::string key = std::string(className) + "::" + funcName;
        auto it = funcCache.find(key);
        if (it != funcCache.end())
            return it->second;

        SDK::UClass* cls = FindClass(className);
        if (!cls) return nullptr;

        SDK::UFunction* func = cls->GetFunction(className, funcName);
        if (func)
            funcCache[key] = func;

        return func;
    }

    // =========================================================================
    // Property Finding
    // =========================================================================

    SDK::FProperty* FindProperty(SDK::UStruct* structType, const char* propName)
    {
        if (!structType) return nullptr;

        // Walk the property chain
        for (SDK::FField* field = structType->ChildProperties; field; field = field->Next)
        {
            if (field->Name.ToString() == propName)
                return static_cast<SDK::FProperty*>(field);
        }

        // Check parent struct
        if (structType->SuperStruct)
            return FindProperty(structType->SuperStruct, propName);

        return nullptr;
    }

    int32 FindPropertyOffset(SDK::UStruct* structType, const char* propName)
    {
        if (!structType) return -1;

        std::string key = structType->GetName() + "." + propName;
        auto it = propOffsetCache.find(key);
        if (it != propOffsetCache.end())
            return it->second;

        SDK::FProperty* prop = FindProperty(structType, propName);
        if (!prop) return -1;

        propOffsetCache[key] = prop->Offset;
        return prop->Offset;
    }

    // =========================================================================
    // Internal: Build params buffer and call ProcessEvent
    // =========================================================================

    // Helper to find the first param property on a UFunction
    static SDK::FProperty* FindFirstParam(SDK::UFunction* func)
    {
        for (SDK::FField* field = func->ChildProperties; field; field = field->Next)
        {
            auto* prop = static_cast<SDK::FProperty*>(field);
            // Skip return values (check ReturnParm flag)
            if (!(prop->PropertyFlags & (uint64)SDK::EPropertyFlags::ReturnParm))
                return prop;
        }
        return nullptr;
    }

    // Helper to find the return param property on a UFunction
    static SDK::FProperty* FindReturnParam(SDK::UFunction* func)
    {
        for (SDK::FField* field = func->ChildProperties; field; field = field->Next)
        {
            auto* prop = static_cast<SDK::FProperty*>(field);
            if (prop->PropertyFlags & (uint64)SDK::EPropertyFlags::ReturnParm)
                return prop;
        }
        return nullptr;
    }

    // Helper to find the Nth non-return parameter
    static SDK::FProperty* FindNthParam(SDK::UFunction* func, int n)
    {
        int count = 0;
        for (SDK::FField* field = func->ChildProperties; field; field = field->Next)
        {
            auto* prop = static_cast<SDK::FProperty*>(field);
            if (!(prop->PropertyFlags & (uint64)SDK::EPropertyFlags::ReturnParm))
            {
                if (count == n) return prop;
                count++;
            }
        }
        return nullptr;
    }

    // =========================================================================
    // Static Function Calls
    // =========================================================================

    bool CallStatic(const char* className, const char* funcName)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return false;

        // Allocate zeroed params buffer
        uint8_t params[512] = {};
        cdo->ProcessEvent(func, func->Size > 0 ? params : nullptr);
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
        cdo->ProcessEvent(func, params);
        return true;
    }

    bool CallStaticInt32(const char* className, const char* funcName, int32 value)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return false;

        uint8_t params[512] = {};
        SDK::FProperty* p = FindFirstParam(func);
        if (p) *(int32*)(params + p->Offset) = value;
        cdo->ProcessEvent(func, params);
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
        cdo->ProcessEvent(func, params);
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
        cdo->ProcessEvent(func, params);
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
        cdo->ProcessEvent(func, params);
        return true;
    }

    bool CallStaticInt32Bool(const char* className, const char* funcName, int32 p1, bool p2)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return false;

        uint8_t params[512] = {};
        SDK::FProperty* param0 = FindNthParam(func, 0);
        SDK::FProperty* param1 = FindNthParam(func, 1);
        if (param0) *(int32*)(params + param0->Offset) = p1;
        if (param1) *(bool*)(params + param1->Offset) = p2;
        cdo->ProcessEvent(func, params);
        return true;
    }

    bool CallStaticRaw(const char* className, const char* funcName, void* paramsIn, int32 paramsSize)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return false;

        cdo->ProcessEvent(func, paramsIn);
        return true;
    }

    // =========================================================================
    // Instance Function Calls
    // =========================================================================

    bool CallInstance(SDK::UObject* obj, const char* className, const char* funcName)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        if (!func || !obj) return false;

        uint8_t params[512] = {};
        obj->ProcessEvent(func, func->Size > 0 ? params : nullptr);
        return true;
    }

    bool CallInstanceBool(SDK::UObject* obj, const char* className, const char* funcName, bool value)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        if (!func || !obj) return false;

        uint8_t params[512] = {};
        SDK::FProperty* p = FindFirstParam(func);
        if (p) *(bool*)(params + p->Offset) = value;
        obj->ProcessEvent(func, params);
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
        obj->ProcessEvent(func, params);
        return true;
    }

    // =========================================================================
    // Return Value Calls
    // =========================================================================

    int32 CallStaticRetInt32(const char* className, const char* funcName)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return 0;

        uint8_t params[512] = {};
        cdo->ProcessEvent(func, params);

        SDK::FProperty* ret = FindReturnParam(func);
        if (!ret) return 0;
        return *(int32*)(params + ret->Offset);
    }

    uint8_t CallStaticUInt8RetUInt8(const char* className, const char* funcName, uint8_t param)
    {
        SDK::UFunction* func = FindFunction(className, funcName);
        SDK::UObject* cdo = FindCDO(className);
        if (!func || !cdo) return 0;

        uint8_t params[512] = {};
        SDK::FProperty* p = FindFirstParam(func);
        if (p) *(uint8_t*)(params + p->Offset) = param;
        cdo->ProcessEvent(func, params);

        SDK::FProperty* ret = FindReturnParam(func);
        if (!ret) return 0;
        return *(uint8_t*)(params + ret->Offset);
    }

    // =========================================================================
    // Enum Helpers
    // =========================================================================

    int32 GetEnumNum(const char* enumName)
    {
        // Search GObjects for the UEnum with this name
        for (int i = 0; i < SDK::UObject::GObjects->Num(); i++)
        {
            SDK::UObject* obj = SDK::UObject::GObjects->GetByIndex(i);
            if (!obj) continue;

            if (obj->IsA(SDK::EClassCastFlags::Enum) && obj->GetName() == enumName)
            {
                auto* uenum = static_cast<SDK::UEnum*>(obj);

                // Walk the Names array looking for "::Num" entry
                for (int j = 0; j < uenum->Names.Num(); j++)
                {
                    std::string name = uenum->Names[j].Key().ToString();
                    // Entries are formatted as "EnumName::ValueName"
                    size_t colonPos = name.rfind("::");
                    std::string valueName = (colonPos != std::string::npos)
                        ? name.substr(colonPos + 2)
                        : name;

                    if (valueName == "Num")
                        return (int32)uenum->Names[j].Value();
                }

                // Fallback: return count - 1 (last entry is usually _MAX)
                if (uenum->Names.Num() > 0)
                    return (int32)uenum->Names[uenum->Names.Num() - 2].Value();
            }
        }

        std::cout << "[Reflect] Warning: enum '" << enumName << "' not found\n";
        return 0;
    }
}
