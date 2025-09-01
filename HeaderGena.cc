#include <algorithm>
#include <format>
#include <optional>
#include <ranges>
#include <source_location>
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

#include "HeaderGena.hh"
#include "StringList.hh"
#include "Types.hh"

namespace {
std::string_view func(std::source_location s = std::source_location::current())
{
    return s.function_name();
}

StringList indent(StringList &in)
{
    StringList o;
    for (const std::string &str : in.get()) {
        o += std::format("    {}", str);
    }
    return o;
}

} // namespace

namespace wl_gena {

struct InterfaceTraits
{
    std::string typename_string;
    std::string wayland_client_library_typename;
    std::string wayland_client_core_wl_proxy_typename;
    std::string wayland_client_core_wl_interface_typename;
    std::string wayland_client_core_wl_message_typename;
};

struct NamespaceInfo
{
    NamespaceInfo(
        const types::Protocol &main_protocol,
        const std::span<const types::Protocol> context_protocols,
        std::optional<std::string> top_namespace)
        : _interface_protocol_map{[&main_protocol, &context_protocols]() {
              std::unordered_map<std::string, std::string> o;

              auto throw_if_iface_exist =
                  [&o](
                      const std::string &iface_name,
                      const std::string &new_proto_name) {
                      if (o.contains(iface_name)) {
                          std::string protocol_with_same_interface =
                              o.at(iface_name);
                          std::string message = std::format(
                              "Found multiple definition of inteface [{}] "
                              "defined in [{}] and [{}]. "
                              "Protocol resolution would not be possible",
                              iface_name,
                              new_proto_name,
                              protocol_with_same_interface);
                          throw std::runtime_error{std::move(message)};
                      }
                  };

              for (const types::Interface &iface : main_protocol.interfaces) {
                  throw_if_iface_exist(iface.name, main_protocol.name);
                  o[iface.name] = main_protocol.name;
              }

              for (const types::Protocol &proto : context_protocols) {
                  const std::string proto_name = proto.name;
                  for (const types::Interface &iface : proto.interfaces) {
                      throw_if_iface_exist(iface.name, proto_name);
                      o[iface.name] = proto_name;
                  }
              }
              return o;
          }()},
          _top_namespace{std::move(top_namespace)}
    {
    }

    std::string get_namespace(const std::string &interface_name) const
    {
        std::optional<std::string> proto_name_op =
            protocol_by_interface(interface_name);
        if (!proto_name_op.has_value()) {
            std::string msg = std::format(
                "Cannot resolve protocol for [{}] interface", interface_name);
            throw std::runtime_error{std::move(msg)};
        }
        const std::string proto_name = proto_name_op.value();

        std::string upstream_namespace;
        if (_top_namespace) {
            upstream_namespace = std::format("::{}", _top_namespace.value());
        }

        return std::format("{}::{}", upstream_namespace, proto_name);
    };

    const std::optional<std::string> &top_namespace() const
    {
        return _top_namespace;
    }

  private:
    std::optional<std::string>
        protocol_by_interface(const std::string &interface) const
    {
        std::optional<std::string> o;
        auto it = _interface_protocol_map.find(interface);
        if (it == std::end(_interface_protocol_map)) {
            return {};
        }
        return it->second;
    }

    std::unordered_map<std::string, std::string> _interface_protocol_map;
    std::optional<std::string> _top_namespace;
};

struct HeaderGenerator
{
    HeaderGenerator(
        const types::Protocol &protocol, const NamespaceInfo &ns_info)
        : _protocol{protocol}, _ns_info{ns_info}
    {
    }

    StringList generate() const;
    StringList emit_object_forward() const;

    std::vector<std::string> &includes()
    {
        return _includes;
    }

  private:
    const types::Protocol &_protocol;
    const NamespaceInfo &_ns_info;
    std::vector<std::string> _includes;
};

struct InterfaceGenerator
{
    InterfaceGenerator(
        const wl_gena::types::Interface &interface,
        const NamespaceInfo &ns_info)
        : _interface{interface}, _ns_info{ns_info}
    {
        _traits.typename_string = std::format("{}_traits", _interface.name);
        _traits.wayland_client_library_typename =
            std::format("{}::client_library_t", _traits.typename_string);
        _traits.wayland_client_core_wl_interface_typename =
            std::format("{}::wl_interface_t", _traits.typename_string);
        _traits.wayland_client_core_wl_proxy_typename =
            std::format("{}::wl_proxy_t", _traits.typename_string);
        _traits.wayland_client_core_wl_message_typename =
            std::format("{}::wl_message_t", _traits.typename_string);
    }

