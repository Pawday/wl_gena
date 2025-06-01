#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Wayland {
namespace ScannerTypes {

struct InterfaceNameable
{
    std::optional<std::string> interface_name;
};

// clang-format off
struct ArgTypeInt {};
struct ArgTypeUInt {};
struct ArgTypeUIntEnum : InterfaceNameable
{
    std::string name;
};
struct ArgTypeFixed {};
struct ArgTypeString {};
struct ArgTypeNullString {};
struct ArgTypeObject : InterfaceNameable {};
struct ArgTypeNullObject : InterfaceNameable {};
struct ArgTypeNewID : InterfaceNameable {};
struct ArgTypeArray {};
struct ArgTypeFD {};

using ArgType = std::variant<
    ArgTypeInt,
    ArgTypeUInt,
    ArgTypeUIntEnum,
    ArgTypeFixed,
    ArgTypeString,
    ArgTypeNullString,
    ArgTypeObject,
    ArgTypeNullObject,
    ArgTypeNewID,
    ArgTypeArray,
    ArgTypeFD
>;
// clang-format on

struct Arg
{
    std::string name;
    ArgType type;
};

struct Enum
{
    std::string name;
    struct Entry
    {
        std::string name;
        uint32_t value;
        bool is_hex;
    };
    std::vector<Entry> entries;
};

struct Message
{
    // clang-format off
    struct TypeDestructor {};

    using Type = std::variant<
        TypeDestructor
    >;
    // clang-format on

    std::string name;
    std::optional<Type> type;
    std::vector<Arg> args;
};

// clang-format off
struct Request : Message {};
struct Event : Message {};
// clang-format on

struct Interface
{
    std::string name;
    uint32_t verison;
    std::vector<Request> requests;
    std::vector<Event> events;
    std::vector<Enum> enums;
};

struct Protocol
{
    std::string name;
    std::vector<Interface> interfaces;
};

} // namespace ScannerTypes
} // namespace Wayland
