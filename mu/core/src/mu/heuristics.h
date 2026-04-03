#pragma once

#include "belief.h"

#include <span>

namespace mu {

    // ── play context ────────────────────────────────────────

    /**
     * Snapshot of the observation available to a soft heuristic
     * at the moment a card is played.
     *
     * Captured BEFORE rules::play_card() mutates game state,
     * so trick.num_played reflects the count prior to this play.
     *
     * This is the sole input to every predicate / adjuster;
     * keeping it in one struct avoids a long parameter list
     * and makes it easy to extend without breaking existing
     * rules.
     */
    template<typename Variant>
    struct play_context {
        seat      player;               /* Who played.                      */
        card      card_played;          /* Card index that was played.      */
        suit      card_suit;            /* Suit of the played card.         */
        uint8_t   card_rank;            /* Rank within its suit (0-based).  */
        bool      is_lead;              /* First card in the trick?         */
        suit      lead_suit;            /* Lead suit (undefined if is_lead).*/
        suit      trump;                /* Current trump suit.              */
        seat      observer;             /* Observer's seat.                 */

        /* Pre-play trick state (read-only snapshot). */
        trick_state trick;

        /* Number of cards the player held BEFORE this play.
           Includes the card just played.                     */
        int       player_hand_size;

        /* Position in trick (0 = lead, 1 = 2nd, …, 3 = 4th). */
        uint8_t   trick_position;

        /* Did this card beat the current trick winner? */
        bool      beats_winner;

        /* Is the player the observer's partner? */
        bool      is_partner;

        /* Was the player able to follow suit?
           (True if they played in lead_suit OR they were leading.) */
        bool      followed_suit;
    };

    /**
     * Build a play_context from the current game state.
     *
     * Must be called BEFORE rules::play_card() so that
     * trick.num_played is the pre-play count.
     */
    template<typename Variant>
    [[nodiscard]] play_context<Variant> make_play_context(
            const game_state<Variant>& state,
            const belief_state<Variant>& belief,
            seat player, card c) {

        play_context<Variant> ctx;

        ctx.player         = player;
        ctx.card_played    = c;
        ctx.card_suit      = suit_of<Variant>(c);
        ctx.card_rank      = rank_of<Variant>(c);
        ctx.is_lead        = (state.trick.num_played == 0);
        ctx.lead_suit      = state.trick.lead_suit;
        ctx.trump          = state.trump;
        ctx.observer       = belief.observer;
        ctx.trick          = state.trick;
        ctx.player_hand_size =
            ops::popcount(
                state.hands[static_cast<uint8_t>(player)]);
        ctx.trick_position = state.trick.num_played;
        ctx.beats_winner   =
            rules<Variant>::beats_current(
                state.trick, state.trump, c);
        ctx.is_partner     =
            (partner_of(belief.observer) == player);
        ctx.followed_suit  =
            ctx.is_lead
            || (ctx.card_suit == state.trick.lead_suit);

        return ctx;
    }

    // ── soft rule ───────────────────────────────────────────

    /**
     * A single soft Bayesian inference rule.
     *
     * Design
     * ------
     * Each rule is a { name, strength, predicate, apply } tuple.
     *
     *   predicate(ctx)           → true if this rule fires.
     *   apply(ctx, belief, str)  → multiplies targeted
     *                              prob[opp][c] entries by a
     *                              scaling factor.  Renormalize
     *                              is called afterwards by the
     *                              caller — rules only scale.
     *
     * `strength` is a tuneable scalar (default 1.0) that scales
     * the adjustment magnitude.  This lets you tune heuristic
     * weights without recompiling — e.g., from a config file
     * or via hyperparameter search.
     *
     * Function pointers are used instead of std::function to
     * keep the struct trivially copyable and avoid heap
     * allocation.
     */
    template<typename Variant>
    struct soft_rule {
        using pred_fn  = bool (*)(const play_context<Variant>&);
        using apply_fn = void (*)(const play_context<Variant>&,
                                  belief_state<Variant>&,
                                  float strength,
                                  float base_factor);

        const char* name;
        float       strength;       // overall rule weight (ML knob)
        float       base_factor;    // scaling magnitude  (ML knob)
        pred_fn     predicate;
        apply_fn    apply;
    };

    // ── apply heuristics ────────────────────────────────────

