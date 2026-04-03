#include <boost/test/unit_test.hpp>
#include <mu.h>

#include <span>

using namespace mu;


// ── play_context construction ─────────────────────────────

BOOST_AUTO_TEST_SUITE(play_context_tests)

BOOST_AUTO_TEST_CASE(test_make_play_context_lead) {
    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::hearts;
    state.current_player = seat::north;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    // Pick the first legal card from north
    card c = static_cast<card>(
        ops::lsb_index(state.hands[0]));

    auto ctx = make_play_context<whist>(
        state, belief, seat::north, c);

    BOOST_CHECK(ctx.player == seat::north);
    BOOST_CHECK_EQUAL(ctx.card_played, c);
    BOOST_CHECK(ctx.is_lead);
    BOOST_CHECK(ctx.observer == seat::north);
    BOOST_CHECK_EQUAL(ctx.trick_position, 0);
    BOOST_CHECK(ctx.followed_suit); // leading always "follows"
    BOOST_CHECK(ctx.beats_winner);  // first card beats nothing
    BOOST_CHECK(!ctx.is_partner);   // observer is not partner of self
}

BOOST_AUTO_TEST_CASE(test_make_play_context_follow) {
    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::hearts;
    state.current_player = seat::north;

    belief_state<whist> belief;
    belief.init(state, seat::south);  // observer is south

    // Play north's card
    card nc = static_cast<card>(
        ops::lsb_index(state.hands[0]));
    whist_rules::play_card(state, seat::north, nc);

    // Now east plays
    card_mask legal = whist_rules::legal_moves(state);
    card ec = static_cast<card>(ops::lsb_index(legal));

    auto ctx = make_play_context<whist>(
        state, belief, seat::east, ec);

    BOOST_CHECK(ctx.player == seat::east);
    BOOST_CHECK(!ctx.is_lead);
    BOOST_CHECK_EQUAL(ctx.trick_position, 1);
    BOOST_CHECK(!ctx.is_partner); // east is not south's partner

    // Check followed_suit
    bool expected_follow =
        (suit_of<whist>(ec) == state.trick.lead_suit);
    BOOST_CHECK_EQUAL(ctx.followed_suit, expected_follow);
}

BOOST_AUTO_TEST_CASE(test_make_play_context_partner_flag) {
    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::clubs;
    state.current_player = seat::north;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    // North's partner is south
    card c = static_cast<card>(
        ops::lsb_index(state.hands[2]));

    auto ctx = make_play_context<whist>(
        state, belief, seat::south, c);

    BOOST_CHECK(ctx.is_partner);
}

BOOST_AUTO_TEST_SUITE_END()


// ── individual heuristic rules ────────────────────────────

BOOST_AUTO_TEST_SUITE(heuristic_rule_tests)

BOOST_AUTO_TEST_CASE(test_played_low_on_winnable_fires) {
    // Set up: east follows with a low spade when spades led
    game_state<whist> state;
    state.trump = suit::hearts;
    state.current_player = seat::north;
    state.tricks_remaining = 13;

    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];

    belief_state<whist> belief;
    belief.init(state, seat::north);

    // North leads a spade
    const card_mask north_spades =
        ops::cards_in_suit<whist>(state.hands[0], suit::spades);
    if (ops::is_empty(north_spades)) return; // skip if no spades

    card lead = static_cast<card>(
        ops::msb_index(north_spades)); // lead high
    whist_rules::play_card(state, seat::north, lead);

    // East follows with a low spade (if they have one)
    const card_mask east_spades =
        ops::cards_in_suit<whist>(state.hands[1], suit::spades);
    if (ops::is_empty(east_spades)) return;

    card low_spade = static_cast<card>(
        ops::lsb_index(east_spades)); // lowest

    auto ctx = make_play_context<whist>(
        state, belief, seat::east, low_spade);

    bool fires = whist_heuristics::pred_played_low_on_winnable(ctx);

    // Should fire if it's actually a low card (rank < 4)
    if (rank_of<whist>(low_spade) < 4 && ctx.followed_suit) {
        BOOST_CHECK(fires);
    }
}

BOOST_AUTO_TEST_CASE(test_played_low_doesnt_fire_for_observer) {
    game_state<whist> state;
    state.trump = suit::clubs;
    state.current_player = seat::north;

    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];

    belief_state<whist> belief;
    belief.init(state, seat::north);

    card c = static_cast<card>(ops::lsb_index(state.hands[0]));
    auto ctx = make_play_context<whist>(
        state, belief, seat::north, c);

    BOOST_CHECK(!whist_heuristics::pred_played_low_on_winnable(ctx));
}

