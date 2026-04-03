#pragma once

#include "game_state.h"
#include "rules.h"
#include <random>

namespace mu {

    /**
     * Generic ISMCTS rollout.
     * 
     * Performs a random rollout from the current state until the game is complete.
     * 
     * @tparam Variant The game variant.
     * @tparam Rules   The ruleset to use (defaults to rules<Variant>).
     * @tparam RNG     The random number generator type.
     * 
     * @param state The starting game state.
     * @param rng   The random number generator.
     * @return The final scores (tricks won) for each partnership.
     */
    template<typename Variant, typename Rules = rules<Variant>, typename RNG>
    std::array<uint8_t, num_partnerships> rollout(game_state<Variant> state, RNG& rng) {
        std::uniform_int_distribution<int> dist;

        while (state.tricks_remaining > 0) {
            card_mask moves = Rules::legal_moves(state);
            assert(moves != 0 && "No legal moves available in rollout");

            // Pick a random move — O(1) with PDEP, O(k) fallback.
            const int count = ops::popcount(moves);
            const int index = dist(rng, std::uniform_int_distribution<int>::param_type{0, count - 1});
            const card_mask selected = ops::nth_set_bit(moves, index);
            card c = static_cast<card>(ops::lsb_index(selected));

            Rules::play_card(state, state.current_player, c);
        }

        return state.tricks_won;
    }

}
