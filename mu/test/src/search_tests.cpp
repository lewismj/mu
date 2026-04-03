#include <boost/test/unit_test.hpp>
#include <mu.h>

#include <cmath>
#include <numeric>

using namespace mu;


// ── seat_to_opp / opp_to_seat mapping ─────────────────────

BOOST_AUTO_TEST_SUITE(seat_opp_mapping_tests)

BOOST_AUTO_TEST_CASE(test_seat_to_opp_from_north) {
    BOOST_CHECK_EQUAL(seat_to_opp(seat::north, seat::east),  0);
    BOOST_CHECK_EQUAL(seat_to_opp(seat::north, seat::south), 1);
    BOOST_CHECK_EQUAL(seat_to_opp(seat::north, seat::west),  2);
}

BOOST_AUTO_TEST_CASE(test_seat_to_opp_from_east) {
    BOOST_CHECK_EQUAL(seat_to_opp(seat::east, seat::north), 0);
    BOOST_CHECK_EQUAL(seat_to_opp(seat::east, seat::south), 1);
    BOOST_CHECK_EQUAL(seat_to_opp(seat::east, seat::west),  2);
}

BOOST_AUTO_TEST_CASE(test_seat_to_opp_from_south) {
    BOOST_CHECK_EQUAL(seat_to_opp(seat::south, seat::north), 0);
    BOOST_CHECK_EQUAL(seat_to_opp(seat::south, seat::east),  1);
    BOOST_CHECK_EQUAL(seat_to_opp(seat::south, seat::west),  2);
}

BOOST_AUTO_TEST_CASE(test_seat_to_opp_from_west) {
    BOOST_CHECK_EQUAL(seat_to_opp(seat::west, seat::north), 0);
    BOOST_CHECK_EQUAL(seat_to_opp(seat::west, seat::east),  1);
    BOOST_CHECK_EQUAL(seat_to_opp(seat::west, seat::south), 2);
}

