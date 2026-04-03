#pragma once

#include <array>
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <numeric>
#include <random>
#include <span>

#include <iosfwd>
#include "arch.h"

namespace mu {

    /**
     *  BitBoard-style bitmask for sets of cards.
     *
     *  52 standard cards mapped to bits 0–51:
     *
     *  0–12 Spades (bit 0 = 2♠, bit 12 = A♠)
     *  13–25 Hearts (bit 13 = 2♥, bit 25 = A♥)
     *  26–38 Diamonds (bit 26 = 2♦, bit 38 = A♦)
     *  39–51 Clubs (bit 39 = 2♣, bit 51 = A♣)
     *
     *  Jass:
     *      Schellen (Bells)    =  Diamonds
     *      Schilten (Shields)  = Spades
     *      Rosen (Roses)        = Hearts
     *      Eicheln (Acorns)    = Clubs
     */
    using card_mask = uint64_t;

    /**
     * Type for the card index. 0–51 for standard cards, or special values for jokers/wildcards if needed.
     */
    using card = uint8_t;

    /**
     * Enum. For the four standard suits in a deck of cards.
     *       Spades, Hearts, Diamonds, Clubs.
     */
    enum class suit: uint8_t {
        spades,
        hearts,
        diamonds,
        clubs
    };

    std::ostream& operator<<(std::ostream& os, const suit s);

    /**
     * Enum. Represents a maximum (and default) thirteen standard ranks in a deck of cards.
     */
    enum class rank: uint8_t {
        two,
        three,
        four,
        five,
        six,
        seven,
        eight,
        nine,
        ten,
        jack,
        queen,
        king,
        ace
    };

    /** Allow for future variants, e.g. Jass. */
    template<typename Variant> struct deck_traits;

    /* Game types. */
    struct whist {};
    struct bridge {};
    struct jass {};

    /* Deck traits for Whist. */
    template<>
    struct deck_traits<whist> {
        static constexpr bool has_trump = true;
        static constexpr bool trump_reorders = false;
        static constexpr int num_ranks = 13;
        static constexpr int num_suits = 4;

        [[nodiscard]] static constexpr card_mask remap(const card_mask m, const suit) {
            return m;
        }
    };

    /* Deck traits for Bridge.*/
    template<>
    struct deck_traits<bridge> : deck_traits<whist> {};

    /* Deck traits for Jass. */
    template<>
    struct deck_traits<jass> {
        static constexpr bool has_trump = true;
        static constexpr bool trump_reorders = true;
        static constexpr int num_ranks = 9;
        static constexpr int num_suits = 4;

        /** To be defined after remap_trump specialization. */
        [[nodiscard]] static constexpr card_mask remap(card_mask m, suit trump);
    };

    template<typename Variant>
    inline constexpr int num_cards = deck_traits<Variant>::num_ranks * deck_traits<Variant>::num_suits;

    /** Alias template for an array of cards for a specific variant. */
    template <typename Variant>
    using card_array = std::array<card, num_cards<Variant>>;

    /** 
     * Consteval generator for Jass trump reordering.
     * Maps standard bits [0..8] (6..A) to trump bits where J=bit 8, 9=bit 7, etc.
     */
    static constexpr int NELL_INDEX = 3;
    static constexpr int UNDER_INDEX = 5;

    consteval auto make_jass_trump_lut() {
        std::array<uint16_t, 512> t{};
        for (int i = 0; i < 512; ++i) {
            uint16_t r = 0;

            // Standard Jass rank indices (0–8):
            // 6, 7, 8, 9 (Nell), 10, Under (Jack), Ober (Queen), König (King), Ass (Ace)
            if (i & (1 << UNDER_INDEX)) r |= (1 << 8); // Under -> strongest
            if (i & (1 << NELL_INDEX)) r |= (1 << 7); // Nell -> 2nd strongest
            if (i & (1 << 8)) r |= (1 << 6); // As (Ace)
            if (i & (1 << 4)) r |= (1 << 5); // 10
            if (i & (1 << 7)) r |= (1 << 4); // König (King)
            if (i & (1 << 6)) r |= (1 << 3); // Ober (Queen)
            if (i & (1 << 2)) r |= (1 << 2); // 8
            if (i & (1 << 1)) r |= (1 << 1); // 7
            if (i & (1 << 0)) r |= (1 << 0); // 6

            t[i] = r;
        }
        return t;
    }

