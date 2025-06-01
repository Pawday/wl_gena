#include <format>
#include <iostream>
#include <string>
#include <vector>

#include <cstdlib>

#include "Gena.hh"

int main(int argc, char **argv)
{
    if (argc < 1) {
        std::cerr << std::format("WTF, argc({}) < 1", argc) << '\n';
        return EXIT_FAILURE;
    }

    std::vector<std::string> argv_vec;
    for (size_t arg_idx = 1; arg_idx != argc; ++arg_idx) {
        argv_vec.emplace_back(argv[arg_idx]);
    }

    return wl_gena_main(argv_vec);
}
