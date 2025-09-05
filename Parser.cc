#include <charconv>
#include <expected>
#include <format>
#include <optional>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <cstddef>
#include <cstdint>

#include <expat.h>
#include <expat_external.h>

#include "Parser.hh"
#include "Types.hh"

namespace wl_gena {

namespace {

struct Parser
{
    Parser()
    {
        handle = []() {
            auto parser = XML_ParserCreate(nullptr);
            if (!parser) {
                throw std::runtime_error("Cannot init parser");
            }
            return parser;
        }();
    }

    struct Attribute
    {
        std::string_view key;
        std::string_view value;
    };

    template <typename UserDataT>
    struct Callbacks
    {
        UserDataT &user_data;
        void (UserDataT::*start)(
            std::string_view el, const std::vector<Attribute> &attrs);
        void (UserDataT::*data)(std::string_view data);
        void (UserDataT::*end)(std::string_view el);
    };

    template <typename UserDataT>
    void parse(Callbacks<UserDataT> &callbacks, std::string_view data)
    {
        struct Context
        {
            Callbacks<UserDataT> &cb;
            std::vector<Attribute> attrs;
        } context{callbacks, {}};

        XML_ParserReset(handle, nullptr);
        XML_SetUserData(handle, &context);
        XML_SetCharacterDataHandler(
            handle, [](void *userData, const XML_Char *s, int len) {
                Context &ctx = *reinterpret_cast<Context *>(userData);
                (ctx.cb.user_data.*ctx.cb.data)(std::string_view{s, s + len});
            });
        XML_SetElementHandler(
            handle,
            [](void *userData, const XML_Char *name, const XML_Char **atts_p) {
                Context &ctx = *reinterpret_cast<Context *>(userData);

                auto &attrs = ctx.attrs;
                ctx.attrs.clear();

                size_t attr_offset = 0;
                while (atts_p[attr_offset]) {
                    std::string_view key{atts_p[attr_offset]};
                    std::string_view val{atts_p[attr_offset + 1]};
                    attr_offset += 2;
                    attrs.emplace_back(key, val);
                }

                (ctx.cb.user_data.*ctx.cb.start)(std::string_view{name}, attrs);
            },
            [](void *userData, const XML_Char *name) {
                Context &ctx = *reinterpret_cast<Context *>(userData);
                (ctx.cb.user_data.*ctx.cb.end)(std::string_view{name});
            });

        XML_Status status = XML_Parse(handle, data.data(), data.size(), true);
        if (status != XML_STATUS_OK) {
            XML_Error error = XML_GetErrorCode(handle);
            std::string message =
                std::format("XML_Parser error: ({})", XML_ErrorString(error));
            throw std::runtime_error(std::move(message));
        }
    }

    ~Parser()
    {
        if (handle) {
            XML_ParserFree(handle);
        }
    }

    Parser(Parser &&) = delete;
    Parser &operator=(Parser &&) = delete;
    Parser(const Parser &) = delete;
    Parser &operator=(const Parser &) = delete;

  private:
    XML_Parser handle = nullptr;
};

struct ProtoParser
{
    // clang-format off
    using ParseTarget = std::variant<
        types::Arg,
        types::Enum,
        types::Enum::Entry,
        types::Message,
        types::Request,
        types::Event,
        types::Interface,
        types::Protocol
    >;
    // clang-format on

    using AttributeMap = std::unordered_map<std::string, std::string>;

    auto make_attr_map(const std::vector<Parser::Attribute> &attrs)
        -> AttributeMap
    {
        AttributeMap out;

        for (auto &attr : attrs) {
            std::string key{attr.key};
            std::string val{attr.value};
            if (out.contains(key)) {
                std::string message = std::format(
                    "Duplicate interface attribute [{}=[{}]]", key, val);
                throw std::runtime_error(std::move(message));
            }
            out[std::move(key)] = std::move(val);
        }

        return out;
    };

    template <typename T>
    std::expected<T, std::string>
        parse_num(const std::string &str, int base = 10)
    {
        T out = 0;
        auto status =
            std::from_chars(str.data(), str.data() + str.size(), out, base);

        std::optional<std::string> error_string;

        if (!error_string && status.ec != std::errc{}) {
            error_string = std::make_error_code(status.ec).message();
        }

        if (!error_string && status.ptr != str.data() + str.size()) {
            error_string = std::format("Incomplete output [{}]", str);
        }

        if (error_string) {
            return std::unexpected{std::move(error_string.value())};
        }

        return out;
    }