    /**
     * Run all matching soft rules against the belief state.
     *
     * Called by belief_state::update_on_play() after hard
     * constraints and before renormalize().
     */
    template<typename Variant>
    void apply_soft_rules(
            const play_context<Variant>& ctx,
            belief_state<Variant>& belief,
            std::span<const soft_rule<Variant>> rules_span) {

        for (const auto& rule : rules_span) {
            if (rule.predicate(ctx))
                rule.apply(ctx, belief, rule.strength, rule.base_factor);
        }
    }

    // ── play history entry ──────────────────────────────────

    /**
     * Compact record of a single play, for multi-play pattern
     * detection (e.g., high-low signalling across two tricks).
     */
    struct play_record {
        seat    player    = {};
        card    c         = no_card;
        suit    lead_suit = {};
        bool    is_lead   = false;
    };

    // ─────────────────────────────────────────────────────────
    //  WHIST HEURISTICS — built-in soft rules for Whist/Bridge
    // ─────────────────────────────────────────────────────────

    namespace whist_heuristics {
        /**
         * Helpers to reduce duplication in rule implementation.
         */
        template<typename Variant>
        [[nodiscard]] inline uint8_t opp_index(const play_context<Variant>& ctx) {
            return seat_to_opp(ctx.observer, ctx.player);
        }

        template<typename Variant>
        [[nodiscard]] inline std::pair<int, int> suit_range(suit s) {
            constexpr int ranks = deck_traits<Variant>::num_ranks;
            const int base = static_cast<int>(s) * ranks;
            return {base, base + ranks};
        }

        [[nodiscard]] inline bool is_opponent(const play_context<whist>& ctx) {
            return ctx.player != ctx.observer && !ctx.is_partner;
        }

        /**
         * Rule 1 — Played low on a winnable trick.
         *
         * If an opponent followed suit but played a low card
         * (bottom third of the suit) when the trick was
         * potentially winnable, they likely lack high cards
         * in that suit.
         *
         * Effect: dampen probability of high ranks (top third)
         *         in the lead suit for this opponent.
         */
        inline bool pred_played_low_on_winnable(
                const play_context<whist>& ctx) {
            if (!is_opponent(ctx))      return false;
            if (ctx.is_lead)            return false;
            if (!ctx.followed_suit)     return false;

            constexpr int ranks = deck_traits<whist>::num_ranks;
            /* "Low" = bottom third of ranks (0..3 in a 13-rank deck). */
            return ctx.card_rank < (ranks / 3);
        }

        inline void apply_played_low_on_winnable(
                const play_context<whist>& ctx,
                belief_state<whist>& belief,
                float strength,
                float base_factor) {

            const uint8_t opp = opp_index(ctx);
            const auto [base, end] = suit_range<whist>(ctx.lead_suit);

            constexpr int ranks = deck_traits<whist>::num_ranks;
            /* Dampen probability for the top third of ranks. */
            const int high_start = base + (ranks * 2 / 3);

            const float factor = base_factor * strength;
            for (int c = high_start; c < end; ++c)
                belief.prob[opp][c] *= factor;
        }

        /**
         * Rule 2 — Led a suit (voluntary lead).
         *
         * If an opponent chose to lead a particular suit,
         * they likely hold length (multiple cards) in it.
         *
         * Effect: boost probability that this opponent holds
         *         other cards in the led suit.
         */
        inline bool pred_led_a_suit( const play_context<whist>& ctx) {
            if (ctx.player == ctx.observer) return false;
            return ctx.is_lead;
        }

        inline void apply_led_a_suit(
                const play_context<whist>& ctx,
                belief_state<whist>& belief,
                float strength,
                float base_factor) {

            const uint8_t opp = opp_index(ctx);
            const auto [base, end] = suit_range<whist>(ctx.card_suit);

            /* Boost remaining cards in the led suit. */
            const float factor = base_factor * strength;
            for (int c = base; c < end; ++c) {
                if (c != ctx.card_played)
                    belief.prob[opp][c] *= factor;
            }
        }

        /**
         * Rule 3 — Trumped with a low trump.
         *
         * If an opponent ruffed (played trump when void in
         * lead suit) using a low trump, they likely hold
         * higher trumps they're preserving.
         *
         * Effect: boost probability of higher trump ranks
         *         for this opponent.
         */
        inline bool pred_low_trump_ruff(
                const play_context<whist>& ctx) {
            if (!is_opponent(ctx))           return false;
            if (ctx.is_lead)                 return false;
            if (ctx.followed_suit)           return false;
            if (ctx.card_suit != ctx.trump)  return false;

            /* "Low" trump = bottom half of ranks. */
            constexpr int ranks = deck_traits<whist>::num_ranks;
            return ctx.card_rank < (ranks / 2);
        }

