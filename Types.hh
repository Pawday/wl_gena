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

struct ArgTypes
{
    // clang-format off
struct Int {};
struct UInt {};
struct UIntEnum : InterfaceNameable
{
    std::string name;
};
struct Fixed {};
struct String {};
struct NullString {};
struct Object : InterfaceNameable {};
struct NullObject : InterfaceNameable {};
struct NewID : InterfaceNameable {};
struct Array {};
struct FD {};
};

using ArgType = std::variant<
    ArgTypes::Int,
    ArgTypes::UInt,
    ArgTypes::UIntEnum,
    ArgTypes::Fixed,
    ArgTypes::String,
    ArgTypes::NullString,
    ArgTypes::Object,
    ArgTypes::NullObject,
    ArgTypes::NewID,
    ArgTypes::Array,
    ArgTypes::FD
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