    auto parse_protocol(const AttributeMap &attrs) -> void
    {
        types::Protocol new_proto;
        std::string name = attrs.at("name");
        new_proto.name = name;
        targets.emplace(std::move(new_proto));
    }

    auto fin_protocol() -> void
    {
        if (output_proto.has_value()) {
            throw std::runtime_error(
                "Multiple protocol parsing is not supported");
        }

        ParseTarget &active_target = targets.top();
        types::Protocol active_proto = std::get<types::Protocol>(active_target);
        targets.pop();

        output_proto = std::move(active_proto);
    }

    auto parse_interface(const AttributeMap &attrs) -> void
    {
        types::Interface new_interface{};

        std::string name = attrs.at("name");
        new_interface.name = std::move(name);
        std::string vesion_string = attrs.at("version");

        auto version_op =
            parse_num<decltype(types::Interface::version)>(vesion_string);

        std::optional<std::string> error_string{};
        if (!version_op.has_value()) {
            error_string = std::move(version_op.error());
        }

        if (error_string) {
            std::string message = std::format(
                "Cannot parse version string [{}] of interface [{}] : "
                "status "
                "[{}]",
                vesion_string,
                new_interface.name,
                error_string.value());
            throw std::runtime_error(std::move(message));
        }

        new_interface.version = version_op.value();

        targets.emplace(std::move(new_interface));
    }

    auto fin_interface() -> void
    {
        ParseTarget &active_interface_target = targets.top();
        types::Interface active_interface =
            std::get<types::Interface>(active_interface_target);
        targets.pop();

        auto &active_proto_target = targets.top();
        types::Protocol &active_protocol =
            std::get<types::Protocol>(active_proto_target);

        active_protocol.interfaces.emplace_back(std::move(active_interface));
    }

    auto parse_request(const AttributeMap &attrs) -> void
    {
        types::Request new_request;
        std::string request_name = attrs.at("name");

        if (attrs.contains("type")) {
            std::string type_string = attrs.at("type");
            if (type_string == "destructor") {
                new_request.destructor = true;
            } else {
                std::string message =
                    std::format("Unknown request type [{}]", type_string);
                throw std::runtime_error(std::move(message));
            }
        }

        std::optional<uint32_t> since;
        if (attrs.contains("since")) {
            std::string since_str = attrs.at("since");
            auto since_parsed = parse_num<uint32_t>(since_str);
            if (!since_parsed) {
                std::string message = std::format(
                    "Bad since [{}]: {}", since_str, since_parsed.error());
                throw std::runtime_error{std::move(message)};
            }
            since = since_parsed.value();
        }

        new_request.name = std::move(request_name);
        new_request.since = since;
        targets.emplace(std::move(new_request));
    }

    struct FinRequestVisitor
    {
        explicit FinRequestVisitor(types::Request &req) : _req(req)
        {
        }

        template <typename T>
        void operator()(T &t)
        {
            std::string message = std::format(
                "Attempt to add request field to {}", target_name(t));
            throw std::runtime_error(std::move(message));
        }

        void operator()(types::Interface &interface)
        {
            interface.requests.emplace_back(std::move(_req));
        }

      private:
        types::Request &_req;
    };

    auto fin_request() -> void
    {
        ParseTarget &active_target = targets.top();
        types::Request request =
            std::move(std::get<types::Request>(active_target));
        targets.pop();

        ParseTarget &request_parent_target = targets.top();
        std::visit(FinRequestVisitor{request}, request_parent_target);
    }

    auto parse_event(const AttributeMap &attrs) -> void
    {
        types::Event new_event;
        std::string event_name = attrs.at("name");

        std::optional<uint32_t> since;
        if (attrs.contains("since")) {
            std::string since_str = attrs.at("since");
            auto since_parsed = parse_num<uint32_t>(since_str);
            if (!since_parsed) {
                std::string message = std::format(
                    "Bad since [{}]: {}", since_str, since_parsed.error());
                throw std::runtime_error{std::move(message)};
            }
            since = since_parsed.value();
        }

        new_event.name = std::move(event_name);
        new_event.since = since;
        targets.emplace(std::move(new_event));
    }

    struct FinEventVisitor
    {
        explicit FinEventVisitor(types::Event &ev) : _ev(ev)
        {
        }

