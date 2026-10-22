module;

#include <CLI/CLI.hpp>
#include <charconv>
#include <cstddef>
#include <string>
#include <system_error>

#include <nuxe/macros.h>

export module nuxe.cli.options;

export namespace nuxe::cli {

fn size_option(bool allow_zero = false) -> CLI::Validator {
    return CLI::Validator{
        [allow_zero](const std::string &text) -> std::string {
            let value = std::size_t{};
            const let[end, error] =
                std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size() ||
                (!allow_zero && value == 0))
                return allow_zero ? "must be a nonnegative integer"
                                  : "must be a positive integer";
            return {};
        },
        allow_zero ? "NONNEGATIVE" : "POSITIVE"};
}

} // namespace nuxe::cli
