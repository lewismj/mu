#include <iostream>

#include "repr.h"
#include "game_state.h"

namespace mu {

    std::ostream& operator<<(std::ostream& os, const suit s) {
        switch (s) {
            case suit::spades: os << "spades"; break;
            case suit::hearts: os << "hearts"; break;
            case suit::diamonds: os << "diamonds"; break;
            case suit::clubs: os << "clubs"; break;
            default: os << "unknown_suit"; break;
        }
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const seat s) {
        switch (s) {
            case seat::north: os << "north"; break;
            case seat::east: os << "east"; break;
            case seat::south: os << "south"; break;
            case seat::west: os << "west"; break;
            default: os << "unknown_seat"; break;
        }
        return os;
    }

}