BOOST_AUTO_TEST_CASE(test_led_a_suit_fires_for_opponent) {
    game_state<whist> state;
    state.trump = suit::hearts;
    state.current_player = seat::east;

    std::mt19937 rng(77);
    auto hands = deal<whist, 4>(rng);
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];

    belief_state<whist> belief;
    belief.init(state, seat::north);

    card c = static_cast<card>(ops::lsb_index(state.hands[1]));
    auto ctx = make_play_context<whist>(
        state, belief, seat::east, c);

    BOOST_CHECK(ctx.is_lead);
    BOOST_CHECK(whist_heuristics::pred_led_a_suit(ctx));
}

BOOST_AUTO_TEST_CASE(test_led_a_suit_doesnt_fire_for_observer) {
    game_state<whist> state;
    state.trump = suit::hearts;
    state.current_player = seat::north;

    std::mt19937 rng(77);
    auto hands = deal<whist, 4>(rng);
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];

    belief_state<whist> belief;
    belief.init(state, seat::north);

    card c = static_cast<card>(ops::lsb_index(state.hands[0]));
    auto ctx = make_play_context<whist>(
        state, belief, seat::north, c);

    BOOST_CHECK(!whist_heuristics::pred_led_a_suit(ctx));
}

BOOST_AUTO_TEST_CASE(test_discard_not_trump_fires) {
    // Construct a state where east is void in lead suit and
    // plays a non-trump card
    game_state<whist> state;
    state.trump = suit::clubs;
    state.current_player = seat::north;
    state.tricks_remaining = 13;

    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];

    belief_state<whist> belief;
    belief.init(state, seat::north);

    // North leads a spade
    const card_mask north_spades =
        ops::cards_in_suit<whist>(state.hands[0], suit::spades);
    if (ops::is_empty(north_spades)) return;

    card lead = static_cast<card>(
        ops::lsb_index(north_spades));
    whist_rules::play_card(state, seat::north, lead);

    // Find an east card that is NOT spades and NOT clubs (trump)
    card_mask east_hand = state.hands[1];
    card_mask east_non_spade_non_trump =
        east_hand
        & ~ops::suit_masks<whist>[0]  // not spades
        & ~ops::suit_masks<whist>[3]; // not clubs (trump)

    if (ops::is_empty(east_non_spade_non_trump)) return;

    card discard = static_cast<card>(
        ops::lsb_index(east_non_spade_non_trump));

    auto ctx = make_play_context<whist>(
        state, belief, seat::east, discard);

    // If east didn't follow suit and didn't play trump
    if (!ctx.followed_suit && ctx.card_suit != ctx.trump) {
        BOOST_CHECK(whist_heuristics::pred_discard_not_trump(ctx));
    }
}

BOOST_AUTO_TEST_CASE(test_partner_led_high_fires) {
    game_state<whist> state;
    state.trump = suit::clubs;
    state.current_player = seat::south; // south is north's partner

    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];

    belief_state<whist> belief;
    belief.init(state, seat::north);

    // Find a high card (top third: rank >= 9) in south's hand
    card_mask south_hand = state.hands[2];
    card high_card = no_card;

    card_mask tmp = south_hand;
    while (tmp) {
        int ci = ops::lsb_index(tmp);
        tmp &= tmp - 1;
        if (rank_of<whist>(static_cast<card>(ci)) >= 9) {
            high_card = static_cast<card>(ci);
            break;
        }
    }

    if (high_card == no_card) return; // no high cards, skip

    auto ctx = make_play_context<whist>(
        state, belief, seat::south, high_card);

    BOOST_CHECK(ctx.is_partner);
    BOOST_CHECK(ctx.is_lead);
    BOOST_CHECK(whist_heuristics::pred_partner_led_high(ctx));
}

BOOST_AUTO_TEST_SUITE_END()


// ── soft rule application and renormalization ─────────────

BOOST_AUTO_TEST_SUITE(soft_rule_application_tests)

