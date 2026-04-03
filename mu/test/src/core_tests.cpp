#include <boost/test/unit_test.hpp>
#include <mu.h>


using namespace mu;

BOOST_AUTO_TEST_SUITE(core_tests)

BOOST_AUTO_TEST_CASE(test_beats_current_no_trump) {
    // Whist: Higher index = higher rank.
    // Spades: 0-12 (2-A)
    // Hearts: 13-25 (2-A)
    
    trick_state t;
    t.num_played = 1;
    t.lead_suit = suit::spades;
    t.lead_seat = seat::north;
    t.winning_card = 5; // 7 of Spades
    t.current_winner = seat::north;
    
    // Higher rank in lead suit beats current
    BOOST_CHECK(whist_rules::beats_current(t, suit::hearts, 10)); // Queen of Spades

    // Lower rank in lead suit does not beat current
    BOOST_CHECK(!whist_rules::beats_current(t, suit::hearts, 2)); // 4 of Spades
    
    // Off-suit non-trump does not beat current
    BOOST_CHECK(!whist_rules::beats_current(t, suit::hearts, 26)); // 2 of Diamonds
}

BOOST_AUTO_TEST_CASE(test_beats_current_with_trump) {
    trick_state t;
    t.num_played = 1;
    t.lead_suit = suit::spades;
    t.lead_seat = seat::north;
    t.winning_card = 12; // Ace of Spades
    t.current_winner = seat::north;
    
    // Trump beats non-trump (even if non-trump is higher rank)
    BOOST_CHECK(whist_rules::beats_current(t, suit::hearts, 13)); // 2 of Hearts (trump)
    
    // Higher trump beats lower trump
    t.winning_card = 13; // 2 of Hearts (trump)
    BOOST_CHECK(whist_rules::beats_current(t, suit::hearts, 25)); // Ace of Hearts (trump)
    
    // Lower trump does not beat higher trump
    t.winning_card = 20; // 9 of Hearts (trump)
    BOOST_CHECK(!whist_rules::beats_current(t, suit::hearts, 13)); // 2 of Hearts (trump)
    
    // Non-trump cannot beat trump
    BOOST_CHECK(!whist_rules::beats_current(t, suit::hearts, 12)); // Ace of Spades
}

BOOST_AUTO_TEST_CASE(test_legal_moves) {
    game_state<whist> state;
    state.current_player = seat::east;
    state.trump = suit::diamonds;
    
    // Hand: 2S, 3S, 2H, 2D
    state.hands[1] = (1ull << 0) | (1ull << 1) | (1ull << 13) | (1ull << 26);
    
    // Leading: any card is legal
    state.trick.num_played = 0;
    BOOST_CHECK_EQUAL(whist_rules::legal_moves(state), state.hands[1]);
    
    // Following Spades: must play Spades
    state.trick.num_played = 1;
    state.trick.lead_suit = suit::spades;
    card_mask expected_follow = (1ull << 0) | (1ull << 1);
    BOOST_CHECK_EQUAL(whist_rules::legal_moves(state), expected_follow);
    
    // Void in lead suit: any card is legal
    state.trick.lead_suit = suit::clubs;
    BOOST_CHECK_EQUAL(whist_rules::legal_moves(state), state.hands[1]);
}

BOOST_AUTO_TEST_CASE(test_play_card_and_trick_resolution) {
    game_state<whist> state;
    state.trump = suit::hearts;
    state.current_player = seat::north;
    state.tricks_remaining = 13;
    
    // North: AS (12)
    // East: 2S (0)
    // South: 2H (13) - Trump!
    // West: KS (11)
    state.hands[0] = (1ull << 12);
    state.hands[1] = (1ull << 0);
    state.hands[2] = (1ull << 13);
    state.hands[3] = (1ull << 11);
    
    whist_rules::play_card(state, seat::north, 12);
    BOOST_CHECK_EQUAL(state.trick.num_played, 1);
    BOOST_CHECK_EQUAL(state.trick.current_winner, seat::north);
    BOOST_CHECK_EQUAL(state.current_player, seat::east);
    
    whist_rules::play_card(state, seat::east, 0);
    BOOST_CHECK_EQUAL(state.trick.num_played, 2);
    BOOST_CHECK_EQUAL(state.trick.current_winner, seat::north);
    
    whist_rules::play_card(state, seat::south, 13);
    BOOST_CHECK_EQUAL(state.trick.num_played, 3);
    BOOST_CHECK_EQUAL(state.trick.current_winner, seat::south); // Trump wins
    
    whist_rules::play_card(state, seat::west, 11);
    
    // Trick should be complete and resolved
    BOOST_CHECK_EQUAL(state.trick.num_played, 0); // Reset
    BOOST_CHECK_EQUAL(state.tricks_won[partnership_of(seat::south)], 1);
    BOOST_CHECK_EQUAL(state.tricks_remaining, 12);
    BOOST_CHECK_EQUAL(state.current_player, seat::south); // Winner leads next
}

BOOST_AUTO_TEST_SUITE_END()

// ── representation utilities ──────────────────────────────

BOOST_AUTO_TEST_SUITE(repr_tests)

BOOST_AUTO_TEST_CASE(test_suit_of) {
    // Spades: 0-12
    BOOST_CHECK(suit_of<whist>(0)  == suit::spades);
    BOOST_CHECK(suit_of<whist>(12) == suit::spades);
    // Hearts: 13-25
    BOOST_CHECK(suit_of<whist>(13) == suit::hearts);
    BOOST_CHECK(suit_of<whist>(25) == suit::hearts);
    // Diamonds: 26-38
    BOOST_CHECK(suit_of<whist>(26) == suit::diamonds);
    BOOST_CHECK(suit_of<whist>(38) == suit::diamonds);
    // Clubs: 39-51
    BOOST_CHECK(suit_of<whist>(39) == suit::clubs);
    BOOST_CHECK(suit_of<whist>(51) == suit::clubs);
}

BOOST_AUTO_TEST_CASE(test_rank_of) {
    BOOST_CHECK_EQUAL(rank_of<whist>(0),  0);  // 2 of Spades
    BOOST_CHECK_EQUAL(rank_of<whist>(12), 12); // Ace of Spades
    BOOST_CHECK_EQUAL(rank_of<whist>(13), 0);  // 2 of Hearts
    BOOST_CHECK_EQUAL(rank_of<whist>(25), 12); // Ace of Hearts
    BOOST_CHECK_EQUAL(rank_of<whist>(39), 0);  // 2 of Clubs
    BOOST_CHECK_EQUAL(rank_of<whist>(51), 12); // Ace of Clubs
}

