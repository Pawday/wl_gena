#include <algorithm>
#include <expected>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "wl_gena/GenaMain.hh"

#include "Format.hh"
#include "Parser.hh"
#include "StringDG.hh"
#include "StringList.hh"
#include "Types.hh"

namespace {

using InterfaceData = Wayland::ScannerTypes::Interface;

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
    parse_header_mode(std::vector<std::string> args)
{
    auto flag_it = std::ranges::find(args, "--header");
    if (flag_it == std::end(args)) {
        return std::unexpected("No --header flag");
    }
    args.erase(flag_it);

    if (args.size() != 2) {
        std::string message;
        message += "Expected";
        if (args.size() > 2) {
            message += " only";
        }
        message += " <protocol_file> and <output_file> arguments";
        message += " with --header flag";

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

struct InterfaceTraits
{
    std::string typename_string;
    std::string wayland_client_core_functions_typename;
    std::string wayland_client_core_wl_interface_typename;
    std::string wayland_client_core_wl_message_typename;
};

struct TypeToStringVisitor
{
    explicit TypeToStringVisitor(const InterfaceTraits &traits)
        : _traits{traits}
    {
    }
    /*
         * from wayland-scanner
        static void emit_type(struct arg *a)
        {
            switch (a->type) {
            default:
            case INT:
            case FD:
                printf("int32_t ");
                break;
            case NEW_ID:
            case UNSIGNED:
                printf("uint32_t ");
                break;
            case FIXED:
                printf("wl_fixed_t ");
                break;
            case STRING:
                printf("const char *");
                break;
            case OBJECT:
                printf("struct %s *", a->interface_name);
                break;
            case ARRAY:
                printf("struct wl_array *");
                break;
            }
        }
        */

    using ArgTypes = Wayland::ScannerTypes::ArgTypes;

#define OVERLOAD(TYPE, type_literal)                                           \
    std::string operator()(ArgTypes::TYPE)                                     \
    {                                                                          \
        return type_literal;                                                   \
    }

    OVERLOAD(Int, "int32_t");
    OVERLOAD(FD, "/* fd */ int32_t");

    std::string operator()(ArgTypes::NewID id)
    {
        return std::format(
            "/* new_id {} */ uint32_t", id.interface_name.value());
    };

    OVERLOAD(UInt, "uint32_t");

    std::string operator()(ArgTypes::UIntEnum e)
    {
        std::string enum_name = e.name;
        if (e.interface_name.has_value()) {
            enum_name = std::format(
                "{}_interface<{}>::{}",
                e.interface_name.value(),
                _traits.typename_string,
                enum_name);
        }
        return std::format("enum {}", enum_name);
    };

    OVERLOAD(Fixed, "/* wl_fixed_t */ int32_t");

    OVERLOAD(String, "const char *");
    OVERLOAD(NullString, "/* nullptr */ const char *");

    std::string object_interface_name(
        Wayland::ScannerTypes::InterfaceNameable &iface,
        const std::string &orig_type_diag)
    {
        if (!iface.interface_name.has_value()) {
            return std::format("/* {} */ void*", orig_type_diag);
        }
        return std::format(
            "/* {} */ {} *", orig_type_diag, iface.interface_name.value());
    }

    std::string operator()(ArgTypes::Object obj)
    {
        return object_interface_name(obj, "object");
    };

    std::string operator()(ArgTypes::NullObject obj)
    {
        return object_interface_name(obj, "nullptr<object>");
    };

    OVERLOAD(Array, "struct wl_array *")

#undef OVERLOAD

  private:
    const InterfaceTraits &_traits;
};

StringList emit_interface_listener_type_event(
    const Wayland::ScannerTypes::Event &ev,
    const InterfaceTraits &interface_traits)
{
    StringList o;
    o += std::format("// {}", __func__);

    StringList args;

    args += "void *data";
    for (auto &arg : ev.args) {
        std::string type_string =
            std::visit(TypeToStringVisitor{interface_traits}, arg.type);
        args += std::format("{} {}", type_string, arg.name);
    }

    auto rargs = std::views::reverse(args.get());

    bool first = true;
    for (auto &arg : rargs) {
        std::string val = arg;
        if (!first) {
            val += ',';
        }
        first = false;
        arg = std::move(val);
    }

    o += std::format("using {}_FN = void(", ev.name);
    args.leftPad("    ");
    o += std::move(args);
    o += ");";
    o += std::format("{}_FN *{} = nullptr;", ev.name, ev.name);

    return o;
}

StringList emit_interface_event_listener_type(
    const std::vector<Wayland::ScannerTypes::Event> &events,
    const InterfaceTraits &interface_traits)
{
    if (events.empty()) {
        throw std::logic_error("Cannot generate listener for empty events");
    }

    StringList o;
    o += std::format("// {}", __func__);
    o += "struct listener";
    o += "{";
    bool first = true;
    for (auto &event : events) {
        if (!first) {
            o += "";
        }
        first = false;
        auto typedef_str =
            emit_interface_listener_type_event(event, interface_traits);
        typedef_str.leftPad("    ");
        o += std::move(typedef_str);
    }
    o += "};";

    return o;
}

StringList emit_interface_add_listener_member_fn(const InterfaceData &iface)
{
    StringList o;
    std::string n = iface.name;

    o += std::format("// {}", __func__);
    o += std::format(
        "int add_listener({0} *{0}, const listener *listener, void *data)", n);
    o += "{";
    {
        StringList b;
        b += std::format("return _core.wl_proxy_add_listener(");
        b += std::format("    reinterpret_cast<wl_proxy*>({0}),", n);
        b += std::format("    (void (**)(void))listener,");
        b += std::format("    data");
        b += std::format(");");
        b.leftPad("    ");
        o += std::move(b);
    }
    o += "}";
    return o;
}

StringList emit_interface_ctor(
    const InterfaceData &iface, const InterfaceTraits &interface_traits)
{
    StringList o;
    o += std::format("// {}", __func__);
    o += std::format(
        "{}_interface(typename {} &core) : _core{{core}}{{}};",
        iface.name,
        interface_traits.wayland_client_core_functions_typename);
    return o;
}

StringList emit_enum(const Wayland::ScannerTypes::Enum &eenum)
{
    StringList o;
    o += std::format("// {}", __func__);

    o += std::format("enum class {}", eenum.name);
    o += "{";

    StringList es;
    for (auto &entry : eenum.entries) {
        std::string val;
        if (entry.is_hex) {
            val = std::format("0x{:x}", entry.value);
        } else {
            val = std::format("{}", entry.value);
        }
        std::string enum_name = entry.name;
        if (std::isdigit(enum_name.at(0))) {
            enum_name = std::format("n{}", enum_name);
        }

        auto bad_enum_name = [](const std::string &name) {
            if (name == "default") {
                return true;
            }
            return false;
        };

        if (bad_enum_name(enum_name)) {
            enum_name = std::format("e{}", enum_name);
        }
        es += std::format("{} = {}", enum_name, val);
    }

    auto rev = std::ranges::views::reverse(es.get());

    bool first = true;
    for (std::string &s : rev) {
        if (first) {
            first = false;
            continue;
        }
        s = std::format("{},", s);
    }

    es.leftPad("    ");
    o += std::move(es);
    o += "};";

    return o;
}

struct ArgInterfaceDependencyGetterVisitor
{
    using ArgTypes = Wayland::ScannerTypes::ArgTypes;
    using InterfaceNameable = Wayland::ScannerTypes::InterfaceNameable;

    std::optional<std::string> from_namable(const InterfaceNameable &n)
    {
        return n.interface_name;
    }

#define OVERLOAD(TYPENAME)                                                     \
    std::optional<std::string> operator()(const ArgTypes::TYPENAME &)          \
    {                                                                          \
        return std::nullopt;                                                   \
    }

#define OVERLOAD_N(TYPENAME)                                                   \
    std::optional<std::string> operator()(const ArgTypes::TYPENAME &n)         \
    {                                                                          \
        return from_namable(n);                                                \
    }

    // TODO Deal with different type of dependecies (all types form a cycles)
    OVERLOAD(Int);
    OVERLOAD(UInt);
    OVERLOAD_N(UIntEnum);
    OVERLOAD(Fixed);
    OVERLOAD(String);
    OVERLOAD(NullString);
    OVERLOAD_N(Object);
    OVERLOAD_N(NullObject);
    OVERLOAD_N(NewID);
    OVERLOAD(Array);
    OVERLOAD(FD);

#undef OVERLOAD
#undef OVERLOAD_N
};

std::optional<std::string>
    get_arg_interface_dep(const Wayland::ScannerTypes::Arg &arg)
{
    return std::visit(ArgInterfaceDependencyGetterVisitor{}, arg.type);
}

/*
 * TODO: Remove code duplication in:
 *  interface_get_dep_names
 *  and
 *  interface_get_enum_dep_names
 */
std::unordered_set<std::string>
    interface_get_dep_names(const InterfaceData &iface)
{
    std::unordered_set<std::string> o;

    std::vector<Wayland::ScannerTypes::Message> msgs;
    for (auto &event : iface.events) {
        msgs.push_back(event);
    }

    for (auto &req : iface.requests) {
        msgs.push_back(req);
    }

    for (auto &msg : msgs) {
        for (auto &arg : msg.args) {

            std::optional<std::string> dep_iface_name =
                get_arg_interface_dep(arg);
            if (!dep_iface_name.has_value()) {
                continue;
            }
            o.insert(dep_iface_name.value());
        }
    }

    return o;
}

/*
 * TODO: Remove code duplication in:
 *  interface_get_dep_names
 *  and
 *  interface_get_enum_dep_names
 */
std::unordered_set<std::string>
    interface_get_enum_dep_names(const InterfaceData &iface)
{
    std::unordered_set<std::string> o;

    std::vector<Wayland::ScannerTypes::Message> msgs;
    for (auto &event : iface.events) {
        msgs.push_back(event);
    }

    for (auto &req : iface.requests) {
        msgs.push_back(req);
    }

    for (auto &msg : msgs) {
        for (auto &arg : msg.args) {

            using ArgTypes = Wayland::ScannerTypes::ArgTypes;
            if (!std::holds_alternative<ArgTypes::UIntEnum>(arg.type)) {
                continue;
            }

            std::optional<std::string> dep_iface_name =
                get_arg_interface_dep(arg);
            if (!dep_iface_name.has_value()) {
                continue;
            }
            o.insert(dep_iface_name.value());
        }
    }

    return o;
}

struct NewIDAsReturnType
{
    Wayland::ScannerTypes::ArgTypes::NewID arg;
    std::string name;
};

struct EmitRequestFunctionData
{
    EmitRequestFunctionData(
        const Wayland::ScannerTypes::Request &i_request,
        const std::optional<NewIDAsReturnType> &i_return_type_op,
        const InterfaceTraits &i_interface_traits)
        : request(i_request), return_type_op(i_return_type_op),
          interface_traits{i_interface_traits}
    {
    }

    const Wayland::ScannerTypes::Request &request;
    const std::optional<NewIDAsReturnType> &return_type_op;
    const InterfaceTraits &interface_traits;
    std::string interface_name;
    std::string first_arg_name;
    std::string request_index_name;
    std::string new_id_inteface_name;
};

StringList
    emit_interface_request_signature_args(const EmitRequestFunctionData &D)
{
    using NewID = Wayland::ScannerTypes::ArgTypes::NewID;

    StringList args_strings;
    args_strings += std::format("// {}", __func__);

    std::vector<std::expected<std::string, std::string>> signature_args;

    std::string first_arg =
        std::format("{} *{}", D.interface_name, D.first_arg_name);
    signature_args.emplace_back(std::move(first_arg));

    for (auto &arg : D.request.args) {
        const NewID *arg_new_id_p = std::get_if<NewID>(&arg.type);
        if (arg_new_id_p != nullptr) {
            auto arg_interface_name = arg_new_id_p->interface_name;
            if (!arg_interface_name) {
                signature_args.emplace_back(
                    std::format(
                        "const {} *{}",
                        D.interface_traits
                            .wayland_client_core_wl_interface_typename,
                        D.new_id_inteface_name));
                signature_args.emplace_back("uint32_t version");
                continue;
            }

            std::string diagnostic = std::format(
                "(name=[{}] type=[new_id] interface=[{}])",
                arg.name,
                arg_interface_name.value());
            signature_args.emplace_back(std::unexpected{diagnostic});

            continue;
        }

        std::string arg_typename =
            std::visit(TypeToStringVisitor{D.interface_traits}, arg.type);
        signature_args.emplace_back(
            std::format("{} {}", arg_typename, arg.name));
    }

    auto args_inv = std::ranges::views::reverse(signature_args);
    bool first = true;
    for (auto &arg : args_inv) {
        if (!arg.has_value()) {
            continue;
        }

        if (!first) {
            arg.value() = std::format("{},", arg.value());
        }
        first = false;
    }

    for (auto arg : signature_args) {
        if (!arg) {
            args_strings += std::format("// [[nogen]]: {}", arg.error());
            continue;
        }
        args_strings += std::move(arg.value());
    }

    return args_strings;
}

StringList emit_interface_request_body(const EmitRequestFunctionData &D)
{
    StringList o;
    o += std::format("// {}", __func__);

    std::string first_arg_proxy_id =
        std::format("{}_as_proxy", D.first_arg_name);
    std::string first_arg_proxy;
    first_arg_proxy += std::format("wl_proxy *{}", first_arg_proxy_id);
    first_arg_proxy += " = reinterpret_cast<wl_proxy*>(";
    first_arg_proxy += std::format("{}", D.first_arg_name);
    first_arg_proxy += ");";
    o += std::move(first_arg_proxy);

    std::optional<std::string> output_identifier;
    if (D.return_type_op.has_value()) {
        output_identifier =
            std::format("out_{}", D.return_type_op.value().name);
        o += std::format("wl_proxy *{} = nullptr;", output_identifier.value());
    }

    std::string wl_proxy_marshal_flags_call_start =
        "_core.wl_proxy_marshal_flags(";
    if (output_identifier) {
        wl_proxy_marshal_flags_call_start = std::format(
            "{} = {}",
            output_identifier.value(),
            wl_proxy_marshal_flags_call_start);
    }
    o += std::move(wl_proxy_marshal_flags_call_start);

    StringList args;
    args += std::format("{}", first_arg_proxy_id);
    args += std::string{D.request_index_name};

    if (D.return_type_op) {
        const NewIDAsReturnType &return_type = D.return_type_op.value();
        if (return_type.arg.interface_name) {
            args += std::format(
                "/* TODO generate and paste here [{}] interface rtti"
                "*/ nullptr",
                return_type.arg.interface_name.value());
        } else {
            args += std::string{D.new_id_inteface_name};
        }
    } else {
        args += "nullptr";
    }

    if (D.return_type_op &&
        !D.return_type_op.value().arg.interface_name.has_value()) {
        args += "version";
    } else {
        args +=
            std::format("_core.wl_proxy_get_version({})", first_arg_proxy_id);
    }

    bool is_destructor = false;
    if (D.request.type.has_value()) {
        using Message = Wayland::ScannerTypes::Message;
        Message::Type type = D.request.type.value();
        is_destructor = std::get_if<Message::TypeDestructor>(&type) != nullptr;
    }

    if (is_destructor) {
        args += "/* WL_MARSHAL_FLAG_DESTROY */ (1 << 0)";
    } else {
        args += "0";
    }

    using Arg = Wayland::ScannerTypes::Arg;
    using ArgTypes = Wayland::ScannerTypes::ArgTypes;
    for (const Arg &arg : D.request.args) {
        const ArgTypes::NewID *new_id_arg =
            std::get_if<ArgTypes::NewID>(&arg.type);
        bool is_new_id = new_id_arg != nullptr;
        if (is_new_id) {
            bool no_interface = !new_id_arg->interface_name.has_value();
            if (no_interface) {
                args += std::format("{}->name", D.new_id_inteface_name);
                args += "version";
            }
            args += "nullptr";
            continue;
        }

        args += std::string{arg.name};
    }

    auto args_rev = std::ranges::views::reverse(args.get());
    bool first = true;
    for (auto &arg : args_rev) {
        if (!first) {
            arg = std::format("{},", arg);
        }
        first = false;
    }

    args.leftPad("    ");
    o += std::move(args);
    o += ");";

    if (D.return_type_op &&
        !D.return_type_op.value().arg.interface_name.has_value()) {
        o += std::format(
            "return reinterpret_cast<void*>({});", output_identifier.value());
    } else if (D.return_type_op) {
        o += std::format(
            "return reinterpret_cast<{}*>({});",
            D.return_type_op.value().arg.interface_name.value(),
            output_identifier.value());
    }

    return o;
}

struct EmitSingleRequestData
{
    EmitSingleRequestData(
        const Wayland::ScannerTypes::Request &i_req,
        const InterfaceTraits &i_interfaca_traits)
        : request{i_req}, interface_traits{i_interfaca_traits}
    {
    }
    const Wayland::ScannerTypes::Request &request;
    const InterfaceTraits &interface_traits;
    std::string interface_name;
    std::string request_index_name;
    std::string interface_rtty_typename;
};

StringList emit_interface_request(const EmitSingleRequestData &D)
{
    using NewID = Wayland::ScannerTypes::ArgTypes::NewID;
    StringList o;
    o += std::format("// {}", __func__);

    std::optional<NewIDAsReturnType> return_type;
    std::vector<std::string> new_id_arg_names;
    for (auto &arg : D.request.args) {
        const NewID *new_id_arg_p = std::get_if<NewID>(&arg.type);
        if (new_id_arg_p == nullptr) {
            continue;
        }
        if (!return_type) {
            return_type = {*new_id_arg_p, arg.name};
        }
        new_id_arg_names.emplace_back(arg.name);
    }

    if (new_id_arg_names.size() > 1) {
        /*
         * I have no idea why it is that way:
         * Reference implementation seems to ignore requests with
         * more than one argument with type="new_id"
         *
         * So it seems right to do the same thing here
         */
        o += "/*";
        o += std::format(
            " * Multiple new_id args: Ignore [{}] request generation",
            D.request.name);
        size_t new_id_name_i = 0;
        for (auto &new_id_name : new_id_arg_names) {
            o += std::format(" * new_id[{}] {}", new_id_name_i, new_id_name);
            new_id_name_i++;
        }
        o += " */";
        return o;
    }

    std::string return_type_string = "void";
    if (return_type.has_value()) {
        return_type_string = "void *";
        auto &new_id = return_type.value().arg;
        if (new_id.interface_name.has_value()) {
            return_type_string =
                std::format("{}*", new_id.interface_name.value());
        }
    }

    EmitRequestFunctionData emit_fn_data{
        D.request, return_type, D.interface_traits};
    emit_fn_data.interface_name = D.interface_name;
    emit_fn_data.first_arg_name = std::format("{}_ptr", D.interface_name);
    emit_fn_data.request_index_name = D.request_index_name;
    emit_fn_data.new_id_inteface_name = "interface";

    auto signature_args = emit_interface_request_signature_args(emit_fn_data);
    signature_args.leftPad("    ");

    o += std::format("{} {}(", return_type_string, D.request.name);
    o += std::move(signature_args);
    o += ")";

    StringList body = emit_interface_request_body(emit_fn_data);

    o += "{";
    body.leftPad("    ");
    o += std::move(body);
    o += "}";

    return o;
}

struct EmitInterfaceRequestsData
{
    EmitInterfaceRequestsData(
        const InterfaceData &i_iface, const InterfaceTraits &i_interface_traits)
        : iface{i_iface}, interface_traits{i_interface_traits}
    {
    }
    const InterfaceData &iface;
    const InterfaceTraits &interface_traits;
};

StringList emit_interface_requests(const EmitInterfaceRequestsData &D)
{
    StringList o;
    o += std::format("// {}", __func__);

    size_t next_req_index = 0;
    for (auto &request : D.iface.requests) {
        auto req_i = next_req_index;
        next_req_index++;
        std::string request_index_name =
            std::format("request_index_{}", request.name);
        o += std::format(
            "static constexpr size_t {} = {};", request_index_name, req_i);
        EmitSingleRequestData iface_emit_data{request, D.interface_traits};
        iface_emit_data.interface_name = D.iface.name;
        iface_emit_data.request_index_name = request_index_name;
        o += emit_interface_request(iface_emit_data);
        if (next_req_index != 0) {
            o += "";
        }
    }

    return o;
}

StringList emit_interface(const InterfaceData &iface)
{
    StringList o;
    o += std::format("// {}", __func__);

    InterfaceTraits traits;
    traits.typename_string = std::format("{}_traits", iface.name);
    traits.wayland_client_core_functions_typename =
        std::format("{}::client_core_functions_t", traits.typename_string);
    traits.wayland_client_core_wl_interface_typename =
        std::format("{}::wl_interface_t", traits.typename_string);
    traits.wayland_client_core_wl_message_typename =
        std::format("{}::wl_message_t", traits.typename_string);

    {
        auto enum_deps = interface_get_enum_dep_names(iface);
        if (!enum_deps.empty()) {
            o += "";
            StringList deps;
            for (auto &dep : enum_deps) {
                deps += std::format("[{}]", dep);
            }
            deps.leftPad(" * ");
            o += "/*";
            o += " * Dependencies:";
            o += std::move(deps);
            o += " */";
        }
    }

    o += std::format("template <typename {}>", traits.typename_string);
    o += std::format("struct {}_interface", iface.name);
    o += "{";
    bool has_structure = false;
    auto add_sep = [&has_structure, &o]() {
        if (has_structure) {
            o += "";
        }
        has_structure = true;
    };

    {
        for (auto &e : iface.enums) {
            add_sep();
            StringList eenum = emit_enum(e);
            eenum.leftPad("    ");

            o += std::move(eenum);
        }
    }

    bool has_events = !iface.events.empty();
    if (has_events) {
        add_sep();

        StringList type =
            emit_interface_event_listener_type(iface.events, traits);
        type.leftPad("    ");
        o += std::move(type);
    }

    {
        add_sep();
        StringList ctor = emit_interface_ctor(iface, traits);
        ctor.leftPad("    ");
        o += std::move(ctor);
    }

    if (has_events) {
        add_sep();
        StringList add_listener_code =
            emit_interface_add_listener_member_fn(iface);
        add_listener_code.leftPad("    ");
        o += std::move(add_listener_code);
    }

    {
        add_sep();
        EmitInterfaceRequestsData data{iface, traits};
        StringList requests = emit_interface_requests(data);
        requests.leftPad("    ");
        o += std::move(requests);
    }

    o += "";
    o += "private:";
    o += std::format(
        "    typename {} &_core;",
        traits.wayland_client_core_functions_typename);
    o += "};";

    return o;
}

std::vector<InterfaceData>
    topo_sort_interfaces(const std::vector<InterfaceData> &in)
{
    std::unordered_map<std::string, InterfaceData> ifaces;
    for (auto &iface : in) {
        ifaces.insert({iface.name, iface});
    }

    StringDG graph;
    for (auto &iface : ifaces) {
        graph.add_node(iface.first);
    }

    for (auto &iface : ifaces) {
        // TODO: Deal with different type of dependecies
        // (all types form a cycles)
        auto deps = interface_get_enum_dep_names(iface.second);
        for (auto &dep_name : deps) {
            graph.add_dependency(dep_name, iface.first);
        }
    }

    std::vector<InterfaceData> o;
    auto sorted_names = graph.topo_sorted();
    for (auto &name : sorted_names) {
        o.emplace_back(std::move(ifaces.at(name)));
    }
    std::ranges::reverse(o);
    return o;
}

StringList emit_object_forward(
    const std::vector<Wayland::ScannerTypes::Interface> &interfaces)
{
    StringList o;
    o += std::format("// {}", __func__);

    std::unordered_set<std::string> emited;

    std::unordered_set<std::string> emit_object_forward;
    for (auto &iface : interfaces) {
        emited.insert(iface.name);
        o += std::format("struct {};", iface.name);
    }

    for (auto &iface : interfaces) {
        auto deps = interface_get_dep_names(iface);

        for (auto &emited_el : emited) {
            deps.erase(emited_el);
        }

        if (deps.empty()) {
            continue;
        }

        o += "";
        o += std::format("// deps for {}", iface.name);
        for (auto &dep : deps) {
            o += std::format("struct {};", dep);
        }
    }

    return o;
}

void process_header_mode(const EnumsModeArgs &args)
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
    auto interfaces = protocol.interfaces;
    auto sorted_interfaces = topo_sort_interfaces(interfaces);

    StringList o;
    o += "#pragma once";
    o += "";
    o += "#include <cstdint>";
    o += "#include <cstddef>";
    o += "";

    o += "struct wl_proxy;";

    o += "";

    o += emit_object_forward(interfaces);

    o += "";

    o += std::format("namespace {} {{", protocol.name);
    o += "";

    bool first = true;
    for (auto &iface : sorted_interfaces) {
        if (!first) {
            o += "";
        }
        first = false;
        o += emit_interface(iface);
    }

    o += std::format("}} // namespace {}", protocol.name);

    auto make_empty_if_blank = [](std::string &str) {
        for (auto c : str) {
            if (c != ' ') {
                return;
            }
        }
        str = "";
    };

    std::string output;
    for (auto &o_line : o.get()) {
        make_empty_if_blank(o_line);
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

    auto header_mode_args_op = parse_header_mode(argv);
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