        template <typename T>
        void operator()(T &t)
        {
            std::string message =
                std::format("Attempt to add event field to {}", target_name(t));
            throw std::runtime_error(std::move(message));
        }

        void operator()(types::Interface &interface)
        {
            interface.events.emplace_back(std::move(_ev));
        }

      private:
        types::Event &_ev;
    };

    auto fin_event() -> void
    {
        ParseTarget &active_target = targets.top();
        types::Event event = std::move(std::get<types::Event>(active_target));
        targets.pop();

        ParseTarget &request_parent_target = targets.top();
        std::visit(FinEventVisitor{event}, request_parent_target);
    }

    auto parse_arg_type(
        const std::string &arg_type_string, const AttributeMap &attrs)
        -> std::expected<types::ArgType, std::string>
    {

        const auto interface_name = [&attrs]() -> std::optional<std::string> {
            if (attrs.contains("interface")) {
                return attrs.at("interface");
            }
            return std::nullopt;
        }();

        /*
         * * * `i`: int
         * * `u`: uint
         * * `f`: fixed
         * * `s`: string
         * * `o`: object
         * * `n`: new_id
         * * `a`: array
         * * `h`: fd
         * * `?`: following argument (`o` or `s`) is nullable
         */

        if (arg_type_string == "int") {
            return types::ArgTypes::Int{};
        }

        if (arg_type_string == "uint") {

            if (!attrs.contains("enum")) {
                return types::ArgTypes::UInt{};
            }

            /*
             * "<interface_name>.<enum_name>"
             * or
             * "<enum_name>"
             */
            std::string enum_location = attrs.at("enum");

            bool has_interface_name =
                enum_location.find(".") != enum_location.npos;

            if (!has_interface_name) {
                types::ArgTypes::UIntEnum out{};
                out.name = std::move(enum_location);
                return out;
            }

            auto split_dot = [](const std::string &s)
                -> std::
                    expected<std::pair<std::string, std::string>, std::string> {
                        std::string first;
                        bool found_sep = false;
                        std::string second;

                        for (auto c : s) {
                            if (c == '.') {
                                found_sep = true;
                                continue;
                            }

                            if (!found_sep) {
                                first += c;
                                continue;
                            }

                            second += c;
                        }

                        if (!found_sep) {
                            std::string message = std::format(
                                "Cannot split string [{}] by dot", s);
                            return std::unexpected(std::move(message));
                        }

                        return std::make_pair(
                            std::move(first), std::move(second));
                    };

            auto sep_op = split_dot(enum_location);
            if (!sep_op.has_value()) {
                return std::unexpected(sep_op.error());
            }
            auto &sep = sep_op.value();

            types::ArgTypes::UIntEnum out{};
            out.interface_name = std::move(sep.first);
            out.name = std::move(sep.second);
            return out;
        }

        if (arg_type_string == "fixed") {
            return types::ArgTypes::Fixed{};
        }

        if (arg_type_string == "string" || arg_type_string == "object") {

            types::ArgType out_t;
            types::ArgType null_out_t;

            if (arg_type_string == "string") {
                out_t = types::ArgTypes::String{};
                null_out_t = types::ArgTypes::NullString{};
            } else if (arg_type_string == "object") {
                types::ArgTypes::Object obj{};
                obj.interface_name = interface_name;
                out_t = std::move(obj);
                types::ArgTypes::NullObject null_obj{};
                null_obj.interface_name = interface_name;
                null_out_t = std::move(null_obj);
            } else {
                std::string message = std::format(
                    "Unexpected arg_type_string value change from"
                    " \"string\" or \"object\" to [{}]",
                    arg_type_string);
                return std::unexpected(std::move(message));
            }

            if (!attrs.contains("allow-null")) {
                return out_t;
            }

            std::string allow_null_value = attrs.at("allow-null");
            if (allow_null_value != "true") {
                std::string message = std::format(
                    "for tag <arg> \"allow-null\" attribute value must be set "
                    "to \"true\", got [{}] instead",
                    allow_null_value);
                return std::unexpected(std::move(message));
            }

            return null_out_t;
        }

        if (arg_type_string == "new_id") {
            types::ArgTypes::NewID new_id{};
            new_id.interface_name = interface_name;
            return new_id;
        }

        if (arg_type_string == "array") {
            return types::ArgTypes::Array{};
        }

        if (arg_type_string == "fd") {
            return types::ArgTypes::FD{};
        }

        return std::unexpected(
            std::format("[{}] is unknown type", arg_type_string));
    }

