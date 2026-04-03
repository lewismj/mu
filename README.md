# Mu Core Project Design

**mu/core** is a high-performance C++ library for trick-taking card games (e.g., Whist, Bridge, Jass) featuring 
an **Information Set Monte Carlo Tree Search (ISMCTS)** engine. It uses bitboard representations for card sets 
and template-based game variants for zero-overhead abstractions.

---

## Table of Contents

1. [System Architecture](#system-architecture)
2. [Core Data Structures](#core-data-structures)
   - [Card Representation (Bitboards)](#card-representation-bitboards)
   - [Game State](#game-state)
   - [Trick State](#trick-state)
3. [Algorithms](#algorithms)
   - [Bitwise Operations](#bitwise-operations)
   - [Trick Resolution](#trick-resolution)
   - [Legal Move Generation](#legal-move-generation)
4. [Belief Model](#belief-model)
   - [Probability Matrix](#probability-matrix)
   - [Hard Constraints](#hard-constraints)
   - [Soft Rules (Heuristics)](#soft-rules-heuristics)
5. [Determinization Algorithm](#determinization-algorithm)
6. [ISMCTS Search Algorithm](#ismcts-search-algorithm)
   - [Tree Structure](#tree-structure)
   - [UCB1 Selection](#ucb1-selection)
   - [Rollout](#rollout)
   - [Backpropagation](#backpropagation)
7. [Double Dummy Solver (DDS)](#double-dummy-solver-dds)
   - [Zobrist Hashing](#zobrist-hashing)
   - [Transposition Table](#transposition-table)
   - [Alpha-Beta Search](#alpha-beta-search)
   - [Search Policy Abstraction](#search-policy-abstraction)
8. [Game Variants](#game-variants)
9. [ML Integration Opportunities](#ml-integration-opportunities)
10. [File Structure](#file-structure)

---

## System Architecture

The system is designed with a clear separation between:
- **Representation layer** — bitboard card masks, deck traits
- **Game logic layer** — state management, rules, legal moves
- **Belief layer** — probability tracking, heuristic inference
- **Search layer** — ISMCTS, determinization, rollouts

```
┌─────────────────────────────────────────────────────────────┐
│                    ISMCTS Search Engine                     │
│  ┌─────────────────┐    ┌─────────────────┐                 │
│  │  Determinizer   │◄───│  Belief State   │                 │
│  │ (weighted deal) │    │ (prob matrix)   │                 │
│  └────────┬────────┘    └────────▲────────┘                 │
│           │                      │                          │
│           ▼                      │                          │
│  ┌─────────────────┐    ┌────────┴────────┐                 │
│  │    Rollout      │    │  Soft Rules &   │                 │
│  │ (random playout)│    │   Heuristics    │                 │
│  └────────┬────────┘    └─────────────────┘                 │
└───────────┼─────────────────────────────────────────────────┘
            │
            ▼
┌─────────────────────────────────────────────────────────────┐
│                      Game Rules Layer                       │
│  ┌─────────────────┐    ┌─────────────────┐                 │
│  │   rules<V>      │    │   game_state<V> │                 │
│  │ (legal moves,   │───▶│ (hands, trick,  │                 │
│  │  play_card)     │    │  scores)        │                 │
│  └─────────────────┘    └─────────────────┘                 │
└─────────────────────────────────────────────────────────────┘
            │
            ▼
┌─────────────────────────────────────────────────────────────┐
│                   Representation Layer                      │
│  ┌─────────────────┐    ┌─────────────────┐                 │
│  │   card_mask     │    │  deck_traits<V> │                 │
│  │  (64-bit BB)    │    │ (suit/rank cfg) │                 │
│  └─────────────────┘    └─────────────────┘                 │
└─────────────────────────────────────────────────────────────┘
```

---

## Core Data Structures

### Card Representation (Bitboards)

Cards are represented using **64-bit bitboards** (`card_mask`), where each bit corresponds to a specific card. This enables extremely fast set operations using hardware-accelerated bit manipulation.

```cpp
using card_mask = uint64_t;  // 64-bit bitboard
using card = uint8_t;        // Card index (0–51)
```

**Bit Layout for Standard 52-Card Deck (Whist/Bridge):**

```
Bit Index:  0   1   2   3   4   5   6   7   8   9  10  11  12
Card:       2♠  3♠  4♠  5♠  6♠  7♠  8♠  9♠ 10♠  J♠  Q♠  K♠  A♠
            ─────────────────────────────────────────────────────
Bit Index: 13  14  15  16  17  18  19  20  21  22  23  24  25
Card:       2♥  3♥  4♥  5♥  6♥  7♥  8♥  9♥ 10♥  J♥  Q♥  K♥  A♥
            ─────────────────────────────────────────────────────
Bit Index: 26  27  28  29  30  31  32  33  34  35  36  37  38
Card:       2♦  3♦  4♦  5♦  6♦  7♦  8♦  9♦ 10♦  J♦  Q♦  K♦  A♦
            ─────────────────────────────────────────────────────
Bit Index: 39  40  41  42  43  44  45  46  47  48  49  50  51
Card:       2♣  3♣  4♣  5♣  6♣  7♣  8♣  9♣ 10♣  J♣  Q♣  K♣  A♣
```

**Suit Masks (13 bits each):**

```cpp
template<typename Variant>
constexpr card_mask suit_masks[4] = {
    0x0000000000001FFF,  // Spades   (bits 0–12)
    0x0000000003FFE000,  // Hearts   (bits 13–25)
    0x00000007FFC00000,  // Diamonds (bits 26–38)
    0x000FF80000000000,  // Clubs    (bits 39–51)
};
```

**Example — Representing a Hand:**

```cpp
// Hand: A♠, K♠, Q♥, 7♦, 2♣
card_mask hand = (1ULL << 12)   // A♠ (bit 12)
               | (1ULL << 11)   // K♠ (bit 11)
               | (1ULL << 24)   // Q♥ (bit 24)
               | (1ULL << 31)   // 7♦ (bit 31)
               | (1ULL << 39);  // 2♣ (bit 39)

// Check if hand contains any spades:
bool has_spades = (hand & suit_masks<whist>[0]) != 0;  // true

// Count cards in hand:
int count = std::popcount(hand);  // 5
```

### Game State

The `game_state<Variant>` struct holds the complete state of a trick-taking game:

```cpp
template<typename Variant>
struct game_state {
    std::array<card_mask, 4> hands;       // Remaining cards per seat
    card_mask                played;      // All cards played so far
    trick_state              trick;       // Current trick in progress
    std::array<uint8_t, 2>   tricks_won;  // Score per partnership
    suit                     trump;       // Trump suit
    seat                     dealer;      // Dealer position
    seat                     current_player;
    uint8_t                  tricks_remaining;
};
```

**Seats and Partnerships:**

```
        North (0)
           │
           │ Partnership 0
           │
West (3)───┼───East (1)
           │
           │ Partnership 1
           │
       South (2)

Partnership: seat & 1  → North/South = 0, East/West = 1
```

### Trick State

The `trick_state` struct tracks the current trick with O(1) winner updates:

```cpp
struct trick_state {
    card_mask mask;                       // All cards in this trick (bitmask)
    std::array<card, 4> cards;            // Card per seat (for observation)
    suit      lead_suit;                  // Suit of first card played
    seat      lead_seat;                  // Who led this trick
    seat      current_winner;             // Seat currently winning
    card      winning_card;               // Index of winning card
    uint8_t   num_played;                 // 0..4 cards played
};
```

**Example — Trick in Progress:**

```
Lead: East plays 10♥
      South plays K♥  → South now winning
      West plays 5♥
      North plays 2♠ (trump) → North now winning (trump beats hearts)

trick_state:
  mask = 0b...01000010000100...  // K♥, 10♥, 5♥, 2♠
  cards = [2♠, 10♥, K♥, 5♥]     // indexed by seat
  lead_suit = hearts
  lead_seat = east
  current_winner = north
  winning_card = 0              // 2♠
  num_played = 4
```

---

## Algorithms

### Bitwise Operations

The `ops` namespace provides hardware-optimized bit operations:

| Operation | Description | Complexity |
|-----------|-------------|------------|
| `lsb(m)` | Isolate lowest set bit | O(1) |
| `pop_lsb(m)` | Remove and return lowest bit | O(1) |
| `lsb_index(m)` | Index of lowest set bit | O(1) via `countr_zero` |
| `msb_index(m)` | Index of highest set bit | O(1) via `countl_zero` |
| `popcount(m)` | Count set bits | O(1) via hardware |
| `nth_set_bit(m, n)` | Get n-th set bit | O(1) with PDEP, O(n) fallback |
| `suit_ranks<V>(h, s)` | Extract rank bits for suit | O(1) with PEXT |

**Example — Iterating Over Cards:**

```cpp
card_mask hand = /* ... */;
while (hand) {
    card c = ops::lsb_index(hand);   // Get lowest card index
    hand &= hand - 1;                // Clear that bit (pop_lsb)
    // Process card c...
}
```

**PEXT/PDEP Optimization (BMI2 Instructions):**

```cpp
// Extract spade ranks from a hand (with PEXT):
uint16_t spade_ranks = _pext_u64(hand, suit_masks<whist>[0]);
// Result: bit 0 = has 2♠, bit 1 = has 3♠, ..., bit 12 = has A♠

// Scatter ranks back to card positions (with PDEP):
card_mask spades = _pdep_u64(spade_ranks, suit_masks<whist>[0]);
```

### Trick Resolution

The `trick_winner<Variant>()` function determines the winning card:

```cpp
template<typename Variant>
card_mask trick_winner(card_mask trick, suit lead, suit trump) {
    // 1. Apply variant-specific remapping (e.g., Jass trump order)
    trick = deck_traits<Variant>::remap(trick, trump);

    // 2. Extract lead-suit and trump-suit cards
    card_mask lead_cards  = trick & suit_masks<Variant>[lead];
    card_mask trump_cards = trick & suit_masks<Variant>[trump];

    // 3. Trump beats lead suit (branchless selection)
    card_mask use_trump  = -(trump_cards != 0);  // All 1s if trump present
    card_mask candidates = (trump_cards & use_trump) 
                         | (lead_cards & ~use_trump);

    // 4. Highest card wins (MSB)
    return 1ULL << ops::msb_index(candidates);
}
```

**Incremental Winner Tracking:**

Instead of recomputing the winner after each play, `beats_current()` checks if the new card beats the current winner in O(1):

```cpp
static bool beats_current(const trick_state& t, suit trump, card c) {
    if (t.winning_card == no_card) return true;  // First card wins

    suit c_suit = suit_of<Variant>(c);
    suit w_suit = suit_of<Variant>(t.winning_card);

    // Off-suit, non-trump never wins
    if (c_suit != t.lead_suit && c_suit != trump) return false;
    
    // Trump beats non-trump
    if (c_suit == trump && w_suit != trump) return true;
    if (c_suit != trump && w_suit == trump) return false;

    // Same suit: higher index wins (for non-reordering variants)
    return c > t.winning_card;
}
```

### Legal Move Generation

Legal moves are computed as a bitmask in O(1):

```cpp
static card_mask legal_moves(const game_state<Variant>& state) {
    card_mask hand = state.current_hand_mask();

    if (state.trick.num_played == 0)
        return hand;  // Leading: any card

    // Must follow suit if possible
    card_mask follow = ops::cards_in_suit<Variant>(hand, state.trick.lead_suit);
    return follow ? follow : hand;
}
```

**Example:**

```cpp
// Hand: A♠, K♠, 7♥, 3♦
// Lead suit: Hearts

card_mask hand = (1ULL << 12) | (1ULL << 11) | (1ULL << 19) | (1ULL << 27);
card_mask follow = hand & suit_masks<whist>[1];  // Hearts only
// follow = 7♥ → must play 7♥

// If hand had no hearts:
// follow = 0 → can play any card
```

---

## Belief Model

The belief model tracks what cards opponents might hold, essential for ISMCTS in imperfect-information games.

### Probability Matrix

```cpp
template<typename Variant>
struct belief_state {
    seat observer;
    std::array<std::array<float, N>, 3> prob;  // prob[opponent][card]
    std::array<card_mask, 3> possible;          // Hard constraint masks
};
```

**Structure:** `prob[i][c]` = probability that opponent `i` holds card `c`

**Invariant:** For each unseen card `c`: `Σ prob[i][c] = 1.0`

**Example — Initial State:**

```
Observer: North holds {A♠, K♠, Q♠, ...}
Unseen cards: 39 cards (52 - 13)

prob[East][c]  = 1/3  for each unseen card c
prob[South][c] = 1/3
prob[West][c]  = 1/3
```

### Hard Constraints

When absolute information is revealed, the `possible` bitmask is updated:

1. **Card Played:** Remove from all opponents' possible sets
2. **Void Signal:** If opponent fails to follow suit, clear entire suit

```cpp
void update_on_play(seat player, card c, bool is_lead, suit lead_suit) {
    // Remove played card from all
    for (int i = 0; i < 3; ++i) {
        prob[i][c] = 0.0f;
        possible[i] &= ~(1ULL << c);
    }

    // Void signal: opponent couldn't follow suit
    if (player != observer && !is_lead) {
        suit played_suit = suit_of<Variant>(c);
        if (played_suit != lead_suit) {
            uint8_t opp = seat_to_opp(observer, player);
            possible[opp] &= ~suit_masks<Variant>[lead_suit];
            // Zero probabilities for voided suit
            for (int sc = lead_base; sc < lead_end; ++sc)
                prob[opp][sc] = 0.0f;
        }
    }

    renormalize();  // Ensure columns sum to 1.0
}
```

### Soft Rules (Heuristics)

Soft rules adjust probabilities based on gameplay patterns:

```cpp
template<typename Variant>
struct soft_rule {
    const char* name;
    float strength;       // Tuneable weight (ML knob)
    float base_factor;    // Probability multiplier
    pred_fn predicate;    // When does this rule apply?
    apply_fn apply;       // How to adjust probabilities
};
```

**Built-in Whist Heuristics:**

| Rule | Trigger | Effect |
|------|---------|--------|
| **Played Low on Winnable** | Opponent followed suit with low card | Dampen high-rank probabilities (×0.3) |
| **Led a Suit** | Opponent chose to lead a suit | Boost remaining cards in that suit (×1.5) |
| **Low Trump Ruff** | Opponent trumped with low trump | Boost higher trump probabilities (×1.8) |
| **Discard Not Trump** | Opponent void but didn't trump | Dampen all trump probabilities (×0.4) |
| **Partner Led High** | Partner led top-third rank | Dampen opponents' high cards (×0.5) |

**Example — "Played Low on Winnable":**

```cpp
// East played 3♥ when hearts were led and the trick was winnable
// Inference: East likely doesn't have high hearts

// Before:
prob[East][Q♥] = 0.33, prob[East][K♥] = 0.33, prob[East][A♥] = 0.33

// After applying rule (factor = 0.3):
prob[East][Q♥] *= 0.3 → 0.10
prob[East][K♥] *= 0.3 → 0.10
prob[East][A♥] *= 0.3 → 0.10

// After renormalization (assuming South/West unchanged):
prob[East][Q♥] = 0.10 / 0.76 ≈ 0.13
```

---

## Determinization Algorithm

ISMCTS requires "determinizing" the game — sampling a specific deal consistent with beliefs.

**Algorithm:**

```
1. Collect unseen cards into array
2. Shuffle randomly
3. Stable-sort by constraint tightness (fewest eligible holders first)
4. For each card:
   a. Compute weight w[i] = prob[i][card] for each eligible opponent
   b. Weighted roulette selection → assign to chosen opponent
   c. Decrement that opponent's remaining capacity
```

**Pseudocode:**

```cpp
template<typename Variant, typename RNG>
game_state<Variant> determinize(
        const game_state<Variant>& state,
        const belief_state<Variant>& belief,
        RNG& rng) {

    // 1. Collect unseen cards
    card_mask unseen = ~(state.played | observer_hand) & deck_mask;
    vector<card> unseen_cards = extract_cards(unseen);

    // 2. Shuffle + sort by constraint tightness
    shuffle(unseen_cards, rng);
    stable_sort(unseen_cards, [&](card a, card b) {
        return count_eligible(a) < count_eligible(b);
    });

    // 3. Weighted assignment
    array<int, 3> remaining = target_hand_sizes;

    for (card c : unseen_cards) {
        // Build weight vector
        float total = 0;
        for (int i = 0; i < 3; ++i) {
            if (remaining[i] > 0 && (possible[i] >> c) & 1) {
                w[i] = max(prob[i][c], 1e-6f);
            } else {
                w[i] = 0;
            }
            total += w[i];
        }

        // Roulette selection
        float r = uniform(0, total);
        int chosen = select_by_weight(w, r);

        // Assign card
        result.hands[chosen] |= (1ULL << c);
        remaining[chosen]--;
    }

    return result;
}
```

**Example:**

```
Observer: North
Unseen: {K♥, Q♥, 7♦, 3♣}  (4 cards)
Target sizes: East=2, South=1, West=1
East is void in diamonds (hard constraint)

Constraint-sorted order: [7♦, K♥, Q♥, 3♣]
                         (7♦ can only go to South/West)

Assignment:
  7♦ → South (50/50 between South/West)
  K♥ → East  (weighted by prob)
  Q♥ → West  (weighted by prob)
  3♣ → East  (only East has remaining capacity)

Result: East={K♥, 3♣}, South={7♦}, West={Q♥}
```

---

## ISMCTS Search Algorithm

**Single-Observer ISMCTS (SO-ISMCTS)** builds a search tree only at the observer's decision points.

### Tree Structure

```cpp
template<typename Variant>
struct ismcts_node {
    card      action;       // Card played to reach this node
    uint32_t  visits;       // Times visited
    uint32_t  availability; // Times this action was legal
    float     total_value;  // Cumulative normalized payoff
    array<int32_t, 52> children;  // Child indices (-1 if unexpanded)
    card_mask expanded;     // Which actions have child nodes
};
```

**Tree Example:**

```
                    Root (observer's turn)
                   /     |      \
                 A♠     K♠      Q♠
                /       |         \
            [visits=50] [visits=30] [visits=20]
            /    \
          ...    ...
```

### UCB1 Selection

The UCB1 formula balances exploration vs. exploitation:

```
UCB1(node) = exploit + explore
           = (total_value / visits) + C × √(ln(availability) / visits)
```

Where:
- `exploit` = average payoff (exploitation term)
- `explore` = confidence bound (exploration bonus)
- `C` = exploration constant (default √2 ≈ 1.414)
- `availability` = times this action was legal (ISMCTS-specific)

```cpp
float ucb1_score(const ismcts_node& node, float C) {
    if (node.visits == 0)
        return +∞;  // Always try unvisited nodes first

    float exploit = node.total_value / node.visits;
    float explore = C * sqrt(log(node.availability) / node.visits);
    return exploit + explore;
}
```

### Rollout

Fast random playout from a state to game completion:

```cpp
template<typename Variant, typename RNG>
array<uint8_t, 2> rollout(game_state<Variant> state, RNG& rng) {
    while (state.tricks_remaining > 0) {
        card_mask moves = rules<Variant>::legal_moves(state);

        // Random selection using nth_set_bit (O(1) with PDEP)
        int count = popcount(moves);
        int index = uniform(0, count - 1);
        card c = lsb_index(nth_set_bit(moves, index));

        rules<Variant>::play_card(state, state.current_player, c);
    }
    return state.tricks_won;
}
```

### Backpropagation

After a rollout completes, the payoff is propagated back through all visited nodes:

```cpp
// Compute payoff (normalized to [0,1])
float value = state.tricks_won[observer_partnership] / total_tricks;

// Backpropagate through the path
for (int32_t idx : path) {
    arena[idx].visits++;
    arena[idx].total_value += value;
}
```

**Key points:**
- Payoff is normalized to `[0, 1]` (fraction of tricks won by observer's partnership)
- Every node on the descent path is updated (not just the expanded node)
- Both `visits` and `total_value` are incremented to maintain running averages

### Search Algorithm

```
FOR each iteration:
    1. DETERMINIZE: Sample deal from belief state
    2. DESCEND tree:
       - Observer's turn: UCB1 select or expand
       - Opponent's turn: Random legal move
    3. ROLLOUT: Random playout to game end
    4. BACKPROPAGATE: Update visits and values

RETURN most-visited root child
```

**Full Algorithm:**

```cpp
template<typename Variant, typename RNG>
card ismcts_search(
        const game_state<Variant>& root_state,
        const belief_state<Variant>& belief,
        const search_config& config,
        RNG& rng) {

    vector<ismcts_node<Variant>> arena;
    arena.reserve(config.iterations + 1);
    arena.emplace_back();  // Root node

    for (int iter = 0; iter < config.iterations; ++iter) {
        // 1. Determinize
        auto state = determinize(root_state, belief, rng);

        int32_t node = ROOT;
        vector<int32_t> path = {ROOT};
        bool in_tree = true;

        // 2. Tree descent
        while (state.tricks_remaining > 0) {
            seat cur = state.current_player;

            if (cur == observer && in_tree) {
                card_mask legal = rules::legal_moves(state);

                // Update availability for existing children
                for each child c in legal:
                    arena[c].availability++;

                card_mask unexplored = legal & ~arena[node].expanded;

                if (unexplored != 0) {
                    // EXPAND: create new child node
                    card c = random_from(unexplored);
                    int32_t child = create_child(c);
                    arena[node].children[c] = child;
                    arena[node].expanded |= (1ULL << c);

                    rules::play_card(state, observer, c);
                    path.push_back(child);
                    in_tree = false;  // Switch to rollout
                } else {
                    // SELECT: UCB1 best child
                    card best_card = argmax(legal, ucb1_score);
                    rules::play_card(state, observer, best_card);
                    path.push_back(arena[node].children[best_card]);
                    node = children[best_card];
                }
            } else {
                // Opponent or rollout: random move
                card c = random_legal_move(state);
                rules::play_card(state, cur, c);
            }
        }

        // 3. Compute payoff (normalized to [0,1])
        float value = state.tricks_won[observer_partnership] / total_tricks;

        // 4. Backpropagate
        for (int32_t idx : path) {
            arena[idx].visits++;
            arena[idx].total_value += value;
        }
    }

    // Return most-visited action
    return argmax(root.children, by_visits);
}
```

**Example Search:**

```
Config: 1000 iterations, C = 1.414
Observer: North, holding {A♠, K♠, 5♥, 3♦}
Current trick: empty (North to lead)

After 1000 iterations:
  A♠: visits=412, value=0.68
  K♠: visits=298, value=0.62
  5♥: visits=187, value=0.41
  3♦: visits=103, value=0.35

Best action: A♠ (most visited)
```

---

## Double Dummy Solver (DDS)

The **Double Dummy Solver** computes optimal play when all hands are visible (perfect information). It's used for:
- **Endgame solving** — Replace random rollouts with exact solutions for small endgames
- **Ground truth** — Validate ISMCTS convergence and generate training data
- **Analysis tools** — Post-game analysis and optimal play computation

### Zobrist Hashing

Positions are hashed using **Zobrist hashing** for efficient transposition table lookups:

```cpp
// Xorshift64 PRNG for deterministic key generation
constexpr uint64_t xorshift64(uint64_t& state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

// Keys indexed by: card_keys[seat][card], player_keys[seat], trump_keys[suit]
template<typename Variant>
struct zobrist_keys {
    std::array<std::array<uint64_t, N>, 4> card_keys;
    std::array<uint64_t, 4> player_keys;
    std::array<uint64_t, 4> trump_keys;
};
```

**Hash Computation:**

```cpp
uint64_t hash = 0;
for (seat s : all_seats) {
    for (card c : hand[s]) {
        hash ^= zkeys.card_keys[s][c];
    }
}
hash ^= zkeys.player_keys[current_player];
hash ^= zkeys.trump_keys[trump];
```

**Incremental Updates:** When a card is played, XOR out the card key and update the player key—O(1) per move.

### Transposition Table

The transposition table stores search results to avoid recomputing positions:

```cpp
struct tt_entry {
    uint64_t key;        // Zobrist hash (collision detection)
    int8_t   value;      // Score (tricks for maximizing side)
    int8_t   depth;      // Remaining cards when stored
    tt_bound bound;      // exact / lower / upper
    uint8_t  best_move;  // Best card found
};

enum class tt_bound : uint8_t {
    none  = 0,
    lower = 1,   // Score >= value (failed high)
    upper = 2,   // Score <= value (failed low)
    exact = 3    // Exact score
};
```

**Table Configuration:**
- Size: 2²⁰ entries (1M) ≈ 24MB
- Indexing: `hash & (SIZE - 1)` (power-of-2 for fast modulo)
- Replacement: Always replace (simple policy)
- Storage: Heap-allocated via `std::vector` to avoid stack overflow

### Alpha-Beta Search

The DDS uses **minimax with alpha-beta pruning**:

```cpp
int search_impl(state, alpha, beta, hash, tt, root_partnership) {
    // Terminal check
    if (no cards remaining) return 0;

    // TT probe
    if (entry = tt.probe(hash, depth)) {
        if (entry.bound == exact) return entry.value;
        if (entry.bound == lower && entry.value >= beta) return entry.value;
        if (entry.bound == upper && entry.value <= alpha) return entry.value;
    }

    // Move ordering: TT best move first, then high cards
    moves = order_moves(legal_moves, tt_best_move);

    for (move : moves) {
        child_state = play(state, move);
        child_hash = update_hash(hash, move);
        
        value = search_impl(child_state, alpha, beta, child_hash, tt, root_partnership);
        
        // Add trick point if root_partnership won
        if (trick_completed && winner_partnership == root_partnership)
            value += 1;

        // Minimax update
        if (is_maximizing) {
            best = max(best, value);
            alpha = max(alpha, best);
        } else {
            best = min(best, value);
            beta = min(beta, best);
        }

        if (alpha >= beta) break;  // Cutoff
    }

    tt.store(hash, depth, best, bound_type, best_move);
    return best;
}
```

**Key Features:**
- Returns tricks for the **root player's partnership** (not current player)
- Proper minimax: maximizes when root partnership plays, minimizes otherwise
- Move ordering with TT best move hint for better pruning

### Search Policy Abstraction

The search algorithm is **templated** for easy swapping:

```cpp
// Default: alpha-beta search
auto result = dds_solve<whist>(state);

// Explicit policy specification
auto result = dds_solve<whist, alpha_beta_policy<whist>>(state);

// Future: partition search for larger endgames
auto result = dds_solve<whist, partition_search_policy<whist>>(state);
```

**Policy Interface:**

```cpp
template<typename Variant>
struct alpha_beta_policy {
    using TT = default_tt;
    
    static int search(
        game_state<Variant> state,
        int alpha, int beta,
        uint64_t hash,
        TT& tt);
};
```

**DDS Result:**

```cpp
struct dds_result {
    std::array<int8_t, 4> tricks_ns;  // Tricks for N/S when each seat leads
    card best_move;                    // Optimal opening lead
};

// Convenience functions
int tricks = dds_tricks<whist>(state);
card best = dds_best_move<whist>(state);
```

**Example Usage:**

```cpp
// Create endgame: 4 cards each
game_state<whist> state = make_endgame(...);

// Solve
auto result = dds_solve<whist>(state);

// N/S optimal tricks when North leads
int ns_tricks = result.tricks_ns[0];

// Best move for current player
card optimal = result.best_move;
```

---

## Game Variants

The library uses C++ templates for zero-overhead variant support:

| Variant | Deck | Trump Reordering | Special Rules |
|---------|------|------------------|---------------|
| `whist` | 52 cards (13×4) | No | Standard |
| `bridge` | 52 cards | No | Partnership bidding (not yet implemented) |
| `jass` | 36 cards (9×4) | Yes | J and 9 are highest trump |

**Jass Trump Reordering:**

```
Normal rank order:  6 < 7 < 8 < 9 < 10 < J < Q < K < A
Trump rank order:   6 < 7 < 8 < Q < K < 10 < A < 9 < J
                                          (Nell) (Under)
```

Implemented via a 512-entry lookup table:

```cpp
consteval auto make_jass_trump_lut() {
    array<uint16_t, 512> t{};
    for (int i = 0; i < 512; ++i) {
        uint16_t r = 0;
        if (i & (1 << UNDER_INDEX)) r |= (1 << 8);  // J → highest
        if (i & (1 << NELL_INDEX))  r |= (1 << 7);  // 9 → second highest
        // ... remaining mappings
        t[i] = r;
    }
    return t;
}
```

---

## ML Integration Opportunities

The belief model provides clear integration points for machine learning:

### Tunable Parameters

```cpp
struct soft_rule {
    float strength;      // Overall rule weight [0, ∞)
    float base_factor;   // Probability multiplier
};
```

**Optimization Targets:**

1. **Supervised Learning:** Minimize cross-entropy between `prob[i][c]` predictions and actual hidden hands from expert datasets

2. **Reinforcement Learning:** Tune `strength` and `base_factor` via self-play to maximize win rate

3. **Feature Vectors:** Flatten `prob[i][c]` as input to neural networks:
   ```
   input = [prob[0][0], prob[0][1], ..., prob[2][51]]  // 156 features
   ```

### Training Loop

```
┌─────────────────────────────────────────────────────────────┐
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────────┐ │
│  │ Game History│───▶│ Soft Rules  │───▶│ Prob Matrix     │ │
│  └─────────────┘    │ (w/ params) │    │ (predictions)   │ │
│         ▲           └──────┬──────┘    └────────┬────────┘ │
│         │                  ▲                    │          │
│         │      ┌───────────┴─────────┐          ▼          │
│         │      │  ML-Tuned Params    │    ┌───────────┐    │
│         │      │ (strength, factor)  │◀───│ Loss Fn   │    │
│         │      └─────────────────────┘    │ (vs truth)│    │
│         │                                 └───────────┘    │
│         └─────────── Self-Play Loop ───────────────────────┘
└─────────────────────────────────────────────────────────────┘
```

---

## File Structure

| File | Description |
|------|-------------|
| `arch.h` | Compiler-specific macros (`inline_always`, `PREFETCH`, cache alignment) |
| `repr.h` | Card bitboards, `deck_traits<V>`, bit operations, dealing, trick winner |
| `game_state.h` | `seat`, `trick_state`, `game_state<V>` definitions |
| `rules.h` | `rules<V>`: legal moves, `play_card()`, trick resolution |
| `whist.h` | Whist-specific rules (uses default `rules<whist>`) |
| `belief.h` | `belief_state<V>`: probability matrix, hard/soft updates |
| `heuristics.h` | `soft_rule<V>`, `play_context<V>`, built-in Whist heuristics |
| `determinize.h` | `determinize()`: weighted card assignment from beliefs |
| `ismcts.h` | `rollout()`: random playout to game end |
| `search.h` | `ismcts_search()`: full SO-ISMCTS implementation |
| `zobrist.h` | Xorshift64 PRNG, Zobrist key tables, hash computation |
| `tt.h` | Transposition table with exact/lower/upper bounds |
| `alpha_beta.h` | Alpha-beta minimax search policy for DDS |
| `dds.h` | `dds_solve<V, Policy>()`: Double Dummy Solver interface |

