#include <algorithm>
#include <expected>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "wl_gena/GenaMain.hh"

#include "Format.hh"
#include "Parser.hh"
#include "Types.hh"

namespace {

struct JsonModeArgs
{
    std::string proto_file_name;
};

std::expected<JsonModeArgs, std::string>
    parse_json_mode_args(std::vector<std::string> args)
{
    auto json_flag_it = std::ranges::find(args, "--json");
    if (json_flag_it == std::end(args)) {
        return std::unexpected("No --json flag");
    }
    args.erase(json_flag_it);

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
    out.proto_file_name = input_proto_filename;

    return out;
}

std::vector<std::byte> read_file(const std::string &name)
{
    std::ifstream ifile{name, std::ios::binary};
    ifile.exceptions(std::fstream::badbit);
    ifile.exceptions(std::fstream::failbit);
    auto start = std::istreambuf_iterator<char>{ifile};
    auto end = std::istreambuf_iterator<char>{};
    std::vector<std::byte> out;
    while (start != end) {
        uint8_t val = *start;
        out.emplace_back(std::byte{val});
        start++;
    }

    return out;
}

std::string read_text_file(const std::string &name)
{
    std::string output;
    auto data = read_file(name);

    auto to_char = [](std::byte b) { return std::to_integer<char>(b); };
    std::ranges::copy(
        data | std::views::transform(to_char), std::back_inserter(output));
    return output;
}

void process_json_mode(const JsonModeArgs &args)
{
    std::string protocol_xml = read_text_file(args.proto_file_name);
    auto protocol_op = Wayland::parse_protocol(protocol_xml);
    if (!protocol_op) {
        std::cerr << protocol_op.error();
        return;
    }
    auto &protocol = protocol_op.value();

    std::cout << std::format("{}\n", protocol);
}

struct EnumsModeArgs
{
    std::string proto_file_name;
    std::string output_file_name;
};

std::expected<EnumsModeArgs, std::string>
    parse_enums_mode(std::vector<std::string> args)
{
    auto flag_it = std::ranges::find(args, "--enums");
    if (flag_it == std::end(args)) {
        return std::unexpected("No --enums flag");
    }
    args.erase(flag_it);

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
    std::string protocol_xml = read_text_file(args.proto_file_name);
    std::ofstream output_file{args.output_file_name};
    output_file.exceptions(std::ifstream::failbit);
    output_file.exceptions(std::ifstream::badbit);
    auto protocol_op = Wayland::parse_protocol(protocol_xml);
    if (!protocol_op) {
        std::cerr << protocol_op.error();
        return;
    }
    auto protocol = protocol_op.value();
    auto &interfaces = protocol.interfaces;

    std::vector<std::string> o_lines;
    o_lines.emplace_back("#pragma once");
    o_lines.emplace_back("");
    o_lines.emplace_back(std::format("namespace {}", protocol.name));
    o_lines.emplace_back("{");
    o_lines.emplace_back("");

    bool first_interface = true;
    for (auto &interface : interfaces) {
        using EnumStringLines = std::vector<std::string>;
        std::vector<EnumStringLines> interface_enums;

        for (auto &eenum : interface.enums) {
            interface_enums.emplace_back(emit_enum(eenum));
        }

        if (interface_enums.empty()) {
            continue;
        }

        if (!first_interface) {
            o_lines.emplace_back("");
        }
        first_interface = false;

        o_lines.emplace_back(std::format("namespace {}", interface.name));
        o_lines.emplace_back("{");
        for (auto &enum_lines : interface_enums) {
            for (auto &line : enum_lines) {
                o_lines.emplace_back(std::format("    {}", line));
            }
        }
        o_lines.emplace_back("}");
    }

    o_lines.emplace_back("}"); // namespace <protocol_name>

    std::string output;
    for (auto &o_line : o_lines) {
        output += o_line;
        output += '\n';
    }

    output_file << output;
}

} // namespace

int wl_gena_main(const std::vector<std::string> argv)
{
    std::vector<std::string> failue_msgs;

    auto json_mode_args_op = parse_json_mode_args(argv);
    if (json_mode_args_op) {
        process_json_mode(json_mode_args_op.value());
        return EXIT_SUCCESS;
    }
    std::string json_mode_message =
        std::format("JSON Mode: [{}]", json_mode_args_op.error());
    failue_msgs.emplace_back(std::move(json_mode_message));

    auto enums_mode_args_op = parse_enums_mode(argv);
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