    StringList generate() const;
    StringList emit_enums() const;
    static StringList emit_enum(const wl_gena::types::Enum &eenum);
    StringList emit_interface_event_listener_type() const;
    StringList emit_interface_listener_type_event(size_t event_index) const;
    StringList emit_interface_add_listener_member_fn() const;
    StringList emit_interface_requests() const;

    InterfaceGenerator(const InterfaceGenerator &) = delete;
    InterfaceGenerator(InterfaceGenerator &&) = delete;
    InterfaceGenerator &operator=(const InterfaceGenerator &) = delete;
    InterfaceGenerator &operator=(InterfaceGenerator &&) = delete;

  private:
    const wl_gena::types::Interface &_interface;
    const NamespaceInfo &_ns_info;
    InterfaceTraits _traits;
};

struct RequestGenerator
{
    struct NewIDArg
    {
        std::string name;
        wl_gena::types::ArgTypes::NewID arg;
    };

    RequestGenerator(
        const wl_gena::types::Request &request,
        const InterfaceTraits &traits,
        const NamespaceInfo &ns_info,
        std::string interface_name,
        std::string request_index_name)
        : _request{request}, _traits{traits}, _ns_info{ns_info},
          _interface_name{std::move(interface_name)},
          _request_index_name{std::move(request_index_name)},
          _first_arg_name{std::format("{}_ptr", _interface_name)},
          _new_id_inteface_name{"interface"}
    {
        namespace V = std::views;

        using Arg = wl_gena::types::Arg;
        using NewID = wl_gena::types::ArgTypes::NewID;

        auto is_new_id = [](const Arg &arg) {
            return std::holds_alternative<NewID>(arg.type);
        };

        auto to_new_id_arg = [](const Arg &arg) {
            return NewIDArg{arg.name, std::get<NewID>(arg.type)};
        };

        _new_ids = std::ranges::to<std::vector>(
            _request.args | V::filter(is_new_id) | V::transform(to_new_id_arg));
        if (!_new_ids.empty()) {
            _return_type = _new_ids[0];
        }
    }

    StringList emit_interface_request() const;
    StringList emit_interface_request_signature_args() const;
    StringList emit_interface_request_body() const;

  private:
    const wl_gena::types::Request &_request;
    const InterfaceTraits &_traits;
    const NamespaceInfo &_ns_info;
    std::string _interface_name;
    std::string _request_index_name;

    std::vector<NewIDArg> _new_ids;

    std::optional<NewIDArg> _return_type;
    std::string _first_arg_name;
    std::string _new_id_inteface_name;
};

struct TypeToStringVisitor
{
    explicit TypeToStringVisitor(
        const InterfaceTraits &traits, const NamespaceInfo &ns_info)
        : _traits{traits}, _ns_info{ns_info}
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

    using ArgTypes = wl_gena::types::ArgTypes;

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
        std::string enum_typename = e.name;
        if (e.interface_name.has_value()) {

            const std::string &interface_name = e.interface_name.value();
            std::string interface_type = std::format(
                "{}::{}<{}>",
                _ns_info.get_namespace(interface_name),
                interface_name,
                _traits.typename_string);

            enum_typename =
                std::format("typename {}::{}", interface_type, enum_typename);
        }
        return std::format("{}_e", enum_typename);
    };

    OVERLOAD(Fixed, "/* wl_fixed_t */ int32_t");

    OVERLOAD(String, "const char *");
    OVERLOAD(NullString, "/* nullptr */ const char *");