        inline void apply_low_trump_ruff(
                const play_context<whist>& ctx,
                belief_state<whist>& belief,
                const float strength,
                const float base_factor) {

            const uint8_t opp = opp_index(ctx);
            const auto [base, end] = suit_range<whist>(ctx.trump);

            /* Boost higher trumps (those above the played rank). */
            const float factor = base_factor * strength;
            for (int c = base + ctx.card_rank + 1; c < end; ++c)
                belief.prob[opp][c] *= factor;
        }

        /**
         * Rule 4 — Discarded from a short suit.
         *
         * If an opponent was void in the lead suit and chose
         * to discard (not trump), they likely have few or no
         * trumps.
         *
         * Effect: dampen probability of trump cards for this
         *         opponent.
         */
        inline bool pred_discard_not_trump(
                const play_context<whist>& ctx) {
            if (!is_opponent(ctx))           return false;
            if (ctx.is_lead)                 return false;
            if (ctx.followed_suit)           return false;
            /* Didn't follow suit AND didn't trump → discard. */
            return ctx.card_suit != ctx.trump;
        }

        inline void apply_discard_not_trump(
                const play_context<whist>& ctx,
                belief_state<whist>& belief,
                const float strength,
                const float base_factor) {

            const uint8_t opp = opp_index(ctx);
            const auto [base, end] = suit_range<whist>(ctx.trump);

            /* Dampen the probability of all trump cards. */
            const float factor = base_factor * strength;
            for (int c = base; c < end; ++c)
                belief.prob[opp][c] *= factor;
        }

        /**
         * Rule 5 — Partner led high (top-of-sequence).
         *
         * If partner led a high card (top third), they are
         * signalling strength.  Decrease probability that
         * opponents hold high cards in that suit.
         *
         * Effect: dampen opponents' probability for high ranks
         *         in the led suit.
         */
        inline bool pred_partner_led_high(
                const play_context<whist>& ctx) {
            if (!ctx.is_partner)    return false;
            if (!ctx.is_lead)       return false;

            constexpr int ranks = deck_traits<whist>::num_ranks;
            return ctx.card_rank >= (ranks * 2 / 3);
        }

        inline void apply_partner_led_high(
                const play_context<whist>& ctx,
                belief_state<whist>& belief,
                const float strength,
                const float base_factor) {

            const auto [base, end] = suit_range<whist>(ctx.card_suit);
            constexpr int ranks = deck_traits<whist>::num_ranks;
            const int high_start = base + (ranks * 2 / 3);

            const float factor = base_factor * strength;

            /* Dampen high cards in this suit for all opponents
               (not partner — partner played it).               */
            for (uint8_t i = 0; i < num_other_players; ++i) {
                const seat s = opp_to_seat(ctx.observer, i);
                if (s == ctx.player) continue; /* skip partner */

                for (int c = high_start; c < end; ++c)
                    belief.prob[i][c] *= factor;
            }
        }

        // ── built-in rule table ─────────────────────────────

        /**
         * Default heuristic set for Whist / Bridge.
         *
         * Rules are evaluated in order; all matching rules
         * fire (they are not mutually exclusive).  Strengths
         * default to 1.0 and can be tuned.
         */
        inline const soft_rule<whist> default_rules[] = {
            {
                "played_low_on_winnable", 1.0f, 0.3f,
                pred_played_low_on_winnable,
                apply_played_low_on_winnable
            },
            {
                "led_a_suit", 1.0f, 1.5f,
                pred_led_a_suit,
                apply_led_a_suit
            },
            {
                "low_trump_ruff", 1.0f, 1.8f,
                pred_low_trump_ruff,
                apply_low_trump_ruff
            },
            {
                "discard_not_trump", 1.0f, 0.4f,
                pred_discard_not_trump,
                apply_discard_not_trump
            },
            {
                "partner_led_high", 1.0f, 0.5f,
                pred_partner_led_high,
                apply_partner_led_high
            },
        };

        inline constexpr int num_default_rules = sizeof(default_rules) / sizeof(default_rules[0]);

    } // namespace whist_heuristics

}

