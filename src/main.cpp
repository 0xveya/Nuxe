#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <nuxe/macros.h>
#include <print>

import nuxe;
import nlohmann.json;

fn main() -> int {
    const let bytes = std::array{std::byte{0x01}, std::byte{0x02},
                                 std::byte{0x03}, std::byte{0x04}};
    let input = nuxe::reader{bytes, std::endian::little};

    const let value = input.read<std::uint32_t>();
    if (!value) {
        std::println("read failed at offset {}", input.position());
        return 1;
    }

    std::println("value: {:#010x}", *value);
    std::println("position: {}", input.position());
    const let output = nlohmann::json{
        {"value", *value},
        {"position", input.position()},
    };
    std::println("{}", output.dump(2));
    return 0;
}
