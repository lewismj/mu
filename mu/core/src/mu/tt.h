#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace mu {

    // ── transposition table entry ───────────────────────────

    /**
     * Entry in the DDS transposition table.
     *
     * Stores alpha-beta bounds for a position:
     *   - EXACT:  lower == upper == exact score
     *   - LOWER:  lower bound (failed high, score >= lower)
     *   - UPPER:  upper bound (failed low, score <= upper)
     *
     * best_move is stored to improve move ordering on re-search.
     */
    enum class tt_bound : uint8_t {
        none  = 0,
        lower = 1,   // Alpha cutoff (score >= value)
        upper = 2,   // Beta cutoff (score <= value)
        exact = 3    // Exact score
    };

    struct tt_entry {
        uint64_t key      = 0;           // Zobrist hash (for collision detection)
        int8_t   value    = 0;           // Score (tricks for maximizing side)
        int8_t   depth    = -1;          // Remaining cards when stored (-1 = empty)
        tt_bound bound    = tt_bound::none;
        uint8_t  best_move = 0xFF;       // Best card found (no_card if none)

        [[nodiscard]] bool is_empty() const { return depth < 0; }

        [[nodiscard]] bool matches(uint64_t h, int8_t d) const {
            return key == h && depth == d;
        }
    };

    // ── transposition table ─────────────────────────────────

    /**
     * Fixed-size transposition table for DDS.
     *
     * Uses simple replacement: always replace on collision.
     * Size must be a power of 2 for fast modulo via bitmask.
     *
     * Uses std::vector internally to avoid stack overflow with large tables.
     *
     * @tparam SizeBits  log2 of table size (e.g., 20 → 1M entries)
     */
    template<int SizeBits = 20>
    class transposition_table {
    public:
        static constexpr size_t SIZE = size_t{1} << SizeBits;
        static constexpr size_t MASK = SIZE - 1;

    private:
        std::vector<tt_entry> table_;

    public:
        transposition_table() : table_(SIZE) {}

        /**
         * Clear all entries.
         */
        void clear() {
            std::fill(table_.begin(), table_.end(), tt_entry{});
        }

        /**
         * Probe the table for a matching entry.
         *
         * @param hash   Zobrist hash of position
         * @param depth  Remaining cards (for depth matching)
         * @return       Pointer to entry if found and matches, nullptr otherwise
         */
        [[nodiscard]] const tt_entry* probe(uint64_t hash, int8_t depth) const {
            const size_t idx = hash & MASK;
            const auto& entry = table_[idx];

            if (entry.matches(hash, depth))
                return &entry;

            return nullptr;
        }

        /**
         * Store a result in the table.
         *
         * Always replaces existing entry (simple replacement policy).
         *
         * @param hash       Zobrist hash
         * @param depth      Remaining cards
         * @param value      Score (tricks)
         * @param bound      Bound type (exact/lower/upper)
         * @param best_move  Best move found (optional)
         */
        void store(uint64_t hash, int8_t depth, int8_t value,
                   tt_bound bound, uint8_t best_move = 0xFF) {

            const size_t idx = hash & MASK;
            auto& entry = table_[idx];

            entry.key       = hash;
            entry.depth     = depth;
            entry.value     = value;
            entry.bound     = bound;
            entry.best_move = best_move;
        }

        /**
         * Get entry at index for direct access (e.g., for best move hint).
         */
        [[nodiscard]] const tt_entry& at(uint64_t hash) const {
            return table_[hash & MASK];
        }

        [[nodiscard]] size_t size() const { return SIZE; }
    };

    // ── default table type ──────────────────────────────────

    /** Default TT size: 1M entries (~24MB with current entry size) */
    using default_tt = transposition_table<20>;

}

