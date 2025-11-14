#include "match/core/Board.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <random>
#include <set>
#include <unordered_set>

namespace match::core {

namespace {

using CellSet = std::set<Cell>;

bool CellsOverlap(const CellSet& a, const CellSet& b) {
    auto it_a = a.begin();
    auto it_b = b.begin();
    while (it_a != a.end() && it_b != b.end()) {
        if (*it_a == *it_b) {
            return true;
        }
        if (*it_a < *it_b) {
            ++it_a;
        } else {
            ++it_b;
        }
    }
    return false;
}

}  // namespace

Board::Board(int cols, int rows, int tile_types, bool bombs_enabled, bool color_chain_enabled,
             std::uint32_t seed)
    : rules_{cols, rows, tile_types, bombs_enabled, color_chain_enabled},
      cols_(cols),
      rows_(rows),
      tile_types_(tile_types),
      bombs_enabled_(bombs_enabled),
      color_chain_enabled_(color_chain_enabled),
      cells_(static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows), kEmptyCell),
      rng_(seed) {}

Board::Board(const Rules& rules, std::uint32_t seed)
    : Board(rules.cols, rules.rows, rules.tile_types, rules.bombs_enabled,
            rules.color_chain_enabled, seed) {}

bool Board::inBounds(int col, int row) const noexcept {
    return col >= 0 && col < cols_ && row >= 0 && row < rows_;
}

int Board::get(int col, int row) const noexcept {
    return cells_[index(col, row)];
}

void Board::set(int col, int row, int value) noexcept {
    cells_[index(col, row)] = value;
}

void Board::swapCells(const Cell& a, const Cell& b) noexcept {
    auto idx_a = index(a.col, a.row);
    auto idx_b = index(b.col, b.row);
    std::swap(cells_[idx_a], cells_[idx_b]);
}

int Board::randomTile() {
    if (tile_types_ <= 0) {
        return 0;
    }
    std::uniform_int_distribution<int> dist(0, tile_types_ - 1);
    return dist(rng_);
}

void Board::fillAll(int value) {
    std::fill(cells_.begin(), cells_.end(), value);
}

int Board::index(int col, int row) const noexcept {
    return col * rows_ + row;
}

Board NewBoard(const Board::Rules& rules, std::uint32_t seed) {
    Board board(rules, seed);
    for (int col = 0; col < board.cols(); ++col) {
        for (int row = 0; row < board.rows(); ++row) {
            board.set(col, row, board.randomTile());
            int tries = 0;
            while (HasMatchAt(board, col, row) && tries <= 20) {
                board.set(col, row, board.randomTile());
                ++tries;
            }
        }
    }

    bool changed = true;
    int iter_guard = 0;
    while (changed && iter_guard < 1000) {
        changed = false;
        ++iter_guard;
        for (int col = 0; col < board.cols() - 1; ++col) {
            for (int row = 0; row < board.rows() - 1; ++row) {
                const int t = board.get(col, row);
                if (t == kEmptyCell) {
                    continue;
                }
                if (board.get(col + 1, row) == t && board.get(col, row + 1) == t &&
                    board.get(col + 1, row + 1) == t) {
                    board.set(col + 1, row + 1, board.randomTile());
                    changed = true;
                }
            }
        }
    }

    return board;
}

bool HasMatchAt(const Board& board, int col, int row) {
    if (!board.inBounds(col, row)) {
        return false;
    }
    const int tile = board.get(col, row);
    if (tile == kEmptyCell) {
        return false;
    }
    int count = 1;
    for (int c = col - 1; c >= 0 && board.get(c, row) == tile; --c) {
        ++count;
    }
    for (int c = col + 1; c < board.cols() && board.get(c, row) == tile; ++c) {
        ++count;
    }
    if (count >= 3) {
        return true;
    }
    count = 1;
    for (int r = row - 1; r >= 0 && board.get(col, r) == tile; --r) {
        ++count;
    }
    for (int r = row + 1; r < board.rows() && board.get(col, r) == tile; ++r) {
        ++count;
    }
    return count >= 3;
}

bool HasMatchAt(const Board& board, const Cell& cell) {
    return HasMatchAt(board, cell.col, cell.row);
}

