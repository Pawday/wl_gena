#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "Types.hh"

namespace Wayland {

std::expected<ScannerTypes::Protocol, std::string>
    parse_protocol(std::string_view protocol_xml);

}
