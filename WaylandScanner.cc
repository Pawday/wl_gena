#include <algorithm>
#include <expected>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <cstddef>
#include <cstdlib>

#include "Application.hh"

#include "Format.hh"
#include "ParseProtocol.hh"
#include "Types.hh"

namespace {

struct JsonModeArgs
{
    std::string proto_file_name;
    bool wl_strip = false;
};

std::string strip_wayland_prefix(const std::string &in)
{
    std::string_view in_v{in};
    constexpr char pref_val[] = "wl_";
    auto pref_size = in_v.find(pref_val);
    if (pref_size == in_v.npos) {
        return in;
    }

    if (pref_size != 0) {
        return in;
    }

    in_v.remove_prefix(sizeof(pref_val) - 1);

    return std::string{in_v};
}

struct ArgInterfaceNameDemangleVisitor
{

    template <typename T>
    void interface_nameable_wayland_prefix_strip(T &arg)
    {
        constexpr bool is_interface_nameable =
            std::is_base_of_v<Wayland::ScannerTypes::InterfaceNameable, T>;

        if constexpr (is_interface_nameable) {
            if (!arg.interface_name.has_value()) {
                return;
            }
            arg.interface_name =
                strip_wayland_prefix(arg.interface_name.value());
        }
    }

#define OVERLOAD(TYPENAME)                                                     \
    void operator()(TYPENAME &arg)                                             \
    {                                                                          \
        interface_nameable_wayland_prefix_strip(arg);                          \
    }

