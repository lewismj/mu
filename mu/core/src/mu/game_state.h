#pragma once

#include <array>
#include <cstdint>

#include <iosfwd>
#include "repr.h"

namespace mu {

    /**
     * Seat & Partnership definitions.
     */
    enum class seat : uint8_t { north = 0, east = 1, south = 2, west = 3 };

    std::ostream& operator<<(std::ostream& os, const seat s);

    constexpr seat next_seat(seat s) { return static_cast<seat>((static_cast<uint8_t>(s) + 1) & 3); }
    constexpr seat partner_of(seat s) { return static_cast<seat>((static_cast<uint8_t>(s) + 2) & 3); }
    constexpr uint8_t partnership_of(seat s) { return static_cast<uint8_t>(s) & 1; }

    inline constexpr uint8_t num_players = 4;
    inline constexpr uint8_t num_partnerships = 2;

    constexpr card no_card = 0xFF;

    /** Suit of a card index (variant-aware: 13 ranks for Whist/Bridge, 9 for Jass). */
    template<typename Variant>
    [[nodiscard]] constexpr suit suit_of(card c) {
        return static_cast<suit>(c / deck_traits<Variant>::num_ranks);
    }

    /** Rank index within its suit (variant-aware). */
    template<typename Variant>
    [[nodiscard]] constexpr uint8_t rank_of(card c) {
        return c % deck_traits<Variant>::num_ranks;
    }


    /**
     * Trick state — per-trick bookkeeping.
     *
     * Invariants (enforced by play_card):
     *   - num_played == 0  →  lead_suit / lead_seat are undefined.
     *   - num_played >= 1  →  lead_suit == suit_of(cards[lead_seat]),
     *                         lead_seat == seat that played first.
     *   - winning_card is an index (not a mask) for O(1) comparisons;
     *     masks represent sets, singletons are stored as indices.
     *   - lead_seat is stored (not derived from current_player − num_played)
     *     because this sits on the hot path and costs only 1 byte.
     */
    struct trick_state {
        card_mask mask = 0;             /* All cards played in this trick.  */
        std::array<card, num_players> cards = {no_card, no_card, no_card, no_card};
                                        /* Card played per seat (indexed by seat).
                                           4 bytes; enables observation encoding,
                                           debugging, and rule variants (Jass bonuses). */
        suit      lead_suit{};          /* Suit of the first card played.   */
        seat      lead_seat{};          /* Seat that led this trick.        */
        seat      current_winner{};     /* Seat currently winning.          */
        card      winning_card = no_card; /* Index of the highest-priority card so far. */
        uint8_t   num_played = 0;       /* 0..4                             */

        /** Trick complete? */
        [[nodiscard]] constexpr bool complete() const {
            return num_played == num_players;
        }

        /**
         * Fast reset for the next trick.
         * Only clears fields that are read before being written;
         * cards[], lead_suit, lead_seat, current_winner are all
         * overwritten before being read — no need to clear.
         */
        constexpr void reset() {
            mask         = 0;
            winning_card = no_card;
            num_played   = 0;
        }
    };


    /**
     * Full game state for a 4-player trick-taking game.
     */
    template<typename Variant>
    struct game_state {
        std::array<card_mask, num_players> hands{};     /* Remaining cards per seat.    */
        card_mask                          played{};    /* All cards played so far.     */
        trick_state                        trick{};     /* Current trick in progress.   */

        std::array<uint8_t, num_partnerships> tricks_won{};

        suit      trump{};
        seat      dealer{};
        seat      current_player{};
        uint8_t   tricks_remaining = num_cards<Variant> / num_players;

        [[nodiscard]] card_mask current_hand_mask() const {
            return hands[static_cast<uint8_t>(current_player)];
        }
    };


}