    auto parse_arg(const AttributeMap &attrs)
    {
        types::Arg arg;
        std::string name = attrs.at("name");
        arg.name = std::move(name);

        std::string type_string = attrs.at("type");
        auto arg_type = parse_arg_type(type_string, attrs);
        if (!arg_type) {
            std::string message = std::format(
                "Parsing [{}] type failure [{}]",
                type_string,
                arg_type.error());
            throw std::runtime_error(std::move(message));
        }
        arg.type = std::move(arg_type.value());

        targets.emplace(std::move(arg));
    }

    struct FinArgVisitor
    {
        explicit FinArgVisitor(types::Arg &arg) : _arg{arg}
        {
        }

        template <typename T>
        void operator()(T &t)
        {
            std::string message = std::format(
                "Attempt to add argument field to {}", target_name(t));
            throw std::runtime_error(std::move(message));
        }

        void operator()(types::Request &req)
        {
            req.args.emplace_back(std::move(_arg));
        }

        void operator()(types::Event &event)
        {
            event.args.emplace_back(std::move(_arg));
        }

      private:
        types::Arg &_arg;
    };

    auto fin_arg()
    {
        ParseTarget &active_target = targets.top();
        types::Arg arg_target = std::move(std::get<types::Arg>(active_target));
        targets.pop();

        ParseTarget &request_parent_target = targets.top();
        std::visit(FinArgVisitor{arg_target}, request_parent_target);
    }

    auto parse_enum(const AttributeMap &attrs)
    {
        types::Enum new_enum{};
        if (!attrs.contains("name")) {

            std::string message = "Found unnamed enum tag";
            if (targets.size() != 0) {
                auto &parent = targets.top();
                message = std::format("{} in {}", message, target_name(parent));
            }
            throw std::runtime_error{std::move(message)};
        }
        new_enum.name = attrs.at("name");
        targets.emplace(std::move(new_enum));
    }

    void fin_enum()
    {
        ParseTarget &active_target = targets.top();
        types::Enum enum_target =
            std::move(std::get<types::Enum>(active_target));
        targets.pop();

        ParseTarget &interface_parent_target = targets.top();
        types::Interface &interface =
            std::get<types::Interface>(interface_parent_target);

        interface.enums.emplace_back(std::move(enum_target));
    }

    auto parse_entry(const AttributeMap &attrs)
    {
        types::Enum::Entry entry{};

        std::string name = attrs.at("name");
        std::string value_string = attrs.at("value");

        bool is_hex = true;
        is_hex = is_hex && value_string.size() > 2;
        is_hex = is_hex && value_string[0] == '0';
        is_hex = is_hex && (value_string[1] == 'x' || value_string[1] == 'X');

        int base = 10;
        if (is_hex) {
            base = 16;
            value_string = value_string.substr(2);
        }

        auto value_op =
            parse_num<decltype(types::Enum::Entry::value)>(value_string, base);

        std::optional<std::string> error_string{};
        if (!value_op.has_value()) {
            error_string = std::move(value_op.error());
        }

        if (error_string) {
            std::string message = std::format(
                "Cannot parse version string [{}] of entry [{}] : "
                "status "
                "[{}]",
                value_string,
                name,
                error_string.value());
            throw std::runtime_error(std::move(message));
        }

        entry.name = std::move(name);
        entry.value = value_op.value();
        entry.is_hex = is_hex;

        targets.emplace(std::move(entry));
    }

    auto fin_entry()
    {
        ParseTarget &active_target = targets.top();
        types::Enum::Entry entry =
            std::move(std::get<types::Enum::Entry>(active_target));
        targets.pop();

        ParseTarget &enum_target = targets.top();
        types::Enum &wl_enum = std::get<types::Enum>(enum_target);

        wl_enum.entries.emplace_back(std::move(entry));
    }

    // clang-format off
    struct Tag {
    struct Protocol {};
    struct Interface {};
    struct Request {};
    struct Event {};
    struct Arg {};
    struct Enum {};
    struct Entry {};
    };
    using KnownTag = std::variant<
        Tag::Protocol,
        Tag::Interface,
        Tag::Request,
        Tag::Event,
        Tag::Arg,
        Tag::Enum,
        Tag::Entry
    >;
    // clang-format on

