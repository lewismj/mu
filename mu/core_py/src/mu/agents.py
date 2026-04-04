"""
mu.agents — Player implementations for trick-taking games.

This module provides various agent types that can play games:
- RandomPlayer: Selects uniformly from legal moves
- MCTSPlayer: Uses ISMCTS search for decision making
- HumanPlayer: Interactive input (for debugging/demos)

Example
-------
>>> from mu.agents import RandomPlayer, MCTSPlayer
>>> players = [MCTSPlayer(iterations=1000), RandomPlayer(), MCTSPlayer(), RandomPlayer()]
>>> # Use players in a game loop
"""

from abc import ABC, abstractmethod
from typing import Optional
import random

import mu


class Player(ABC):
    """Abstract base class for all player agents."""
    
    @abstractmethod
    def select_move(
        self, 
        state: mu.WhistState, 
        seat: mu.Seat,
        belief: Optional[mu.WhistBelief] = None
    ) -> int:
        """
        Select a card to play.
        
        Parameters
        ----------
        state : WhistState
            Current game state.
        seat : Seat
            This player's seat.
        belief : WhistBelief, optional
            Belief state for this player (required for MCTS).
            
        Returns
        -------
        int
            Card index to play.
        """
        pass
    
    def on_game_start(self, state: mu.WhistState, seat: mu.Seat) -> None:
        """Called at the start of a new game. Override to initialize state."""
        pass
    
    def on_card_played(
        self, 
        state: mu.WhistState, 
        player: mu.Seat, 
        card: int,
        is_lead: bool,
        lead_suit: mu.Suit
    ) -> None:
        """Called after any card is played. Override to update internal state."""
        pass


class RandomPlayer(Player):
    """
    Player that selects uniformly at random from legal moves.
    
    Useful as a baseline and for fast rollouts.
    """
    
    def select_move(
        self, 
        state: mu.WhistState, 
        seat: mu.Seat,
        belief: Optional[mu.WhistBelief] = None
    ) -> int:
        legal = mu.legal_moves(state)
        cards = mu.mask_to_cards(legal)
        return random.choice(cards)


class MCTSPlayer(Player):
    """
    Player that uses Information Set Monte Carlo Tree Search.
    
    Maintains a belief state and uses the C++ ISMCTS implementation
    to select moves.
    
    Parameters
    ----------
    iterations : int
        Number of MCTS iterations per move (default: 1000).
    exploration : float
        UCB1 exploration constant (default: sqrt(2)).
    """
    
    def __init__(self, iterations: int = 1000, exploration: float = 1.41421356):
        self.iterations = iterations
        self.exploration = exploration
        self._belief: Optional[mu.WhistBelief] = None
        self._seat: Optional[mu.Seat] = None
    
    def on_game_start(self, state: mu.WhistState, seat: mu.Seat) -> None:
        """Initialize belief state at game start."""
        self._seat = seat
        self._belief = mu.WhistBelief()
        self._belief.init(state, seat)
    
    def on_card_played(
        self, 
        state: mu.WhistState, 
        player: mu.Seat, 
        card: int,
        is_lead: bool,
        lead_suit: mu.Suit
    ) -> None:
        """Update belief state when a card is played."""
        if self._belief is not None and player != self._seat:
            self._belief.update_on_play(player, card, is_lead, lead_suit)
    
    def select_move(
        self, 
        state: mu.WhistState, 
        seat: mu.Seat,
        belief: Optional[mu.WhistBelief] = None
    ) -> int:
        # Use provided belief or internal belief
        b = belief if belief is not None else self._belief
        
        if b is None:
            # Fallback: create fresh belief if not initialized
            b = mu.WhistBelief()
            b.init(state, seat)
        
        return mu.whist_search(state, b, self.iterations, self.exploration)


class HumanPlayer(Player):
    """
    Interactive player for debugging and demos.
    
    Displays the hand and prompts for input via stdin.
    """
    
    def select_move(
        self, 
        state: mu.WhistState, 
        seat: mu.Seat,
        belief: Optional[mu.WhistBelief] = None
    ) -> int:
        hand_mask = state.hands[int(seat)]
        legal_mask = mu.legal_moves(state)
        legal_cards = mu.mask_to_cards(legal_mask)
        
        print(f"\n--- {seat} to play ---")
        print(f"Trump: {state.trump}")
        print(f"Your hand: {mu.mask_str(hand_mask)}")
        print(f"Legal moves: {mu.mask_str(legal_mask)}")
        
        if state.trick.num_played > 0:
            print(f"Trick so far: {mu.mask_str(state.trick.mask)} (lead: {state.trick.lead_suit})")
        
        while True:
            try:
                inp = input("Enter card (e.g., 'As' for Ace of spades): ").strip()
                card = self._parse_card(inp)
                if card in legal_cards:
                    return card
                print(f"Card {inp} is not a legal move. Try again.")
            except (ValueError, KeyError) as e:
                print(f"Invalid input: {e}. Try again.")
    
    def _parse_card(self, s: str) -> int:
        """Parse a card string like 'As', 'Kh', '7d' to card index."""
        if len(s) != 2:
            raise ValueError("Card must be 2 characters (rank + suit)")
        
        rank_char = s[0].upper()
        suit_char = s[1].lower()
        
        ranks = "23456789TJQKA"
        suits = "shdc"
        
        rank_idx = ranks.index(rank_char)
        suit_idx = suits.index(suit_char)
        
        return suit_idx * 13 + rank_idx


__all__ = ["Player", "RandomPlayer", "MCTSPlayer", "HumanPlayer"]

