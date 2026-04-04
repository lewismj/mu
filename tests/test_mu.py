"""
Basic tests for the mu Python bindings.

Run with: pytest tests/
"""

import pytest


def test_import():
    """Test that the mu module can be imported."""
    import mu
    assert hasattr(mu, 'Seat')
    assert hasattr(mu, 'Suit')
    assert hasattr(mu, 'WhistState')


def test_enums():
    """Test enum values."""
    import mu
    
    # Seats
    assert mu.NORTH is not None
    assert mu.EAST is not None
    assert mu.SOUTH is not None
    assert mu.WEST is not None
    
    # Suits
    assert mu.SPADES is not None
    assert mu.HEARTS is not None
    assert mu.DIAMONDS is not None
    assert mu.CLUBS is not None


def test_constants():
    """Test constants."""
    import mu
    
    assert mu.NUM_PLAYERS == 4
    assert mu.NUM_PARTNERSHIPS == 2
    assert mu.WHIST_NUM_CARDS == 52
    assert mu.WHIST_NUM_RANKS == 13
    assert mu.WHIST_CARDS_PER_HAND == 13


def test_seed_and_new_game():
    """Test seeding and creating a new game."""
    import mu
    
    mu.seed(42)
    game1 = mu.whist_new_game(mu.SPADES, mu.NORTH)
    
    mu.seed(42)
    game2 = mu.whist_new_game(mu.SPADES, mu.NORTH)
    
    # Same seed should produce same hands
    assert game1.hands[0] == game2.hands[0]
    assert game1.hands[1] == game2.hands[1]
    assert game1.hands[2] == game2.hands[2]
    assert game1.hands[3] == game2.hands[3]


def test_card_helpers():
    """Test card mask helper functions."""
    import mu
    
    # Create a mask with a few cards
    cards = [0, 12, 25]  # 2s, As, Kh
    mask = mu.cards_to_mask(cards)
    
    # Convert back
    result = mu.mask_to_cards(mask)
    assert sorted(result) == sorted(cards)
    
    # Count
    assert mu.popcount(mask) == 3


def test_card_str():
    """Test card string representation."""
    import mu
    
    # Ace of spades is index 12
    assert mu.card_str(12) == "As"
    
    # 2 of spades is index 0
    assert mu.card_str(0) == "2s"
    
    # King of hearts is index 24
    assert mu.card_str(24) == "Kh"


def test_seat_helpers():
    """Test seat helper functions."""
    import mu
    
    # Next seat (clockwise)
    assert mu.next_seat(mu.NORTH) == mu.EAST
    assert mu.next_seat(mu.EAST) == mu.SOUTH
    assert mu.next_seat(mu.SOUTH) == mu.WEST
    assert mu.next_seat(mu.WEST) == mu.NORTH
    
    # Partner
    assert mu.partner_of(mu.NORTH) == mu.SOUTH
    assert mu.partner_of(mu.SOUTH) == mu.NORTH
    assert mu.partner_of(mu.EAST) == mu.WEST
    assert mu.partner_of(mu.WEST) == mu.EAST
    
    # Partnership
    assert mu.partnership_of(mu.NORTH) == 0
    assert mu.partnership_of(mu.SOUTH) == 0
    assert mu.partnership_of(mu.EAST) == 1
    assert mu.partnership_of(mu.WEST) == 1


def test_legal_moves():
    """Test legal move generation."""
    import mu
    
    mu.seed(123)
    game = mu.whist_new_game(mu.HEARTS, mu.SOUTH)
    
    # At start, all cards in hand should be legal
    legal = mu.legal_moves(game)
    hand = game.current_hand_mask()
    assert legal == hand


def test_play_card():
    """Test playing a card."""
    import mu
    
    mu.seed(456)
    game = mu.whist_new_game(mu.DIAMONDS, mu.EAST)
    
    initial_player = game.current_player
    legal = mu.legal_moves(game)
    cards = mu.mask_to_cards(legal)
    card = cards[0]
    
    mu.play_card(game, initial_player, card)
    
    # Card should be removed from hand
    assert (game.hands[int(initial_player)] & (1 << card)) == 0
    
    # Card should be in played set
    assert (game.played & (1 << card)) != 0
    
    # Current player should advance
    assert game.current_player == mu.next_seat(initial_player)


def test_belief_state():
    """Test belief state initialization."""
    import mu
    
    mu.seed(789)
    game = mu.whist_new_game(mu.CLUBS, mu.WEST)
    
    belief = mu.WhistBelief()
    belief.init(game, mu.NORTH)
    
    assert belief.observer == mu.NORTH


def test_rollout():
    """Test random rollout."""
    import mu
    
    mu.seed(111)
    game = mu.whist_new_game(mu.SPADES, mu.NORTH)
    
    result = mu.whist_rollout(game)
    
    # Result should be [NS_tricks, EW_tricks]
    assert len(result) == 2
    assert result[0] + result[1] == 13  # Total tricks


def test_full_game_random():
    """Test playing a full game with random moves."""
    import mu
    import random
    
    mu.seed(222)
    random.seed(222)
    
    game = mu.whist_new_game(mu.HEARTS, mu.EAST)
    
    while game.tricks_remaining > 0:
        legal = mu.legal_moves(game)
        cards = mu.mask_to_cards(legal)
        card = random.choice(cards)
        mu.play_card(game, game.current_player, card)
    
    # Game should be complete
    assert game.tricks_remaining == 0
    assert game.tricks_won[0] + game.tricks_won[1] == 13


@pytest.mark.slow
def test_mcts_search():
    """Test MCTS search (may be slow)."""
    import mu
    
    mu.seed(333)
    game = mu.whist_new_game(mu.SPADES, mu.SOUTH)
    
    belief = mu.WhistBelief()
    belief.init(game, game.current_player)
    
    # Run search with few iterations for speed
    best = mu.whist_search(game, belief, iterations=100, exploration=1.414)
    
    # Result should be a legal move
    legal = mu.legal_moves(game)
    assert (legal & (1 << best)) != 0

