#pragma once

#include "belief.h"
#include "determinize.h"
#include "ismcts.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

namespace mu {

    // ── search configuration ────────────────────────────────

    /**
     * Tuneable knobs for ISMCTS.
     *
     * iterations  — total number of determinize → rollout cycles.
     * exploration — UCB1 exploration constant C  (√2 ≈ 1.414).
     */
    struct search_config {
        int   iterations  = 1000;
        float exploration = 1.41421356f;
    };

    // ── ISMCTS tree node ────────────────────────────────────

    /**
     * Single node in the SO-ISMCTS (Single Observer) tree.
     *
     * Keyed by the action (card) the observer played to reach
     * this node.  Shared across determinizations — different
     * worlds can visit the same node via the same action
     * sequence.
     *
     * Fields
     * ------
     * action        — card that was played to arrive here.
     * visits        — how many times simulation passed through.
     * availability  — how many times this action was *legal*
     *                 when its parent was visited (needed for
     *                 the ISMCTS variant of UCB1).
     * total_value   — cumulative payoff (tricks-won fraction).
     * children[c]   — index into the arena for action c, or
     *                 -1 if not yet expanded.
     * expanded      — bitmask of actions that have been given
     *                 a child node.
     */
    template<typename Variant>
    struct ismcts_node {
        static constexpr int N = num_cards<Variant>;

        card      action       = no_card;
        uint32_t  visits       = 0;
        uint32_t  availability = 0;
        float     total_value  = 0.0f;

        std::array<int32_t, N> children;
        card_mask              expanded = 0;

        ismcts_node() { children.fill(-1); }
    };

    // ── UCB1 with availability ──────────────────────────────

    /**
     * UCB1 score using the availability count in place of the
     * parent visit count — the standard adjustment for ISMCTS
     * where not every action is legal in every determinization.
     *
     *   exploit = total_value / visits
     *   explore = C · √( ln(availability) / visits )
     *
     * Returns +∞ for unvisited nodes so they are selected
     * before any revisits.
     */
    template<typename Variant>
    [[nodiscard]] inline float ucb1_score(
            const ismcts_node<Variant>& node,
            float C) {

        if (node.visits == 0)
            return std::numeric_limits<float>::max();

        const float exploit = node.total_value / static_cast<float>(node.visits);
        const float explore =
            C * std::sqrt( std::log(static_cast<float>(node.availability)) / static_cast<float>(node.visits));

        return exploit + explore;
    }

    // ── SO-ISMCTS search ────────────────────────────────────

