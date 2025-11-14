#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <tuple>

namespace match::core {

struct Cell {
    std::int32_t col{};
    std::int32_t row{};

    constexpr bool operator==(const Cell& other) const noexcept {
        return col == other.col && row == other.row;
    }

    constexpr bool operator!=(const Cell& other) const noexcept {
        return !(*this == other);
    }

    constexpr bool operator<(const Cell& other) const noexcept {
        return col < other.col || (col == other.col && row < other.row);
    }
};

struct Move {
    Cell a{};
    Cell b{};
};

struct CellHash {
    std::size_t operator()(const Cell& cell) const noexcept {
        auto key = static_cast<std::uint64_t>(
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.col)) << 32) |
            static_cast<std::uint32_t>(cell.row));
        return std::hash<std::uint64_t>{}(key);
    }
};

}  // namespace match::core

