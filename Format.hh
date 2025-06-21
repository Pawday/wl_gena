#pragma once

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Types.hh"

template <typename T>
struct FormatVectorWrap
{
    const std::vector<T> &val;
};

struct FormatterNoParseArgs
{
    template <class ParseContext>
    constexpr ParseContext::iterator parse(ParseContext &ctx)
    {
        if (*ctx.begin() != '}') {
            throw std::format_error("Unexpected arguments");
        }

        return ctx.begin();
    }
};

template <typename T>
struct std::formatter<FormatVectorWrap<T>> : FormatterNoParseArgs
{
    template <class FmtContext>
    FmtContext::iterator format(FormatVectorWrap<T> s, FmtContext &ctx) const
    {
        bool first = true;
        std::format_to(ctx.out(), "[");
        for (auto &el : s.val) {
            if (!first) {
                std::format_to(ctx.out(), ",");
            }
            first = false;

            std::format_to(ctx.out(), "{}", el);
        }
        std::format_to(ctx.out(), "]");
        return ctx.out();
    }
};

template <>
struct std::formatter<Wayland::ScannerTypes::ArgType> : FormatterNoParseArgs
{
    template <typename FmtContext>
    struct WaylandArgTypeNameVisitor
    {
        explicit WaylandArgTypeNameVisitor(FmtContext &ctx) : _ctx{ctx}
        {
        }
#define ADD_OVERLOAD(TYPENAME, type_print_name)                                \
    auto operator()(const TYPENAME &)->FmtContext::iterator                    \
    {                                                                          \
        std::format_to(_ctx.out(), "{{\"name\":\"{}\"}}", type_print_name);    \
        return _ctx.out();                                                     \
    }

        ADD_OVERLOAD(Wayland::ScannerTypes::ArgTypes::Int, "int")
        ADD_OVERLOAD(Wayland::ScannerTypes::ArgTypes::UInt, "uint")

        auto operator()(const Wayland::ScannerTypes::ArgTypes::UIntEnum &v)
            -> FmtContext::iterator
        {
            std::format_to(_ctx.out(), "{{");
            std::format_to(_ctx.out(), "\"name\":\"enum\"");
            if (v.interface_name) {
                std::format_to(_ctx.out(), ",");
                std::format_to(
                    _ctx.out(),
                    "\"interface\":\"{}\"",
                    v.interface_name.value());
            }
            std::format_to(_ctx.out(), ",");
            std::format_to(_ctx.out(), "\"enum_name\":\"{}\"", v.name);
            std::format_to(_ctx.out(), "}}");
            return _ctx.out();
        }

        ADD_OVERLOAD(Wayland::ScannerTypes::ArgTypes::Fixed, "fixed")
        ADD_OVERLOAD(Wayland::ScannerTypes::ArgTypes::String, "string")
        ADD_OVERLOAD(Wayland::ScannerTypes::ArgTypes::NullString, "?str")

        auto format_with_interface(
            const std::string_view name,
            const std::optional<std::string> &interface_name)
            -> FmtContext ::iterator

        {
            std::format_to(_ctx.out(), "{{");
            std ::format_to(_ctx.out(), "\"name\":\"{}\"", name);
            if (interface_name.has_value()) {
                std::format_to(_ctx.out(), ",");
                std::format_to(
                    _ctx.out(), "\"interface\":\"{}\"", interface_name.value());
            }
            std::format_to(_ctx.out(), "}}");
            return _ctx.out();
        }

        auto operator()(const Wayland::ScannerTypes::ArgTypes::Object &o)
            -> FmtContext ::iterator
        {
            return format_with_interface("obj", o.interface_name);
        }
        auto operator()(const Wayland::ScannerTypes::ArgTypes::NullObject &o)
            -> FmtContext ::iterator
        {
            return format_with_interface("?obj", o.interface_name);
        }
        auto operator()(const Wayland::ScannerTypes::ArgTypes::NewID &i)
            -> FmtContext ::iterator
        {
            return format_with_interface("id", i.interface_name);
        }
        ADD_OVERLOAD(Wayland::ScannerTypes::ArgTypes::Array, "arr")
        ADD_OVERLOAD(Wayland::ScannerTypes::ArgTypes::FD, "fd")
#undef ADD_OVERLOAD

      private:
        FmtContext &_ctx;
    };

    template <class FmtContext>
    FmtContext::iterator format(
        const Wayland::ScannerTypes::ArgType &type, FmtContext &ctx) const
    {
        WaylandArgTypeNameVisitor vis{ctx};
        return std::visit(vis, type);
    }
};

template <>
struct std::formatter<Wayland::ScannerTypes::Arg> : FormatterNoParseArgs
{
    template <class FmtContext>
    FmtContext::iterator
        format(const Wayland::ScannerTypes::Arg &s, FmtContext &ctx) const
    {
        std::format_to(ctx.out(), "{{");
        std::format_to(ctx.out(), "\"name\":\"{}\"", s.name);
        std::format_to(ctx.out(), ",");
        std::format_to(ctx.out(), "\"type\":{}", s.type);
        std::format_to(ctx.out(), "}}");
        return ctx.out();
    }
};

