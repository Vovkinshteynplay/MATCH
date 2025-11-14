#include <cassert>
#include <iostream>

#include "match/core/AI.hpp"
#include "match/core/Board.hpp"

using namespace match::core;

namespace {

void TestNewBoardNoInitialMatches() {
    Board::Rules rules;
    rules.cols = 6;
    rules.rows = 6;
    rules.tile_types = 6;
    auto board = NewBoard(rules, /*seed=*/1337);
    for (int c = 0; c < board.cols(); ++c) {
        for (int r = 0; r < board.rows(); ++r) {
            assert(!HasMatchAt(board, c, r));
        }
    }
}

Board MakeTestBoard() {
    Board::Rules rules;
    rules.cols = 3;
    rules.rows = 3;
    rules.tile_types = 3;
    Board board(rules, /*seed=*/1);
    board.fillAll(kEmptyCell);

    // Column-major assignment to mirror original Python layout.
    // col 0
    board.set(0, 0, 0);
    board.set(0, 1, 0);
    board.set(0, 2, 1);
    // col 1
    board.set(1, 0, 1);
    board.set(1, 1, 1);
    board.set(1, 2, 2);
    // col 2
    board.set(2, 0, 2);
    board.set(2, 1, 2);
    board.set(2, 2, 1);

    return board;
}

void TestFindAllMatches() {
    auto board = MakeTestBoard();
    auto matches = FindAllMatches(board);
    assert(matches.empty());
}

void TestLegalSwapAndSimulate() {
    auto board = MakeTestBoard();
    Move move{{1, 2}, {0, 2}};  // Swap bottom cells in columns 1 and 0.

    Board temp = board;
    assert(LegalSwap(temp, move));
    auto result = SimulateFullChain(board, move);
    assert(result.score == 1);
    assert(result.total_cleared == 3);
    assert(result.chains == 1);
}

void TestAnyLegalMovesAndAI() {
    auto board = MakeTestBoard();
    assert(AnyLegalMoves(board));

    auto ai_result = match::core::ai::BestMove(board);
    assert(ai_result.has_value());
    auto expected_move = Move{{1, 1}, {1, 2}};
    assert((ai_result->move.a == expected_move.a && ai_result->move.b == expected_move.b) ||
           (ai_result->move.a == expected_move.b && ai_result->move.b == expected_move.a));
    assert(ai_result->score == 4);
}

}  // namespace

int main() {
    TestNewBoardNoInitialMatches();
    TestFindAllMatches();
    TestLegalSwapAndSimulate();
    TestAnyLegalMovesAndAI();
    std::cout << "All core tests passed.\n";
    return 0;
}
