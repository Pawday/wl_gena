#pragma once

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