template <>
struct std::formatter<Wayland::ScannerTypes::Enum::Entry> : FormatterNoParseArgs
{
    template <class FmtContext>
    FmtContext::iterator format(
        const Wayland::ScannerTypes::Enum::Entry &entry, FmtContext &ctx) const
    {
        std::format_to(ctx.out(), "{{");
        std::format_to(ctx.out(), "\"name\":\"{}\"", entry.name);
        std::format_to(ctx.out(), ",");
        std::format_to(ctx.out(), "\"value\":{}", entry.value);
        if (entry.is_hex) {
            std::format_to(ctx.out(), ",");
            std::format_to(ctx.out(), "\"value_hex\":\"{:x}\"", entry.value);
        }
        std::format_to(ctx.out(), "}}");
        return ctx.out();
    }
};

template <>
struct std::formatter<Wayland::ScannerTypes::Enum> : FormatterNoParseArgs
{
    template <class FmtContext>
    FmtContext::iterator
        format(const Wayland::ScannerTypes::Enum &s, FmtContext &ctx) const
    {
        FormatVectorWrap<Wayland::ScannerTypes::Enum::Entry> fmt_entries(
            s.entries);
        std::format_to(
            ctx.out(),
            "{{\"name\":\"{}\",\"entries\":{}}}",
            s.name,
            fmt_entries);
        return ctx.out();
    }
};

template <>
struct std::formatter<Wayland::ScannerTypes::Message> : FormatterNoParseArgs
{
    template <typename FmtContext>
    struct MessageTypenameVisitor
    {
        FmtContext &_ctx;

        void operator()(const Wayland::ScannerTypes::Message::TypeDestructor &)
        {
            std::format_to(_ctx.out(), "\"type\":\"DESTRUCTOR\"");
        }
    };

    template <class FmtContext>
    FmtContext::iterator
        format(const Wayland::ScannerTypes::Message &s, FmtContext &ctx) const
    {
        std::format_to(ctx.out(), "{{");
        std::format_to(ctx.out(), "\"name\":\"{}\"", s.name);
        if (s.type) {
            std::format_to(ctx.out(), ",");
            std::visit(MessageTypenameVisitor{ctx}, s.type.value());
        }
        FormatVectorWrap<Wayland::ScannerTypes::Arg> arg_fmt{s.args};
        std::format_to(ctx.out(), ",");
        std::format_to(ctx.out(), "\"args\":{}", arg_fmt);
        std::format_to(ctx.out(), "}}");
        return ctx.out();
    }
};

template <>
struct std::formatter<Wayland::ScannerTypes::Request> : FormatterNoParseArgs
{
    template <class FmtContext>
    FmtContext::iterator
        format(const Wayland::ScannerTypes::Request &s, FmtContext &ctx) const
    {
        std::format_to(
            ctx.out(),
            "{}",
            static_cast<const Wayland::ScannerTypes::Message &>(s));
        return ctx.out();
    }
};

template <>
struct std::formatter<Wayland::ScannerTypes::Event> : FormatterNoParseArgs
{
    template <class FmtContext>
    FmtContext::iterator
        format(const Wayland::ScannerTypes::Event &s, FmtContext &ctx) const
    {
        std::format_to(
            ctx.out(),
            "{}",
            static_cast<const Wayland::ScannerTypes::Message &>(s));
        return ctx.out();
    }
};

template <>
struct std::formatter<Wayland::ScannerTypes::Interface> : FormatterNoParseArgs
{
    template <class FmtContext>
    FmtContext::iterator
        format(const Wayland::ScannerTypes::Interface &i, FmtContext &ctx) const
    {
        std::format_to(ctx.out(), "{{");
        std::format_to(ctx.out(), "\"name\":\"{}\"", i.name);
        std::format_to(ctx.out(), ",");
        std::format_to(ctx.out(), "\"version\":{}", i.verison);
        FormatVectorWrap<Wayland::ScannerTypes::Request> request_fmt{
            i.requests};
        std::format_to(ctx.out(), ",");
        std::format_to(ctx.out(), "\"requests\":{}", request_fmt);
        FormatVectorWrap<Wayland::ScannerTypes::Event> event_fmt{i.events};
        std::format_to(ctx.out(), ",");
        std::format_to(ctx.out(), "\"events\":{}", event_fmt);
        FormatVectorWrap<Wayland::ScannerTypes::Enum> enums_fmt{i.enums};
        std::format_to(ctx.out(), ",");
        std::format_to(ctx.out(), "\"enums\":{}", enums_fmt);
        std::format_to(ctx.out(), "}}");
        return ctx.out();
    }
};

template <>
struct std::formatter<Wayland::ScannerTypes::Protocol> : FormatterNoParseArgs
{
    template <class FmtContext>
    FmtContext::iterator
        format(const Wayland::ScannerTypes::Protocol &p, FmtContext &ctx) const
    {
        std::format_to(ctx.out(), "{{");
        std::format_to(ctx.out(), "\"name\":\"{}\"", p.name);
        FormatVectorWrap interfaces_fmt{p.interfaces};
        std::format_to(ctx.out(), ",");
        std::format_to(ctx.out(), "\"interfaces\":{}", interfaces_fmt);
        std::format_to(ctx.out(), "}}");
        return ctx.out();
    }
};