BOOST_AUTO_TEST_CASE(test_num_cards) {
    BOOST_CHECK_EQUAL(num_cards<whist>, 52);
}

BOOST_AUTO_TEST_CASE(test_popcount) {
    BOOST_CHECK_EQUAL(ops::popcount(card_mask{0}), 0);
    BOOST_CHECK_EQUAL(ops::popcount(card_mask{1}), 1);
    BOOST_CHECK_EQUAL(ops::popcount(card_mask{0b1010101}), 4);
    BOOST_CHECK_EQUAL(ops::popcount((card_mask{1} << 52) - 1), 52);
}

BOOST_AUTO_TEST_CASE(test_is_empty) {
    BOOST_CHECK(ops::is_empty(card_mask{0}));
    BOOST_CHECK(!ops::is_empty(card_mask{1}));
}

BOOST_AUTO_TEST_CASE(test_lsb_and_index) {
    card_mask m = (card_mask{1} << 5) | (card_mask{1} << 20);
    BOOST_CHECK_EQUAL(ops::lsb(m), card_mask{1} << 5);
    BOOST_CHECK_EQUAL(ops::lsb_index(m), 5);
}

BOOST_AUTO_TEST_CASE(test_msb_index) {
    card_mask m = (card_mask{1} << 5) | (card_mask{1} << 20);
    BOOST_CHECK_EQUAL(ops::msb_index(m), 20);
}

BOOST_AUTO_TEST_CASE(test_pop_lsb) {
    card_mask m = (card_mask{1} << 3) | (card_mask{1} << 7) | (card_mask{1} << 12);
    card_mask b = ops::pop_lsb(m);
    BOOST_CHECK_EQUAL(b, card_mask{1} << 3);
    BOOST_CHECK_EQUAL(m, (card_mask{1} << 7) | (card_mask{1} << 12));
}

BOOST_AUTO_TEST_CASE(test_cards_in_suit) {
    // Hand with 2S, AS, 2H, AD
    card_mask hand = (card_mask{1} << 0) | (card_mask{1} << 12) |
                     (card_mask{1} << 13) | (card_mask{1} << 38);

    card_mask spades = ops::cards_in_suit<whist>(hand, suit::spades);
    BOOST_CHECK_EQUAL(spades, (card_mask{1} << 0) | (card_mask{1} << 12));

    card_mask hearts = ops::cards_in_suit<whist>(hand, suit::hearts);
    BOOST_CHECK_EQUAL(hearts, card_mask{1} << 13);

    card_mask clubs = ops::cards_in_suit<whist>(hand, suit::clubs);
    BOOST_CHECK_EQUAL(clubs, card_mask{0});
}

BOOST_AUTO_TEST_CASE(test_has_suit) {
    card_mask hand = (card_mask{1} << 0) | (card_mask{1} << 13);
    BOOST_CHECK(ops::has_suit<whist>(hand, suit::spades));
    BOOST_CHECK(ops::has_suit<whist>(hand, suit::hearts));
    BOOST_CHECK(!ops::has_suit<whist>(hand, suit::diamonds));
    BOOST_CHECK(!ops::has_suit<whist>(hand, suit::clubs));
}

BOOST_AUTO_TEST_CASE(test_highest_and_lowest_in_suit) {
    // Hand: 3S (1), 7S (5), AS (12)
    card_mask hand = (card_mask{1} << 1) | (card_mask{1} << 5) | (card_mask{1} << 12);

    BOOST_CHECK_EQUAL(ops::highest_in_suit<whist>(hand, suit::spades), card_mask{1} << 12);
    BOOST_CHECK_EQUAL(ops::lowest_in_suit<whist>(hand, suit::spades), card_mask{1} << 1);

    // No diamonds in hand
    BOOST_CHECK_EQUAL(ops::highest_in_suit<whist>(hand, suit::diamonds), card_mask{0});
    BOOST_CHECK_EQUAL(ops::lowest_in_suit<whist>(hand, suit::diamonds), card_mask{0});
}

BOOST_AUTO_TEST_CASE(test_suit_ranks_and_ranks_to_cards) {
    // Hand: 2S (0), 5S (3), AS (12)
    card_mask hand = (card_mask{1} << 0) | (card_mask{1} << 3) | (card_mask{1} << 12);
    uint16_t ranks = ops::suit_ranks<whist>(hand, suit::spades);
    BOOST_CHECK_EQUAL(ranks, (1 << 0) | (1 << 3) | (1 << 12));

    // Round-trip: ranks_to_cards should give back the same spade cards
    card_mask rebuilt = ops::ranks_to_cards<whist>(ranks, suit::spades);
    BOOST_CHECK_EQUAL(rebuilt, hand);

    // Empty suit yields 0
    BOOST_CHECK_EQUAL(ops::suit_ranks<whist>(hand, suit::hearts), 0);
}

BOOST_AUTO_TEST_CASE(test_nth_set_bit) {
    // Mask with bits 2, 5, 10 set
    card_mask m = (card_mask{1} << 2) | (card_mask{1} << 5) | (card_mask{1} << 10);
    BOOST_CHECK_EQUAL(ops::nth_set_bit(m, 0), card_mask{1} << 2);
    BOOST_CHECK_EQUAL(ops::nth_set_bit(m, 1), card_mask{1} << 5);
    BOOST_CHECK_EQUAL(ops::nth_set_bit(m, 2), card_mask{1} << 10);
}

BOOST_AUTO_TEST_SUITE_END()


// ── game state helpers ────────────────────────────────────

BOOST_AUTO_TEST_SUITE(game_state_tests)

BOOST_AUTO_TEST_CASE(test_next_seat) {
    BOOST_CHECK(next_seat(seat::north) == seat::east);
    BOOST_CHECK(next_seat(seat::east)  == seat::south);
    BOOST_CHECK(next_seat(seat::south) == seat::west);
    BOOST_CHECK(next_seat(seat::west)  == seat::north);  // wraps around
}

BOOST_AUTO_TEST_CASE(test_partner_of) {
    BOOST_CHECK(partner_of(seat::north) == seat::south);
    BOOST_CHECK(partner_of(seat::east)  == seat::west);
    BOOST_CHECK(partner_of(seat::south) == seat::north);
    BOOST_CHECK(partner_of(seat::west)  == seat::east);
}

