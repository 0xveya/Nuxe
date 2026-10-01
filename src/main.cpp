#include <CLI/CLI.hpp>
#include <nuxe/macros.h>
#include <print>

import nuxe;

fn main(int argc, char **argv) -> int {
    let app = CLI::App{"FromSoftware archive extractor", "nuxe"};

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return app.exit(error);
    }

    std::println("gleep");
    return 67;
}
