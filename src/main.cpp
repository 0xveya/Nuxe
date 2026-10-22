#include <CLI/CLI.hpp>
#include <cstdio>
#include <exception>
#include <nuxe/macros.h>
#include <print>

import nuxe.cli.inspect;

fn main(int argc, char **argv) -> int {
    let app = CLI::App{"FromSoftware archive extractor", "nuxe"};
    app.require_subcommand(1);

    let exit_code = 0;
    let inspect = nuxe::cli::inspect_command{};
    inspect.attach(app, exit_code);

    try {
        app.parse(argc, argv);
        return exit_code;
    } catch (const CLI::ParseError &error) {
        return app.exit(error);
    } catch (const std::exception &error) {
        std::println(stderr, "command failed: {}", error.what());
        return 1;
    }
}