BOOST_AUTO_TEST_CASE(test_partnership_of) {
    // North (0) & South (2) → partnership 0
    BOOST_CHECK_EQUAL(partnership_of(seat::north), 0);
    BOOST_CHECK_EQUAL(partnership_of(seat::south), 0);
    // East (1) & West (3) → partnership 1
    BOOST_CHECK_EQUAL(partnership_of(seat::east), 1);
    BOOST_CHECK_EQUAL(partnership_of(seat::west), 1);
}

BOOST_AUTO_TEST_CASE(test_trick_state_complete) {
    trick_state t;
    BOOST_CHECK(!t.complete());  // num_played == 0
    t.num_played = 3;
    BOOST_CHECK(!t.complete());
    t.num_played = 4;
    BOOST_CHECK(t.complete());
}

BOOST_AUTO_TEST_CASE(test_trick_state_reset) {
    trick_state t;
    t.mask = 0xFF;
    t.winning_card = 10;
    t.num_played = 4;

    t.reset();
    BOOST_CHECK_EQUAL(t.mask, card_mask{0});
    BOOST_CHECK_EQUAL(t.winning_card, no_card);
    BOOST_CHECK_EQUAL(t.num_played, 0);
}

BOOST_AUTO_TEST_SUITE_END()


// ── additional beats_current edge cases ───────────────────

BOOST_AUTO_TEST_SUITE(beats_current_extra_tests)

BOOST_AUTO_TEST_CASE(test_first_card_always_wins) {
    trick_state t;
    t.num_played = 0;
    t.winning_card = no_card;  // no card played yet

    // Any card should beat "nothing"
    BOOST_CHECK(whist_rules::beats_current(t, suit::hearts, 0));   // 2 of Spades
    BOOST_CHECK(whist_rules::beats_current(t, suit::hearts, 51));  // Ace of Clubs
}

BOOST_AUTO_TEST_CASE(test_same_lead_suit_comparison_no_trump) {
    // Lead suit is diamonds, trump is clubs (irrelevant)
    trick_state t;
    t.num_played = 1;
    t.lead_suit = suit::diamonds;
    t.lead_seat = seat::north;
    t.winning_card = 30; // 6♦ (rank 4)
    t.current_winner = seat::north;

    // Higher diamond beats
    BOOST_CHECK(whist_rules::beats_current(t, suit::clubs, 38));  // A♦
    // Lower diamond does not
    BOOST_CHECK(!whist_rules::beats_current(t, suit::clubs, 26)); // 2♦
    // Equal card (same card) does not beat itself
    BOOST_CHECK(!whist_rules::beats_current(t, suit::clubs, 30));
}

BOOST_AUTO_TEST_CASE(test_off_suit_non_trump_never_wins) {
    // Lead spades, trump hearts
    trick_state t;
    t.num_played = 1;
    t.lead_suit = suit::spades;
    t.lead_seat = seat::north;
    t.winning_card = 0;  // 2♠
    t.current_winner = seat::north;

    // Diamonds and clubs (off-suit, non-trump) never beat
    BOOST_CHECK(!whist_rules::beats_current(t, suit::hearts, 38)); // A♦
    BOOST_CHECK(!whist_rules::beats_current(t, suit::hearts, 51)); // A♣
}

BOOST_AUTO_TEST_SUITE_END()


// ── play_card bitboard bookkeeping ────────────────────────

BOOST_AUTO_TEST_SUITE(play_card_tests)

BOOST_AUTO_TEST_CASE(test_play_card_updates_bitboards) {
    game_state<whist> state;
    state.trump = suit::hearts;
    state.current_player = seat::north;
    state.tricks_remaining = 13;

    // Give North a single card: A♠ (12)
    state.hands[0] = (card_mask{1} << 12);
    // Give remaining players cards for a full trick
    state.hands[1] = (card_mask{1} << 0);   // East: 2♠
    state.hands[2] = (card_mask{1} << 1);   // South: 3♠
    state.hands[3] = (card_mask{1} << 2);   // West: 4♠

    whist_rules::play_card(state, seat::north, 12);

    // Card removed from hand
    BOOST_CHECK_EQUAL(state.hands[0], card_mask{0});
    // Card added to played mask
    BOOST_CHECK(state.played & (card_mask{1} << 12));
    // Card added to trick mask
    BOOST_CHECK(state.trick.mask & (card_mask{1} << 12));
    // Per-seat card recorded
    BOOST_CHECK_EQUAL(state.trick.cards[0], 12);
}

BOOST_AUTO_TEST_CASE(test_play_card_lead_established) {
    game_state<whist> state;
    state.trump = suit::clubs;
    state.current_player = seat::east;
    state.tricks_remaining = 13;

    state.hands[1] = (card_mask{1} << 26); // East: 2♦
    state.hands[2] = (card_mask{1} << 27); // South: 3♦
    state.hands[3] = (card_mask{1} << 28); // West: 4♦
    state.hands[0] = (card_mask{1} << 29); // North: 5♦

    whist_rules::play_card(state, seat::east, 26);

    BOOST_CHECK(state.trick.lead_suit == suit::diamonds);
    BOOST_CHECK(state.trick.lead_seat == seat::east);
}

BOOST_AUTO_TEST_CASE(test_non_trump_trick_winner_leads_next) {
    // Full trick with no trump: highest lead-suit card wins
    game_state<whist> state;
    state.trump = suit::clubs;
    state.current_player = seat::north;
    state.tricks_remaining = 13;

    state.hands[0] = (card_mask{1} << 5);   // North: 7♠
    state.hands[1] = (card_mask{1} << 11);  // East: K♠
    state.hands[2] = (card_mask{1} << 3);   // South: 5♠
    state.hands[3] = (card_mask{1} << 8);   // West: 10♠

    whist_rules::play_card(state, seat::north, 5);
    whist_rules::play_card(state, seat::east, 11);
    whist_rules::play_card(state, seat::south, 3);
    whist_rules::play_card(state, seat::west, 8);

    // East played K♠ (highest), should win and lead next
    BOOST_CHECK_EQUAL(state.current_player, seat::east);
    BOOST_CHECK_EQUAL(state.tricks_won[partnership_of(seat::east)], 1);
    BOOST_CHECK_EQUAL(state.tricks_remaining, 12);
    // Trick reset
    BOOST_CHECK_EQUAL(state.trick.num_played, 0);
}

BOOST_AUTO_TEST_SUITE_END()


// ── multi-trick sequence ──────────────────────────────────

BOOST_AUTO_TEST_SUITE(multi_trick_tests)

