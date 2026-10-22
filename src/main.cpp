#include <CLI/CLI.hpp>
#include <exception>
#include <nuxe/macros.h>

import nuxe.cli.diagnostics;
import nuxe.cli.inspect;
import nuxe.cli.extract;

fn main(int argc, char **argv) -> int {
    const let diagnostics = nuxe::cli::diagnostics{};
    try {
        let app = CLI::App{"FromSoftware archive extractor", "nuxe"};
        app.require_subcommand(1);
        let exit_code = 0;
        let inspect = nuxe::cli::inspect_command{};
        inspect.attach(app, exit_code, diagnostics);
        let extract = nuxe::cli::extract_command{};
        extract.attach(app, exit_code, diagnostics);
        try {
            app.parse(argc, argv);
            return exit_code;
        } catch (const CLI::ParseError &error) {
            if (error.get_exit_code() == 0)
                return app.exit(error);
            diagnostics.report(
                error.what(),
                "run 'nuxe --help' or 'nuxe <command> --help' for usage");
            return error.get_exit_code();
        }
    } catch (const std::exception &error) {
        diagnostics.report("command failed", error.what());
        return 1;
    }
}
