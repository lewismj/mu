#pragma once

#include "game_state.h"

namespace mu {

    /**
     * Game rules — variant-specific game-level logic.
     *
     * The default template covers the common trick-taking path
     * (Whist / Bridge).  Specialize for variants that diverge
     * (Jass scoring, undertrump restrictions, etc.).
     *
     * Low-level bitboard operations (card comparison, remap)
     * live in repr.h and deck_traits<Variant>.  This layer
     * operates on game_state and produces game-level results
     * (seats, scores, legal-move masks).
     */
    template<typename Variant>
    struct rules {

        // ── trick resolution ────────────────────────────────

        /**
         * Determine the winning seat of the current trick.
         *
         * Delegates card-level winner to the free-function
         * trick_winner<Variant>() in repr.h, then maps the
         * winning card back to a seat via trick.cards[].
         */
        [[nodiscard]] static seat trick_winner_seat(const game_state<Variant>& state) {
            const auto& t = state.trick;

            assert(t.num_played > 0 && "trick_winner_seat called on empty trick");

            // If winner was tracked incrementally via play_card, just return it.
            if (t.winning_card != no_card)
                return t.current_winner;

            // Fallback: compute from the mask using the low-level free function.
            const card_mask winner_mask = trick_winner<Variant>(
                t.mask, t.lead_suit, state.trump);

            const card winner_idx = static_cast<card>(ops::lsb_index(winner_mask));

            for (uint8_t i = 0; i < num_players; ++i) {
                const auto s = static_cast<seat>((static_cast<uint8_t>(t.lead_seat) + i) & 3);
                if (t.cards[static_cast<uint8_t>(s)] == winner_idx)
                    return s;
            }

            assert(false && "winning card not found in trick");
            return t.lead_seat;
        }

        // ── card play ───────────────────────────────────────

        /**
         * Does card c (played into the current trick state)
         * beat the current winning_card?
         *
         * Uses the remapped bitboard comparison: a card beats
         * the current winner if it is the MSB of the candidate
         * set {winner, card} after remap.
         */
        [[nodiscard]] inline_always static bool beats_current( const trick_state& t, suit trump, const card c) {

            if (t.winning_card == no_card) return true;   // first card always wins

            const suit c_suit = suit_of<Variant>(c);
            const suit w_suit = suit_of<Variant>(t.winning_card);

            // Off-suit, non-trump card never wins.
            if (c_suit != t.lead_suit && c_suit != trump)
                return false;

            // Trump beats non-trump.
            if (c_suit == trump && w_suit != trump)
                return true;

            // Non-trump cannot beat trump.
            if (c_suit != trump && w_suit == trump)
                return false;

            // Same suit: compare.
            if constexpr (!deck_traits<Variant>::trump_reorders) {
                // Non-reordering variants (whist/bridge): higher index = higher rank.
                return c > t.winning_card;
            } else {
                // Reordering variants (jass): compare after variant remap.
                const card_mask pair = (card_mask{1} << c) | (card_mask{1} << t.winning_card);
                const card_mask remapped = deck_traits<Variant>::remap(pair, trump);
                return ops::msb_index(remapped) == static_cast<int>(c);
            }
        }

        /**
         * Play card c from seat s into the current trick.
         *
         * Updates hands, played mask, trick state, and the
         * incrementally-tracked winner.  When the trick
         * completes, resolves it and resets for the next one.
         */
        static void play_card(game_state<Variant>& state, seat s, card c) {
            assert(s == state.current_player && "playing out of turn");
            assert((state.hands[static_cast<uint8_t>(s)] & (card_mask{1} << c))
                   && "card not in hand");

            auto& t = state.trick;

            // ── update bitboards ──
            const card_mask bit = card_mask{1} << c;
            state.hands[static_cast<uint8_t>(s)] &= ~bit;
            state.played |= bit;
            t.mask       |= bit;

            // ── record per-seat card ──
            t.cards[static_cast<uint8_t>(s)] = c;
            ++t.num_played;

            // ── first card: establish lead ──
            if (t.num_played == 1) {
                t.lead_suit = suit_of<Variant>(c);
                t.lead_seat = s;
            }

            // ── update incremental winner ──
            if (beats_current(t, state.trump, c)) {
                t.current_winner = s;
                t.winning_card   = c;
            }

            state.current_player = next_seat(s);

            // ── trick complete → resolve ──
            if (t.complete()) {
                const seat winner = t.current_winner;
                state.tricks_won[partnership_of(winner)]++;
                state.tricks_remaining--;
                state.current_player = winner;

                t.reset();   // fast reset for next trick
            }
        }

        // ── legal moves ─────────────────────────────────────

        /**
         * Legal-move mask for the current player.
         *
         * Default rule: must follow the lead suit if possible,
         * otherwise any card is legal.
         */
        [[nodiscard]] static card_mask legal_moves(const game_state<Variant>& state) {
            const card_mask hand = state.current_hand_mask();

            if (state.trick.num_played == 0)
                return hand;   // leading: any card

            const card_mask follow = ops::cards_in_suit<Variant>(hand, state.trick.lead_suit);
            return follow ? follow : hand;
        }
    };

}