BOOST_AUTO_TEST_CASE(test_two_consecutive_tricks) {
    game_state<whist> state;
    state.trump = suit::hearts;
    state.current_player = seat::north;
    state.tricks_remaining = 13;

    // Give each player two cards
    // North: A♠ (12), 2♦ (26)
    state.hands[0] = (card_mask{1} << 12) | (card_mask{1} << 26);
    // East: K♠ (11), 3♦ (27)
    state.hands[1] = (card_mask{1} << 11) | (card_mask{1} << 27);
    // South: Q♠ (10), 4♦ (28)
    state.hands[2] = (card_mask{1} << 10) | (card_mask{1} << 28);
    // West: J♠ (9), 5♦ (29)
    state.hands[3] = (card_mask{1} << 9) | (card_mask{1} << 29);

    // Trick 1: all spades, North leads A♠ → North wins
    whist_rules::play_card(state, seat::north, 12);
    whist_rules::play_card(state, seat::east, 11);
    whist_rules::play_card(state, seat::south, 10);
    whist_rules::play_card(state, seat::west, 9);

    BOOST_CHECK_EQUAL(state.tricks_won[partnership_of(seat::north)], 1);
    BOOST_CHECK_EQUAL(state.current_player, seat::north); // North won, leads again

    // Trick 2: North leads 2♦, others follow diamonds
    whist_rules::play_card(state, seat::north, 26);
    whist_rules::play_card(state, seat::east, 27);
    whist_rules::play_card(state, seat::south, 28);
    whist_rules::play_card(state, seat::west, 29);

    // West played 5♦ (highest), wins trick 2
    BOOST_CHECK_EQUAL(state.tricks_won[partnership_of(seat::west)], 1);
    BOOST_CHECK_EQUAL(state.tricks_won[partnership_of(seat::north)], 1);
    BOOST_CHECK_EQUAL(state.tricks_remaining, 11);
    BOOST_CHECK_EQUAL(state.current_player, seat::west);
}

BOOST_AUTO_TEST_SUITE_END()


// ── legal moves extra cases ───────────────────────────────

BOOST_AUTO_TEST_SUITE(legal_moves_extra_tests)

BOOST_AUTO_TEST_CASE(test_legal_moves_single_card_follows_suit) {
    game_state<whist> state;
    state.current_player = seat::south;
    state.trump = suit::clubs;

    // South has a single spade
    state.hands[2] = (card_mask{1} << 7);  // 9♠

    state.trick.num_played = 1;
    state.trick.lead_suit = suit::spades;

    BOOST_CHECK_EQUAL(whist_rules::legal_moves(state), card_mask{1} << 7);
}

BOOST_AUTO_TEST_CASE(test_legal_moves_entire_hand_is_lead_suit) {
    game_state<whist> state;
    state.current_player = seat::west;
    state.trump = suit::diamonds;

    // West: all hearts (13-25)
    card_mask all_hearts = ops::suit_masks<whist>[static_cast<int>(suit::hearts)];
    state.hands[3] = all_hearts;

    state.trick.num_played = 1;
    state.trick.lead_suit = suit::hearts;

    BOOST_CHECK_EQUAL(whist_rules::legal_moves(state), all_hearts);
}

BOOST_AUTO_TEST_CASE(test_legal_moves_void_plays_anything) {
    game_state<whist> state;
    state.current_player = seat::east;
    state.trump = suit::spades;

    // East: only diamonds and clubs, no hearts
    card_mask hand = (card_mask{1} << 26) | (card_mask{1} << 39);
    state.hands[1] = hand;

    state.trick.num_played = 1;
    state.trick.lead_suit = suit::hearts;

    // Void in hearts → any card is legal
    BOOST_CHECK_EQUAL(whist_rules::legal_moves(state), hand);
}

BOOST_AUTO_TEST_SUITE_END()


// ── deal validity ─────────────────────────────────────────

BOOST_AUTO_TEST_SUITE(deal_tests)

BOOST_AUTO_TEST_CASE(test_deal_produces_valid_hands) {
    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);

    // Each hand has 13 cards
    for (int i = 0; i < 4; ++i) {
        BOOST_CHECK_EQUAL(ops::popcount(hands[i]), 13);
    }

    // Hands are pairwise disjoint
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            BOOST_CHECK_EQUAL(hands[i] & hands[j], card_mask{0});
        }
    }

    // Union of all hands is the full 52-card deck
    card_mask all = hands[0] | hands[1] | hands[2] | hands[3];
    BOOST_CHECK_EQUAL(all, (card_mask{1} << 52) - 1);
}

BOOST_AUTO_TEST_CASE(test_deal_span_produces_valid_hands) {
    std::mt19937 rng(123);
    std::array<card_mask, 4> hands{};
    deal_span<whist>(rng, hands, 13);

    for (int i = 0; i < 4; ++i) {
        BOOST_CHECK_EQUAL(ops::popcount(hands[i]), 13);
    }

    card_mask all = hands[0] | hands[1] | hands[2] | hands[3];
    BOOST_CHECK_EQUAL(all, (card_mask{1} << 52) - 1);
}

BOOST_AUTO_TEST_SUITE_END()


// ── trick_winner free function ────────────────────────────

BOOST_AUTO_TEST_SUITE(trick_winner_tests)

BOOST_AUTO_TEST_CASE(test_trick_winner_lead_suit_wins) {
    // 4 spades: 2♠, 5♠, K♠, 9♠ — K♠ (11) should win
    card_mask trick = (card_mask{1} << 0) | (card_mask{1} << 3) |
                      (card_mask{1} << 11) | (card_mask{1} << 7);

    card_mask winner = trick_winner<whist>(trick, suit::spades, suit::hearts);
    BOOST_CHECK_EQUAL(winner, card_mask{1} << 11);
}

BOOST_AUTO_TEST_CASE(test_trick_winner_trump_beats_lead) {
    // 3 spades + 1 trump (heart): 2♠, A♠, K♠, 2♥ — 2♥ wins (trump)
    card_mask trick = (card_mask{1} << 0) | (card_mask{1} << 12) |
                      (card_mask{1} << 11) | (card_mask{1} << 13);

    card_mask winner = trick_winner<whist>(trick, suit::spades, suit::hearts);
    BOOST_CHECK_EQUAL(winner, card_mask{1} << 13); // 2♥ (lowest trump still wins)
}

BOOST_AUTO_TEST_CASE(test_trick_winner_highest_trump_wins) {
    // 2 spades + 2 trumps: 2♠, 3♠, 2♥, A♥ — A♥ wins
    card_mask trick = (card_mask{1} << 0) | (card_mask{1} << 1) |
                      (card_mask{1} << 13) | (card_mask{1} << 25);

    card_mask winner = trick_winner<whist>(trick, suit::spades, suit::hearts);
    BOOST_CHECK_EQUAL(winner, card_mask{1} << 25); // A♥
}

