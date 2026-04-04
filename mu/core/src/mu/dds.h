#pragma once

#include "game_state.h"
#include "rules.h"
#include "zobrist.h"
#include "tt.h"
#include "alpha_beta.h"

#include <array>

namespace mu {

    // ── DDS result ──────────────────────────────────────────

    /**
     * Result of a Double Dummy Solver analysis.
     *
     * Contains the optimal number of tricks for each possible
     * leader, from the perspective of the N/S partnership
     * (partnership 0).
     */
    struct dds_result {
        /** Tricks for N/S when each seat leads. */
        std::array<int8_t, num_players> tricks_ns{};

        /** Best opening lead card for the current leader. */
        card best_move = no_card;

        /** Tricks for N/S when current player leads. */
        [[nodiscard]] int8_t tricks() const {
            return tricks_ns[0];  // Convention: index 0 = original leader
        }
    };

    // ── DDS configuration ───────────────────────────────────

    /**
     * Configuration for DDS solve.
     */
    struct dds_config {
        bool solve_all_seats = false;  // Solve for all 4 leaders?
    };

    // ── DDS solve ───────────────────────────────────────────

    /**
     * Double Dummy Solver — find optimal play with all hands visible.
     *
     * Uses the SearchPolicy template parameter for the actual search
     * algorithm. Default is alpha_beta_policy, but can be swapped
     * for partition_search_policy or other implementations.
     *
     * @tparam Variant       Game variant (whist, bridge, jass)
     * @tparam SearchPolicy  Search algorithm (alpha_beta_policy, etc.)
     * @tparam Rules         Game rules (defaults to rules<Variant>)
     *
     * @param state   Game state with all hands visible
     * @param config  Search configuration
     * @return        DDS result with optimal tricks
     *
     * Example usage:
     * @code
     *   game_state<whist> state = ...;
     *   auto result = dds_solve<whist>(state);
     *   int tricks_for_ns = result.tricks_ns[0];
     *
     *   // Or with explicit policy:
     *   auto result = dds_solve<whist, alpha_beta_policy<whist>>(state);
     * @endcode
     */
    template<
        typename Variant,
        typename SearchPolicy = alpha_beta_policy<Variant>,
        typename Rules = rules<Variant>
    >
    [[nodiscard]] dds_result dds_solve( const game_state<Variant>& state, const dds_config& config = {}) {

        using TT = typename SearchPolicy::TT;

        dds_result result{};
        TT tt;

        const seat leader = state.current_player;
        const uint8_t leader_partnership = partnership_of(leader);

        // Compute initial hash
        const uint64_t hash = zobrist_hash(state);

        // Total tricks remaining
        const int total_tricks = state.tricks_remaining;

        // Search with full alpha-beta window
        constexpr int NEG_INF = -100;
        constexpr int POS_INF = 100;

        const int tricks_for_leader = SearchPolicy::search( state, NEG_INF, POS_INF, hash, tt);

        // Convert to N/S tricks
        if (leader_partnership == 0) {
            // Leader is N/S - tricks_for_leader is N/S tricks
            result.tricks_ns[static_cast<uint8_t>(leader)] = static_cast<int8_t>(tricks_for_leader);
        } else {
            // Leader is E/W - tricks_for_leader is E/W tricks
            // N/S tricks = total - E/W tricks
            result.tricks_ns[static_cast<uint8_t>(leader)] = static_cast<int8_t>(total_tricks - tricks_for_leader);
        }

        // Get best move from TT
        const auto depth = static_cast<int8_t>(
            ops::popcount(state.hands[0]) +
            ops::popcount(state.hands[1]) +
            ops::popcount(state.hands[2]) +
            ops::popcount(state.hands[3]));

        const tt_entry& root_entry = tt.at(hash);
        if (root_entry.key == hash && root_entry.depth == depth) {
            result.best_move = root_entry.best_move;
        }

        // Optionally solve for all seats
        if (config.solve_all_seats) {
            for (uint8_t s = 0; s < num_players; ++s) {
                if (static_cast<seat>(s) == leader) continue;

                // Create state with different leader
                game_state<Variant> alt_state = state;
                alt_state.current_player = static_cast<seat>(s);
                alt_state.trick.reset();

                TT alt_tt;
                const uint64_t alt_hash = zobrist_hash(alt_state);
                const uint8_t alt_partnership = partnership_of(static_cast<seat>(s));

                const int alt_tricks = SearchPolicy::search(
                    alt_state, NEG_INF, POS_INF, alt_hash, alt_tt);

                if (alt_partnership == 0) {
                    result.tricks_ns[s] = static_cast<int8_t>(alt_tricks);
                } else {
                    result.tricks_ns[s] = static_cast<int8_t>(total_tricks - alt_tricks);
                }
            }
        }

        return result;
    }

    // ── convenience functions ───────────────────────────────

    /**
     * Solve for optimal tricks with default settings.
     */
    template<typename Variant>
    [[nodiscard]] int dds_tricks(const game_state<Variant>& state) {
        const auto result = dds_solve<Variant>(state);
        const auto leader = static_cast<uint8_t>(state.current_player);
        return result.tricks_ns[leader];
    }

    /**
     * Get the optimal move for the current position.
     */
    template<typename Variant>
    [[nodiscard]] card dds_best_move(const game_state<Variant>& state) {
        const auto result = dds_solve<Variant>(state);
        return result.best_move;
    }

}

