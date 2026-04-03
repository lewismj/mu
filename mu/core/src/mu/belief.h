#pragma once

#include "game_state.h"

#include <array>
#include <cassert>
#include <span>

namespace mu {

    inline constexpr uint8_t num_other_players = num_players - 1;

    // ── seat ↔ opponent-index mapping ───────────────────────

    /**
     * Map a seat to an other-player index (0..2) relative to
     * an observer.  The observer's own seat is excluded.
     *
     * Seats below the observer keep their raw value;
     * seats above are shifted down by one.
     */
    [[nodiscard]] constexpr uint8_t seat_to_opp(seat observer, seat s) {
        assert(s != observer && "cannot map observer to opponent index");
        const auto raw = static_cast<uint8_t>(s);
        const auto obs = static_cast<uint8_t>(observer);
        return raw < obs ? raw : static_cast<uint8_t>(raw - 1);
    }

    /**
     * Inverse of seat_to_opp: map other-player index (0..2)
     * back to a seat, relative to an observer.
     */
    [[nodiscard]] constexpr seat opp_to_seat(seat observer, uint8_t opp) {
        const auto obs = static_cast<uint8_t>(observer);
        const uint8_t raw = opp < obs ? opp : static_cast<uint8_t>(opp + 1);
        return static_cast<seat>(raw);
    }

    // ── forward declarations for soft heuristics ──────────

    template<typename Variant> struct belief_state;
    template<typename Variant> struct soft_rule;
    template<typename Variant> struct play_context;

    template<typename Variant>
    [[nodiscard]] play_context<Variant> make_play_context(
        const game_state<Variant>&,
        const belief_state<Variant>&,
        seat, card);

    template<typename Variant>
    void apply_soft_rules(const play_context<Variant>&, belief_state<Variant>&, std::span<const soft_rule<Variant>>);

    // ── belief state ────────────────────────────────────────

    /**
     * Card probability matrix — the observer's belief about
     * which unseen cards each other player holds.
     *
     * Storage
     * -------
     * prob[i][c] ∈ [0,1]  — probability that other-player i
     *                        holds card c.
     * possible[i]          — hard-constraint bitmask
     *                        (1 = can hold, 0 = definitely cannot).
     *
     * Invariants  (after renormalize)
     * ----------
     *   For every unseen card c:
     *     Σ_{i=0}^{2} prob[i][c] = 1
     *
     *   prob[i][c] = 0  whenever:
     *     • bit c is clear in possible[i], OR
     *     • card c is played or in the observer's hand.
     *
     * Update flow
     * -----------
     *   1. init()            — uniform prior from starting state.
     *   2. update_on_play()  — called once per card played;
     *                          applies hard constraints (void
     *                          signals) then renormalizes.
     *
     * The matrix is maintained at the "real game" level and
     * fed into determinize() before each ISMCTS iteration.
     */
    template<typename Variant>
    struct belief_state {
        static constexpr int N = num_cards<Variant>;

        seat observer{};

        /** Per-card probability for each of the 3 other players. */
        std::array<std::array<float, N>, num_other_players> prob{};

        /** Hard constraint: bitmask of cards each other player can hold. */
        std::array<card_mask, num_other_players> possible{};

        /**
         * Initialize from a game state with a uniform prior.
         *
         * Each unseen card is equally likely to be in any of
         * the three other players' hands: P = 1/3.
         */
        void init(const game_state<Variant>& state, seat obs) {
            observer = obs;

            const card_mask own = state.hands[static_cast<uint8_t>(obs)];
            const card_mask deck_mask = (card_mask{1} << N) - 1;
            const card_mask unseen = ~(state.played | own) & deck_mask;

            for (uint8_t i = 0; i < num_other_players; ++i)
                possible[i] = unseen;

            constexpr float uniform_p =
                1.0f / static_cast<float>(num_other_players);

            for (int c = 0; c < N; ++c) {
                const float p = ((unseen >> c) & 1) ? uniform_p : 0.0f;
                for (uint8_t i = 0; i < num_other_players; ++i)
                    prob[i][c] = p;
            }
        }