    OVERLOAD(Wayland::ScannerTypes::ArgTypeInt)
    OVERLOAD(Wayland::ScannerTypes::ArgTypeUInt)
    OVERLOAD(Wayland::ScannerTypes::ArgTypeUIntEnum)
    OVERLOAD(Wayland::ScannerTypes::ArgTypeFixed)
    OVERLOAD(Wayland::ScannerTypes::ArgTypeString)
    OVERLOAD(Wayland::ScannerTypes::ArgTypeNullString)
    OVERLOAD(Wayland::ScannerTypes::ArgTypeObject)
    OVERLOAD(Wayland::ScannerTypes::ArgTypeNullObject)
    OVERLOAD(Wayland::ScannerTypes::ArgTypeNewID)
    OVERLOAD(Wayland::ScannerTypes::ArgTypeArray)
    OVERLOAD(Wayland::ScannerTypes::ArgTypeFD)
#undef OVERLOAD
};

Wayland::ScannerTypes::Arg
    demangle_wayland_names(const Wayland::ScannerTypes::Arg &arg)
{
    auto out = arg;
    std::visit(ArgInterfaceNameDemangleVisitor{}, out.type);
    return out;
}

Wayland::ScannerTypes::Interface
    demangle_wayland_names(const Wayland::ScannerTypes::Interface &iface)
{
    auto out = iface;
    out.name = strip_wayland_prefix(iface.name);

    for (auto &req : out.requests) {
        for (auto &arg : req.args) {
            arg = demangle_wayland_names(arg);
        }
    }

    for (auto &event : out.events) {
        for (auto &arg : event.args) {
            arg = demangle_wayland_names(arg);
        }
    }

    return out;
}

Wayland::ScannerTypes::Protocol
    demangle_wayland_names(const Wayland::ScannerTypes::Protocol &proto)
{
    auto out = proto;
    out.name = strip_wayland_prefix(out.name);

    for (auto &iface : out.interfaces) {
        iface = demangle_wayland_names(iface);
    }

    return out;
}

std::expected<JsonModeArgs, std::string>
    parse_json_mode_args(std::vector<std::string> args)
{
    auto json_flag_it = std::ranges::find(args, "--json");
    if (json_flag_it == std::end(args)) {
        return std::unexpected("No --json flag");
    }
    args.erase(json_flag_it);

    bool wl_strip = false;
    auto wl_strip_flag_it = std::ranges::find(args, "--wl_strip");
    if (wl_strip_flag_it != std::end(args)) {
        args.erase(wl_strip_flag_it);
        wl_strip = true;
    }

    if (args.empty()) {
        return std::unexpected("Expected <protocol_file> argument");
    }

    if (args.size() != 1) {
        for (auto &dec_arg : args) {
            dec_arg = std::format("({})", dec_arg);
        }
        FormatVectorWrap args_f{args};

        return std::unexpected(
            std::format("Expected <protocol_file> only: got {}", args_f));
    }

    std::string input_proto_filename = *args.begin();
    args.clear();

    JsonModeArgs out{};
    out.wl_strip = wl_strip;
    out.proto_file_name = input_proto_filename;

    return out;
}

std::string read_file(const std::string &filename)
{
    std::ifstream file{filename};
    file.exceptions(std::ifstream::failbit);
    file.exceptions(std::ifstream::badbit);
    std::stringstream content;
    content << file.rdbuf();
    return content.str();
}

void process_json_mode(const JsonModeArgs &args)
{
    std::string protocol_xml = read_file(args.proto_file_name);
    auto protocol_op = Wayland::parse_protocol(protocol_xml);
    if (!protocol_op) {
        std::cerr << protocol_op.error();
        return;
    }
    auto &protocol = protocol_op.value();
    if (args.wl_strip) {
        std::cout << std::format("{}\n", demangle_wayland_names(protocol));
        return;
    }

    std::cout << std::format("{}\n", protocol);
}

struct EnumsModeArgs
{
    std::string proto_file_name;
    std::string output_file_name;
    bool wl_strip = false;
};

std::expected<EnumsModeArgs, std::string>
    parse_enums_mode(std::vector<std::string> args)
{
    auto flag_it = std::ranges::find(args, "--enums");
    if (flag_it == std::end(args)) {
        return std::unexpected("No --enums flag");
    }
    args.erase(flag_it);

    bool wl_strip = false;
    auto wl_strip_flag_it = std::ranges::find(args, "--wl_strip");
    if (wl_strip_flag_it != std::end(args)) {
        args.erase(wl_strip_flag_it);
        wl_strip = true;
    }

    if (args.size() != 2) {
        std::string message;
        message += "Expected";
        if (args.size() > 2) {
            message += " only";
        }
        message += " <protocol_file> and <output_file> arguments";
        message += " with --enums flag";

        std::vector<std::string> dec_args;
        std::ranges::copy(args, std::back_inserter(dec_args));
        for (auto &dec_arg : dec_args) {
            dec_arg = std::format("({})", dec_arg);
        }
        FormatVectorWrap args_f{dec_args};

        message += std::format(", got {} instead", args_f);

        return std::unexpected(std::move(message));
    }

    EnumsModeArgs out{};

    out.proto_file_name = args.at(0);
    out.output_file_name = args.at(1);
    out.wl_strip = wl_strip;

    return out;
}

std::vector<std::string> emit_enum(const Wayland::ScannerTypes::Enum &e)
{
    std::vector<std::string> out;

    out.emplace_back(std::format("enum class {}", e.name));
    out.emplace_back("{");

    std::vector<std::string> entries;
    for (auto &entry : e.entries) {
        std::string value = std::format("{}", entry.value);
        if (entry.is_hex) {
            value = std::format("0x{:x}", entry.value);
        }
        entries.emplace_back(std::format("{} = {}", entry.name, value));
    }

    auto except_last = entries | std::views::take(entries.size() - 1);
    for (auto &entry : except_last) {
        entry = std::format("{},", entry);
    }

    for (auto &entry : entries) {
        entry = std::format("    e{}", entry);
    }

    for (auto &entry : entries) {
        out.emplace_back(std::move(entry));
    }

    out.emplace_back("};");

    return out;
}

void process_enums_mode(const EnumsModeArgs &args)
{
    std::string protocol_xml = read_file(args.proto_file_name);
    std::ofstream output_file{args.output_file_name};
    output_file.exceptions(std::ifstream::failbit);
    output_file.exceptions(std::ifstream::badbit);
    auto protocol_op = Wayland::parse_protocol(protocol_xml);
    if (!protocol_op) {
        std::cerr << protocol_op.error();
        return;
    }
    auto protocol = protocol_op.value();
    if (args.wl_strip) {
        protocol = demangle_wayland_names(protocol_op.value());
    }
    auto &interfaces = protocol.interfaces;

    std::vector<std::string> o_lines;
    o_lines.emplace_back("#pragma once");
    o_lines.emplace_back("");

    o_lines.emplace_back("namespace Wayland");
    o_lines.emplace_back("{");
    o_lines.emplace_back("");

    o_lines.emplace_back(std::format("namespace {}_protocol", protocol.name));
    o_lines.emplace_back("{");
    o_lines.emplace_back("");

    bool first_interface = true;
    for (auto &interface : interfaces) {
        if (!first_interface) {
            o_lines.emplace_back("");
        }
        first_interface = false;

        o_lines.emplace_back(std::format("namespace {}", interface.name));
        o_lines.emplace_back("{");

        for (auto &eenum : interface.enums) {
            auto enum_lines = emit_enum(eenum);

            for (auto &enum_line : enum_lines) {
                o_lines.emplace_back(std::move(enum_line));
            }
        }

        o_lines.emplace_back("}");
    }

    o_lines.emplace_back("}"); // namespace <protocol_name>
    o_lines.emplace_back("}"); // namespace Wayland

    std::string output;
    for (auto &o_line : o_lines) {
        output += o_line;
        output += '\n';
    }

    output_file << output;
}

} // namespace

int Application::main()
{
    auto args = get_libc_args().value();

    if (args.argv.size() == 0) {
        std::cout << "Hacked args";
        return EXIT_FAILURE;
    }

    args.argv.erase(args.argv.begin());

    std::vector<std::string> failue_msgs;

    auto json_mode_args_op = parse_json_mode_args(args.argv);
    if (json_mode_args_op) {
        process_json_mode(json_mode_args_op.value());
        return EXIT_SUCCESS;
    }
    std::string json_mode_message =
        std::format("JSON Mode: [{}]", json_mode_args_op.error());
    failue_msgs.emplace_back(std::move(json_mode_message));

    auto enums_mode_args_op = parse_enums_mode(args.argv);
    if (enums_mode_args_op) {
        process_enums_mode(enums_mode_args_op.value());
        return EXIT_SUCCESS;
    }
    std::string enums_mode_message =
        std::format("ENUMS Mode: [{}]", enums_mode_args_op.error());
    failue_msgs.emplace_back(std::move(enums_mode_message));

    std::cout << "Failue:" << '\n';
    for (auto &msg : failue_msgs) {
        std::cout << msg << '\n';
    }
    return EXIT_FAILURE;
}
