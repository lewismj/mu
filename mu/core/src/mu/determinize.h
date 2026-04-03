#pragma once

#include "belief.h"

#include <algorithm>
#include <cassert>
#include <random>

namespace mu {

    /**
     * Sample a complete deal of unseen cards, weighted by the
     * card probability matrix.
     *
     * Given the observer's known hand, played cards, and the
     * belief state, re-deals unseen cards among the 3 other
     * players respecting:
     *
     *   • Hard constraints  (void signals in possible[]).
     *   • Soft weights      (prob[][] from belief model).
     *   • Hand-size targets (each other player receives exactly
     *     as many cards as the real state says they hold).
     *
     * The resulting game_state has all 4 hands filled and can
     * be passed directly to rules::play_card / rollout.
     *
     * Sampling strategy
     * -----------------
     * Cards are shuffled to a random order, then assigned one
     * at a time via weighted roulette selection among eligible
     * players.  If the belief model assigns zero weight to all
     * eligible players for a card, a uniform fallback is used
     * over players that still have remaining capacity.
     */
    template<typename Variant, typename RNG>
    [[nodiscard]] game_state<Variant> determinize(
            const game_state<Variant>& state,
            const belief_state<Variant>& belief,
            RNG& rng) {

        constexpr int N = num_cards<Variant>;

        game_state<Variant> result = state;

        const seat  obs       = belief.observer;
        const auto  obs_idx   = static_cast<uint8_t>(obs);
        const card_mask own   = state.hands[obs_idx];
        const card_mask deck  = (card_mask{1} << N) - 1;
        const card_mask unseen = ~(state.played | own) & deck;

        // ── collect unseen card indices ──────────────────────

        card_array<Variant> unseen_cards{};
        int num_unseen = 0;
        {
            card_mask tmp = unseen;
            while (tmp) {
                const int idx = ops::lsb_index(tmp);
                tmp &= tmp - 1;
                unseen_cards[num_unseen++] = static_cast<card>(idx);
            }
        }

        /* Shuffle, then stable-sort by constraint tightness
           (fewest eligible opponents first).  This ensures
           that heavily constrained cards (e.g. cards in a
           suit an opponent is void in) are assigned while
           eligible players still have remaining capacity,
           while preserving randomness within each tier.     */
        std::shuffle(unseen_cards.begin(),
                     unseen_cards.begin() + num_unseen,
                     rng);

        std::stable_sort(
            unseen_cards.begin(),
            unseen_cards.begin() + num_unseen,
            [&belief](card a, card b) {
                int ea = 0, eb = 0;
                for (uint8_t i = 0; i < num_other_players; ++i) {
                    ea += static_cast<int>((belief.possible[i] >> a) & 1);
                    eb += static_cast<int>((belief.possible[i] >> b) & 1);
                }
                return ea < eb;
            });

        // ── target hand sizes for other players ─────────────

        std::array<int, num_other_players> target{};
        for (uint8_t i = 0; i < num_other_players; ++i) {
            const seat s = opp_to_seat(obs, i);
            target[i] = ops::popcount( state.hands[static_cast<uint8_t>(s)]);
        }

        // ── clear other players' hands in result ────────────

        for (uint8_t i = 0; i < num_other_players; ++i) {
            const seat s = opp_to_seat(obs, i);
            result.hands[static_cast<uint8_t>(s)] = 0;
        }

        // ── remaining capacity per other player ─────────────

        std::array<int, num_other_players> remaining = target;

        {
            int total_needed = 0;
            for (auto t : target) total_needed += t;
            assert(num_unseen == total_needed
                   && "unseen card count must equal sum of "
                      "target hand sizes");
        }

        // ── weighted assignment ─────────────────────────────

        std::uniform_real_distribution<float> udist(0.0f, 1.0f);

        for (int ci = 0; ci < num_unseen; ++ci) {
            const card c = unseen_cards[ci];

            /* Build per-player weight vector for this card. */
            float w[num_other_players];
            float total = 0.0f;

            for (uint8_t i = 0; i < num_other_players; ++i) {
                if (remaining[i] > 0
                    && ((belief.possible[i] >> c) & 1)) {
                    w[i] = belief.prob[i][c];
                    /* Epsilon fallback: prob may be zero after
                       aggressive zeroing but possible is set. */
                    if (w[i] <= 0.0f) w[i] = 1e-6f;
                } else {
                    w[i] = 0.0f;
                }
                total += w[i];
            }

            /* Hard fallback: if no valid weighted assignment,
               spread uniformly among players with capacity
               that are allowed to hold this card.           */
            if (total <= 0.0f) {
                for (uint8_t i = 0; i < num_other_players; ++i) {
                    if (remaining[i] > 0
                        && ((belief.possible[i] >> c) & 1)) {
                        w[i] = 1.0f;
                        total += 1.0f;
                    }
                }
            }

            assert(total > 0.0f
                   && "no valid assignment for unseen card");

            /* Weighted roulette selection. */
            float r = udist(rng) * total;
            uint8_t chosen = 0;
            for (uint8_t i = 0; i < num_other_players; ++i) {
                r -= w[i];
                if (r <= 0.0f) { chosen = i; break; }
                chosen = i;
            }

            const seat s = opp_to_seat(obs, chosen);
            result.hands[static_cast<uint8_t>(s)] |=
                (card_mask{1} << c);
            remaining[chosen]--;
        }

        return result;
    }

}

