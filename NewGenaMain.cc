#include <algorithm>
#include <expected>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <span>
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

struct HeaderModeArgs
{
    std::string proto_file_name;
    std::string output_file_name;
    std::vector<std::string> includes;
};

auto parse_header_mode_args(std::vector<std::string> args)
    -> std::expected<HeaderModeArgs, std::string>
{
    HeaderModeArgs out{};

    auto flag_it = std::ranges::find(args, "--header");
    if (flag_it == std::end(args)) {
        return std::unexpected("No --header flag");
    }
    args.erase(flag_it);

    std::string syntax_message;
    syntax_message += "<protocol_file> <output_file> [--includes "
                      "file[,file_2,/system_file,/system_file_2,...]]";

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

    auto protocol_op = Wayland::parse_protocol(protocol_xml);
    if (!protocol_op) {
        std::cerr << protocol_op.error();
        return;
    }
    auto protocol = protocol_op.value();

    wl_gena::GenerateHeaderInput I;
    I.protocol = std::move(protocol);
    I.includes = args.includes;

    auto O = generate_header(I);

    output_file << O.output;
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

    auto header_mode_args_op = parse_header_mode_args(argv);
    if (header_mode_args_op) {
        process_header_mode(header_mode_args_op.value());
        return EXIT_SUCCESS;
    }
    std::string header_mode_message =
        std::format("HEADER Mode: [{}]", header_mode_args_op.error());
    failue_msgs.emplace_back(std::move(header_mode_message));

    std::cout << "Failue:" << '\n';
    for (auto &msg : failue_msgs) {
        std::cout << msg << '\n';
    }
    return EXIT_FAILURE;
}