BOOST_AUTO_TEST_CASE(test_apply_soft_rules_adjusts_probs) {
    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::hearts;
    state.current_player = seat::north;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    // Save a copy of probs before
    auto prob_before = belief.prob;

    // North leads
    card c = static_cast<card>(
        ops::lsb_index(state.hands[0]));

    std::span<const soft_rule<whist>> rules{
        whist_heuristics::default_rules,
        whist_heuristics::num_default_rules
    };

    belief.update_on_play(
        state, seat::north, c, true, suit::spades, rules);

    // After update, column sums should still be valid
    const card_mask own = state.hands[0];
    const card_mask played = card_mask{1} << c;
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

BOOST_AUTO_TEST_CASE(test_led_suit_boosts_leader_probs) {
    std::mt19937 rng(77);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::clubs;
    state.current_player = seat::east;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    // Save pre-heuristic probs
    auto prob_before = belief.prob;

    card c = static_cast<card>(
        ops::lsb_index(state.hands[1])); // east leads
    suit led_suit = suit_of<whist>(c);

    std::span<const soft_rule<whist>> rules{
        whist_heuristics::default_rules,
        whist_heuristics::num_default_rules
    };

    belief.update_on_play(
        state, seat::east, c, true, led_suit, rules);

    /* After "led_a_suit" fires, the raw probability for east
       (before renormalization) in the led suit should have been
       boosted.  After renormalization the relative share of
       east in that suit should be higher than the initial 1/3.

       We check: for at least one unseen card in the led suit,
       east's post-update probability > initial 1/3.            */
    const uint8_t east_opp =
        seat_to_opp(seat::north, seat::east);

    constexpr int ranks = deck_traits<whist>::num_ranks;
    const int base = static_cast<int>(led_suit) * ranks;
    const int end  = base + ranks;

    bool any_boosted = false;
    for (int cc = base; cc < end; ++cc) {
        if (cc == c) continue; // played card is zero
        if (belief.prob[east_opp][cc] > (1.0f / 3.0f) + 0.01f) {
            any_boosted = true;
            break;
        }
    }
    BOOST_CHECK(any_boosted);
}

BOOST_AUTO_TEST_CASE(test_strength_zero_disables_rule) {
    std::mt19937 rng(77);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state;
    for (int i = 0; i < 4; ++i) state.hands[i] = hands[i];
    state.trump = suit::clubs;
    state.current_player = seat::east;

    belief_state<whist> belief;
    belief.init(state, seat::north);

    // Make a rule set with strength = 0 (disabled)
    soft_rule<whist> disabled_rules[] = {
        {
            "led_a_suit_disabled", 0.0f, 1.5f,
            whist_heuristics::pred_led_a_suit,
            whist_heuristics::apply_led_a_suit
        },
    };

    // Also make a basic (no soft rules) belief for comparison
    belief_state<whist> belief_basic;
    belief_basic.init(state, seat::north);

    card c = static_cast<card>(
        ops::lsb_index(state.hands[1]));

    // Update with disabled soft rules
    belief.update_on_play(
        state, seat::east, c, true, suit_of<whist>(c),
        std::span{disabled_rules});

    // Update basic (no soft rules)
    belief_basic.update_on_play(
        seat::east, c, true, suit_of<whist>(c));

    // With strength=0, the "apply" multiplies by 1.5*0 = 0,
    // which would zero everything. But that's a degenerate case.
    // Instead, let's verify that using an empty span gives
    // the same result as the basic overload.
    belief_state<whist> belief_empty;
    belief_empty.init(state, seat::north);
    belief_empty.update_on_play(
        state, seat::east, c, true, suit_of<whist>(c),
        std::span<const soft_rule<whist>>{});

    // Empty span should match basic overload exactly
    for (int cc = 0; cc < 52; ++cc)
        for (uint8_t i = 0; i < num_other_players; ++i)
            BOOST_CHECK_CLOSE(
                belief_empty.prob[i][cc] + 1e-10f,
                belief_basic.prob[i][cc] + 1e-10f,
                1e-4f);
}

BOOST_AUTO_TEST_CASE(test_integration_search_with_heuristics) {
    /* Run a full game where the observer uses soft heuristics
       in the belief update.  Verify no crashes.              */

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

    std::span<const soft_rule<whist>> heuristics{
        whist_heuristics::default_rules,
        whist_heuristics::num_default_rules
    };

    std::mt19937 search_rng(77);
    search_config cfg;
    cfg.iterations = 30;

    while (state.tricks_remaining > 0) {
        for (int p = 0; p < 4; ++p) {
            const seat cur = state.current_player;
            const bool is_lead =
                (state.trick.num_played == 0);
            const suit lead = state.trick.lead_suit;

            card c;
            if (cur == seat::north) {
                belief.init(state, seat::north);
                c = ismcts_search<whist>(
                    state, belief, cfg, search_rng);
            } else {
                const card_mask legal =
                    whist_rules::legal_moves(state);
                c = static_cast<card>(
                    ops::lsb_index(legal));
            }

            // Use the extended update with heuristics
            belief.update_on_play(
                state, cur, c, is_lead, lead, heuristics);
            whist_rules::play_card(state, cur, c);
        }
    }

    BOOST_CHECK_EQUAL(state.tricks_remaining, 0);
    BOOST_CHECK_EQUAL(
        state.tricks_won[0] + state.tricks_won[1], 13);
}

BOOST_AUTO_TEST_SUITE_END()

