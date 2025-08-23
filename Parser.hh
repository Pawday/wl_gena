#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "Types.hh"

namespace wl_gena {

auto parse_protocol(std::string_view protocol_xml)
    -> std::expected<types::Protocol, std::string>;

}