BOOST_AUTO_TEST_CASE(test_trick_winner_lead_is_trump) {
    // Lead suit IS trump (hearts): highest heart wins
    card_mask trick = (card_mask{1} << 13) | (card_mask{1} << 20) |
                      (card_mask{1} << 25) | (card_mask{1} << 14);

    card_mask winner = trick_winner<whist>(trick, suit::hearts, suit::hearts);
    BOOST_CHECK_EQUAL(winner, card_mask{1} << 25); // A♥
}

BOOST_AUTO_TEST_SUITE_END()


// ── rollout ───────────────────────────────────────────────

BOOST_AUTO_TEST_SUITE(rollout_tests)

BOOST_AUTO_TEST_CASE(test_rollout_all_tricks_accounted_for) {
    std::mt19937 rng(99);

    game_state<whist> state;
    state.trump = suit::diamonds;
    state.current_player = seat::north;
    state.dealer = seat::west;
    state.tricks_remaining = 13;

    auto hands = deal<whist, 4>(rng);
    for (int i = 0; i < 4; ++i)
        state.hands[i] = hands[i];

    auto result = rollout<whist>(state, rng);

    // Total tricks won by both partnerships must equal 13
    BOOST_CHECK_EQUAL(result[0] + result[1], 13);
}

BOOST_AUTO_TEST_CASE(test_rollout_deterministic_with_same_seed) {
    auto run = [](int seed) {
        std::mt19937 rng(seed);
        game_state<whist> state;
        state.trump = suit::clubs;
        state.current_player = seat::north;
        state.tricks_remaining = 13;

        auto hands = deal<whist, 4>(rng);
        for (int i = 0; i < 4; ++i)
            state.hands[i] = hands[i];

        std::mt19937 rollout_rng(seed + 1000);
        return rollout<whist>(state, rollout_rng);
    };

    auto r1 = run(77);
    auto r2 = run(77);
    BOOST_CHECK_EQUAL(r1[0], r2[0]);
    BOOST_CHECK_EQUAL(r1[1], r2[1]);
}

BOOST_AUTO_TEST_CASE(test_rollout_multiple_seeds_all_valid) {
    // Run several rollouts with distinct seeds; every result must total 13 tricks
    for (int seed = 0; seed < 20; ++seed) {
        std::mt19937 rng(seed);
        game_state<whist> state;
        state.trump = static_cast<suit>(seed % 4);
        state.current_player = static_cast<seat>(seed % 4);
        state.tricks_remaining = 13;

        auto hands = deal<whist, 4>(rng);
        for (int i = 0; i < 4; ++i)
            state.hands[i] = hands[i];

        auto result = rollout<whist>(state, rng);
        BOOST_CHECK_EQUAL(result[0] + result[1], 13);
    }
}

BOOST_AUTO_TEST_CASE(test_rollout_from_mid_game) {
    // Start a partial game (1 trick already resolved), then rollout the rest
    game_state<whist> state;
    state.trump = suit::hearts;
    state.current_player = seat::north;
    state.tricks_remaining = 13;

    std::mt19937 rng(500);
    auto hands = deal<whist, 4>(rng);
    for (int i = 0; i < 4; ++i)
        state.hands[i] = hands[i];

    // Play one full trick manually
    for (int p = 0; p < 4; ++p) {
        card_mask moves = whist_rules::legal_moves(state);
        card c = static_cast<card>(ops::lsb_index(moves)); // pick lowest legal card
        whist_rules::play_card(state, state.current_player, c);
    }

    BOOST_CHECK_EQUAL(state.tricks_remaining, 12);

    // Rollout remaining 12 tricks
    auto result = rollout<whist>(state, rng);
    BOOST_CHECK_EQUAL(result[0] + result[1], 13); // 1 pre-played + 12 from rollout
}

BOOST_AUTO_TEST_SUITE_END()


// ── deck_traits & suit_masks compile-time constants ───────

BOOST_AUTO_TEST_SUITE(deck_traits_tests)

BOOST_AUTO_TEST_CASE(test_whist_deck_traits) {
    BOOST_CHECK_EQUAL(deck_traits<whist>::num_ranks, 13);
    BOOST_CHECK_EQUAL(deck_traits<whist>::num_suits, 4);
    BOOST_CHECK(deck_traits<whist>::has_trump);
    BOOST_CHECK(!deck_traits<whist>::trump_reorders);
}

BOOST_AUTO_TEST_CASE(test_bridge_deck_traits_same_as_whist) {
    BOOST_CHECK_EQUAL(deck_traits<bridge>::num_ranks, 13);
    BOOST_CHECK_EQUAL(deck_traits<bridge>::num_suits, 4);
    BOOST_CHECK(!deck_traits<bridge>::trump_reorders);
    BOOST_CHECK_EQUAL(num_cards<bridge>, 52);
}

BOOST_AUTO_TEST_CASE(test_jass_deck_traits) {
    BOOST_CHECK_EQUAL(deck_traits<jass>::num_ranks, 9);
    BOOST_CHECK_EQUAL(deck_traits<jass>::num_suits, 4);
    BOOST_CHECK(deck_traits<jass>::has_trump);
    BOOST_CHECK(deck_traits<jass>::trump_reorders);
    BOOST_CHECK_EQUAL(num_cards<jass>, 36);
}

BOOST_AUTO_TEST_CASE(test_suit_masks_whist) {
    // Spades mask: bits 0..12
    BOOST_CHECK_EQUAL(ops::suit_masks<whist>[0], (card_mask{1} << 13) - 1);
    // Hearts mask: bits 13..25
    BOOST_CHECK_EQUAL(ops::suit_masks<whist>[1], ((card_mask{1} << 13) - 1) << 13);
    // Diamonds mask: bits 26..38
    BOOST_CHECK_EQUAL(ops::suit_masks<whist>[2], ((card_mask{1} << 13) - 1) << 26);
    // Clubs mask: bits 39..51
    BOOST_CHECK_EQUAL(ops::suit_masks<whist>[3], ((card_mask{1} << 13) - 1) << 39);

    // All four masks together = full 52-card deck
    card_mask all = ops::suit_masks<whist>[0] | ops::suit_masks<whist>[1]
                  | ops::suit_masks<whist>[2] | ops::suit_masks<whist>[3];
    BOOST_CHECK_EQUAL(all, (card_mask{1} << 52) - 1);
}