    inline constexpr auto jass_trump_lut = make_jass_trump_lut();

    static_assert(jass_trump_lut[1 << UNDER_INDEX] == (1 << 8), "Jass LUT: Under mapping incorrect");
    static_assert(jass_trump_lut[1 << NELL_INDEX] == (1 << 7), "Jass LUT: Nell mapping incorrect");

    namespace ops {

        /** Generic masks for each suit, adjusted for the variant's deck size. */
        template<typename Variant>
        constexpr card_mask suit_masks[4] = {
            ((1ull << deck_traits<Variant>::num_ranks) - 1) << (deck_traits<Variant>::num_ranks * 0),
            ((1ull << deck_traits<Variant>::num_ranks) - 1) << (deck_traits<Variant>::num_ranks * 1),
            ((1ull << deck_traits<Variant>::num_ranks) - 1) << (deck_traits<Variant>::num_ranks * 2),
            ((1ull << deck_traits<Variant>::num_ranks) - 1) << (deck_traits<Variant>::num_ranks * 3),
        };

        /** Least significant bit in mask. */
        [[nodiscard]] constexpr card_mask lsb(const card_mask m) {
            assert(m != 0);
            return m & (~m + 1);
        }

        /** Remove and return the lowest-set bit. */
        [[nodiscard]] constexpr card_mask pop_lsb(card_mask& m) {
            card_mask b = lsb(m);
            m &= m - 1;
            return b;
        }

        /** Index of the lowest set bit (0-based). Undefined if m == 0. */
        [[nodiscard]] constexpr int lsb_index(const card_mask m) {
            return std::countr_zero(m);
        }

        /** Index of the highest set bit (0-based). Undefined if m == 0. */
        [[nodiscard]] constexpr int msb_index(const card_mask m) {
            return 63 - std::countl_zero(m);
        }

        /** Return the number of cards in the mask. */
        [[nodiscard]] constexpr int popcount(const card_mask m) { return std::popcount(m); }

        /** True if no cards in the mask, false otherwise. */
        [[nodiscard]] constexpr bool is_empty(const card_mask m) { return m == 0; }

        /**
         * Returns a mask containing only the cards of the specified suit present in the input mask.
         */
        template<typename Variant>
        [[nodiscard]] constexpr card_mask cards_in_suit(const card_mask m, const suit s) {
            return m & suit_masks<Variant>[static_cast<int>(s)];
        }

        /**
         * Checks if the input mask contains any cards of the specified suit.
         */
        template<typename Variant>
        [[nodiscard]] constexpr bool has_suit(const card_mask m, const suit s) {
            return !is_empty(cards_in_suit<Variant>(m, s));
        }

        /**
         * Extract the rank pattern for a given suit from a hand.
         * With PEXT this is a single instruction; otherwise shift+mask.
         * Result: bit 0 = has the lowest rank, ...
         */
        template<typename Variant>
        [[nodiscard]] inline_always uint16_t suit_ranks(const card_mask hand, const suit s) {
#ifdef USE_PEXT
            return static_cast<uint16_t>(_pext_u64(hand, suit_masks<Variant>[static_cast<int>(s)]));
#else
            return static_cast<uint16_t>((hand >> (static_cast<int>(s) * deck_traits<Variant>::num_ranks)) & ((1ull << deck_traits<Variant>::num_ranks) - 1));
#endif
        }

