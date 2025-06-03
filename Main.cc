#include <format>
#include <iostream>
#include <string>
#include <vector>

#include <cstdlib>

#include "wl_gena/GenaMain.hh"

int main(int argc, char **argv)
{
    if (argc < 1) {
        std::cerr << std::format("WTF, argc({}) < 1", argc) << '\n';
        return EXIT_FAILURE;
    }
    size_t uargc = argc;

    std::vector<std::string> argv_vec;
    for (size_t arg_idx = 1; arg_idx != uargc; ++arg_idx) {
        argv_vec.emplace_back(argv[arg_idx]);
    }

    return wl_gena_main(argv_vec);
}