    /**
     * Single-Observer Information Set Monte Carlo Tree Search.
     *
     * Builds a tree only at the observer's decision points.
     * Opponent moves are played uniformly at random in both
     * the tree-descent and rollout phases.
     *
     * Algorithm (per iteration)
     * -------------------------
     *   1. Determinize — sample a complete deal from the CPM.
     *   2. Tree descent — alternate between:
     *        • Observer's turn → UCB1 select / expand.
     *        • Other players   → uniform random.
     *   3. After the first expansion, switch to rollout mode
     *      (all players uniform random) until the game ends.
     *   4. Backpropagate the normalised payoff through all
     *      observer nodes on the descent path.
     *
     * @return  The card the observer should play
     *          (most-visited root child).
     */
    template<typename Variant,
             typename Rules = rules<Variant>,
             typename RNG>
    [[nodiscard]] card ismcts_search(
            const game_state<Variant>& root_state,
            const belief_state<Variant>& belief,
            const search_config& config,
            RNG& rng) {

        constexpr int N = num_cards<Variant>;
        constexpr int total_tricks = N / num_players;

        const seat    obs = belief.observer;
        const uint8_t obs_part = partnership_of(obs);

        assert(root_state.current_player == obs
               && "search must start at observer's turn");

        // ── tree arena (pre-allocated) ──────────────────────

        std::vector<ismcts_node<Variant>> arena;
        arena.reserve(config.iterations + 1);
        arena.emplace_back();                       // root = 0
        constexpr int32_t ROOT = 0;

        std::uniform_int_distribution<int> move_dist;

        // ── main loop ───────────────────────────────────────

        for (int iter = 0; iter < config.iterations; ++iter) {

            // 1. Determinize
            game_state<Variant> state =
                determinize<Variant>(root_state, belief, rng);

            // 2. Tree descent / rollout
            int32_t              node_idx = ROOT;
            std::vector<int32_t> path;
            path.reserve(total_tricks);
            path.push_back(ROOT);
            bool in_tree = true;

            while (state.tricks_remaining > 0) {
                const seat cur = state.current_player;

                if (cur == obs && in_tree) {

                    // ── observer's turn: tree policy ────────

                    const card_mask legal =
                        Rules::legal_moves(state);
                    assert(legal != 0);

                    /* Update availability for every existing
                       child whose action is currently legal.  */
                    {
                        card_mask leg = legal;
                        while (leg) {
                            const int ci = ops::lsb_index(leg);
                            leg &= leg - 1;
                            const int32_t ch =
                                arena[node_idx].children[ci];
                            if (ch >= 0)
                                arena[ch].availability++;
                        }
                    }

                    const card_mask unexplored =
                        legal & ~arena[node_idx].expanded;

                    if (!ops::is_empty(unexplored)) {

                        // ── expand ──────────────────────────

                        const int cnt =
                            ops::popcount(unexplored);
                        const int pick =
                            move_dist(rng, std::uniform_int_distribution<int> ::param_type{0, cnt - 1});
                        const card_mask sel = ops::nth_set_bit(unexplored, pick);
                        const card c = static_cast<card>( ops::lsb_index(sel));

                        const auto child_idx =
                            static_cast<int32_t>(arena.size());
                        arena.emplace_back();
                        /* Safe: arena was pre-reserved. */
                        arena[child_idx].action       = c;
                        arena[child_idx].availability = 1;

                        arena[node_idx].children[c] = child_idx;
                        arena[node_idx].expanded   |= sel;

                        Rules::play_card(state, obs, c);
                        path.push_back(child_idx);
                        node_idx = child_idx;

                        in_tree = false;   // → rollout

                    } else {

                        // ── select (UCB1) ───────────────────

                        float   best_score = -1.0f;
                        int32_t best_child = -1;
                        card    best_card  = no_card;

                        card_mask leg = legal;
                        while (leg) {
                            const int ci =
                                ops::lsb_index(leg);
                            leg &= leg - 1;
                            const int32_t ch =
                                arena[node_idx].children[ci];
                            if (ch < 0) continue;

                            const float s = ucb1_score(
                                arena[ch],
                                config.exploration);
                            if (s > best_score) {
                                best_score = s;
                                best_child = ch;
                                best_card  =
                                    static_cast<card>(ci);
                            }
                        }

                        assert(best_child >= 0);

                        Rules::play_card(state, obs, best_card);
                        path.push_back(best_child);
                        node_idx = best_child;
                    }

                } else {

                    // ── other player / rollout: random ──────

                    const card_mask legal =
                        Rules::legal_moves(state);
                    const int cnt = ops::popcount(legal);
                    const int pick =
                        move_dist(rng, std::uniform_int_distribution<int> ::param_type{0, cnt - 1});
                    const card_mask sel = ops::nth_set_bit(legal, pick);
                    const card c = static_cast<card>(ops::lsb_index(sel));

                    Rules::play_card(state, cur, c);
                }
            }

            // 3. Payoff (normalised to [0, 1])
            const float value = static_cast<float>( state.tricks_won[obs_part]) / static_cast<float>(total_tricks);

            // 4. Backpropagate
            for (const int32_t idx : path) {
                arena[idx].visits++;
                arena[idx].total_value += value;
            }
        }

        // ── choose most-visited root child ──────────────────

        uint32_t best_visits  = 0;
        card     best_action  = no_card;

        for (int c = 0; c < N; ++c) {
            const int32_t ch = arena[ROOT].children[c];
            if (ch >= 0 && arena[ch].visits > best_visits) {
                best_visits = arena[ch].visits;
                best_action = static_cast<card>(c);
            }
        }

        assert(best_action != no_card && "search produced no action");
        return best_action;
    }

}

