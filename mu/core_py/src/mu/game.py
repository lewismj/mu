"""
mu.game — Game management and tournament utilities.

This module provides high-level game orchestration:
- GameRunner: Run a single game with any combination of agents
- Tournament: Run multiple games and collect statistics

Example
-------
>>> from mu.game import GameRunner
>>> from mu.agents import RandomPlayer, MCTSPlayer
>>> 
>>> runner = GameRunner()
>>> players = {
...     mu.NORTH: MCTSPlayer(iterations=500),
...     mu.EAST: RandomPlayer(),
...     mu.SOUTH: MCTSPlayer(iterations=500),
...     mu.WEST: RandomPlayer(),
... }
>>> result = runner.play_game(players, trump=mu.SPADES, dealer=mu.NORTH)
>>> print(f"NS won {result.tricks_won[0]} tricks")
"""

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Callable
import time

import mu
from mu.agents import Player


@dataclass
class GameResult:
    """Result of a completed game."""
    tricks_won: List[int]  # [NS_tricks, EW_tricks]
    winner: int  # Partnership index (0=NS, 1=EW)
    trump: mu.Suit
    dealer: mu.Seat
    play_history: List[tuple]  # [(seat, card), ...]
    elapsed_time: float  # seconds
    
    @property
    def ns_tricks(self) -> int:
        return self.tricks_won[0]
    
    @property
    def ew_tricks(self) -> int:
        return self.tricks_won[1]


class GameRunner:
    """
    Manages a single game of Whist.
    
    Handles the game loop, notifying players of state changes,
    and collecting the play history.
    """
    
    def __init__(self, verbose: bool = False):
        self.verbose = verbose
    
    def play_game(
        self,
        players: Dict[mu.Seat, Player],
        trump: mu.Suit,
        dealer: mu.Seat,
        seed: Optional[int] = None
    ) -> GameResult:
        """
        Play a complete game of Whist.
        
        Parameters
        ----------
        players : dict
            Mapping from Seat to Player instance.
        trump : Suit
            Trump suit for this game.
        dealer : Seat
            Dealer seat (first player is next_seat(dealer)).
        seed : int, optional
            RNG seed for reproducibility.
            
        Returns
        -------
        GameResult
            Result containing tricks won, winner, and play history.
        """
        if seed is not None:
            mu.seed(seed)
        
        start_time = time.perf_counter()
        
        # Create game
        state = mu.whist_new_game(trump, dealer)
        play_history: List[tuple] = []
        
        # Notify players of game start
        for seat, player in players.items():
            player.on_game_start(state, seat)
        
        if self.verbose:
            self._print_initial_state(state)
        
        # Main game loop
        while state.tricks_remaining > 0:
            current = state.current_player
            player = players[current]
            
            # Capture trick state before the move
            is_lead = state.trick.num_played == 0
            lead_suit = state.trick.lead_suit if not is_lead else mu.SPADES  # placeholder
            
            # Get player's move
            card = player.select_move(state, current)
            
            # Update lead_suit for notification if this is the lead
            if is_lead:
                lead_suit = mu.suit_of(card)
            
            if self.verbose:
                self._print_move(current, card, is_lead, state)
            
            # Play the card (mutates state)
            mu.play_card(state, current, card)
            play_history.append((current, card))
            
            # Notify all players
            for seat, p in players.items():
                p.on_card_played(state, current, card, is_lead, lead_suit)
            
            if self.verbose and state.trick.num_played == 0:
                # Trick just completed
                self._print_trick_result(state)
        
        elapsed = time.perf_counter() - start_time
        
        tricks_won = list(state.tricks_won)
        winner = 0 if tricks_won[0] > tricks_won[1] else 1
        
        if self.verbose:
            self._print_final_result(tricks_won, winner)
        
        return GameResult(
            tricks_won=tricks_won,
            winner=winner,
            trump=trump,
            dealer=dealer,
            play_history=play_history,
            elapsed_time=elapsed
        )
    
    def _print_initial_state(self, state: mu.WhistState) -> None:
        print(f"\n{'='*50}")
        print(f"New Game - Trump: {state.trump}, Dealer: {state.dealer}")
        print(f"First player: {state.current_player}")
        for i, seat in enumerate([mu.NORTH, mu.EAST, mu.SOUTH, mu.WEST]):
            hand = mu.mask_str(state.hands[i])
            print(f"  {seat}: {hand}")
        print(f"{'='*50}\n")
    
    def _print_move(self, seat: mu.Seat, card: int, is_lead: bool, state: mu.WhistState) -> None:
        card_str = mu.card_str(card)
        prefix = "LEAD" if is_lead else "    "
        print(f"{prefix} {seat} plays {card_str}")
    
    def _print_trick_result(self, state: mu.WhistState) -> None:
        print(f"  -> Tricks: NS={state.tricks_won[0]}, EW={state.tricks_won[1]}")
        print()
    
    def _print_final_result(self, tricks_won: List[int], winner: int) -> None:
        winner_name = "North-South" if winner == 0 else "East-West"
        print(f"\n{'='*50}")
        print(f"Game Over! {winner_name} wins {tricks_won[winner]}-{tricks_won[1-winner]}")
        print(f"{'='*50}\n")