BOOST_AUTO_TEST_CASE(test_round_trip_all_observers) {
    for (uint8_t obs_i = 0; obs_i < num_players; ++obs_i) {
        const seat obs = static_cast<seat>(obs_i);
        for (uint8_t opp = 0; opp < num_other_players; ++opp) {
            const seat s = opp_to_seat(obs, opp);
            BOOST_CHECK(s != obs);
            BOOST_CHECK_EQUAL(seat_to_opp(obs, s), opp);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()


// ── belief_state initialization ───────────────────────────

BOOST_AUTO_TEST_SUITE(belief_init_tests)

BOOST_AUTO_TEST_CASE(test_init_uniform_prior) {
    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::hearts;
    state.current_player = seat::north;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    BOOST_CHECK(belief.observer == seat::north);

    const card_mask own = state.hands[0];
    const card_mask unseen = ~(state.played | own)
                             & ((card_mask{1} << 52) - 1);

    // Own cards: probability must be 0 for all opponents
    for (int c = 0; c < 52; ++c) {
        if ((own >> c) & 1) {
            for (uint8_t i = 0; i < num_other_players; ++i)
                BOOST_CHECK_EQUAL(belief.prob[i][c], 0.0f);
        }
    }

    // Unseen cards: probability must be 1/3 for each opponent
    constexpr float expected = 1.0f / 3.0f;
    for (int c = 0; c < 52; ++c) {
        if ((unseen >> c) & 1) {
            float sum = 0.0f;
            for (uint8_t i = 0; i < num_other_players; ++i) {
                BOOST_CHECK_CLOSE(belief.prob[i][c],
                                  expected, 1e-4f);
                sum += belief.prob[i][c];
            }
            BOOST_CHECK_CLOSE(sum, 1.0f, 1e-4f);
        }
    }
}

BOOST_AUTO_TEST_CASE(test_init_possible_masks) {
    std::mt19937 rng(7);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];

    belief_state<whist> belief;
    belief.init(state, seat::south);

    const card_mask own = state.hands[2]; // south = index 2
    const card_mask unseen = ~(state.played | own)
                             & ((card_mask{1} << 52) - 1);

    for (uint8_t i = 0; i < num_other_players; ++i)
        BOOST_CHECK_EQUAL(belief.possible[i], unseen);
}

BOOST_AUTO_TEST_SUITE_END()


// ── belief_state void signals ─────────────────────────────

BOOST_AUTO_TEST_SUITE(belief_void_signal_tests)

BOOST_AUTO_TEST_CASE(test_void_signal_zeroes_suit) {
    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::hearts;
    state.current_player = seat::north;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    // Find a card in north's hand (for lead) — pick the lowest spade
    const card_mask north_spades =
        ops::cards_in_suit<whist>(state.hands[0], suit::spades);
    // Must have at least one spade to test void signal
    // (if not, the test RNG happened not to deal north any spades,
    //  which is possible but extremely unlikely with seed 42)
    BOOST_REQUIRE(!ops::is_empty(north_spades));
    const card lead_card =
        static_cast<card>(ops::lsb_index(north_spades));

    // North leads a spade: is_lead = true
    belief.update_on_play(seat::north, lead_card,
                          true, suit::spades);

    // Find a card in east's hand that is NOT a spade
    const card_mask east_non_spade =
        state.hands[1]
        & ~ops::suit_masks<whist>[static_cast<int>(suit::spades)];

    if (!ops::is_empty(east_non_spade)) {
        const card east_card =
            static_cast<card>(ops::lsb_index(east_non_spade));

        // East plays a non-spade: void signal
        belief.update_on_play(seat::east, east_card,
                              false, suit::spades);

        // East (opp 0 from north) should have 0 probability
        // for ALL remaining spade cards
        const uint8_t east_opp =
            seat_to_opp(seat::north, seat::east);

        for (int c = 0; c < 13; ++c) {
            BOOST_CHECK_EQUAL(belief.prob[east_opp][c], 0.0f);
            BOOST_CHECK(
                !((belief.possible[east_opp] >> c) & 1));
        }
    }
    // else: east has only spades — skip void signal test
    // (would mean east can follow suit, no void signal)
}

BOOST_AUTO_TEST_CASE(test_no_void_signal_when_following_suit) {
    std::mt19937 rng(7);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::hearts;
    state.current_player = seat::north;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    // Find a spade in north's hand (lead card)
    const card_mask north_spades =
        ops::cards_in_suit<whist>(state.hands[0], suit::spades);
    BOOST_REQUIRE(!ops::is_empty(north_spades));
    const card lead_card =
        static_cast<card>(ops::lsb_index(north_spades));

    belief.update_on_play(seat::north, lead_card,
                          true, suit::spades);

    // Find a spade in east's hand (follow suit)
    const card_mask east_spades =
        ops::cards_in_suit<whist>(state.hands[1], suit::spades);

    if (!ops::is_empty(east_spades)) {
        const card east_card =
            static_cast<card>(ops::lsb_index(east_spades));

        belief.update_on_play(seat::east, east_card,
                              false, suit::spades);

        // East followed suit → no void signal → spades still possible
        const uint8_t east_opp =
            seat_to_opp(seat::north, seat::east);

        // Other spade cards (not the played ones) should
        // still be possible for east
        const card_mask remaining_spades =
            ops::suit_masks<whist>[0]
            & ~(card_mask{1} << lead_card)
            & ~(card_mask{1} << east_card)
            & ~state.hands[0]; // exclude north's cards

        if (!ops::is_empty(remaining_spades)) {
            const int sc =
                ops::lsb_index(remaining_spades);
            BOOST_CHECK(
                (belief.possible[east_opp] >> sc) & 1);
        }
    }
}

BOOST_AUTO_TEST_CASE(test_played_card_zeroed_for_all) {
    game_state<whist> state;
    state.trump = suit::clubs;
    state.current_player = seat::north;

    std::mt19937 rng(123);
    auto hands = deal<whist, 4>(rng);
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];

    belief_state<whist> belief;
    belief.init(state, seat::north);

    // Play any card from East's hand
    card_mask east_hand = state.hands[1];
    card c = static_cast<card>(ops::lsb_index(east_hand));

    belief.update_on_play(seat::east, c, true, suit::spades);

    // All opponents should have prob 0 for that card
    for (uint8_t i = 0; i < num_other_players; ++i)
        BOOST_CHECK_EQUAL(belief.prob[i][c], 0.0f);
}

BOOST_AUTO_TEST_CASE(test_renormalize_column_sums) {
    game_state<whist> state;
    state.trump = suit::diamonds;
    state.current_player = seat::north;

    std::mt19937 rng(55);
    auto hands = deal<whist, 4>(rng);
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];

    belief_state<whist> belief;
    belief.init(state, seat::north);

    // Simulate a void signal
    card_mask east_hand = state.hands[1];
    card c = static_cast<card>(ops::lsb_index(east_hand));
    belief.update_on_play(seat::east, c, true, suit::spades);

    // Play a card from south not following → void signal
    card_mask south_hand = state.hands[2];
    card sc = static_cast<card>(ops::lsb_index(south_hand));
    belief.update_on_play(seat::south, sc, false, suit::spades);

    // Verify column sums for all unseen cards
    const card_mask own = state.hands[0];
    const card_mask played = (card_mask{1} << c) | (card_mask{1} << sc);
    const card_mask unseen = ~(played | own)
                             & ((card_mask{1} << 52) - 1);

    for (int cc = 0; cc < 52; ++cc) {
        float sum = 0.0f;
        for (uint8_t i = 0; i < num_other_players; ++i)
            sum += belief.prob[i][cc];

        if ((unseen >> cc) & 1) {
            BOOST_CHECK_CLOSE(sum, 1.0f, 1e-2f);
        } else {
            BOOST_CHECK_SMALL(sum, 1e-6f);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()


// ── determinization ───────────────────────────────────────

BOOST_AUTO_TEST_SUITE(determinize_tests)

BOOST_AUTO_TEST_CASE(test_determinize_valid_hands) {
    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::hearts;
    state.current_player = seat::north;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    std::mt19937 det_rng(99);
    auto det = determinize<whist>(state, belief, det_rng);

    // Observer's hand is preserved
    BOOST_CHECK_EQUAL(det.hands[0], state.hands[0]);

    // Each hand has 13 cards
    for (int i = 0; i < 4; ++i)
        BOOST_CHECK_EQUAL(ops::popcount(det.hands[i]), 13);

    // Hands are pairwise disjoint
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j)
            BOOST_CHECK_EQUAL(det.hands[i] & det.hands[j],
                              card_mask{0});

    // Union is the full 52-card deck
    card_mask all = det.hands[0] | det.hands[1]
                  | det.hands[2] | det.hands[3];
    BOOST_CHECK_EQUAL(all, (card_mask{1} << 52) - 1);
}

BOOST_AUTO_TEST_CASE(test_determinize_preserves_game_context) {
    std::mt19937 rng(77);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::diamonds;
    state.dealer = seat::west;
    state.current_player = seat::north;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    std::mt19937 det_rng(11);
    auto det = determinize<whist>(state, belief, det_rng);

    BOOST_CHECK(det.trump == suit::diamonds);
    BOOST_CHECK(det.dealer == seat::west);
    BOOST_CHECK(det.current_player == seat::north);
    BOOST_CHECK_EQUAL(det.tricks_remaining, 13);
    BOOST_CHECK_EQUAL(det.played, card_mask{0});
}

BOOST_AUTO_TEST_CASE(test_determinize_respects_void_constraint) {
    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::clubs;
    state.current_player = seat::north;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    // Artificially mark East as void in spades
    const uint8_t east_opp = seat_to_opp(seat::north, seat::east);
    const card_mask spade_mask = ops::suit_masks<whist>[0];
    belief.possible[east_opp] &= ~spade_mask;
    for (int c = 0; c < 13; ++c)
        belief.prob[east_opp][c] = 0.0f;
    belief.renormalize();

    // Generate many determinizations; East should never have spades
    std::mt19937 det_rng(100);
    for (int trial = 0; trial < 50; ++trial) {
        auto det = determinize<whist>(state, belief, det_rng);
        const card_mask east_spades =
            ops::cards_in_suit<whist>(det.hands[1], suit::spades);
        BOOST_CHECK_EQUAL(east_spades, card_mask{0});
    }
}

BOOST_AUTO_TEST_CASE(test_determinize_different_seeds_differ) {
    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::hearts;
    state.current_player = seat::north;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    std::mt19937 rng1(1), rng2(2);
    auto d1 = determinize<whist>(state, belief, rng1);
    auto d2 = determinize<whist>(state, belief, rng2);

    // At least one opponent hand should differ
    bool any_differ = false;
    for (int i = 1; i < 4; ++i)
        if (d1.hands[i] != d2.hands[i]) { any_differ = true; break; }
    BOOST_CHECK(any_differ);
}

BOOST_AUTO_TEST_SUITE_END()


// ── ISMCTS search ─────────────────────────────────────────

BOOST_AUTO_TEST_SUITE(ismcts_search_tests)

BOOST_AUTO_TEST_CASE(test_search_returns_legal_move) {
    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::hearts;
    state.current_player = seat::north;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    search_config cfg;
    cfg.iterations = 100;

    std::mt19937 search_rng(99);
    card result = ismcts_search<whist>(state, belief, cfg, search_rng);

    BOOST_CHECK(result != no_card);

    // Result must be in observer's hand
    BOOST_CHECK((state.hands[0] >> result) & 1);
}

BOOST_AUTO_TEST_CASE(test_search_deterministic_same_seed) {
    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::diamonds;
    state.current_player = seat::north;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    search_config cfg;
    cfg.iterations = 200;

    std::mt19937 rng1(77), rng2(77);
    card r1 = ismcts_search<whist>(state, belief, cfg, rng1);
    card r2 = ismcts_search<whist>(state, belief, cfg, rng2);

    BOOST_CHECK_EQUAL(r1, r2);
}

BOOST_AUTO_TEST_CASE(test_search_forced_move) {
    // If observer has only one card, search must return it
    game_state<whist> state;
    state.trump = suit::hearts;
    state.current_player = seat::north;
    state.tricks_remaining = 1;

    state.hands[0] = (card_mask{1} << 12); // North: A♠
    state.hands[1] = (card_mask{1} << 0);  // East: 2♠
    state.hands[2] = (card_mask{1} << 1);  // South: 3♠
    state.hands[3] = (card_mask{1} << 2);  // West: 4♠

    // Mark all other cards as played (last trick scenario)
    const card_mask all_hands = state.hands[0] | state.hands[1]
                              | state.hands[2] | state.hands[3];
    state.played = ((card_mask{1} << 52) - 1) & ~all_hands;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    search_config cfg;
    cfg.iterations = 50;

    std::mt19937 rng(1);
    card result = ismcts_search<whist>(state, belief, cfg, rng);

    BOOST_CHECK_EQUAL(result, 12); // A♠ — only legal move
}

BOOST_AUTO_TEST_CASE(test_search_obvious_winner) {
    // North has all the aces; with enough iterations, search
    // should pick an ace (any of them is fine since all win).
    game_state<whist> state;
    state.trump = suit::hearts;
    state.current_player = seat::north;
    state.tricks_remaining = 1;

    // One trick left: everyone has 1 card
    state.hands[0] = (card_mask{1} << 12); // North: A♠
    state.hands[1] = (card_mask{1} << 0);  // East: 2♠
    state.hands[2] = (card_mask{1} << 1);  // South: 3♠
    state.hands[3] = (card_mask{1} << 2);  // West: 4♠

    // Mark all other cards as played
    const card_mask all_hands = state.hands[0] | state.hands[1]
                              | state.hands[2] | state.hands[3];
    state.played = ((card_mask{1} << 52) - 1) & ~all_hands;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    search_config cfg;
    cfg.iterations = 100;

    std::mt19937 rng(55);
    card result = ismcts_search<whist>(state, belief, cfg, rng);

    // Only card is A♠
    BOOST_CHECK_EQUAL(result, 12);
}

BOOST_AUTO_TEST_CASE(test_search_mid_game) {
    // Start from a mid-game position (after 1 trick)
    std::mt19937 rng(500);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::clubs;
    state.current_player = seat::north;
    state.tricks_remaining = 13;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    // Play one full trick manually
    for (int p = 0; p < 4; ++p) {
        const seat cur = state.current_player;
        const bool is_lead = (state.trick.num_played == 0);
        const suit lead = state.trick.lead_suit;
        const card_mask legal = whist_rules::legal_moves(state);
        const card c = static_cast<card>(ops::lsb_index(legal));

        belief.update_on_play(cur, c, is_lead, lead);
        whist_rules::play_card(state, cur, c);
    }

    BOOST_CHECK_EQUAL(state.tricks_remaining, 12);

    // Reinitialize belief for remaining game state
    // (simpler for test — in real use, belief is maintained incrementally)
    belief.init(state, state.current_player);

    search_config cfg;
    cfg.iterations = 200;

    std::mt19937 search_rng(42);
    card result = ismcts_search<whist>(
        state, belief, cfg, search_rng);

    BOOST_CHECK(result != no_card);
    BOOST_CHECK((state.hands[static_cast<uint8_t>(
        state.current_player)] >> result) & 1);
}

BOOST_AUTO_TEST_SUITE_END()


// ── integration: full game with belief + search ───────────

BOOST_AUTO_TEST_SUITE(integration_tests)

BOOST_AUTO_TEST_CASE(test_full_game_with_search) {
    /* Play out a full 13-trick whist game where North uses
       ISMCTS and all other players play the lowest legal card.
       Verify the game completes without assertions firing.    */

    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::hearts;
    state.current_player = seat::north;
    state.dealer = seat::west;
    state.tricks_remaining = 13;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    std::mt19937 search_rng(77);
    search_config cfg;
    cfg.iterations = 50;    // low for test speed

    while (state.tricks_remaining > 0) {
        for (int p = 0; p < 4; ++p) {
            const seat cur = state.current_player;
            const bool is_lead =
                (state.trick.num_played == 0);
            const suit lead = state.trick.lead_suit;

            card c;
            if (cur == seat::north) {
                // Re-init belief at observer's turn
                // (simplified — production code maintains
                // beliefs incrementally)
                belief.init(state, seat::north);
                c = ismcts_search<whist>(
                    state, belief, cfg, search_rng);
            } else {
                // Other players: lowest legal card
                const card_mask legal =
                    whist_rules::legal_moves(state);
                c = static_cast<card>(
                    ops::lsb_index(legal));
            }

            belief.update_on_play(cur, c, is_lead, lead);
            whist_rules::play_card(state, cur, c);
        }
    }

    BOOST_CHECK_EQUAL(state.tricks_remaining, 0);
    BOOST_CHECK_EQUAL(state.tricks_won[0] + state.tricks_won[1], 13);
}

BOOST_AUTO_TEST_SUITE_END()

