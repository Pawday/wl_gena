#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Types.hh"

namespace wl_gena {

struct GenerateHeaderInput
{
    Wayland::ScannerTypes::Protocol protocol;
    std::optional<std::string> top_namespace_id;
    std::vector<std::string> includes;
};

struct GenerateHeaderOutput
{
    std::string output;
};

GenerateHeaderOutput generate_header(const GenerateHeaderInput &I);

} // namespace wl_gena
