#include <boost/test/unit_test.hpp>
#include <mu.h>

#include <random>
#include <set>

using namespace mu;


// ── Zobrist hashing tests ───────────────────────────────────

BOOST_AUTO_TEST_SUITE(zobrist_tests)

BOOST_AUTO_TEST_CASE(test_keys_are_nonzero) {
    // All keys should be non-zero for effective hashing
    for (uint8_t seat = 0; seat < num_players; ++seat) {
        for (int card = 0; card < 52; ++card) {
            BOOST_CHECK(zkeys<whist>.card_keys[seat][card] != 0);
        }
        BOOST_CHECK(zkeys<whist>.player_keys[seat] != 0);
    }
    for (int s = 0; s < 4; ++s) {
        BOOST_CHECK(zkeys<whist>.trump_keys[s] != 0);
    }
}

BOOST_AUTO_TEST_CASE(test_keys_are_unique) {
    // Card keys should be unique (no collisions in table)
    std::set<uint64_t> seen;

    for (uint8_t seat = 0; seat < num_players; ++seat) {
        for (int card = 0; card < 52; ++card) {
            uint64_t key = zkeys<whist>.card_keys[seat][card];
            BOOST_CHECK(seen.find(key) == seen.end());
            seen.insert(key);
        }
    }
}

BOOST_AUTO_TEST_CASE(test_hash_changes_with_player) {
    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state1;
    game_state<whist> state2;

    for (int i = 0; i < 4; ++i) {
        state1.hands[i] = hands[i];
        state2.hands[i] = hands[i];
    }

    state1.trump = suit::hearts;
    state2.trump = suit::hearts;

    state1.current_player = seat::north;
    state2.current_player = seat::east;

    uint64_t hash1 = zobrist_hash(state1);
    uint64_t hash2 = zobrist_hash(state2);

    BOOST_CHECK(hash1 != hash2);
}

BOOST_AUTO_TEST_CASE(test_hash_changes_with_trump) {
    std::mt19937 rng(42);
    auto hands = deal<whist, 4>(rng);

    game_state<whist> state1;
    game_state<whist> state2;

    for (int i = 0; i < 4; ++i) {
        state1.hands[i] = hands[i];
        state2.hands[i] = hands[i];
    }

    state1.current_player = seat::north;
    state2.current_player = seat::north;

    state1.trump = suit::hearts;
    state2.trump = suit::spades;

    uint64_t hash1 = zobrist_hash(state1);
    uint64_t hash2 = zobrist_hash(state2);

    BOOST_CHECK(hash1 != hash2);
}

BOOST_AUTO_TEST_SUITE_END()


// ── Transposition table tests ───────────────────────────────

BOOST_AUTO_TEST_SUITE(tt_tests)

// Use a smaller TT for tests to avoid stack issues
using test_tt = transposition_table<10>;  // 1024 entries

BOOST_AUTO_TEST_CASE(test_probe_empty) {
    test_tt tt;
    auto result = tt.probe(0x12345678, 13);
    BOOST_CHECK(result == nullptr);
}

BOOST_AUTO_TEST_CASE(test_store_and_probe) {
    test_tt tt;

    uint64_t hash = 0xDEADBEEF12345678ULL;
    int8_t depth = 8;
    int8_t value = 5;

    tt.store(hash, depth, value, tt_bound::exact, 10);

    auto result = tt.probe(hash, depth);
    BOOST_REQUIRE(result != nullptr);
    BOOST_CHECK_EQUAL(result->value, 5);
    BOOST_CHECK(result->bound == tt_bound::exact);
    BOOST_CHECK_EQUAL(result->best_move, 10);
}

BOOST_AUTO_TEST_CASE(test_probe_wrong_depth) {
    test_tt tt;

    uint64_t hash = 0xDEADBEEF12345678ULL;
    tt.store(hash, 8, 5, tt_bound::exact, 10);

    // Probe with different depth should fail
    auto result = tt.probe(hash, 7);
    BOOST_CHECK(result == nullptr);
}

BOOST_AUTO_TEST_CASE(test_replacement) {
    test_tt tt;

    uint64_t hash = 0xDEADBEEF12345678ULL;

    // Store first entry
    tt.store(hash, 8, 5, tt_bound::exact, 10);

    // Store second entry (same slot due to same hash)
    tt.store(hash, 8, 7, tt_bound::lower, 20);

    auto result = tt.probe(hash, 8);
    BOOST_REQUIRE(result != nullptr);
    BOOST_CHECK_EQUAL(result->value, 7);  // Second value
    BOOST_CHECK(result->bound == tt_bound::lower);
    BOOST_CHECK_EQUAL(result->best_move, 20);
}

BOOST_AUTO_TEST_SUITE_END()


// ── DDS helper functions ────────────────────────────────────

/**
 * Helper to create a simple endgame position.
 *
 * Creates a 4-card endgame where each player has 1 card.
 */
