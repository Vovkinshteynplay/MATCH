#include "match/core/AI.hpp"

#include <map>

namespace match::core::ai {

namespace {

std::pair<Cell, Cell> NormalizedKey(const Move& move) {
    if (move.b < move.a) {
        return {move.b, move.a};
    }
    return {move.a, move.b};
}

}  // namespace

std::optional<BestMoveResult> BestMove(const Board& board) {
    const int offsets[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};

    struct Entry {
        std::pair<Cell, Cell> key{};
        BestMoveResult result{};
    };

    std::vector<Entry> entries;
    entries.reserve(static_cast<std::size_t>(board.cols()) *
                    static_cast<std::size_t>(board.rows()) * 2);

    auto find_entry = [&](const std::pair<Cell, Cell>& key) -> Entry* {
        for (auto& entry : entries) {
            if (entry.key == key) {
                return &entry;
            }
        }
        return nullptr;
    };

    BestMoveResult best{};
    bool has_best = false;

    for (int col = 0; col < board.cols(); ++col) {
        for (int row = 0; row < board.rows(); ++row) {
            const Cell origin{col, row};
            for (const auto& offset : offsets) {
                const Cell neighbor{col + offset[0], row + offset[1]};
                if (!board.inBounds(neighbor)) {
                    continue;
                }

                Move move{origin, neighbor};

                Board evaluation_board = board;
                if (!LegalSwap(evaluation_board, move)) {
                    continue;
                }

                Board sim_board = board;
                SimulationResult sim = SimulateFullChain(sim_board, move);

                BestMoveResult candidate{};
                candidate.move = move;
                candidate.score = sim.score;
                candidate.simulation = std::move(sim);

                const auto key = NormalizedKey(move);
                if (auto* existing = find_entry(key); existing != nullptr) {
                    if (candidate.score > existing->result.score) {
                        existing->result = candidate;
                    }

                    if (!has_best || existing->result.score > best.score) {
                        best = existing->result;
                        has_best = true;
                    }
                } else {
                    entries.push_back({key, candidate});

                    if (!has_best || candidate.score > best.score) {
                        best = candidate;
                        has_best = true;
                    }
                }
            }
        }
    }

    if (!has_best) {
        return std::nullopt;
    }

    return best;
}

}  // namespace match::core::ai
