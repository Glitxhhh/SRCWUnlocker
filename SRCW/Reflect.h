#pragma once

/*
 * Reflect.h — Runtime UE4 Reflection API
 * 
 * Resolves all game-specific classes, functions, and properties by NAME at runtime.
 * No game-specific SDK headers needed. Survives game updates as long as the 
 * UE engine version stays the same (UObject/UClass layout is stable).
 *
 * Usage:
 *   Reflect::CallStatic("AppSaveGameHelper", "SetOpenMirror", true);
 *   Reflect::CallStatic("AppSaveGameHelper", "SetDriverSelectable", uint8_t(5));
 *   auto* obj = Reflect::FindInstance("ContentDataAsset");
 *   int32 offset = Reflect::FindPropertyOffset(obj->Class, "_UserJukeboxData");
 */

#include "CppSDK/SDK/CoreUObject_classes.hpp"
#include <string>
#include <unordered_map>
#include <cstring>

namespace Reflect
{
    // =========================================================================
    // Class / Object Finding
    // =========================================================================

    // Find a UClass by short name (e.g. "AppSaveGameHelper")
    SDK::UClass* FindClass(const char* className);

    // Get the Class Default Object for a class
    SDK::UObject* FindCDO(const char* className);

    // Find the FIRST non-CDO instance of a class in GObjects
    SDK::UObject* FindInstance(const char* className);

    // =========================================================================
    // Function Finding
    // =========================================================================

    // Find a UFunction on a class (searches class + parent chain)
    SDK::UFunction* FindFunction(const char* className, const char* funcName);

    // =========================================================================
    // Property Finding  
    // =========================================================================

    // Find an FProperty by name on a UStruct (walks ChildProperties chain)
    SDK::FProperty* FindProperty(SDK::UStruct* structType, const char* propName);

    // Get the memory offset of a property
    int32 FindPropertyOffset(SDK::UStruct* structType, const char* propName);

    // Get a typed pointer to a property's value on an object
    template<typename T>
    T* GetPropertyPtr(SDK::UObject* obj, const char* propName)
    {
        int32 offset = FindPropertyOffset(obj->Class, propName);
        if (offset < 0) return nullptr;
        return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(obj) + offset);
    }

    // Get a typed pointer to a nested property (struct.field)
    template<typename T>
    T* GetNestedPropertyPtr(SDK::UObject* obj, const char* structPropName, const char* fieldName)
    {
        SDK::FProperty* structProp = FindProperty(obj->Class, structPropName);
        if (!structProp) return nullptr;

        // Get the struct type from FStructProperty
        auto* structProperty = static_cast<SDK::FStructProperty*>(structProp);
        SDK::UStruct* innerStruct = structProperty->Struct;
        if (!innerStruct) return nullptr;

        int32 innerOffset = FindPropertyOffset(innerStruct, fieldName);
        if (innerOffset < 0) return nullptr;

        return reinterpret_cast<T*>(
            reinterpret_cast<uint8_t*>(obj) + structProp->Offset + innerOffset
        );
    }

    // =========================================================================
    // Function Calling — Static (on CDO via ProcessEvent)
    // =========================================================================

    // Call a static function with no parameters
    bool CallStatic(const char* className, const char* funcName);

    // Call a static function with a single bool parameter
    bool CallStaticBool(const char* className, const char* funcName, bool value);

    // Call a static function with a single int32 parameter
    bool CallStaticInt32(const char* className, const char* funcName, int32 value);

    // Call a static function with a single uint8 parameter (also used for enums)
    bool CallStaticUInt8(const char* className, const char* funcName, uint8_t value);

    // Call a static function with a single float parameter
    bool CallStaticFloat(const char* className, const char* funcName, float value);

    // Call a static function with two parameters: uint8 + bool
    bool CallStaticUInt8Bool(const char* className, const char* funcName, uint8_t p1, bool p2);

    // Call a static function with two parameters: int32 + bool
    bool CallStaticInt32Bool(const char* className, const char* funcName, int32 p1, bool p2);

    // Call a static function with raw params buffer (advanced)
    bool CallStaticRaw(const char* className, const char* funcName, void* params, int32 paramsSize);

    // =========================================================================
    // Function Calling — Instance (on a specific object)
    // =========================================================================

    // Call an instance function with no parameters
    bool CallInstance(SDK::UObject* obj, const char* className, const char* funcName);

    // Call an instance function with a single bool parameter
    bool CallInstanceBool(SDK::UObject* obj, const char* className, const char* funcName, bool value);

    // Call an instance function with two parameters: uint8 + bool
    bool CallInstanceUInt8Bool(SDK::UObject* obj, const char* className, const char* funcName, uint8_t p1, bool p2);

    // =========================================================================
    // Return Value Calls
    // =========================================================================

    // Call a static function that returns int32
    int32 CallStaticRetInt32(const char* className, const char* funcName);

    // Call a static function with uint8 param that returns uint8
    uint8_t CallStaticUInt8RetUInt8(const char* className, const char* funcName, uint8_t param);

    // =========================================================================
    // Enum Helpers
    // =========================================================================

    // Find the "Num" value of an enum by looking up its UEnum and finding "Num" or "MAX" entries
    int32 GetEnumNum(const char* enumName);

    // =========================================================================
    // Cache Management
    // =========================================================================

    // Clear all caches (call if game reloads objects)
    void ClearCache();
}

namespace ReflectRaw
{
    // Raw TArray layout (engine-level, universal)
    struct RawTArray { void* Data; int32 Num; int32 Max; };

    // Clear a TArray (set count to 0)
    inline void ClearTArray(void* arrayAddr)
    {
        auto* arr = reinterpret_cast<RawTArray*>(arrayAddr);
        arr->Num = 0;
    }

    // Get element count
    inline int32 TArrayNum(void* arrayAddr)
    {
        return reinterpret_cast<RawTArray*>(arrayAddr)->Num;
    }

    // Get pointer to element at index
    inline void* TArrayAt(void* arrayAddr, int32 index, int32 elementSize)
    {
        auto* arr = reinterpret_cast<RawTArray*>(arrayAddr);
        return reinterpret_cast<uint8_t*>(arr->Data) + index * elementSize;
    }

    // Read int32 from raw memory
    inline int32 ReadInt32(void* addr, int32 offset)
    {
        return *reinterpret_cast<int32*>(reinterpret_cast<uint8_t*>(addr) + offset);
    }

    // Write bool at raw memory
    inline void WriteBool(void* addr, int32 offset, bool value)
    {
        *reinterpret_cast<bool*>(reinterpret_cast<uint8_t*>(addr) + offset) = value;
    }
}