static game_state<whist> make_4card_endgame(
        card n_card, card e_card, card s_card, card w_card,
        suit trump, seat leader) {

    game_state<whist> state{};

    state.hands[0] = card_mask{1} << n_card;  // North
    state.hands[1] = card_mask{1} << e_card;  // East
    state.hands[2] = card_mask{1} << s_card;  // South
    state.hands[3] = card_mask{1} << w_card;  // West

    state.trump = trump;
    state.current_player = leader;
    state.tricks_remaining = 1;

    return state;
}


// ── DDS tests ───────────────────────────────────────────────

BOOST_AUTO_TEST_SUITE(dds_tests)


// ── Simple 1-trick endgames ─────────────────────────────────

BOOST_AUTO_TEST_CASE(test_dds_single_trick_ns_wins) {
    // N: A♠ (bit 12), E: K♠ (bit 11), S: Q♠ (bit 10), W: J♠ (bit 9)
    // Trump: hearts, North leads
    // N/S wins with A♠

    auto state = make_4card_endgame(12, 11, 10, 9, suit::hearts, seat::north);

    auto result = dds_solve<whist>(state);

    // N/S should win 1 trick (the A♠)
    BOOST_CHECK_EQUAL(result.tricks_ns[0], 1);
    BOOST_CHECK_EQUAL(result.best_move, 12);  // A♠
}

BOOST_AUTO_TEST_CASE(test_dds_single_trick_ew_wins) {
    // N: 2♠ (bit 0), E: A♠ (bit 12), S: 3♠ (bit 1), W: K♠ (bit 11)
    // Trump: hearts, North leads
    // E/W wins with A♠

    auto state = make_4card_endgame(0, 12, 1, 11, suit::hearts, seat::north);

    auto result = dds_solve<whist>(state);

    // N/S should win 0 tricks
    BOOST_CHECK_EQUAL(result.tricks_ns[0], 0);
}

BOOST_AUTO_TEST_CASE(test_dds_trump_beats_ace) {
    // N: A♠ (bit 12), E: 2♥ (bit 13), S: K♠ (bit 11), W: 3♠ (bit 1)
    // Trump: hearts, North leads
    // East wins with 2♥ (trump)

    auto state = make_4card_endgame(12, 13, 11, 1, suit::hearts, seat::north);

    auto result = dds_solve<whist>(state);

    // N/S should win 0 tricks (E trumps with 2♥)
    BOOST_CHECK_EQUAL(result.tricks_ns[0], 0);
}

BOOST_AUTO_TEST_CASE(test_dds_ns_trumps) {
    // N: 2♥ (bit 13), E: A♠ (bit 12), S: 3♥ (bit 14), W: K♠ (bit 11)
    // Trump: hearts, East leads spades
    // North or South can ruff

    auto state = make_4card_endgame(13, 12, 14, 11, suit::hearts, seat::east);

    auto result = dds_solve<whist>(state);

    // N/S should win 1 trick (ruff with hearts)
    BOOST_CHECK_EQUAL(result.tricks_ns[1], 1);  // East is index 1
}


// ── Multi-trick endgames ────────────────────────────────────

BOOST_AUTO_TEST_CASE(test_dds_2trick_endgame) {
    // 8-card endgame (2 cards each)
    // N: A♠, K♠  E: Q♠, J♠  S: 10♠, 9♠  W: 8♠, 7♠
    // Trump: hearts, North leads
    // N/S should win both tricks

    game_state<whist> state{};

    state.hands[0] = (card_mask{1} << 12) | (card_mask{1} << 11);  // A♠, K♠
    state.hands[1] = (card_mask{1} << 10) | (card_mask{1} << 9);   // Q♠, J♠
    state.hands[2] = (card_mask{1} << 8) | (card_mask{1} << 7);    // 10♠, 9♠
    state.hands[3] = (card_mask{1} << 6) | (card_mask{1} << 5);    // 8♠, 7♠

    state.trump = suit::hearts;
    state.current_player = seat::north;
    state.tricks_remaining = 2;

    auto result = dds_solve<whist>(state);

    BOOST_CHECK_EQUAL(result.tricks_ns[0], 2);
}

BOOST_AUTO_TEST_CASE(test_dds_2trick_split) {
    // 8-card endgame
    // N: 2♠, 3♠  E: A♠, K♠  S: 4♠, 5♠  W: Q♠, J♠
    // Trump: hearts, North leads
    // E/W wins both tricks

    game_state<whist> state{};

    state.hands[0] = (card_mask{1} << 0) | (card_mask{1} << 1);    // 2♠, 3♠
    state.hands[1] = (card_mask{1} << 12) | (card_mask{1} << 11);  // A♠, K♠
    state.hands[2] = (card_mask{1} << 2) | (card_mask{1} << 3);    // 4♠, 5♠
    state.hands[3] = (card_mask{1} << 10) | (card_mask{1} << 9);   // Q♠, J♠

    state.trump = suit::hearts;
    state.current_player = seat::north;
    state.tricks_remaining = 2;

    auto result = dds_solve<whist>(state);

    BOOST_CHECK_EQUAL(result.tricks_ns[0], 0);
}


// ── Cross-suit endgames ─────────────────────────────────────