std::vector<MatchGroup> FindAllMatches(const Board& board) {
    std::vector<CellSet> groups;
    groups.reserve(board.cols() * board.rows());

    for (int row = 0; row < board.rows(); ++row) {
        int col = 0;
        while (col < board.cols()) {
            const int tile = board.get(col, row);
            if (tile == kEmptyCell) {
                ++col;
                continue;
            }
            int start = col;
            while (col + 1 < board.cols() && board.get(col + 1, row) == tile) {
                ++col;
            }
            if (col - start + 1 >= 3) {
                CellSet group;
                for (int c = start; c <= col; ++c) {
                    group.insert(Cell{c, row});
                }
                groups.push_back(std::move(group));
            }
            ++col;
        }
    }

    for (int col = 0; col < board.cols(); ++col) {
        int row = 0;
        while (row < board.rows()) {
            const int tile = board.get(col, row);
            if (tile == kEmptyCell) {
                ++row;
                continue;
            }
            int start = row;
            while (row + 1 < board.rows() && board.get(col, row + 1) == tile) {
                ++row;
            }
            if (row - start + 1 >= 3) {
                CellSet group;
                for (int r = start; r <= row; ++r) {
                    group.insert(Cell{col, r});
                }
                groups.push_back(std::move(group));
            }
            ++row;
        }
    }

    bool merged = true;
    while (merged) {
        merged = false;
        std::vector<CellSet> out;
        out.reserve(groups.size());
        while (!groups.empty()) {
            CellSet current = std::move(groups.back());
            groups.pop_back();
            bool expanded = true;
            while (expanded) {
                expanded = false;
                for (auto it = groups.begin(); it != groups.end();) {
                    if (CellsOverlap(current, *it)) {
                        current.insert(it->begin(), it->end());
                        it = groups.erase(it);
                        expanded = true;
                        merged = true;
                    } else {
                        ++it;
                    }
                }
            }
            out.push_back(std::move(current));
        }
        groups = std::move(out);
    }

    std::vector<MatchGroup> result;
    result.reserve(groups.size());
    for (auto& set : groups) {
        MatchGroup group;
        group.assign(set.begin(), set.end());
        result.push_back(std::move(group));
    }
    return result;
}

bool LegalSwap(Board& board, const Move& move) {
    if (!board.inBounds(move.a) || !board.inBounds(move.b)) {
        return false;
    }
    const int dist =
        std::abs(move.a.col - move.b.col) + std::abs(move.a.row - move.b.row);
    if (dist != 1) {
        return false;
    }
    board.swapCells(move);
    bool ok = !FindAllMatches(board).empty();

    if (!ok && board.bombsEnabled()) {
        for (int col = 0; col < board.cols() - 1 && !ok; ++col) {
            for (int row = 0; row < board.rows() - 1 && !ok; ++row) {
                const int t = board.get(col, row);
                if (t != kEmptyCell && board.get(col + 1, row) == t &&
                    board.get(col, row + 1) == t && board.get(col + 1, row + 1) == t) {
                    ok = true;
                }
            }
        }
    }

    board.swapCells(move);
    return ok;
}