BOOST_AUTO_TEST_CASE(test_suit_masks_jass) {
    // Jass: 9 ranks per suit
    BOOST_CHECK_EQUAL(ops::suit_masks<jass>[0], (card_mask{1} << 9) - 1);
    card_mask all_jass = ops::suit_masks<jass>[0] | ops::suit_masks<jass>[1]
                       | ops::suit_masks<jass>[2] | ops::suit_masks<jass>[3];
    BOOST_CHECK_EQUAL(all_jass, (card_mask{1} << 36) - 1);
}

BOOST_AUTO_TEST_CASE(test_no_card_constant) {
    BOOST_CHECK_EQUAL(no_card, 0xFF);
}

BOOST_AUTO_TEST_SUITE_END()


// ── bit ops edge cases ────────────────────────────────────

BOOST_AUTO_TEST_SUITE(bit_ops_edge_tests)

BOOST_AUTO_TEST_CASE(test_lsb_index_single_bit) {
    BOOST_CHECK_EQUAL(ops::lsb_index(card_mask{1} << 0), 0);
    BOOST_CHECK_EQUAL(ops::lsb_index(card_mask{1} << 51), 51);
}

BOOST_AUTO_TEST_CASE(test_msb_index_single_bit) {
    BOOST_CHECK_EQUAL(ops::msb_index(card_mask{1} << 0), 0);
    BOOST_CHECK_EQUAL(ops::msb_index(card_mask{1} << 51), 51);
}

BOOST_AUTO_TEST_CASE(test_lsb_equals_mask_for_single_bit) {
    card_mask single = card_mask{1} << 33;
    BOOST_CHECK_EQUAL(ops::lsb(single), single);
}

BOOST_AUTO_TEST_CASE(test_pop_lsb_exhaustive_small_mask) {
    // Pop all bits from a 4-bit mask and verify
    card_mask m = (card_mask{1} << 10) | (card_mask{1} << 20)
               | (card_mask{1} << 30) | (card_mask{1} << 40);

    BOOST_CHECK_EQUAL(ops::pop_lsb(m), card_mask{1} << 10);
    BOOST_CHECK_EQUAL(ops::pop_lsb(m), card_mask{1} << 20);
    BOOST_CHECK_EQUAL(ops::pop_lsb(m), card_mask{1} << 30);
    BOOST_CHECK_EQUAL(ops::pop_lsb(m), card_mask{1} << 40);
    BOOST_CHECK(ops::is_empty(m));
}

BOOST_AUTO_TEST_CASE(test_popcount_full_suits) {
    BOOST_CHECK_EQUAL(ops::popcount(ops::suit_masks<whist>[0]), 13);
    BOOST_CHECK_EQUAL(ops::popcount(ops::suit_masks<whist>[1]), 13);
    BOOST_CHECK_EQUAL(ops::popcount(ops::suit_masks<whist>[2]), 13);
    BOOST_CHECK_EQUAL(ops::popcount(ops::suit_masks<whist>[3]), 13);
}

BOOST_AUTO_TEST_CASE(test_nth_set_bit_single_bit) {
    card_mask m = card_mask{1} << 42;
    BOOST_CHECK_EQUAL(ops::nth_set_bit(m, 0), m);
}

BOOST_AUTO_TEST_SUITE_END()


// ── suit_ranks / ranks_to_cards round-trip all suits ──────

BOOST_AUTO_TEST_SUITE(suit_ranks_round_trip_tests)

BOOST_AUTO_TEST_CASE(test_round_trip_hearts) {
    // Hand in hearts: 2♥ (13), 5♥ (16), A♥ (25)
    card_mask hand = (card_mask{1} << 13) | (card_mask{1} << 16) | (card_mask{1} << 25);
    uint16_t ranks = ops::suit_ranks<whist>(hand, suit::hearts);
    BOOST_CHECK_EQUAL(ranks, (1 << 0) | (1 << 3) | (1 << 12));
    BOOST_CHECK_EQUAL(ops::ranks_to_cards<whist>(ranks, suit::hearts), hand);
}

BOOST_AUTO_TEST_CASE(test_round_trip_diamonds) {
    // Hand in diamonds: 3♦ (27), K♦ (37)
    card_mask hand = (card_mask{1} << 27) | (card_mask{1} << 37);
    uint16_t ranks = ops::suit_ranks<whist>(hand, suit::diamonds);
    BOOST_CHECK_EQUAL(ranks, (1 << 1) | (1 << 11));
    BOOST_CHECK_EQUAL(ops::ranks_to_cards<whist>(ranks, suit::diamonds), hand);
}

BOOST_AUTO_TEST_CASE(test_round_trip_clubs) {
    // Hand in clubs: 2♣ (39), A♣ (51)
    card_mask hand = (card_mask{1} << 39) | (card_mask{1} << 51);
    uint16_t ranks = ops::suit_ranks<whist>(hand, suit::clubs);
    BOOST_CHECK_EQUAL(ranks, (1 << 0) | (1 << 12));
    BOOST_CHECK_EQUAL(ops::ranks_to_cards<whist>(ranks, suit::clubs), hand);
}

BOOST_AUTO_TEST_CASE(test_round_trip_full_suit) {
    // All 13 spades
    card_mask all_spades = ops::suit_masks<whist>[0];
    uint16_t ranks = ops::suit_ranks<whist>(all_spades, suit::spades);
    BOOST_CHECK_EQUAL(ranks, (1 << 13) - 1);
    BOOST_CHECK_EQUAL(ops::ranks_to_cards<whist>(ranks, suit::spades), all_spades);
}

BOOST_AUTO_TEST_SUITE_END()


// ── current_hand_mask accessor ────────────────────────────

BOOST_AUTO_TEST_SUITE(current_hand_mask_tests)

BOOST_AUTO_TEST_CASE(test_current_hand_mask_tracks_current_player) {
    game_state<whist> state;
    state.hands[0] = 0xAA;
    state.hands[1] = 0xBB;
    state.hands[2] = 0xCC;
    state.hands[3] = 0xDD;

    state.current_player = seat::north;
    BOOST_CHECK_EQUAL(state.current_hand_mask(), 0xAA);

    state.current_player = seat::east;
    BOOST_CHECK_EQUAL(state.current_hand_mask(), 0xBB);

    state.current_player = seat::south;
    BOOST_CHECK_EQUAL(state.current_hand_mask(), 0xCC);

    state.current_player = seat::west;
    BOOST_CHECK_EQUAL(state.current_hand_mask(), 0xDD);
}

BOOST_AUTO_TEST_SUITE_END()


// ── beats_current: lead suit = trump ──────────────────────

