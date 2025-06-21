#pragma once

#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

struct StringList
{
    void operator+=(std::string &&str)
    {
        val.emplace_back(std::move(str));
    }

    void operator+=(StringList &&o)
    {
        for (auto &o_line : o.val) {
            val.emplace_back(std::move(o_line));
        }
        o.val.clear();
    }

    void leftPad(const std::string &pad)
    {
        for (auto &str : val) {
            str = std::format("{}{}", pad, str);
        }
    }

    bool empty() const
    {
        return val.empty();
    }

    std::span<std::string> get()
    {
        return val;
    }

  private:
    std::vector<std::string> val;
};