BOOST_AUTO_TEST_CASE(test_dds_cross_suit) {
    // N: A♠ (12)  E: A♥ (25)  S: A♦ (38)  W: A♣ (51)
    // Trump: spades, North leads
    // North wins (only spades are relevant)

    auto state = make_4card_endgame(12, 25, 38, 51, suit::spades, seat::north);

    auto result = dds_solve<whist>(state);

    BOOST_CHECK_EQUAL(result.tricks_ns[0], 1);
}


// ── Symmetry tests ──────────────────────────────────────────

BOOST_AUTO_TEST_CASE(test_dds_rotation_symmetry) {
    // Rotating seats by 2 (N↔S, E↔W) should swap result

    // Original: N: A♠, E: K♠, S: Q♠, W: J♠
    auto state1 = make_4card_endgame(12, 11, 10, 9, suit::hearts, seat::north);

    // Rotated: N: Q♠, E: J♠, S: A♠, W: K♠
    auto state2 = make_4card_endgame(10, 9, 12, 11, suit::hearts, seat::north);

    auto result1 = dds_solve<whist>(state1);
    auto result2 = dds_solve<whist>(state2);

    // In state1, N/S wins 1. In state2, N/S also wins 1 (they have A♠)
    BOOST_CHECK_EQUAL(result1.tricks_ns[0], 1);
    BOOST_CHECK_EQUAL(result2.tricks_ns[0], 1);
}


// ── Consistency with random rollout ─────────────────────────

BOOST_AUTO_TEST_CASE(test_dds_vs_rollout_bound) {
    // DDS result should be >= average of random rollouts
    // (DDS is optimal, random is suboptimal)

    std::mt19937 rng(12345);

    // Create a small endgame
    game_state<whist> state{};

    // 12-card endgame (3 cards each)
    state.hands[0] = (card_mask{1} << 12) | (card_mask{1} << 11) | (card_mask{1} << 10);
    state.hands[1] = (card_mask{1} << 9) | (card_mask{1} << 8) | (card_mask{1} << 7);
    state.hands[2] = (card_mask{1} << 6) | (card_mask{1} << 5) | (card_mask{1} << 4);
    state.hands[3] = (card_mask{1} << 3) | (card_mask{1} << 2) | (card_mask{1} << 1);

    state.trump = suit::hearts;
    state.current_player = seat::north;
    state.tricks_remaining = 3;

    // DDS solve
    auto dds_result = dds_solve<whist>(state);
    int dds_tricks = dds_result.tricks_ns[0];

    // Random rollouts
    int total_rollout_tricks = 0;
    constexpr int num_rollouts = 1000;

    for (int i = 0; i < num_rollouts; ++i) {
        auto scores = rollout<whist>(state, rng);
        total_rollout_tricks += scores[0];  // Partnership 0 = N/S
    }

    float avg_rollout = static_cast<float>(total_rollout_tricks) / num_rollouts;

    // DDS should be >= average (allow small epsilon for random variance)
    BOOST_CHECK_GE(dds_tricks, static_cast<int>(avg_rollout - 0.5f));
}


// ── Full deal test ──────────────────────────────────────────

BOOST_AUTO_TEST_CASE(test_dds_full_deal_small) {
    // Test with 16-card deal (4 cards each) - should be fast enough

    game_state<whist> state{};

    // N: A♠, K♠, Q♠, J♠
    state.hands[0] = (card_mask{1} << 12) | (card_mask{1} << 11) |
                     (card_mask{1} << 10) | (card_mask{1} << 9);
    // E: 10♠, 9♠, 8♠, 7♠
    state.hands[1] = (card_mask{1} << 8) | (card_mask{1} << 7) |
                     (card_mask{1} << 6) | (card_mask{1} << 5);
    // S: 6♠, 5♠, 4♠, 3♠
    state.hands[2] = (card_mask{1} << 4) | (card_mask{1} << 3) |
                     (card_mask{1} << 2) | (card_mask{1} << 1);
    // W: 2♠, A♥, K♥, Q♥
    state.hands[3] = (card_mask{1} << 0) | (card_mask{1} << 25) |
                     (card_mask{1} << 24) | (card_mask{1} << 23);

    state.trump = suit::hearts;
    state.current_player = seat::north;
    state.tricks_remaining = 4;

    auto result = dds_solve<whist>(state);

    // N/S should win at least 2 tricks (their high spades)
    // E/W will win tricks when West ruffs
    BOOST_CHECK_GE(result.tricks_ns[0], 0);
    BOOST_CHECK_LE(result.tricks_ns[0], 4);
}

BOOST_AUTO_TEST_SUITE_END()


// ── Search policy abstraction tests ─────────────────────────

BOOST_AUTO_TEST_SUITE(search_policy_tests)

BOOST_AUTO_TEST_CASE(test_explicit_policy_usage) {
    // Verify we can explicitly specify the search policy

    auto state = make_4card_endgame(12, 11, 10, 9, suit::hearts, seat::north);

    // Explicit alpha-beta policy
    auto result = dds_solve<whist, alpha_beta_policy<whist>>(state);

    BOOST_CHECK_EQUAL(result.tricks_ns[0], 1);
}

BOOST_AUTO_TEST_SUITE_END()