    std::optional<KnownTag> parse_tag(std::string_view s)
    {
#define RETURN_IF_MATCH(LITERAL, TYPENAME)                                     \
    if (LITERAL == s) {                                                        \
        return TYPENAME{};                                                     \
    }
        RETURN_IF_MATCH("protocol", Tag::Protocol);
        RETURN_IF_MATCH("interface", Tag::Interface);
        RETURN_IF_MATCH("request", Tag::Request);
        RETURN_IF_MATCH("event", Tag::Event);
        RETURN_IF_MATCH("arg", Tag::Arg);
        RETURN_IF_MATCH("enum", Tag::Enum);
        RETURN_IF_MATCH("entry", Tag::Entry);
#undef RETURN_IF_MATCH
        return std::nullopt;
    };

    struct StartTagVisitor
    {
        ProtoParser &P;
        AttributeMap attr_map;

        void operator()(Tag::Protocol)
        {
            P.parse_protocol(attr_map);
        }

        void operator()(Tag::Interface)
        {
            P.parse_interface(attr_map);
        }

        void operator()(Tag::Request)
        {
            P.parse_request(attr_map);
        }

        void operator()(Tag::Event)
        {
            P.parse_event(attr_map);
        }

        void operator()(Tag::Arg)
        {
            P.parse_arg(attr_map);
        }

        void operator()(Tag::Enum)
        {
            P.parse_enum(attr_map);
        }

        void operator()(Tag::Entry)
        {
            P.parse_entry(attr_map);
        }
    };

    void
        start(std::string_view tag, const std::vector<Parser::Attribute> &attrs)
    {
        auto parsed_tag = parse_tag(tag);
        if (!parsed_tag.has_value()) {
            return;
        }

        StartTagVisitor vis{*this, make_attr_map(attrs)};
        std::visit(vis, parsed_tag.value());
    }

    auto data([[maybe_unused]] std::string_view data) -> void
    {
    }

    struct EndTagVisitor
    {
        ProtoParser &parser;

        void operator()(Tag::Protocol)
        {
            parser.fin_protocol();
        }

        void operator()(Tag::Interface)
        {
            parser.fin_interface();
        }

        void operator()(Tag::Request)
        {
            parser.fin_request();
        }

        void operator()(Tag::Event)
        {
            parser.fin_event();
        }

        void operator()(Tag::Arg)
        {
            parser.fin_arg();
        }

        void operator()(Tag::Enum)
        {
            parser.fin_enum();
        }

        void operator()(Tag::Entry)
        {
            parser.fin_entry();
        }
    };

    auto end(std::string_view tag) -> void
    {
        auto parsed_tag = parse_tag(tag);
        if (!parsed_tag.has_value()) {
            return;
        }

        EndTagVisitor vis{*this};
        std::visit(vis, parsed_tag.value());
    }

    auto get() const -> const types::Protocol &
    {
        return output_proto.value();
    };

  private:
    struct ParseTargetNameVisitor
    {
#define ADD_OVERLOAD(TYPENAME, TAG_NAME)                                       \
    std::string operator()(const TYPENAME &tgt)                                \
    {                                                                          \
        return std::format(                                                    \
            "ParseTarget::{} (<{} name=[{}] ...>)",                            \
            #TYPENAME,                                                         \
            #TAG_NAME,                                                         \
            tgt.name);                                                         \
    }

        ADD_OVERLOAD(types::Arg, arg)
        ADD_OVERLOAD(types::Enum, enum)
        ADD_OVERLOAD(types::Enum::Entry, entry)
        ADD_OVERLOAD(types::Message, event | request)
        ADD_OVERLOAD(types::Request, request)
        ADD_OVERLOAD(types::Event, event)
        ADD_OVERLOAD(types::Interface, interface)
        ADD_OVERLOAD(types::Protocol, protocol)
#undef ADD_OVERLOAD
    };

    static std::string target_name(const ParseTarget &tgt)
    {
        return std::visit(ParseTargetNameVisitor{}, tgt);
    }

    std::stack<ParseTarget> targets;

    std::optional<types::Protocol> output_proto;
};

} // namespace

std::expected<types::Protocol, std::string>
    parse_protocol(std::string_view protocol_xml)
{
    ProtoParser ctx;
    Parser::Callbacks<ProtoParser> pcbs{
        ctx, &ProtoParser::start, &ProtoParser::data, &ProtoParser::end};

    Parser p;
    p.parse(pcbs, protocol_xml);

    return ctx.get();
}

} // namespace wl_gena
