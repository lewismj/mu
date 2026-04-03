#pragma once

#include "game_state.h"
#include "rules.h"
#include "zobrist.h"
#include "tt.h"

#include <algorithm>
#include <limits>

namespace mu {

    // ── alpha-beta search policy ────────────────────────────

    /**
     * Alpha-Beta search policy for Double Dummy Solver.
     *
     * Implements minimax with alpha-beta pruning and transposition
     * table. Designed to be used as a template parameter to dds_solve.
     *
     * Search always returns tricks for the ROOT player's partnership,
     * regardless of whose turn it is at the current node.
     *
     * Key optimizations:
     *   - Transposition table with exact/bound cutoffs
     *   - Move ordering: TT best move first, then high cards
     *   - Incremental Zobrist hashing
     */
    template<typename Variant, typename Rules = rules<Variant>>
    struct alpha_beta_policy {

        using TT = default_tt;

        /**
         * Internal recursive search.
         *
         * @param state           Game state (all hands visible)
         * @param alpha           Lower bound
         * @param beta            Upper bound
         * @param hash            Current Zobrist hash
         * @param tt              Transposition table
         * @param root_partnership The partnership we're maximizing for
         * @return                Tricks won by root_partnership from this point
         */
        static int search_impl(
                game_state<Variant> state,
                int alpha,
                int beta,
                uint64_t hash,
                TT& tt,
                uint8_t root_partnership) {

            const int8_t depth = static_cast<int8_t>(
                ops::popcount(state.hands[0]) +
                ops::popcount(state.hands[1]) +
                ops::popcount(state.hands[2]) +
                ops::popcount(state.hands[3]));

            // ── terminal node ───────────────────────────────
            if (depth == 0 || state.tricks_remaining == 0) {
                return 0;  // No more tricks to win from here
            }

            const seat current = state.current_player;
            const uint8_t current_partnership = partnership_of(current);
            const bool is_maximizing = (current_partnership == root_partnership);

            // ── transposition table probe ───────────────────
            const tt_entry* entry = tt.probe(hash, depth);
            uint8_t tt_move = no_card;

            if (entry) {
                tt_move = entry->best_move;

                if (entry->bound == tt_bound::exact) {
                    return entry->value;
                }
                if (entry->bound == tt_bound::lower && entry->value >= beta) {
                    return entry->value;
                }
                if (entry->bound == tt_bound::upper && entry->value <= alpha) {
                    return entry->value;
                }
                // Tighten bounds
                if (entry->bound == tt_bound::lower) {
                    alpha = std::max(alpha, static_cast<int>(entry->value));
                }
                if (entry->bound == tt_bound::upper) {
                    beta = std::min(beta, static_cast<int>(entry->value));
                }
            }

            // ── generate and order moves ────────────────────
            const card_mask legal = Rules::legal_moves(state);

            // Build move list with ordering scores
            struct move_entry {
                card c;
                int  score;  // Higher = search first
            };

            std::array<move_entry, 13> moves{};  // Max 13 cards in hand
            int move_count = 0;

            card_mask remaining = legal;
            while (remaining) {
                const card c = static_cast<card>(ops::lsb_index(remaining));
                remaining &= remaining - 1;

                int score = 0;

                // TT move gets highest priority
                if (c == tt_move) {
                    score = 1000;
                } else {
                    // Prefer high cards (simple heuristic)
                    score = rank_of<Variant>(c);
                }

                moves[move_count++] = {c, score};
            }

            // Sort moves by score (descending)
            std::sort(moves.begin(), moves.begin() + move_count,
                      [](const move_entry& a, const move_entry& b) {
                          return a.score > b.score;
                      });

            // ── search moves ────────────────────────────────
            int best_value = is_maximizing
                ? std::numeric_limits<int>::min()
                : std::numeric_limits<int>::max();
            card best_move = moves[0].c;
            tt_bound result_bound = tt_bound::exact;

            const int orig_alpha = alpha;

            for (int i = 0; i < move_count; ++i) {
                const card c = moves[i].c;

                // Make move
                game_state<Variant> child = state;

                // Capture trick state before play for scoring
                const bool trick_will_complete = (child.trick.num_played == 3);
                const seat old_current = child.current_player;

                Rules::play_card(child, current, c);

                const seat new_current = child.current_player;

                // Update hash incrementally
                const uint64_t child_hash = zobrist_update_play<Variant>(
                    hash, current, c, old_current, new_current);

                // Recursively search
                int child_value = search_impl(child, alpha, beta, child_hash, tt, root_partnership);

                // Add trick point if root_partnership won this trick
                if (trick_will_complete) {
                    const seat winner = new_current;  // Winner leads next
                    const uint8_t winner_partnership = partnership_of(winner);

                    if (winner_partnership == root_partnership) {
                        child_value += 1;
                    }
                }

                // Update best value based on maximizing/minimizing
                if (is_maximizing) {
                    if (child_value > best_value) {
                        best_value = child_value;
                        best_move = c;
                    }
                    alpha = std::max(alpha, best_value);
                } else {
                    if (child_value < best_value) {
                        best_value = child_value;
                        best_move = c;
                    }
                    beta = std::min(beta, best_value);
                }

                // Alpha-beta cutoff
                if (alpha >= beta) {
                    break;
                }
            }

            // ── determine bound type for TT ─────────────────
            if (is_maximizing) {
                if (best_value <= orig_alpha) {
                    result_bound = tt_bound::upper;
                } else if (best_value >= beta) {
                    result_bound = tt_bound::lower;
                }
            } else {
                if (best_value >= beta) {
                    result_bound = tt_bound::lower;
                } else if (best_value <= orig_alpha) {
                    result_bound = tt_bound::upper;
                }
            }

            // ── store in transposition table ────────────────
            tt.store(hash, depth, static_cast<int8_t>(best_value),
                     result_bound, best_move);

            return best_value;
        }

        /**
         * Public entry point.
         *
         * @param state   Game state (all hands visible)
         * @param alpha   Lower bound (best score for maximizer)
         * @param beta    Upper bound (best score for minimizer)
         * @param hash    Current Zobrist hash
         * @param tt      Transposition table
         * @return        Tricks won by current player's partnership
         */
        static int search(
                game_state<Variant> state,
                int alpha,
                int beta,
                uint64_t hash,
                TT& tt) {

            const uint8_t root_partnership = partnership_of(state.current_player);
            return search_impl(state, alpha, beta, hash, tt, root_partnership);
        }
    };

}
