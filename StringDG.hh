#pragma once

#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <cctype>
#include <cstddef>
#include <cstdlib>

#include "DG.hh"
#include "StringList.hh"

struct StringDG
{
    void add_node(const std::string &node_name)
    {
        size_t id = _g.add_node();
        _id_string_map.insert({id, node_name});
        _string_id_lut[node_name].emplace_back(id);
    }

    void add_dependency(
        const std::string &providers_name, const std::string &dependencies_name)
    {
        auto providers_it = _string_id_lut.find(providers_name);
        if (providers_it == std::end(_string_id_lut)) {
            throw std::logic_error{
                std::format("No node named [{}]", providers_name)};
        }

        auto deps_it = _string_id_lut.find(dependencies_name);
        if (deps_it == std::end(_string_id_lut)) {
            throw std::logic_error{
                std::format("No node named [{}]", dependencies_name)};
        }

        auto &providers = providers_it->second;
        auto &deps = deps_it->second;

        for (auto &provider_id : providers) {
            for (auto &dep_id : deps) {
                if (_g.has_edge(dep_id, provider_id)) {
                    continue;
                }
                _g.add_edge(dep_id, provider_id);
            }
        }
    }

    std::vector<std::string> topo_sorted() const
    {
        auto sorted_idxs = _g.topo_sorted();
        std::vector<std::string> o;
        for (auto idx : sorted_idxs) {
            o.emplace_back(_id_string_map.at(idx));
        }
        return o;
    }

    StringDG topo_sorted_dg() const
    {
        auto arr = _g.topo_sorted();

        StringDG o;
        std::optional<std::string> prev;
        for (auto el : arr) {
            auto node_str = _id_string_map.at(el);
            o.add_node(node_str);
            if (prev.has_value()) {
                o.add_dependency(prev.value(), node_str);
            }
            prev = std::move(node_str);
        }

        return o;
    }

    std::string dump() const
    {
        StringList o;
        o += "digraph {";
        for (auto &node : _g.nodes()) {
            std::string name = _id_string_map.at(node);
            o += std::format("\"{}({})\"", name, node);
        }

        for (auto edge : _g.edges()) {
            std::string src_name = std::format(
                "\"{}({})\"", _id_string_map.at(edge.first), edge.first);

            std::string dst_name = std::format(
                "\"{}({})\"", _id_string_map.at(edge.second), edge.second);

            o += std::format("{} -> {}", src_name, dst_name);
        }

        o += "}";

        std::string o_str;

        for (auto &line : o.get()) {
            o_str += std::move(line);
            o_str += '\n';
        }
        return o_str;
    }

  private:
    DG _g;
    std::unordered_map<size_t, std::string> _id_string_map;
    std::unordered_map<std::string, std::vector<size_t>> _string_id_lut;
};