        /**
         * Scatter a rank pattern back into a card_mask for suit s.
         * Inverse of suit_ranks():  ranks_to_cards(suit_ranks(h,s), s) == cards_in_suit(h,s)
         * With PDEP this is a single instruction; otherwise a shift.
         */
        template<typename Variant>
        [[nodiscard]] inline_always card_mask ranks_to_cards(const uint16_t ranks, const suit s) {
#ifdef USE_PEXT
            return _pdep_u64(ranks, suit_masks<Variant>[static_cast<int>(s)]);
#else
            return static_cast<card_mask>(ranks) << (static_cast<int>(s) * deck_traits<Variant>::num_ranks);
#endif
        }

        /** Highest card in the given suit within mask, or 0 if none. */
        template<typename Variant>
        [[nodiscard]] constexpr card_mask highest_in_suit(const card_mask m, const suit s) {
            const card_mask suited = cards_in_suit<Variant>(m, s);
            if (suited == 0) return 0;
            return 1ull << msb_index(suited);
        }

        /** Lowest card in the given suit within mask, or 0 if none. */
        template<typename Variant>
        [[nodiscard]] constexpr card_mask lowest_in_suit(const card_mask m, const suit s) {
            const card_mask suited = cards_in_suit<Variant>(m, s);
            if (suited == 0) return 0;
            return lsb(suited);
        }

        /**
         * Return a mask containing only the n-th (0-based) set bit of m.
         * With PDEP this is a single instruction; otherwise shift+pop_lsb loop.
         */
        [[nodiscard]] inline_always card_mask nth_set_bit(card_mask m, int n) {
#ifdef USE_PEXT
            return _pdep_u64(card_mask{1} << n, m);
#else
            for (int i = 0; i < n; ++i) pop_lsb(m);
            return lsb(m);
#endif
        }
    }

    /**
     * Default implementation of remap_trump (identity).
     * Only reordering variants like Jass need specialization.
     */
    template<typename Variant>
    [[nodiscard]] inline_always card_mask remap_trump(card_mask m, suit) {
        return m;
    }

    /** 
     * Specialization for Jass: reorders trump ranks for BitBoard operations.
     * This implementation uses PEXT/PDEP (if USE_PEXT is defined) via mu::ops.
     */
    template<>
    [[nodiscard]] inline_always card_mask remap_trump<jass>(card_mask m, suit trump) {
        uint16_t ranks = ops::suit_ranks<jass>(m, trump);
        ranks = jass_trump_lut[ranks];
        m &= ~ops::suit_masks<jass>[static_cast<int>(trump)];
        m |= ops::ranks_to_cards<jass>(ranks, trump);
        return m;
    }

    /** Member definition for deck_traits<jass>::remap. */
    [[nodiscard]] inline_always constexpr card_mask deck_traits<jass>::remap(const card_mask m, const suit trump) {
        return remap_trump<jass>(m, trump);
    }

    /** Build and shuffle a fresh card array for a given variant. */
    template <typename Variant, typename T>
    [[nodiscard]] card_array<Variant> shuffled(T& rng) {
        card_array<Variant> cards;

        std::iota(cards.begin(), cards.end(), card{0});
        std::shuffle(cards.begin(), cards.end(), rng); /* Assuming Fisher-Yates shuffle. */

        return cards;
    }