        /**
         * Update beliefs after a card is played.
         *
         * @param player     Seat that played the card.
         * @param c          Card index that was played.
         * @param is_lead    True if this card is the trick lead
         *                   (first card in the trick).
         * @param lead_suit  Lead suit of the current trick
         *                   (only consulted when is_lead == false).
         *
         * Hard effects applied:
         *   1. Card c is removed from every other player's
         *      possible set (it is now known / played).
         *   2. If an other player did not follow the lead suit,
         *      the entire suit is zeroed in their possible set
         *      (void signal — strongest inference in
         *      trick-taking games).
         *
         * Caller should capture is_lead / lead_suit from the
         * trick state BEFORE calling rules::play_card (which
         * mutates the trick state).
         */
        void update_on_play(seat player, card c,
                            bool is_lead, suit lead_suit) {

            // ── card is now known: remove from all other players ──
            const card_mask bit = card_mask{1} << c;
            for (uint8_t i = 0; i < num_other_players; ++i) {
                prob[i][c] = 0.0f;
                possible[i] &= ~bit;
            }

            // ── void signal ──
            if (player != observer && !is_lead) {
                const suit played_suit = suit_of<Variant>(c);

                if (played_suit != lead_suit) {
                    const uint8_t opp = seat_to_opp(observer, player);
                    const card_mask smask = ops::suit_masks<Variant>[ static_cast<int>(lead_suit)];

                    possible[opp] &= ~smask;

                    /* Zero probabilities for the voided suit.
                       Only iterate the suit's bit range.      */
                    const int base = static_cast<int>(lead_suit) * deck_traits<Variant>::num_ranks;
                    const int end =
                        base + deck_traits<Variant>::num_ranks;

                    for (int sc = base; sc < end; ++sc)
                        prob[opp][sc] = 0.0f;
                }
            }

            renormalize();
        }

        /**
         * Extended update: hard constraints + soft heuristics.
         *
         * Same as the basic update_on_play(), but also runs
         * soft Bayesian rules before renormalizing.
         *
         * @param state       Game state BEFORE play_card().
         * @param player      Seat that played the card.
         * @param c           Card index that was played.
         * @param is_lead     True if this card is the trick lead.
         * @param lead_suit   Lead suit (only if !is_lead).
         * @param heuristics  Soft rules to evaluate.
         */
        void update_on_play(
                const game_state<Variant>& state,
                seat player, card c,
                bool is_lead, suit lead_suit,
                std::span<const soft_rule<Variant>> heuristics) {

            // ── hard constraints (same as basic overload) ──
            const card_mask bit = card_mask{1} << c;
            for (uint8_t i = 0; i < num_other_players; ++i) {
                prob[i][c] = 0.0f;
                possible[i] &= ~bit;
            }

            if (player != observer && !is_lead) {
                const suit played_suit = suit_of<Variant>(c);

                if (played_suit != lead_suit) {
                    const uint8_t opp = seat_to_opp(observer, player);
                    const card_mask smask = ops::suit_masks<Variant>[ static_cast<int>(lead_suit)];

                    possible[opp] &= ~smask;

                    const int base = static_cast<int>(lead_suit) * deck_traits<Variant>::num_ranks;
                    const int end = base + deck_traits<Variant>::num_ranks;

                    for (int sc = base; sc < end; ++sc)
                        prob[opp][sc] = 0.0f;
                }
            }

            // ── soft heuristics ──
            if (!heuristics.empty()) {
                auto ctx = make_play_context<Variant>( state, *this, player, c);
                apply_soft_rules<Variant>( ctx, *this, heuristics);
            }

            renormalize();
        }

        // ── renormalization ─────────────────────────────────

        /**
         * Column-normalize: for each card c, ensure
         *   Σ_{i} prob[i][c] = 1
         * across the three other players, honouring the hard
         * constraints in possible[].
         *
         * Cards that no other player can hold (fully played or
         * in the observer's hand) are left at zero.
         */
        void renormalize() {
            for (int c = 0; c < N; ++c) {
                float sum = 0.0f;

                for (uint8_t i = 0; i < num_other_players; ++i) {
                    if (!((possible[i] >> c) & 1))
                        prob[i][c] = 0.0f;
                    sum += prob[i][c];
                }

                if (sum > 0.0f) {
                    const float inv = 1.0f / sum;
                    for (uint8_t i = 0; i < num_other_players; ++i)
                        prob[i][c] *= inv;
                }
            }
        }
    };

}

