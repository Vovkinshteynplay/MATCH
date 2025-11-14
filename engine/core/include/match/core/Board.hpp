#pragma once

#include <random>
#include <vector>

#include "match/core/Types.hpp"

namespace match::core {

inline constexpr int kEmptyCell = -1;

class Board {
public:
    struct Rules {
        int cols = 20;
        int rows = 20;
        int tile_types = 6;
        bool bombs_enabled = false;
        bool color_chain_enabled = false;
    };

    Board() = default;
    Board(int cols, int rows, int tile_types, bool bombs_enabled, bool color_chain_enabled,
          std::uint32_t seed = std::random_device{}());
    explicit Board(const Rules& rules, std::uint32_t seed = std::random_device{}());

    int cols() const noexcept { return cols_; }
    int rows() const noexcept { return rows_; }
    int tileTypes() const noexcept { return tile_types_; }
    bool bombsEnabled() const noexcept { return bombs_enabled_; }
    bool colorChainEnabled() const noexcept { return color_chain_enabled_; }

    void setBombsEnabled(bool value) noexcept {
        bombs_enabled_ = value;
        rules_.bombs_enabled = value;
    }

    void setColorChainEnabled(bool value) noexcept {
        color_chain_enabled_ = value;
        rules_.color_chain_enabled = value;
    }

    bool inBounds(int col, int row) const noexcept;
    bool inBounds(const Cell& cell) const noexcept { return inBounds(cell.col, cell.row); }

    int get(int col, int row) const noexcept;
    int get(const Cell& cell) const noexcept { return get(cell.col, cell.row); }

    void set(int col, int row, int value) noexcept;
    void set(const Cell& cell, int value) noexcept { set(cell.col, cell.row, value); }

    void swapCells(const Cell& a, const Cell& b) noexcept;
    void swapCells(const Move& move) noexcept { swapCells(move.a, move.b); }

    int randomTile();

    std::mt19937& rng() noexcept { return rng_; }
    const std::mt19937& rng() const noexcept { return rng_; }

    const Rules& rules() const noexcept { return rules_; }
    Rules& rules() noexcept { return rules_; }

    void fillAll(int value);

private:
    int index(int col, int row) const noexcept;

    Rules rules_{};
    int cols_{0};
    int rows_{0};
    int tile_types_{0};
    bool bombs_enabled_{false};
    bool color_chain_enabled_{false};
    std::vector<int> cells_;
    std::mt19937 rng_{};
};

Board NewBoard(const Board::Rules& rules, std::uint32_t seed = std::random_device{}());

bool HasMatchAt(const Board& board, int col, int row);
bool HasMatchAt(const Board& board, const Cell& cell);

using MatchGroup = std::vector<Cell>;
std::vector<MatchGroup> FindAllMatches(const Board& board);

bool LegalSwap(Board& board, const Move& move);

struct SimulationResult {
    int score = 0;
    int total_cleared = 0;
    int chains = 0;
    int bombs_triggered = 0;
    bool color_chain_triggered = false;
    Move move{};

    struct ClearedCell {
        Cell position{};
        int tile = kEmptyCell;
    };

    struct ClearEvent {
        std::vector<ClearedCell> cells;
        bool via_bomb = false;
        bool via_color = false;
    };

    struct FallEvent {
        Cell from{};
        Cell to{};
        int tile = kEmptyCell;
    };

    struct SpawnEvent {
        Cell position{};
        int tile = kEmptyCell;
        int distance_cells = 1;
    };

    struct ChainEvent {
        std::vector<ClearEvent> clears;
        std::vector<FallEvent> falls;
        std::vector<SpawnEvent> spawns;
    };

    std::vector<ClearEvent> clear_events;
    std::vector<FallEvent> fall_events;
    std::vector<SpawnEvent> spawn_events;
    std::vector<ChainEvent> chain_events;
};

SimulationResult SimulateFullChain(Board& board, const Move& move);

bool AnyLegalMoves(Board& board);

}  // namespace match::core
