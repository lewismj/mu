/**
 * bind.cpp — nanobind module exposing the mu C++ engine to Python.
 *
 * This is a thin binding layer.  All input validation lives in the
 * Python wrapper package (mu.*); the C++ asserts remain as a
 * debug safety-net only.
 *
 * Module name: _mu_core
 */

#include <nanobind/nanobind.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/ndarray.h>

#include <random>
#include <sstream>
#include <mu.h>

namespace nb = nanobind;
using namespace nb::literals;

// ── RNG wrapper for deterministic seeding ─────────────────────

static thread_local std::mt19937_64 g_rng{std::random_device{}()};

void seed_rng(uint64_t seed) {
    g_rng.seed(seed);
}

// ── Card mask helpers ─────────────────────────────────────────

template<typename Variant>
std::vector<mu::card> mask_to_cards(mu::card_mask mask) {
    std::vector<mu::card> cards;
    cards.reserve(mu::ops::popcount(mask));
    while (mask) {
        int idx = mu::ops::lsb_index(mask);
        mask &= mask - 1;
        cards.push_back(static_cast<mu::card>(idx));
    }
    return cards;
}

mu::card_mask cards_to_mask(const std::vector<mu::card>& cards) {
    mu::card_mask mask = 0;
    for (auto c : cards) {
        mask |= mu::card_mask{1} << c;
    }
    return mask;
}

// ── Card string representation ────────────────────────────────

template<typename Variant>
std::string card_to_str(mu::card c) {
    constexpr int num_ranks = mu::deck_traits<Variant>::num_ranks;
    const char* rank_chars = (num_ranks == 13)
        ? "23456789TJQKA"
        : "6789TJQKA";  // Jass
    const char* suit_chars = "shdc";  // spades, hearts, diamonds, clubs

    int rank = c % num_ranks;
    int suit = c / num_ranks;

    std::string result;
    result += rank_chars[rank];
    result += suit_chars[suit];
    return result;
}

template<typename Variant>
std::string mask_to_str(mu::card_mask mask) {
    std::ostringstream oss;
    bool first = true;
    while (mask) {
        int idx = mu::ops::lsb_index(mask);
        mask &= mask - 1;
        if (!first) oss << " ";
        first = false;
        oss << card_to_str<Variant>(static_cast<mu::card>(idx));
    }
    return oss.str();
}

// ── Factory: create a new whist game ──────────────────────────

mu::game_state<mu::whist> whist_new_game(mu::suit trump, mu::seat dealer) {
    mu::game_state<mu::whist> state{};

    // Deal cards
    auto hands = mu::deal<mu::whist, 4>(g_rng);
    for (int i = 0; i < 4; ++i) {
        state.hands[i] = hands[i];
    }

    state.trump = trump;
    state.dealer = dealer;
    state.current_player = mu::next_seat(dealer);
    state.tricks_remaining = mu::num_cards<mu::whist> / mu::num_players;
    state.played = 0;
    state.tricks_won = {0, 0};

    return state;
}

// ── Wrapped search function (fixed RNG type) ──────────────────

mu::card whist_search(
    const mu::game_state<mu::whist>& state,
    const mu::belief_state<mu::whist>& belief,
    int iterations,
    float exploration
) {
    mu::search_config config{iterations, exploration};
    return mu::ismcts_search<mu::whist>(state, belief, config, g_rng);
}

// ── Wrapped determinize function ──────────────────────────────

mu::game_state<mu::whist> whist_determinize(
    const mu::game_state<mu::whist>& state,
    const mu::belief_state<mu::whist>& belief
) {
    return mu::determinize<mu::whist>(state, belief, g_rng);
}

// ── Wrapped rollout function ──────────────────────────────────

std::array<uint8_t, mu::num_partnerships> whist_rollout(
    mu::game_state<mu::whist> state
) {
    return mu::rollout<mu::whist>(state, g_rng);
}