   /**
    * Deal deck into a number of hands (fused partial Fisher-Yates).
    *
    * @tparam Variant     The game variant (e.g., mu::bridge, mu::whist).
    * @tparam num_players The number of players to deal with.
    * @tparam T           The type of the random number generator.
    */
    template <typename Variant, int num_players, typename T>
    [[nodiscard]] std::array<card_mask, num_players> deal(T& rng) {
        constexpr int total_cards = num_cards<Variant>;

        static_assert(num_players > 0 && num_players <= total_cards,
                      "Number of players must be positive and not exceed deck size");
        static_assert(total_cards % num_players == 0,
                      "Deck does not divide evenly among players");

        constexpr int per_hand = total_cards / num_players;

        // Initialize a deck of card indices [0, 1, ..., N-1]
        card_array<Variant> cards;
        std::iota(cards.begin(), cards.end(), card{0});

        std::array<card_mask, num_players> hands{};
        int i = 0;
        std::uniform_int_distribution<int> dist;
        for (int p = 0; p < num_players; ++p) {
            for (int c = 0; c < per_hand; ++c, ++i) {
                // Partial Fisher-Yates: pick a random card from the remaining pool
                // and swap it into the current 'dealt' position.
                int j = dist(rng, std::uniform_int_distribution<int>::param_type{i, total_cards - 1});

                std::swap(cards[i], cards[j]);

                // Add the card to the player's bitmask hand.
                // Since cards[i] is 0-51, we shift a 64-bit mask.
                hands[p] |= (card_mask{1} << cards[i]);
            }
        }
        return hands;
    }

    /** 
     * Deal deck into a span of hands (fused partial Fisher-Yates). 
     * 
     * @tparam Variant        The game variant (e.g., mu::bridge, mu::whist).
     * @tparam T              The type of the random number generator.
     * @param rng             Random number generator.
     * @param hands           Span of card_masks to be filled.
     * @param cards_per_hand  Number of cards to deal to each hand.
     */
    template <typename Variant, typename T>
    void deal_span(T& rng, std::span<card_mask> hands, const int cards_per_hand) {
        constexpr int total_cards = num_cards<Variant>;
        
        // Ensure we aren't trying to deal more cards than exist in the deck.
        assert(static_cast<int>(hands.size()) * cards_per_hand <= total_cards
               && "total cards requested exceeds deck size");

        // Initialize a deck of card indices [0, 1, ..., N-1]
        card_array<Variant> cards;
        std::iota(cards.begin(), cards.end(), card{0});

        std::fill(hands.begin(), hands.end(), 0);

        int i = 0;
        std::uniform_int_distribution<int> dist;
        for (auto& h : hands) {
            for (int j = 0; j < cards_per_hand; ++j, ++i) {
                // Partial Fisher-Yates: pick a random card from the remaining pool
                // and swap it into the current 'dealt' position.
                int k = dist(rng, std::uniform_int_distribution<int>::param_type{i, total_cards - 1});
                
                std::swap(cards[i], cards[k]);
                
                // Add the card to the current hand bitmask.
                // We use card_mask{1} to ensure 64-bit shifting.
                h |= (card_mask{1} << cards[i]);
            }
        }
    }


 template<typename Variant, int num_players = 4>
 [[nodiscard]] inline_always card_mask trick_winner(
        card_mask trick,     // num_players-card trick (bitmask)
        suit lead,           // lead suit
        suit trump           // trump suit
    ) {
        using Traits = deck_traits<Variant>;

        assert(ops::popcount(trick) == num_players
               && "trick must contain exactly num_players cards");

        // --- Variant-specific remap ---
        // Use Traits::remap, which is already specialized for Jass
        trick = Traits::remap(trick, trump);

        // --- Extract lead-suit cards ---
        const card_mask lead_cards = trick & ops::suit_masks<Variant>[static_cast<int>(lead)];

        // --- When lead IS trump, there is only one relevant suit ---
        if (lead == trump) {
            if (lead_cards == 0) return 0;
            return card_mask{1} << ops::msb_index(lead_cards);
        }

        // --- Extract trump-suit cards (only when lead != trump) ---
        const card_mask trump_cards = trick & ops::suit_masks<Variant>[static_cast<int>(trump)];

        // --- Branchless selection ---
        // If any trump was played, highest trump wins; otherwise highest in lead suit.
        const card_mask use_trump = -(trump_cards != 0);  // all 1s if trump present, else 0
        const card_mask candidates = (trump_cards & use_trump) | (lead_cards & ~use_trump);

        // --- Return highest card as bitmask ---
        if (candidates == 0) return 0;
        return card_mask{1} << ops::msb_index(candidates);
    }

}
