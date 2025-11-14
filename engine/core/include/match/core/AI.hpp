#pragma once

#include <optional>

#include "match/core/Board.hpp"

namespace match::core::ai {

struct BestMoveResult {
    Move move{};
    int score = 0;
    SimulationResult simulation{};
};

std::optional<BestMoveResult> BestMove(const Board& board);

}  // namespace match::core::ai