NB_MODULE(_mu_core, m) {

    m.doc() = "mu card game engine — C++ backend";

    // ══════════════════════════════════════════════════════════
    // RNG Control
    // ══════════════════════════════════════════════════════════

    m.def("seed", &seed_rng, "seed"_a,
          "Seed the internal RNG for reproducible results.");

    // ══════════════════════════════════════════════════════════
    // Enums
    // ══════════════════════════════════════════════════════════

    nb::enum_<mu::seat>(m, "Seat", "Player seat at the table.")
        .value("NORTH", mu::seat::north)
        .value("EAST",  mu::seat::east)
        .value("SOUTH", mu::seat::south)
        .value("WEST",  mu::seat::west)
        .export_values();

    nb::enum_<mu::suit>(m, "Suit", "Card suit.")
        .value("SPADES",   mu::suit::spades)
        .value("HEARTS",   mu::suit::hearts)
        .value("DIAMONDS", mu::suit::diamonds)
        .value("CLUBS",    mu::suit::clubs)
        .export_values();

    // ══════════════════════════════════════════════════════════
    // Constants
    // ══════════════════════════════════════════════════════════

    m.attr("NUM_PLAYERS") = mu::num_players;
    m.attr("NUM_PARTNERSHIPS") = mu::num_partnerships;
    m.attr("WHIST_NUM_CARDS") = mu::num_cards<mu::whist>;
    m.attr("WHIST_NUM_RANKS") = mu::deck_traits<mu::whist>::num_ranks;
    m.attr("WHIST_CARDS_PER_HAND") = mu::num_cards<mu::whist> / mu::num_players;
    m.attr("NO_CARD") = mu::no_card;

    // ══════════════════════════════════════════════════════════
    // Card Mask Helpers
    // ══════════════════════════════════════════════════════════

    m.def("mask_to_cards", &mask_to_cards<mu::whist>, "mask"_a,
          "Convert a card_mask to a list of card indices.");

    m.def("cards_to_mask", &cards_to_mask, "cards"_a,
          "Convert a list of card indices to a card_mask.");

    m.def("card_str", &card_to_str<mu::whist>, "card"_a,
          "Convert a card index to a human-readable string (e.g., 'As', 'Kh').");

    m.def("mask_str", &mask_to_str<mu::whist>, "mask"_a,
          "Convert a card_mask to a human-readable string.");

    m.def("popcount", &mu::ops::popcount, "mask"_a,
          "Count the number of set bits (cards) in a mask.");

    m.def("suit_of", &mu::suit_of<mu::whist>, "card"_a,
          "Get the suit of a card index.");

    m.def("rank_of", &mu::rank_of<mu::whist>, "card"_a,
          "Get the rank index (0-12) of a card.");

    // ══════════════════════════════════════════════════════════
    // Seat Helpers
    // ══════════════════════════════════════════════════════════

    m.def("next_seat", &mu::next_seat, "seat"_a,
          "Get the next seat in clockwise order.");

    m.def("partner_of", &mu::partner_of, "seat"_a,
          "Get the partner's seat (opposite).");

    m.def("partnership_of", &mu::partnership_of, "seat"_a,
          "Get the partnership index (0 or 1) for a seat.");

    // ══════════════════════════════════════════════════════════
    // Trick State (nested in game_state, exposed separately)
    // ══════════════════════════════════════════════════════════

    nb::class_<mu::trick_state>(m, "TrickState", "State of the current trick.")
        .def_ro("mask", &mu::trick_state::mask,
                "Bitmask of cards played in this trick.")
        .def_ro("cards", &mu::trick_state::cards,
                "Cards played per seat (indexed by seat ordinal).")
        .def_ro("lead_suit", &mu::trick_state::lead_suit,
                "Suit of the first card played.")
        .def_ro("lead_seat", &mu::trick_state::lead_seat,
                "Seat that led this trick.")
        .def_ro("current_winner", &mu::trick_state::current_winner,
                "Seat currently winning the trick.")
        .def_ro("winning_card", &mu::trick_state::winning_card,
                "Index of the highest-priority card so far.")
        .def_ro("num_played", &mu::trick_state::num_played,
                "Number of cards played (0-4).")
        .def("complete", &mu::trick_state::complete,
             "Returns True if all 4 players have played.");

    // ══════════════════════════════════════════════════════════
    // Game State (Whist)
    // ══════════════════════════════════════════════════════════

    nb::class_<mu::game_state<mu::whist>>(m, "WhistState",
            "Full game state for 4-player Whist.")
        .def(nb::init<>())
        .def_rw("hands", &mu::game_state<mu::whist>::hands,
                "Card masks for each player's hand (indexed by seat).")
        .def_rw("played", &mu::game_state<mu::whist>::played,
                "Bitmask of all cards played so far.")
        .def_rw("trick", &mu::game_state<mu::whist>::trick,
                "Current trick state.")
        .def_rw("tricks_won", &mu::game_state<mu::whist>::tricks_won,
                "Tricks won per partnership [NS, EW].")
        .def_rw("trump", &mu::game_state<mu::whist>::trump,
                "Trump suit for this game.")
        .def_rw("dealer", &mu::game_state<mu::whist>::dealer,
                "Dealer seat.")
        .def_rw("current_player", &mu::game_state<mu::whist>::current_player,
                "Seat of the player to act.")
        .def_rw("tricks_remaining", &mu::game_state<mu::whist>::tricks_remaining,
                "Number of tricks left to play.")
        .def("current_hand_mask", &mu::game_state<mu::whist>::current_hand_mask,
             "Get the current player's hand as a card_mask.")
        .def("__repr__", [](const mu::game_state<mu::whist>& s) {
            std::ostringstream oss;
            oss << "WhistState(current=" << s.current_player
                << ", trump=" << s.trump
                << ", tricks_remaining=" << (int)s.tricks_remaining
                << ", tricks_won=[" << (int)s.tricks_won[0] << "," << (int)s.tricks_won[1] << "])";
            return oss.str();
        });

    // ══════════════════════════════════════════════════════════
    // Belief State (Whist)
    // ══════════════════════════════════════════════════════════

    // Type alias for the basic update_on_play overload (no heuristics)
    using belief_update_fn = void (mu::belief_state<mu::whist>::*)(
        mu::seat, mu::card, bool, mu::suit);

    nb::class_<mu::belief_state<mu::whist>>(m, "WhistBelief",
            "Card probability matrix for an observer in Whist.")
        .def(nb::init<>())
        .def_rw("observer", &mu::belief_state<mu::whist>::observer,
                "The observing seat.")
        .def_rw("prob", &mu::belief_state<mu::whist>::prob,
                "Probability matrix [3 opponents][52 cards].")
        .def_rw("possible", &mu::belief_state<mu::whist>::possible,
                "Hard constraint masks per opponent.")
        .def("init", &mu::belief_state<mu::whist>::init,
             "state"_a, "observer"_a,
             "Initialize with uniform prior from a game state.")
        .def("update_on_play",
             static_cast<belief_update_fn>(&mu::belief_state<mu::whist>::update_on_play),
             "player"_a, "card"_a, "is_lead"_a, "lead_suit"_a,
             "Update beliefs after a card is played.");

    // ══════════════════════════════════════════════════════════
    // Rules (Whist) — Static Methods
    // ══════════════════════════════════════════════════════════

    m.def("legal_moves", &mu::rules<mu::whist>::legal_moves, "state"_a,
          "Get the legal moves for the current player as a card_mask.");

    m.def("play_card", &mu::rules<mu::whist>::play_card,
          "state"_a, "seat"_a, "card"_a,
          "Play a card, updating the game state in place.");

    // ══════════════════════════════════════════════════════════
    // Game Factory
    // ══════════════════════════════════════════════════════════

    m.def("whist_new_game", &whist_new_game,
          "trump"_a, "dealer"_a,
          "Create a new Whist game with shuffled hands.");

    // ══════════════════════════════════════════════════════════
    // Search & Simulation
    // ══════════════════════════════════════════════════════════

    m.def("whist_search", &whist_search,
          "state"_a, "belief"_a, "iterations"_a = 1000, "exploration"_a = 1.41421356f,
          "Run ISMCTS search and return the best card to play.");

    m.def("whist_determinize", &whist_determinize,
          "state"_a, "belief"_a,
          "Sample a complete deal from the belief state.");

    m.def("whist_rollout", &whist_rollout, "state"_a,
          "Perform a random rollout and return tricks won per partnership.");

    // ══════════════════════════════════════════════════════════
    // Seat/Opponent Index Conversion
    // ══════════════════════════════════════════════════════════

    m.def("seat_to_opp", &mu::seat_to_opp, "observer"_a, "seat"_a,
          "Map a seat to opponent index (0-2) relative to observer.");

    m.def("opp_to_seat", &mu::opp_to_seat, "observer"_a, "opp"_a,
          "Map opponent index (0-2) back to a seat.");

}
