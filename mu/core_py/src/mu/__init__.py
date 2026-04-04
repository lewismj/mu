"""
mu — Python interface for the mu card game engine.

This package provides a clean, Pythonic API over the high-performance
C++ backend (_mu_core). Use this module for game simulation, AI agents,
and ML training pipelines.

Example
-------
>>> import mu
>>> mu.seed(42)
>>> game = mu.whist_new_game(trump=mu.SPADES, dealer=mu.NORTH)
>>> print(game)
WhistState(current=Seat.EAST, trump=Suit.SPADES, ...)
"""

from _mu_core import (
    # RNG
    seed,
    
    # Enums
    Seat, Suit,
    NORTH, EAST, SOUTH, WEST,
    SPADES, HEARTS, DIAMONDS, CLUBS,
    
    # Constants
    NUM_PLAYERS,
    NUM_PARTNERSHIPS,
    WHIST_NUM_CARDS,
    WHIST_NUM_RANKS,
    WHIST_CARDS_PER_HAND,
    NO_CARD,
    
    # Card mask helpers
    mask_to_cards,
    cards_to_mask,
    card_str,
    mask_str,
    popcount,
    suit_of,
    rank_of,
    
    # Seat helpers
    next_seat,
    partner_of,
    partnership_of,
    seat_to_opp,
    opp_to_seat,
    
    # State types
    TrickState,
    WhistState,
    WhistBelief,
    
    # Rules
    legal_moves,
    play_card,
    
    # Game factory
    whist_new_game,
    
    # Search & simulation
    whist_search,
    whist_determinize,
    whist_rollout,
)

__all__ = [
    # RNG
    "seed",
    
    # Enums
    "Seat", "Suit",
    "NORTH", "EAST", "SOUTH", "WEST",
    "SPADES", "HEARTS", "DIAMONDS", "CLUBS",
    
    # Constants
    "NUM_PLAYERS",
    "NUM_PARTNERSHIPS", 
    "WHIST_NUM_CARDS",
    "WHIST_NUM_RANKS",
    "WHIST_CARDS_PER_HAND",
    "NO_CARD",
    
    # Card mask helpers
    "mask_to_cards",
    "cards_to_mask",
    "card_str",
    "mask_str",
    "popcount",
    "suit_of",
    "rank_of",
    
    # Seat helpers
    "next_seat",
    "partner_of",
    "partnership_of",
    "seat_to_opp",
    "opp_to_seat",
    
    # State types
    "TrickState",
    "WhistState",
    "WhistBelief",
    
    # Rules
    "legal_moves",
    "play_card",
    
    # Game factory
    "whist_new_game",
    
    # Search & simulation
    "whist_search",
    "whist_determinize",
    "whist_rollout",
]

__version__ = "0.1.0"

