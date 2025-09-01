#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Types.hh"

namespace wl_gena {

struct GenerateHeaderInput
{
    wl_gena::types::Protocol protocol;
    std::optional<std::string> top_namespace_id;
    std::vector<std::string> includes;
    std::vector<wl_gena::types::Protocol> context_protocols;
};

struct GenerateHeaderOutput
{
    std::string output;
};

GenerateHeaderOutput generate_header(const GenerateHeaderInput &I);

} // namespace wl_gena
