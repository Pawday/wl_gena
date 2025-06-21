#pragma once

#include <algorithm>
#include <format>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <cstddef>

struct DG
{
    using Edge = std::pair<size_t, size_t>;

    size_t add_node()
    {
        size_t node_id = _last_ID;
        if (_nodes.contains(node_id)) {
            throw std::logic_error{"Node exist"};
        }

        auto status = _nodes.insert(node_id);
        if (!status.second) {
            throw std::logic_error{"Cannot insert new node"};
        }

        _last_ID++;
        return node_id;
    }

    bool has_node(size_t id)
    {
        return _nodes.contains(id);
    }

    void delete_node(size_t id)
    {
        if (!has_node(id)) {
            throw std::logic_error{
                std::format("Cannot delete not existng node #{}", id)};
        }
        _nodes.erase(id);

        auto edge_has_node = [id](const Edge &e) {
            if (e.first == id) {
                return true;
            }

            if (e.second == id) {
                return true;
            }

            return false;
        };

        auto removed = std::ranges::remove_if(_edges, edge_has_node);
        _edges.erase(removed.begin(), removed.end());
    }

    void add_edge(size_t src, size_t dst)
    {
        inval_args_if_no_edge_nodes(src, dst);
        if (has_edge(src, dst)) {
            throw std::logic_error{
                std::format("Edge #{} -> #{} already exist", src, dst)};
        }
        Edge new_edge{src, dst};
        _edges.push_back(new_edge);
    }

    bool has_edge(size_t src, size_t dst)
    {
        inval_args_if_no_edge_nodes(src, dst);
        logic_err_if_mult_edge(src, dst);
        return n_edges(src, dst) != 0;
    }

    void invert_edges()
    {
        for (auto &edge : _edges) {
            auto f = edge.first;
            auto s = edge.second;
            edge.first = s;
            edge.second = f;
        }
    }

    std::unordered_set<size_t> get_cycled() const
    {
        DG tmp{*this};

        while (true) {
            auto roots = tmp.roots();
            if (roots.empty()) {
                break;
            }
            for (auto &root : roots) {
                tmp.delete_node(root);
            }
        }

        tmp.invert_edges();

        while (true) {
            auto roots = tmp.roots();
            if (roots.empty()) {
                break;
            }
            for (auto &root : roots) {
                tmp.delete_node(root);
            }
        }

        return tmp.nodes();
    }

    std::vector<std::unordered_set<size_t>> topo_sorted_grouped() const
    {
        DG src{*this};

        std::vector<std::unordered_set<size_t>> o;

        std::optional<size_t> prev_root;

        while (!src.nodes().empty()) {
            std::unordered_set<size_t> group;
            auto roots = src.roots();
            if (roots.empty()) {
                throw_cycled(src);
            }

            for (auto &root : roots) {
                src.delete_node(root);
            }

            for (auto &root : roots) {
                group.insert(root);
                prev_root = root;
            }

            o.emplace_back(std::move(group));
        }

        return o;
    }

    std::vector<size_t> topo_sorted() const
    {
        DG src{*this};

        std::vector<size_t> o;

        std::optional<size_t> prev_root;

        while (!src.nodes().empty()) {
            auto roots = src.roots();
            if (roots.empty()) {
                throw_cycled(src);
            }

            for (auto &root : roots) {
                src.delete_node(root);
            }

            for (auto &root : roots) {
                o.push_back(root);
                prev_root = root;
            }
        }

        return o;
    }

    std::unordered_set<size_t> roots() const
    {
        struct DNode
        {
            size_t id;
            mutable bool is_dst;
            mutable bool is_src;

            struct Hash
            {
                std::size_t operator()(const DNode &i) const noexcept
                {
                    return i.id;
                }
            };

            struct Eq
            {
                bool operator()(const DNode &l, const DNode &r) const
                {
                    return l.id == r.id;
                }
            };
        };

        std::unordered_set<DNode, DNode::Hash, DNode::Eq> meta_nodes;
        for (auto &node : _nodes) {
            DNode new_meta{};
            new_meta.id = node;
            meta_nodes.insert(new_meta);
        }

        for (auto &edge : _edges) {
            DNode esrc_meta;
            esrc_meta.id = edge.first;
            DNode edst_meta;
            edst_meta.id = edge.second;

            auto src_it = meta_nodes.find(esrc_meta);
            auto dst_it = meta_nodes.find(edst_meta);

            if (src_it != std::end(meta_nodes)) {
                src_it->is_src = true;
            }

            if (dst_it != std::end(meta_nodes)) {
                dst_it->is_dst = true;
            }
        }

        std::unordered_set<size_t> o;
        for (auto &meta_node : meta_nodes) {

            if (!meta_node.is_src && !meta_node.is_dst) {
                o.insert(meta_node.id);
                continue;
            }

            if (meta_node.is_src && !meta_node.is_dst) {
                o.insert(meta_node.id);
            }
        }
        return o;
    }

    auto nodes() const -> const std::unordered_set<size_t> &
    {
        return _nodes;
    }

    auto edges() const -> const std::vector<Edge> &
    {
        return _edges;
    }

  private:
    static void throw_cycled(DG src)
    {
        auto cycled_nodes = src.get_cycled();
        std::string message;
        message += "Nodes [";
        bool f = true;
        for (auto cycled : cycled_nodes) {
            if (!f) {
                message += ", ";
            }
            f = false;
            message += std::format("#{}", cycled);
        }
        message += "] involved in a cycle(s)";
        throw std::logic_error{std::move(message)};
    };

    size_t n_edges(size_t src, size_t dst)
    {
        inval_args_if_no_edge_nodes(src, dst);

        auto is_src_edge = [src](Edge &e) { return e.first == src; };
        auto is_dst_edge = [dst](Edge &e) { return e.second == dst; };

        namespace V = std::views;
        auto filter_edges = V::filter(is_src_edge) | V::filter(is_dst_edge);

        size_t nb_edges = 0;
        for (auto &_ : _edges | filter_edges) {
            nb_edges++;
        }

        return nb_edges;
    }

    void logic_err_if_mult_edge(size_t src, size_t dst)
    {
        size_t nb = n_edges(src, dst);
        if (nb > 1) {
            throw std::logic_error{
                std::format("Found {} edges (#{} -> #{})", nb, src, dst)};
        }
    }

    void inval_args_if_no_edge_nodes(size_t src, size_t dst)
    {
        if (!has_node(src)) {
            throw std::invalid_argument{std::format("No src node #{}", src)};
        }

        if (!has_node(dst)) {
            throw std::invalid_argument{std::format("No dst node #{}", dst)};
        }
    }

    size_t _last_ID{};

    std::unordered_set<size_t> _nodes;
    std::vector<Edge> _edges;
};