@dataclass
class TournamentResult:
    """Aggregated results from multiple games."""
    games_played: int
    ns_wins: int
    ew_wins: int
    ns_total_tricks: int
    ew_total_tricks: int
    game_results: List[GameResult] = field(default_factory=list)
    total_time: float = 0.0
    
    @property
    def ns_win_rate(self) -> float:
        return self.ns_wins / self.games_played if self.games_played > 0 else 0.0
    
    @property
    def ew_win_rate(self) -> float:
        return self.ew_wins / self.games_played if self.games_played > 0 else 0.0
    
    @property
    def avg_ns_tricks(self) -> float:
        return self.ns_total_tricks / self.games_played if self.games_played > 0 else 0.0
    
    @property
    def avg_ew_tricks(self) -> float:
        return self.ew_total_tricks / self.games_played if self.games_played > 0 else 0.0


class Tournament:
    """
    Run multiple games between player configurations.
    
    Supports rotating dealers and alternating trump suits.
    """
    
    def __init__(
        self,
        player_factory: Callable[[mu.Seat], Player],
        verbose: bool = False
    ):
        """
        Parameters
        ----------
        player_factory : callable
            Function that takes a Seat and returns a Player.
        verbose : bool
            Print progress during tournament.
        """
        self.player_factory = player_factory
        self.verbose = verbose
        self.runner = GameRunner(verbose=False)
    
    def run(
        self,
        num_games: int,
        rotate_dealer: bool = True,
        rotate_trump: bool = True,
        base_seed: Optional[int] = None
    ) -> TournamentResult:
        """
        Run a tournament of multiple games.
        
        Parameters
        ----------
        num_games : int
            Number of games to play.
        rotate_dealer : bool
            Rotate dealer each game.
        rotate_trump : bool
            Rotate trump suit each game.
        base_seed : int, optional
            Base seed for reproducibility (incremented each game).
            
        Returns
        -------
        TournamentResult
            Aggregated statistics.
        """
        seats = [mu.NORTH, mu.EAST, mu.SOUTH, mu.WEST]
        suits = [mu.SPADES, mu.HEARTS, mu.DIAMONDS, mu.CLUBS]
        
        result = TournamentResult(
            games_played=0,
            ns_wins=0,
            ew_wins=0,
            ns_total_tricks=0,
            ew_total_tricks=0
        )
        
        start_time = time.perf_counter()
        
        for i in range(num_games):
            dealer = seats[i % 4] if rotate_dealer else mu.NORTH
            trump = suits[i % 4] if rotate_trump else mu.SPADES
            seed = (base_seed + i) if base_seed is not None else None
            
            # Create fresh players for each game
            players = {seat: self.player_factory(seat) for seat in seats}
            
            game_result = self.runner.play_game(players, trump, dealer, seed)
            
            result.games_played += 1
            result.ns_total_tricks += game_result.ns_tricks
            result.ew_total_tricks += game_result.ew_tricks
            if game_result.winner == 0:
                result.ns_wins += 1
            else:
                result.ew_wins += 1
            result.game_results.append(game_result)
            
            if self.verbose and (i + 1) % 10 == 0:
                print(f"Game {i+1}/{num_games}: NS {result.ns_wins}-{result.ew_wins} EW")
        
        result.total_time = time.perf_counter() - start_time
        
        if self.verbose:
            self._print_summary(result)
        
        return result
    
    def _print_summary(self, result: TournamentResult) -> None:
        print(f"\n{'='*50}")
        print(f"Tournament Complete: {result.games_played} games")
        print(f"NS wins: {result.ns_wins} ({result.ns_win_rate:.1%})")
        print(f"EW wins: {result.ew_wins} ({result.ew_win_rate:.1%})")
        print(f"Avg tricks: NS={result.avg_ns_tricks:.2f}, EW={result.avg_ew_tricks:.2f}")
        print(f"Total time: {result.total_time:.2f}s ({result.games_played/result.total_time:.1f} games/sec)")
        print(f"{'='*50}\n")


__all__ = ["GameResult", "GameRunner", "TournamentResult", "Tournament"]