BOOST_AUTO_TEST_SUITE(beats_current_lead_is_trump_tests)

BOOST_AUTO_TEST_CASE(test_higher_trump_beats_lower_when_lead_is_trump) {
    // Lead suit = trump = hearts
    trick_state t;
    t.num_played = 1;
    t.lead_suit = suit::hearts;
    t.lead_seat = seat::north;
    t.winning_card = 15; // 4♥ (rank 2)
    t.current_winner = seat::north;

    // Higher heart beats
    BOOST_CHECK(whist_rules::beats_current(t, suit::hearts, 25));  // A♥
    // Lower heart does not
    BOOST_CHECK(!whist_rules::beats_current(t, suit::hearts, 13)); // 2♥
}

BOOST_AUTO_TEST_CASE(test_off_suit_cannot_win_when_lead_is_trump) {
    trick_state t;
    t.num_played = 1;
    t.lead_suit = suit::hearts;
    t.lead_seat = seat::north;
    t.winning_card = 13; // 2♥ (trump, lowest)
    t.current_winner = seat::north;

    // Off-suit never wins, even aces
    BOOST_CHECK(!whist_rules::beats_current(t, suit::hearts, 12)); // A♠
    BOOST_CHECK(!whist_rules::beats_current(t, suit::hearts, 38)); // A♦
    BOOST_CHECK(!whist_rules::beats_current(t, suit::hearts, 51)); // A♣
}

BOOST_AUTO_TEST_SUITE_END()


// ── play_card: played mask accumulates across trick ───────

BOOST_AUTO_TEST_SUITE(played_mask_accumulation_tests)

BOOST_AUTO_TEST_CASE(test_played_mask_full_trick) {
    game_state<whist> state;
    state.trump = suit::diamonds;
    state.current_player = seat::north;
    state.tricks_remaining = 13;

    state.hands[0] = (card_mask{1} << 0);   // North: 2♠
    state.hands[1] = (card_mask{1} << 1);   // East: 3♠
    state.hands[2] = (card_mask{1} << 2);   // South: 4♠
    state.hands[3] = (card_mask{1} << 3);   // West: 5♠

    card_mask expected_played = 0;

    whist_rules::play_card(state, seat::north, 0);
    expected_played |= (card_mask{1} << 0);
    BOOST_CHECK_EQUAL(state.played, expected_played);

    whist_rules::play_card(state, seat::east, 1);
    expected_played |= (card_mask{1} << 1);
    BOOST_CHECK_EQUAL(state.played, expected_played);

    whist_rules::play_card(state, seat::south, 2);
    expected_played |= (card_mask{1} << 2);
    BOOST_CHECK_EQUAL(state.played, expected_played);

    whist_rules::play_card(state, seat::west, 3);
    expected_played |= (card_mask{1} << 3);
    BOOST_CHECK_EQUAL(state.played, expected_played);

    // After trick resolves, played mask retains all cards
    BOOST_CHECK_EQUAL(state.played, (card_mask{1} << 0) | (card_mask{1} << 1)
                                  | (card_mask{1} << 2) | (card_mask{1} << 3));
    // All hands empty
    for (int i = 0; i < 4; ++i) {
        BOOST_CHECK_EQUAL(state.hands[i], card_mask{0});
    }
}

BOOST_AUTO_TEST_SUITE_END()


// ── trump ruff in multi-trick scenario ────────────────────

BOOST_AUTO_TEST_SUITE(trump_ruff_tests)

BOOST_AUTO_TEST_CASE(test_trump_ruff_wins_second_trick) {
    game_state<whist> state;
    state.trump = suit::hearts;
    state.current_player = seat::north;
    state.tricks_remaining = 13;

    // Each player has 2 cards
    // North: A♠ (12), 2♠ (0)
    state.hands[0] = (card_mask{1} << 12) | (card_mask{1} << 0);
    // East: K♠ (11), 3♠ (1)
    state.hands[1] = (card_mask{1} << 11) | (card_mask{1} << 1);
    // South: Q♠ (10), 2♥ (13) — will trump in trick 2
    state.hands[2] = (card_mask{1} << 10) | (card_mask{1} << 13);
    // West: J♠ (9), 4♠ (2)
    state.hands[3] = (card_mask{1} << 9) | (card_mask{1} << 2);

    // Trick 1: all spades, North A♠ wins
    whist_rules::play_card(state, seat::north, 12);
    whist_rules::play_card(state, seat::east, 11);
    whist_rules::play_card(state, seat::south, 10);
    whist_rules::play_card(state, seat::west, 9);

    BOOST_CHECK_EQUAL(state.current_player, seat::north); // North wins

    // Trick 2: North leads 2♠, South ruffs with 2♥
    whist_rules::play_card(state, seat::north, 0);
    whist_rules::play_card(state, seat::east, 1);
    whist_rules::play_card(state, seat::south, 13); // trump ruff!
    whist_rules::play_card(state, seat::west, 2);

    // South (partnership 0) wins trick 2 via trump
    BOOST_CHECK_EQUAL(state.current_player, seat::south);
    BOOST_CHECK_EQUAL(state.tricks_won[partnership_of(seat::south)], 2); // both tricks to partnership 0
    BOOST_CHECK_EQUAL(state.tricks_won[partnership_of(seat::east)], 0);
}

BOOST_AUTO_TEST_SUITE_END()


// ── trick_winner_seat rule function ───────────────────────

BOOST_AUTO_TEST_SUITE(trick_winner_seat_tests)

BOOST_AUTO_TEST_CASE(test_trick_winner_seat_incremental) {
    // Build a state mid-trick and verify trick_winner_seat
    game_state<whist> state;
    state.trump = suit::clubs;
    state.current_player = seat::north;
    state.tricks_remaining = 13;

    state.hands[0] = (card_mask{1} << 5);   // North: 7♠
    state.hands[1] = (card_mask{1} << 11);  // East: K♠
    state.hands[2] = (card_mask{1} << 3);   // South: 5♠
    state.hands[3] = (card_mask{1} << 8);   // West: 10♠

    whist_rules::play_card(state, seat::north, 5);
    whist_rules::play_card(state, seat::east, 11);
    // After 2 cards, East should be winning (K♠ > 7♠)
    BOOST_CHECK(whist_rules::trick_winner_seat(state) == seat::east);

    whist_rules::play_card(state, seat::south, 3);
    // Still East winning
    BOOST_CHECK(whist_rules::trick_winner_seat(state) == seat::east);
}

BOOST_AUTO_TEST_SUITE_END()


// ── deal randomness ───────────────────────────────────────

