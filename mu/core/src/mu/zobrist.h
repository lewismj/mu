#pragma once

#include "repr.h"
#include "game_state.h"

#include <array>
#include <cstdint>

namespace mu {

    // ── xorshift64 PRNG ─────────────────────────────────────

    /**
     * Xorshift64 PRNG for Zobrist key generation.
     *
     * Deterministic, fast, and constexpr-friendly.
     * Used by virtually all chess engines (Stockfish, etc.)
     */
    constexpr uint64_t xorshift64(uint64_t& state) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }

    // ── Zobrist key table ───────────────────────────────────

    /**
     * Zobrist keys for DDS transposition table.
     *
     * Keys are indexed by:
     *   card_keys[seat][card]  — card c is in seat s's hand
     *   player_key[seat]       — current player to move
     *   trump_key[suit]        — trump suit
     *
     * Hash is computed incrementally via XOR.
     */
    template<typename Variant>
    struct zobrist_keys {
        static constexpr int N = num_cards<Variant>;

        std::array<std::array<uint64_t, N>, num_players> card_keys{};
        std::array<uint64_t, num_players> player_keys{};
        std::array<uint64_t, 4> trump_keys{};

        /**
         * Generate all keys using xorshift64.
         * Seed chosen for good distribution (golden ratio derivative).
         */
        constexpr zobrist_keys() {
            uint64_t state = 0x9E3779B97F4A7C15ULL;  // Golden ratio

            for (uint8_t seat = 0; seat < num_players; ++seat) {
                for (int card = 0; card < N; ++card) {
                    card_keys[seat][card] = xorshift64(state);
                }
            }

            for (uint8_t seat = 0; seat < num_players; ++seat) {
                player_keys[seat] = xorshift64(state);
            }

            for (int s = 0; s < 4; ++s) {
                trump_keys[s] = xorshift64(state);
            }
        }
    };

    // ── compile-time key instances ──────────────────────────

    template<typename Variant>
    inline constexpr zobrist_keys<Variant> zkeys{};

    // ── hash computation ────────────────────────────────────

    /**
     * Compute full Zobrist hash for a game state.
     *
     * Used for initial hash; incremental updates are done
     * via XOR when cards move between hands.
     */
    template<typename Variant>
    [[nodiscard]] constexpr uint64_t zobrist_hash(const game_state<Variant>& state) {
        uint64_t hash = 0;

        // Hash all cards in each hand
        for (uint8_t seat = 0; seat < num_players; ++seat) {
            card_mask hand = state.hands[seat];
            while (hand) {
                const int c = ops::lsb_index(hand);
                hand &= hand - 1;
                hash ^= zkeys<Variant>.card_keys[seat][c];
            }
        }

        // Hash current player
        hash ^= zkeys<Variant>.player_keys[static_cast<uint8_t>(state.current_player)];

        // Hash trump suit
        hash ^= zkeys<Variant>.trump_keys[static_cast<int>(state.trump)];

        return hash;
    }

    /**
     * Incrementally update hash when a card is played.
     *
     * XOR out the card from player's hand.
     * XOR in the new current player.
     * XOR out the old current player.
     */
    template<typename Variant>
    [[nodiscard]] constexpr uint64_t zobrist_update_play(
            uint64_t hash,
            seat player,
            card c,
            seat old_current,
            seat new_current) {

        // Remove card from player's hand
        hash ^= zkeys<Variant>.card_keys[static_cast<uint8_t>(player)][c];

        // Update current player
        hash ^= zkeys<Variant>.player_keys[static_cast<uint8_t>(old_current)];
        hash ^= zkeys<Variant>.player_keys[static_cast<uint8_t>(new_current)];

        return hash;
    }

}