SimulationResult SimulateFullChain(Board& board, const Move& move) {
    SimulationResult result{};
    if (!board.inBounds(move.a) || !board.inBounds(move.b)) {
        return result;
    }
    const int dist =
        std::abs(move.a.col - move.b.col) + std::abs(move.a.row - move.b.row);
    if (dist != 1) {
        return result;
    }

    board.swapCells(move);
    result.move = move;

    int total_cleared = 0;
    int chains = 0;
    int bombs_total = 0;
    bool color_chain_happened = false;

    while (true) {
        const auto raw_groups = FindAllMatches(board);

        std::vector<CellSet> base_groups;
        base_groups.reserve(raw_groups.size());
        CellSet matched_cells;
        for (const auto& group : raw_groups) {
            CellSet set;
            set.insert(group.begin(), group.end());
            matched_cells.insert(set.begin(), set.end());
            base_groups.push_back(std::move(set));
        }

        CellSet chain_neighbors;
        if (board.colorChainEnabled() && !base_groups.empty()) {
            constexpr int neighbor_offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& group : base_groups) {
                if (group.empty()) {
                    continue;
                }
                const Cell sample = *group.begin();
                const int match_color = board.get(sample);
                if (match_color == kEmptyCell) {
                    continue;
                }
                for (const auto& cell : group) {
                    for (const auto& off : neighbor_offsets) {
                        Cell neighbor{cell.col + off[0], cell.row + off[1]};
                        if (!board.inBounds(neighbor)) {
                            continue;
                        }
                        if (matched_cells.count(neighbor) > 0) {
                            continue;
                        }
                        if (board.get(neighbor) == match_color) {
                            chain_neighbors.insert(neighbor);
                        }
                    }
                }
            }
        }

        std::vector<CellSet> bomb_groups;
        if (board.bombsEnabled()) {
            for (int col = 0; col < board.cols() - 1; ++col) {
                for (int row = 0; row < board.rows() - 1; ++row) {
                    const int t = board.get(col, row);
                    if (t == kEmptyCell) {
                        continue;
                    }
                    if (board.get(col + 1, row) == t && board.get(col, row + 1) == t &&
                        board.get(col + 1, row + 1) == t) {
                        CellSet bomb_cells;
                        for (int x = col - 1; x <= col + 2; ++x) {
                            for (int y = row - 1; y <= row + 2; ++y) {
                                Cell candidate{x, y};
                                if (board.inBounds(candidate)) {
                                    bomb_cells.insert(candidate);
                                }
                            }
                        }
                        bomb_groups.push_back(std::move(bomb_cells));
                    }
                }
            }
        }

        if (base_groups.empty() && bomb_groups.empty() && chain_neighbors.empty()) {
            break;
        }

        CellSet to_remove;
        for (const auto& group : base_groups) {
            to_remove.insert(group.begin(), group.end());
        }
        for (const auto& group : bomb_groups) {
            to_remove.insert(group.begin(), group.end());
        }
        to_remove.insert(chain_neighbors.begin(), chain_neighbors.end());

        total_cleared += static_cast<int>(to_remove.size()) +
                         (2 * static_cast<int>(bomb_groups.size()));
        ++chains;
        bombs_total += static_cast<int>(bomb_groups.size());
        if (!chain_neighbors.empty()) {
            color_chain_happened = true;
        }

        SimulationResult::ChainEvent chain_event;
        std::vector<SimulationResult::ClearEvent> clear_events;
        clear_events.reserve(base_groups.size() + bomb_groups.size() +
                             (chain_neighbors.empty() ? 0 : 1));

        auto append_clear_event = [&](const CellSet& cells, bool via_bomb, bool via_color) {
            if (cells.empty()) {
                return;
            }
            SimulationResult::ClearEvent evt;
            evt.via_bomb = via_bomb;
            evt.via_color = via_color;
            evt.cells.reserve(cells.size());
            for (const auto& cell : cells) {
                SimulationResult::ClearedCell cleared;
                cleared.position = cell;
                cleared.tile = board.get(cell);
                evt.cells.push_back(cleared);
            }
            chain_event.clears.push_back(evt);
            clear_events.push_back(std::move(evt));
        };

        for (const auto& group : base_groups) {
            append_clear_event(group, /*via_bomb=*/false, /*via_color=*/false);
        }
        for (const auto& group : bomb_groups) {
            append_clear_event(group, /*via_bomb=*/true, /*via_color=*/false);
        }
        if (!chain_neighbors.empty()) {
            append_clear_event(chain_neighbors, /*via_bomb=*/false, /*via_color=*/true);
        }

        for (const auto& cell : to_remove) {
            board.set(cell, kEmptyCell);
        }

        std::vector<SimulationResult::FallEvent> fall_events;
        std::vector<SimulationResult::SpawnEvent> spawn_events;

        for (int col = 0; col < board.cols(); ++col) {
            int write = board.rows() - 1;
            for (int row = board.rows() - 1; row >= 0; --row) {
                const int val = board.get(col, row);
                if (val == kEmptyCell) {
                    continue;
                }
                if (write != row) {
                    board.set(col, write, val);
                    SimulationResult::FallEvent fall;
                    fall.from = Cell{col, row};
                    fall.to = Cell{col, write};
                    fall.tile = val;
                    fall_events.push_back(fall);
                    board.set(col, row, kEmptyCell);
                }
                --write;
            }

            const int holes = write + 1;
            int spawn_index = 0;
            for (int row = write; row >= 0; --row, ++spawn_index) {
                const int new_tile = board.randomTile();
                board.set(col, row, new_tile);
                SimulationResult::SpawnEvent spawn;
                spawn.position = Cell{col, row};
                spawn.tile = new_tile;
                spawn.distance_cells = std::max(1, holes - spawn_index);
                spawn_events.push_back(spawn);
            }
        }

        chain_event.falls.insert(chain_event.falls.end(), fall_events.begin(),
                                 fall_events.end());
        chain_event.spawns.insert(chain_event.spawns.end(), spawn_events.begin(),
                                  spawn_events.end());

        result.fall_events.insert(result.fall_events.end(), fall_events.begin(),
                                  fall_events.end());
        result.spawn_events.insert(result.spawn_events.end(), spawn_events.begin(),
                                   spawn_events.end());

        result.clear_events.insert(result.clear_events.end(),
                                   std::make_move_iterator(clear_events.begin()),
                                   std::make_move_iterator(clear_events.end()));
        result.chain_events.push_back(std::move(chain_event));
    }

    result.total_cleared = total_cleared;
    result.chains = chains;
    result.bombs_triggered = bombs_total;
    result.color_chain_triggered = color_chain_happened;

    result.score = (total_cleared >= 3) ? (1 + (total_cleared - 3)) : 0;
    return result;
}

bool AnyLegalMoves(Board& board) {
    for (int col = 0; col < board.cols(); ++col) {
        for (int row = 0; row < board.rows(); ++row) {
            const Cell origin{col, row};
            const Cell neighbors[2] = {{col + 1, row}, {col, row + 1}};
            for (const auto& neighbor : neighbors) {
                Move move{origin, neighbor};
                if (LegalSwap(board, move)) {
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace match::core