BOOST_AUTO_TEST_SUITE(deal_randomness_tests)

BOOST_AUTO_TEST_CASE(test_different_seeds_produce_different_deals) {
    std::mt19937 rng1(1);
    std::mt19937 rng2(2);

    auto h1 = deal<whist, 4>(rng1);
    auto h2 = deal<whist, 4>(rng2);

    // At least one hand should differ (extremely unlikely to be equal with different seeds)
    bool any_differ = false;
    for (int i = 0; i < 4; ++i) {
        if (h1[i] != h2[i]) { any_differ = true; break; }
    }
    BOOST_CHECK(any_differ);
}

BOOST_AUTO_TEST_CASE(test_deal_span_hands_are_pairwise_disjoint) {
    std::mt19937 rng(999);
    std::array<card_mask, 4> hands{};
    deal_span<whist>(rng, hands, 13);

    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            BOOST_CHECK_EQUAL(hands[i] & hands[j], card_mask{0});
        }
    }
}

BOOST_AUTO_TEST_CASE(test_deal_no_bits_above_52) {
    std::mt19937 rng(7);
    auto hands = deal<whist, 4>(rng);

    card_mask valid_mask = (card_mask{1} << 52) - 1;
    for (int i = 0; i < 4; ++i) {
        BOOST_CHECK_EQUAL(hands[i] & ~valid_mask, card_mask{0});
    }
}

BOOST_AUTO_TEST_SUITE_END()


// ── shuffled utility ──────────────────────────────────────

BOOST_AUTO_TEST_SUITE(shuffled_tests)

BOOST_AUTO_TEST_CASE(test_shuffled_produces_valid_permutation) {
    std::mt19937 rng(55);
    auto cards = shuffled<whist>(rng);

    // Must contain all 52 cards exactly once
    BOOST_CHECK_EQUAL(cards.size(), 52u);

    std::array<int, 52> counts{};
    for (auto c : cards) {
        BOOST_REQUIRE(c < 52);
        counts[c]++;
    }
    for (int i = 0; i < 52; ++i) {
        BOOST_CHECK_EQUAL(counts[i], 1);
    }
}

BOOST_AUTO_TEST_CASE(test_shuffled_different_seeds_differ) {
    std::mt19937 rng1(100);
    std::mt19937 rng2(200);
    auto c1 = shuffled<whist>(rng1);
    auto c2 = shuffled<whist>(rng2);

    bool same = std::equal(c1.begin(), c1.end(), c2.begin());
    BOOST_CHECK(!same);
}

BOOST_AUTO_TEST_SUITE_END()


// ── game_state default initialization ─────────────────────

BOOST_AUTO_TEST_SUITE(game_state_init_tests)

BOOST_AUTO_TEST_CASE(test_game_state_default_values) {
    game_state<whist> state;

    // Hands should be zero-initialized
    for (int i = 0; i < 4; ++i) {
        BOOST_CHECK_EQUAL(state.hands[i], card_mask{0});
    }
    BOOST_CHECK_EQUAL(state.played, card_mask{0});

    // Tricks won start at zero
    BOOST_CHECK_EQUAL(state.tricks_won[0], 0);
    BOOST_CHECK_EQUAL(state.tricks_won[1], 0);

    // Default tricks_remaining = num_cards / num_players = 13
    BOOST_CHECK_EQUAL(state.tricks_remaining, 13);

    // Trick state defaults
    BOOST_CHECK_EQUAL(state.trick.mask, card_mask{0});
    BOOST_CHECK_EQUAL(state.trick.winning_card, no_card);
    BOOST_CHECK_EQUAL(state.trick.num_played, 0);
    BOOST_CHECK(!state.trick.complete());
}

BOOST_AUTO_TEST_SUITE_END()


// ── whist remap is identity ───────────────────────────────

BOOST_AUTO_TEST_SUITE(remap_tests)

BOOST_AUTO_TEST_CASE(test_whist_remap_identity) {
    // For whist, remap should be the identity function
    card_mask test_mask = (card_mask{1} << 5) | (card_mask{1} << 20) | (card_mask{1} << 40);
    BOOST_CHECK_EQUAL(deck_traits<whist>::remap(test_mask, suit::spades), test_mask);
    BOOST_CHECK_EQUAL(deck_traits<whist>::remap(test_mask, suit::hearts), test_mask);
    BOOST_CHECK_EQUAL(deck_traits<whist>::remap(test_mask, suit::diamonds), test_mask);
    BOOST_CHECK_EQUAL(deck_traits<whist>::remap(test_mask, suit::clubs), test_mask);
}

BOOST_AUTO_TEST_SUITE_END()


// ── jass trump LUT smoke tests ────────────────────────────

BOOST_AUTO_TEST_SUITE(jass_lut_tests)

BOOST_AUTO_TEST_CASE(test_jass_trump_lut_under_is_highest) {
    // Under (Jack, index 5) should map to bit 8 (strongest)
    BOOST_CHECK_EQUAL(jass_trump_lut[1 << UNDER_INDEX], 1 << 8);
}

BOOST_AUTO_TEST_CASE(test_jass_trump_lut_nell_is_second) {
    // Nell (9, index 3) should map to bit 7 (second strongest)
    BOOST_CHECK_EQUAL(jass_trump_lut[1 << NELL_INDEX], 1 << 7);
}

BOOST_AUTO_TEST_CASE(test_jass_trump_lut_preserves_popcount) {
    // The LUT should never create or destroy bits (bijection per entry)
    for (int i = 0; i < 512; ++i) {
        BOOST_CHECK_EQUAL(std::popcount(static_cast<unsigned>(i)),
                          std::popcount(static_cast<unsigned>(jass_trump_lut[i])));
    }
}

BOOST_AUTO_TEST_CASE(test_jass_suit_of_and_rank_of) {
    // Jass: 9 ranks per suit
    // Suit 0 (Spades/Schilten): cards 0-8
    BOOST_CHECK(suit_of<jass>(0) == suit::spades);
    BOOST_CHECK(suit_of<jass>(8) == suit::spades);
    // Suit 1 (Hearts/Rosen): cards 9-17
    BOOST_CHECK(suit_of<jass>(9)  == suit::hearts);
    BOOST_CHECK(suit_of<jass>(17) == suit::hearts);
    // Ranks cycle within each suit
    BOOST_CHECK_EQUAL(rank_of<jass>(0), 0);
    BOOST_CHECK_EQUAL(rank_of<jass>(8), 8);
    BOOST_CHECK_EQUAL(rank_of<jass>(9), 0);
}

BOOST_AUTO_TEST_SUITE_END()