    std::string object_interface_name(
        wl_gena::types::InterfaceNameable &iface, const std::string &comment)
    {
        if (!iface.interface_name.has_value()) {
            return std::format("/* {} */ void*", comment);
        }
        const std::string &interface_name = iface.interface_name.value();

        std::string interface_type = std::format(
            "{}::{}<{}>",
            _ns_info.get_namespace(interface_name),
            interface_name,
            _traits.typename_string);

        return std::format(
            "/* {} */ typename {}::handle_t*", comment, interface_type);
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
    const NamespaceInfo &_ns_info;
};

StringList InterfaceGenerator::emit_interface_listener_type_event(
    size_t event_index) const
{
    StringList o;
    o += std::format("// {}", func());

    StringList args;

    const types::Event &ev = _interface.events.at(event_index);

    args += "void *data";
    args += std::format("{} *object", _interface.name);
    for (auto &arg : ev.args) {
        std::string type_string =
            std::visit(TypeToStringVisitor{_traits, _ns_info}, arg.type);
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
    o += indent(args);
    o += ");";
    o += std::format("{}_FN *{} = nullptr;", ev.name, ev.name);

    return o;
}

StringList InterfaceGenerator::emit_interface_event_listener_type() const
{
    if (_interface.events.empty()) {
        throw std::logic_error("Cannot generate listener for empty events");
    }

    StringList o;
    o += std::format("// {}", func());
    o += "struct listener_t";
    o += "{";
    bool first = true;
    for (size_t event_i = 0; event_i != _interface.events.size(); ++event_i) {
        if (!first) {
            o += "";
        }
        first = false;
        auto typedef_str = emit_interface_listener_type_event(event_i);
        o += indent(typedef_str);
    }
    o += "};";

    return o;
}

StringList InterfaceGenerator::emit_interface_add_listener_member_fn() const
{
    StringList o;
    o += std::format("// {}", func());

    const std::string &n = _interface.name;
    const std::string &proxy = _traits.wayland_client_core_wl_proxy_typename;

    std::string interface_type = std::format(
        "{}::{}<{}>", _ns_info.get_namespace(n), n, _traits.typename_string);

    std::string first_arg =
        std::format("{}::handle_t *{}_handle", interface_type, n);

    o += std::format(
        "int add_listener({}, const listener_t *listener, void *data)",
        first_arg);
    o += "{";
    {
        StringList b;
        b += std::format("return L.wl_proxy_add_listener(");
        b += std::format("    reinterpret_cast<{}*>({}_handle),", proxy, n);
        b += std::format("    (void (**)(void))listener,");
        b += std::format("    data");
        b += std::format(");");
        o += indent(b);
    }
    o += "}";
    return o;
}

StringList wl_gena::InterfaceGenerator::emit_enums() const
{
    StringList o;
    o += std::format("// {}", func());

    bool first = true;
    for (auto &e : _interface.enums) {
        if (!first) {
            o += "";
        }
        first = false;
        o += emit_enum(e);
    }

    return o;
};

auto wl_gena::InterfaceGenerator::emit_enum(const wl_gena::types::Enum &eenum)
    -> StringList
{
    StringList o;
    o += std::format("// {}", func());

    o += std::format("enum class {}_e", eenum.name);
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

    o += indent(es);
    o += "};";

    return o;
}

StringList RequestGenerator::emit_interface_request_signature_args() const
{
    using NewID = wl_gena::types::ArgTypes::NewID;

    StringList args_strings;
    args_strings += std::format("// {}", func());

    struct ArgEmitInfo
    {
        ArgEmitInfo() : _v{"No diagnostic"}
        {
        }

        ArgEmitInfo(std::string val) : _v{std::move(val)}
        {
        }

        void set_diagnostic(std::string diagnostic)
        {
            _v.reset();
            _diagnostic = std::move(diagnostic);
        }

        bool has_value() const
        {
            return _v.has_value();
        }

        std::string &value()
        {
            return _v.value();
        }

        std::string &diagnostic()
        {
            return _diagnostic.value();
        }

      private:
        std::optional<std::string> _v{};
        std::optional<std::string> _diagnostic{};
    };

    std::vector<ArgEmitInfo> signature_args;

    std::string interface_type = std::format(
        "{}::{}<{}>",
        _ns_info.get_namespace(_interface_name),
        _interface_name,
        _traits.typename_string);

    signature_args.emplace_back(
        std::format("{}::handle_t *{}", interface_type, _first_arg_name));

    for (auto &arg : _request.args) {
        const NewID *arg_new_id_p = std::get_if<NewID>(&arg.type);
        if (arg_new_id_p != nullptr) {
            auto arg_interface_name = arg_new_id_p->interface_name;
            if (!arg_interface_name) {
                std::string arg_str = std::format(
                    "const {} *{}",
                    _traits.wayland_client_core_wl_interface_typename,
                    _new_id_inteface_name);
                signature_args.emplace_back(arg_str);
                signature_args.emplace_back("uint32_t version");
                continue;
            }

            std::string diagnostic = std::format(
                "(name=[{}] type=[new_id] interface=[{}])",
                arg.name,
                arg_interface_name.value());

            ArgEmitInfo diag_arg;
            diag_arg.set_diagnostic(diagnostic);
            signature_args.emplace_back(std::move(diag_arg));

            continue;
        }

        std::string arg_typename =
            std::visit(TypeToStringVisitor{_traits, _ns_info}, arg.type);
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
        if (!arg.has_value()) {
            args_strings += std::format("// [[nogen]]: {}", arg.diagnostic());
            continue;
        }
        args_strings += std::move(arg.value());
    }

    return args_strings;
}

StringList RequestGenerator::emit_interface_request_body() const
{
    StringList o;
    o += std::format("// {}", func());

    std::string first_arg_proxy_id =
        std::format("{}_as_proxy", _first_arg_name);
    std::string first_arg_proxy;
    first_arg_proxy += std::format(
        "typename {} *{}",
        _traits.wayland_client_core_wl_proxy_typename,
        first_arg_proxy_id);
    first_arg_proxy +=
        std::format(" = reinterpret_cast<decltype({})>", first_arg_proxy_id);
    first_arg_proxy += std::format("({});", _first_arg_name);
    o += std::move(first_arg_proxy);

    std::optional<std::string> output_identifier;
    if (_return_type.has_value()) {
        output_identifier = std::format("out_{}", _return_type.value().name);
        o += std::format(
            "typename {} *{} = nullptr;",
            _traits.wayland_client_core_wl_proxy_typename,
            output_identifier.value());
    }

    std::string wl_proxy_marshal_flags_call_start = "L.wl_proxy_marshal_flags(";
    if (output_identifier) {
        wl_proxy_marshal_flags_call_start = std::format(
            "{} = {}",
            output_identifier.value(),
            wl_proxy_marshal_flags_call_start);
    }
    o += std::move(wl_proxy_marshal_flags_call_start);

    StringList args;
    args += std::format("{}", first_arg_proxy_id);
    args += std::string{_request_index_name};

    if (_return_type) {
        const NewIDArg &return_type = _return_type.value();
        if (return_type.arg.interface_name) {
            const std::string &interface_name =
                return_type.arg.interface_name.value();

            std::string rtti_interface_type = std::format(
                "{}::rtti<{}>",
                _ns_info.get_namespace(interface_name),
                _traits.typename_string);

            args += std::format(
                "&{}::{}_interface", rtti_interface_type, interface_name);
        } else {
            args += std::string{_new_id_inteface_name};
        }
    } else {
        args += "nullptr";
    }

    if (_return_type && !_return_type.value().arg.interface_name.has_value()) {
        args += "version";
    } else {
        args += std::format("L.wl_proxy_get_version({})", first_arg_proxy_id);
    }

    bool is_destructor = false;
    if (_request.type.has_value()) {
        using Message = wl_gena::types::Message;
        Message::Type type = _request.type.value();
        is_destructor = std::get_if<Message::TypeDestructor>(&type) != nullptr;
    }

    if (is_destructor) {
        args += "/* WL_MARSHAL_FLAG_DESTROY */ (1 << 0)";
    } else {
        args += "0";
    }

    using Arg = wl_gena::types::Arg;
    using ArgTypes = wl_gena::types::ArgTypes;
    for (const Arg &arg : _request.args) {
        const ArgTypes::NewID *new_id_arg =
            std::get_if<ArgTypes::NewID>(&arg.type);
        bool is_new_id = new_id_arg != nullptr;
        if (is_new_id) {
            bool no_interface = !new_id_arg->interface_name.has_value();
            if (no_interface) {
                args += std::format("{}->name", _new_id_inteface_name);
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

    o += indent(args);
    o += ");";

    if (_return_type && !_return_type.value().arg.interface_name.has_value()) {
        o += std::format(
            "return reinterpret_cast<void*>({});", output_identifier.value());
    } else if (_return_type) {
        o += std::format(
            "return reinterpret_cast<{}<{}>::handle_t*>({});",
            _return_type.value().arg.interface_name.value(),
            _traits.typename_string,
            output_identifier.value());
    }

    return o;
}

StringList RequestGenerator::emit_interface_request() const
{
    StringList o;
    o += std::format("// {}", func());

    if (_new_ids.size() > 1) {
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
            _request.name);
        size_t new_id_name_i = 0;
        for (auto &new_id : _new_ids) {
            o += std::format(" * new_id[{}] {}", new_id_name_i, new_id.name);
            new_id_name_i++;
        }
        o += " */";
        return o;
    }

    std::string return_type_string = "void";
    if (_return_type.has_value()) {
        return_type_string = "void *";
        auto &new_id = _return_type.value().arg;
        if (new_id.interface_name.has_value()) {
            const std::string &interface_name = new_id.interface_name.value();
            std::string interface_type = std::format(
                "{}::{}<{}>",
                _ns_info.get_namespace(interface_name),
                interface_name,
                _traits.typename_string);

            return_type_string = std::format("{}::handle_t *", interface_type);
        }
    }

    o += std::format("{} {}(", return_type_string, _request.name);
    auto signature_args = emit_interface_request_signature_args();
    o += indent(signature_args);
    o += ")";

    StringList body = emit_interface_request_body();

    o += "{";
    o += indent(body);
    o += "}";

    return o;
}

StringList InterfaceGenerator::emit_interface_requests() const
{
    StringList o;
    o += std::format("// {}", func());

    size_t next_req_index = 0;
    for (auto &request : _interface.requests) {
        auto req_i = next_req_index;
        next_req_index++;
        if (req_i != 0) {
            o += "";
        }
        std::string request_index_name =
            std::format("request_index_{}", request.name);
        o += std::format(
            "static constexpr size_t {} = {};", request_index_name, req_i);
        RequestGenerator req_gen{
            request, _traits, _ns_info, _interface.name, request_index_name};
        o += req_gen.emit_interface_request();
    }

    return o;
}

StringList wl_gena::InterfaceGenerator::generate() const
{
    StringList o;
    o += std::format("// {}", func());

    o += std::format("template <typename {}>", _traits.typename_string);
    o += std::format("struct {}", _interface.name);
    o += "{";
    bool has_structure = false;
    auto add_sep = [&has_structure, &o]() {
        if (has_structure) {
            o += "";
        }
        has_structure = true;
    };

    add_sep();
    StringList handle_def;
    if (_interface.name == "wl_display") {
        handle_def +=
            "// Special case for wl_display from client library via traits";
        handle_def += std::format(
            "using handle_t = {}::wl_display_t;", _traits.typename_string);
    } else {
        handle_def += "struct handle_t;";
    }
    o += indent(handle_def);

    add_sep();
    auto enums = emit_enums();
    o += indent(enums);

    bool has_events = !_interface.events.empty();
    if (has_events) {
        add_sep();

        StringList type = emit_interface_event_listener_type();
        o += indent(type);
    }

    if (has_events) {
        add_sep();
        StringList add_listener_code = emit_interface_add_listener_member_fn();
        o += indent(add_listener_code);
    }

    {
        add_sep();
        StringList requests = emit_interface_requests();
        o += indent(requests);
    }

    add_sep();
    o += std::format(
        "    typename {} L;", _traits.wayland_client_library_typename);
    o += "};";

    return o;
}

StringList wl_gena::HeaderGenerator::emit_object_forward() const
{
    StringList o;
    o += std::format("// {}", func());

    for (auto &iface : _protocol.interfaces) {
        o += std::format(
            "template <typename {0}_traits> struct {0};", iface.name);
    }

    return o;
}

namespace rtti {

struct ArgsSignantureVisitor
{
    using ArgTypes = wl_gena::types::ArgTypes;
#define OVERLOAD(TYPE, type_literal)                                           \
    std::string operator()(const ArgTypes::TYPE &)                             \
    {                                                                          \
        return type_literal;                                                   \
    }

    OVERLOAD(Int, "i");
    OVERLOAD(UInt, "u");
    OVERLOAD(UIntEnum, "u");
    OVERLOAD(Fixed, "f");
    OVERLOAD(String, "s");
    OVERLOAD(NullString, "?s");
    OVERLOAD(Object, "o");
    OVERLOAD(NullObject, "?o");
    std::string operator()(const ArgTypes::NewID &id)
    {
        if (!id.interface_name) {
            return "sun";
        }
        return "i";
    };
    OVERLOAD(Array, "a");
    OVERLOAD(FD, "h");
#undef OVERLOAD
};

struct ArgsTypesVisitor
{
    using ArgTypes = wl_gena::types::ArgTypes;
    using InterfaceNameable = wl_gena::types::InterfaceNameable;

#define OVERLOAD(TYPE)                                                         \
    std::optional<std::string> operator()(const ArgTypes::TYPE &)              \
    {                                                                          \
        return std::nullopt;                                                   \
    }

#define OVERLOAD_IFACE(TYPE)                                                   \
    std::optional<std::string> operator()(const ArgTypes::TYPE &o)             \
    {                                                                          \
        return iface_typename(o);                                              \
    }

    OVERLOAD(Int);
    OVERLOAD(UInt);
    OVERLOAD(UIntEnum);
    OVERLOAD(Fixed);
    OVERLOAD(String);
    OVERLOAD(NullString);
    OVERLOAD_IFACE(Object);
    OVERLOAD_IFACE(NullObject);
    OVERLOAD_IFACE(NewID);
    OVERLOAD(Array);
    OVERLOAD(FD);

#undef OVERLOAD
#undef OVERLOAD_IFACE

    std::optional<std::string> iface_typename(InterfaceNameable n)
    {
        return n.interface_name;
    }
};

struct Arg
{
    std::string name;
    std::optional<std::string> rtti_type;
};

struct Message
{
    Message(const wl_gena::types::Message &msg)
    {
        name = msg.name;

        if (msg.since && msg.since.value() > 1) {
            args_signature += std::format("{}", msg.since.value());
        }

        for (auto &arg : msg.args) {
            args_signature += std::visit(ArgsSignantureVisitor{}, arg.type);
        }

        for (auto &arg : msg.args) {
            auto arg_rtti_type_op = std::visit(ArgsTypesVisitor{}, arg.type);
            if (arg_rtti_type_op.has_value()) {
                only_primitives = false;
            }
            Arg rtti_arg;
            rtti_arg.name = arg.name;
            rtti_arg.rtti_type = arg_rtti_type_op;
            rtti_args.push_back(std::move(rtti_arg));
        }
    }
    std::string name;
    bool only_primitives = true;
    std::vector<Arg> rtti_args;
    std::string args_signature;
};

struct Interface
{
    Interface(const wl_gena::types::Interface &iface)
    {
        name = iface.name;
        version = iface.version;
        for (auto &req : iface.requests) {
            requests.push_back(Message{req});
        }

        for (auto &ev : iface.events) {
            events.push_back(Message{ev});
        }
    }

    std::string name;
    uint32_t version;
    std::vector<Message> requests;
    std::vector<Message> events;
};

struct TypeArrayInfo
{
    struct Entry
    {
        std::optional<size_t> index;
        std::string type;

        std::string interface_name;
        std::string message_name;
        std::string arg_name;
    };

    TypeArrayInfo(
        const std::vector<Interface> &interfaces, const NamespaceInfo &ns_info)
    {
        auto get_max_null_run = [](const std::vector<Message> &msgs) {
            size_t o = 0;
            for (auto &ev : msgs) {
                if (ev.only_primitives) {
                    o = std::max(o, ev.rtti_args.size());
                }
            }
            return o;
        };

        null_run_length = 0;
        for (auto &iface : interfaces) {
            null_run_length =
                std::max(null_run_length, get_max_null_run(iface.events));
            null_run_length =
                std::max(null_run_length, get_max_null_run(iface.requests));
        }

        array = [&interfaces, &ns_info]() {
            auto generate_entries =
                [&ns_info](
                    const std::vector<Message> &msgs,
                    const std::string iface_name) -> std::vector<Entry> {
                std::vector<Entry> o;

                for (auto &msg : msgs) {

                    if (msg.only_primitives) {
                        continue;
                    }
                    for (auto &arg : msg.rtti_args) {

                        std::string type = "nullptr";
                        if (arg.rtti_type) {
                            const std::string &interface_name =
                                arg.rtti_type.value();

                            type = std::format(
                                "&{}::rtti<traits>::{}_interface",
                                ns_info.get_namespace(interface_name),
                                interface_name);
                        }

                        Entry entry{};
                        entry.type = std::move(type);

                        entry.interface_name = iface_name;
                        entry.message_name = msg.name;
                        entry.arg_name = arg.name;

                        o.push_back(std::move(entry));
                    }
                }

                return o;
            };

            std::vector<Entry> o;
            for (auto &iface : interfaces) {
                auto request_entries =
                    generate_entries(iface.requests, iface.name);
                auto event_entries = generate_entries(iface.events, iface.name);

                for (auto &req : request_entries) {
                    o.push_back(std::move(req));
                }

                for (auto &ev : event_entries) {
                    o.push_back(std::move(ev));
                }
            }

            for (size_t e_i = 0; e_i != o.size(); ++e_i) {
                Entry &e = o[e_i];
                if (e.index) {
                    throw std::runtime_error{
                        "Type array should not have indexes here"};
                }
                e.index = e_i;
            }

            return o;
        }();
    }

    size_t find_index(
        const std::string interface_name, const std::string &message_name) const
    {
        namespace V = std::ranges::views;
        auto interface_filtered = V::filter([&interface_name](const Entry &e) {
            return e.interface_name == interface_name;
        });

        auto message_filtered = V::filter([&message_name](const Entry &e) {
            return e.message_name == message_name;
        });

        auto entry_range =
            array | interface_filtered | message_filtered | V::take(1);

        std::optional<Entry> first_entry_op;
        for (auto &entry : entry_range) {
            first_entry_op = entry;
            break;
        }

        if (!first_entry_op) {
            std::string message = std::format(
                "Cannot find index for [{}.{}] message",
                interface_name,
                message_name);
            throw std::runtime_error{std::move(message)};
        }
        auto &first_entry = first_entry_op.value();

        if (!first_entry.index) {
            std::string message = std::format(
                "Message [{}.{}] does not have an index",
                interface_name,
                message_name);
            throw std::runtime_error{std::move(message)};
        }

        return first_entry.index.value();
    }

    size_t null_run_length;
    std::vector<Entry> array;
};

struct Generator
{
    Generator(const types::Protocol &proto, const NamespaceInfo &deps)
        : _deps{deps}, _interfaces{make_interfaces(proto)},
          _type_array_info{_interfaces, _deps}
    {
    }

    static std::vector<Interface> make_interfaces(const types::Protocol &proto)
    {
        std::vector<Interface> interfaces;
        for (auto &iface : proto.interfaces) {
            interfaces.emplace_back(iface);
        }
        return interfaces;
    };

    StringList emit_rtti_struct() const;
    StringList emit_rtti_interface_struct_members_forward(
        size_t interface_index) const;
    StringList emit_rtti() const;
    StringList emit_rtti_interface_struct_types_member() const;
    StringList emit_rtti_interface_struct_members(size_t interface_index) const;

  private:
    const NamespaceInfo &_deps;
    std::vector<Interface> _interfaces;
    TypeArrayInfo _type_array_info;
};

StringList Generator::emit_rtti_interface_struct_members_forward(
    size_t iface_index) const
{
    StringList o;
    o += std::format("// {}", func());

    const rtti::Interface &interface = _interfaces.at(iface_index);

    o += std::format(
        "static const typename traits::wl_interface_t {}_interface;",
        interface.name);

    if (!interface.requests.empty()) {
        o += std::format(
            "static const typename traits::wl_message_t {}_requests[];",
            interface.name);
    }

    if (!interface.events.empty()) {
        o += std::format(
            "static const typename traits::wl_message_t {}_events[];",
            interface.name);
    }

    return o;
}

StringList Generator::emit_rtti_struct() const
{
    StringList o;
    o += std::format("// {}", func());

    o += "template <typename traits>";
    o += "struct rtti";
    o += "{";
    o += "    static const typename traits::wl_interface_t *types[];";
    o += "";
    bool first = true;
    for (size_t iface_i = 0; iface_i != _interfaces.size(); ++iface_i) {
        auto iface_members =
            emit_rtti_interface_struct_members_forward(iface_i);
        if (!first) {
            o += "";
        }
        first = false;
        o += indent(iface_members);
    }

    o += "};";

    return o;
}

StringList Generator::emit_rtti_interface_struct_types_member() const
{
    StringList o;
    o += std::format("// {}", func());

    std::string sig;
    sig += "const typename traits::wl_interface_t *";
    sig += "rtti<traits>::types[]";

    o += "template <typename traits>";
    o += std::format("{} {{", sig);

    struct RTTITypeEntry
    {
        std::string type;
        size_t index;
        std::string debug;
    };

    std::vector<RTTITypeEntry> types_array_entries;

    {
        size_t null_run_length = _type_array_info.null_run_length;

        size_t types_array_offset = 0;
        RTTITypeEntry entry;
        entry.type = "nullptr";
        entry.debug = "[null_run_stub]";
        for (size_t nul_i = 0; nul_i != null_run_length; ++nul_i) {
            entry.index = types_array_offset++;
            types_array_entries.push_back(entry);
        }
    }

    {
        for (const TypeArrayInfo::Entry &type_entry : _type_array_info.array) {
            RTTITypeEntry entry;
            entry.type = type_entry.type;
            entry.index =
                type_entry.index.value() + _type_array_info.null_run_length;
            entry.debug = std::format(
                "[{}.{}.{}]",
                type_entry.interface_name,
                type_entry.message_name,
                type_entry.arg_name);
            types_array_entries.push_back(std::move(entry));
        }
    }

    {
        std::optional<size_t> max_index_size;
        for (auto &e : types_array_entries) {
            size_t index_size = std::formatted_size("{}", e.index);
            max_index_size =
                std::max(max_index_size.value_or(index_size), index_size);
        }

        for (auto &e : types_array_entries) {
            e.debug = std::format(
                "[{:{}}]{}", e.index, max_index_size.value(), e.debug);
        }
    };

    auto types_array_entries_reversed =
        std::views::reverse(types_array_entries);
    bool first = true;
    for (auto &e : types_array_entries_reversed) {
        if (!first) {
            e.type = std::format("{},", e.type);
        }
        first = false;
    }

    std::optional<size_t> max_type_size = [&types_array_entries]() {
        std::optional<size_t> o_size;
        for (auto &entry : types_array_entries) {
            size_t e_size = entry.type.size();
            o_size = std::max(o_size.value_or(e_size), e_size);
        }
        return o_size;
    }();

    StringList types_array_entry_strings;
    for (auto &entry : types_array_entries) {
        types_array_entry_strings += std::format(
            "{:<{}} /* {} */", entry.type, max_type_size.value(), entry.debug);
    }
    o += indent(types_array_entry_strings);

    o += "};";
    return o;
}

StringList
    Generator::emit_rtti_interface_struct_members(size_t iface_index) const
{
    StringList o;
    o += std::format("// {}", func());

    bool has_entity = false;
    auto add_sep = [&has_entity, &o]() {
        if (has_entity) {
            o += "";
        }
        has_entity = true;
    };

    const rtti::Interface &interface = _interfaces.at(iface_index);

    auto emit_rtti_message_elements =
        [&](const std::vector<rtti::Message> &msgs) -> StringList {
        StringList ro;
        if (msgs.empty()) {
            return ro;
        }

        for (auto &msg : msgs) {
            std::string rtti_ref_offset_str = "/* [null_run_stub] */ 0";
            if (!msg.only_primitives) {
                size_t offset =
                    _type_array_info.find_index(interface.name, msg.name);
                offset += _type_array_info.null_run_length;
                std::string info_comment =
                    std::format("[{}.{}]", interface.name, msg.name);
                rtti_ref_offset_str =
                    std::format("/* {} */ {}", info_comment, offset);
            }
            std::string rtti_ref_str =
                std::format("rtti<traits>::types + {}", rtti_ref_offset_str);
            ro += std::format(
                "{{\"{}\", \"{}\", {}}}",
                msg.name,
                msg.args_signature,
                rtti_ref_str);
        }

        auto rev = std::views::reverse(ro.get());
        bool first = true;
        for (std::string &elem : rev) {
            if (!first) {
                elem = elem + ",";
            }
            first = false;
        }

        return ro;
    };

    add_sep();
    o += "template <typename traits>";
    o += std::format(
        "const typename traits::wl_interface_t rtti<traits>::{}_interface {{",
        interface.name);
    o += [&interface]() {
        StringList o_memb;
        o_memb += std::format("\"{}\", {},", interface.name, interface.version);
        if (!interface.requests.empty()) {
            o_memb += std::format(
                "{}, rtti<traits>::{}_requests,",
                interface.requests.size(),
                interface.name);
        } else {
            o_memb += "0, nullptr,";
        }

        if (!interface.events.empty()) {
            o_memb += std::format(
                "{}, rtti<traits>::{}_events",
                interface.events.size(),
                interface.name);
        } else {
            o_memb += "0, nullptr";
        }

        return indent(o_memb);
    }();
    o += "};";

    if (!interface.requests.empty()) {
        add_sep();
        o += "template <typename traits>";
        o += std::format(
            "const typename traits::wl_message_t rtti<traits>::{}_requests[] = "
            "{{",
            interface.name);

        StringList requests_list =
            emit_rtti_message_elements(interface.requests);
        o += indent(requests_list);
        o += "};";
    }

    if (!interface.events.empty()) {
        add_sep();
        o += "template <typename traits>";
        o += std::format(
            "const typename traits::wl_message_t rtti<traits>::{}_events[] = "
            "{{",
            interface.name);

        StringList requests_list = emit_rtti_message_elements(interface.events);
        o += indent(requests_list);
        o += "};";
    }

    return o;
}

StringList Generator::emit_rtti() const
{
    StringList o;
    o += std::format("// {}", func());

    o += emit_rtti_interface_struct_types_member();

    o += "";
    bool first = true;
    for (size_t iface_i = 0; iface_i != _interfaces.size(); ++iface_i) {
        auto iface_members = emit_rtti_interface_struct_members(iface_i);
        if (!first) {
            o += "";
        }
        first = false;
        o += std::move(iface_members);
    }

    return o;
}
} // namespace rtti

StringList wl_gena::HeaderGenerator::generate() const
{
    StringList o;
    o += "#pragma once";
    o += "";
    for (const std::string &include_file : _includes) {
        o += std::format("#include {}", include_file);
    }

    if (_ns_info.top_namespace().has_value()) {
        o += std::format("namespace {} {{", _ns_info.top_namespace().value());
    }

    o += std::format("namespace {} {{", _protocol.name);

    o += "";
    o += emit_object_forward();

    rtti::Generator rtti_gena{_protocol, _ns_info};
    o += "";
    o += rtti_gena.emit_rtti_struct();

    o += "";

    bool first = true;
    for (auto &iface : _protocol.interfaces) {
        if (!first) {
            o += "";
        }
        first = false;

        InterfaceGenerator iface_gena{iface, _ns_info};
        o += iface_gena.generate();
    }

    o += "";
    o += rtti_gena.emit_rtti();

    o += std::format("}} // namespace {}", _protocol.name);

    if (_ns_info.top_namespace().has_value()) {
        o +=
            std::format("}} // namespace {}", _ns_info.top_namespace().value());
    }

    auto make_empty_if_blank = [](std::string &str) {
        auto non_space =
            std::ranges::find_if(str, [](char c) { return c != ' '; });
        if (non_space != std::end(str)) {
            return;
        }
        str.clear();
    };

    std::string output;
    for (auto &o_line : o.get()) {
        make_empty_if_blank(o_line);
    }

    return o;
};

GenerateHeaderOutput generate_header(const GenerateHeaderInput &I)
{
    NamespaceInfo ns_info{I.protocol, I.context_protocols, I.top_namespace_id};

    HeaderGenerator gena{I.protocol, ns_info};
    gena.includes() = I.includes;

    auto lines = gena.generate();

    std::string output;
    for (auto &o_line : lines.get()) {
        output += o_line;
        output += "\n";
    }

    return GenerateHeaderOutput{std::move(output)};
}
} // namespace wl_gena
