#include <algorithm>
#include <expected>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "wl_gena/GenaMain.hh"

#include "Format.hh"
#include "HeaderGena.hh"
#include "Parser.hh"
#include "Types.hh"

namespace {

struct JsonModeArgs
{
    std::string proto_file_name;
};

auto parse_json_mode_args(std::vector<std::string> args)
    -> std::expected<JsonModeArgs, std::string>
{
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
    auto protocol_op = wl_gena::parse_protocol(protocol_xml);
    if (!protocol_op) {
        std::cerr << protocol_op.error();
        return;
    }
    auto &protocol = protocol_op.value();

    std::cout << std::format("{}\n", protocol);
}

struct HeaderModeArgs
{
    std::string proto_file_name;
    std::string output_file_name;
    std::vector<std::string> includes;
    std::vector<std::string> context_protocol_file_names;
};

auto parse_header_mode_args(std::vector<std::string> args)
    -> std::expected<HeaderModeArgs, std::string>
{
    HeaderModeArgs out{};

    std::string syntax_message;
    syntax_message +=
        "<protocol_file> <output_file> "
        "[--includes file[,file_2,/system_file,/system_file_2,...]] "
        "[--context_protocols protocol_file[,protocol_file_2,...]]";

    auto help_it = std::ranges::find(args, "--help");
    if (help_it != std::end(args)) {
        return std::unexpected(std::move(syntax_message));
    }

    auto includes_it = std::ranges::find(args, "--includes");
    if (includes_it != std::end(args)) {
        auto includes_val_it = includes_it + 1;
        if (includes_val_it == std::end(args)) {

            std::string message = "No value for --includes option was found. ";
            message += std::format(
                "Expected arguments with following syntax ({})",
                syntax_message);
            return std::unexpected(std::move(message));
        }

        std::string includes_val = *includes_val_it;
        args.erase(includes_it, includes_val_it + 1);

        std::vector<std::string> includes;
        includes.push_back({});

        for (char c : includes_val) {
            if (c == ',') {
                includes.push_back({});
                continue;
            }
            includes.back() += c;
        }

        auto empty_lines = std::ranges::remove_if(
            includes, [](const std::string &str) { return str.empty(); });
        includes.erase(empty_lines.begin(), empty_lines.end());

        for (std::string &include_line : includes) {
            if (include_line[0] == '/') {
                include_line[0] = '<';
                include_line += '>';
                continue;
            }
            include_line = std::format("\"{}\"", include_line);
        }
        out.includes = std::move(includes);
    }

    auto context_protos_it = std::ranges::find(args, "--context_protocols");
    if (context_protos_it != std::end(args)) {
        auto context_protos_val_it = context_protos_it + 1;
        if (context_protos_val_it == std::end(args)) {
            std::string message =
                "No value for --context_protocols option was found. ";
            message += std::format(
                "Expected arguments with following syntax ({})",
                syntax_message);
            return std::unexpected(std::move(message));
        }

        std::string context_protos_val = *context_protos_val_it;
        args.erase(context_protos_it, context_protos_val_it + 1);

        std::vector<std::string> context_protocol_file_names;
        context_protocol_file_names.push_back({});
        for (char c : context_protos_val) {
            if (c == ',') {
                context_protocol_file_names.push_back({});
                continue;
            }
            context_protocol_file_names.back() += c;
        }

        out.context_protocol_file_names =
            std::move(context_protocol_file_names);
    }

    if (args.size() != 2) {
        std::string message;
        message += std::format(
            "Expected arguments with following syntax ({})", syntax_message);

        std::vector<std::string> dec_args;
        std::ranges::copy(args, std::back_inserter(dec_args));
        for (auto &dec_arg : dec_args) {
            dec_arg = std::format("({})", dec_arg);
        }
        FormatVectorWrap args_f{dec_args};

        message += std::format(", got {} instead", args_f);

        return std::unexpected(std::move(message));
    }

    out.proto_file_name = args.at(0);
    out.output_file_name = args.at(1);

    return out;
}

void process_header_mode(const HeaderModeArgs &args)
{
    std::string protocol_xml = read_text_file(args.proto_file_name);
    std::ofstream output_file{args.output_file_name};

    output_file.exceptions(std::ifstream::failbit);
    output_file.exceptions(std::ifstream::badbit);

    auto protocol_op = wl_gena::parse_protocol(protocol_xml);
    if (!protocol_op) {
        throw std::runtime_error{protocol_op.error()};
    }
    auto protocol = protocol_op.value();

    std::vector<std::string> context_proto_content_list;
    for (auto &ctx_proto_filename : args.context_protocol_file_names) {
        std::string ctx_proto_content = read_text_file(ctx_proto_filename);
        context_proto_content_list.push_back(std::move(ctx_proto_content));
    }

    std::vector<wl_gena::types::Protocol> context_protocols;
    for (const std::string &proto_file_content : context_proto_content_list) {
        auto context_proto_op = wl_gena::parse_protocol(proto_file_content);
        if (!context_proto_op) {
            throw std::runtime_error{context_proto_op.error()};
        }
        context_protocols.push_back(std::move(context_proto_op.value()));
    }

    wl_gena::GenerateHeaderInput I;
    I.protocol = std::move(protocol);
    I.includes = args.includes;
    I.context_protocols = std::move(context_protocols);

    auto O = generate_header(I);

    output_file << O.output;
}

} // namespace

void wl_gena::main(const std::vector<std::string> &argv)
{
    std::vector<std::string> argv_loc{argv};
    if (argv_loc.empty()) {
        throw std::runtime_error{"Expected arguments"};
    }
    std::string mode_str = argv_loc.at(0);
    argv_loc.erase(argv_loc.begin());

    std::vector<std::string> all_modes;

    all_modes.push_back("json");
    if (mode_str == all_modes.back()) {
        auto json_mode_args_op = parse_json_mode_args(argv_loc);
        if (json_mode_args_op) {
            process_json_mode(json_mode_args_op.value());
            return;
        }

        std::string json_mode_message =
            std::format("JSON Mode: [{}]", json_mode_args_op.error());
        throw std::runtime_error{std::move(json_mode_message)};
    }

    all_modes.push_back("header");
    if (mode_str == all_modes.back()) {
        auto header_mode_args_op = parse_header_mode_args(argv_loc);
        if (header_mode_args_op) {
            process_header_mode(header_mode_args_op.value());
            return;
        }
        std::string header_mode_message =
            std::format("HEADER Mode: [{}]", header_mode_args_op.error());
        throw std::runtime_error{std::move(header_mode_message)};
    }

    std::string msg = std::format(
        "Unknown mode [{}]: available modes {}",
        mode_str,
        FormatVectorWrap{all_modes});
    throw std::runtime_error{std::move(msg)};
}
