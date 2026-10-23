"""Tests for the behavioural reference model (model/evm_model.py).

They encode the system specifications S1-S7 of the project report (Section IV)
and the verification scenarios of its Results section, so that a regression in
the model - or a future edit of the firmware that the model is updated to
follow - is caught automatically.
"""
import itertools
import random
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "model"))
from evm_model import (BALLOT, ENDED, ID_ENTRY, LOGIN, PAUSED, PAUSE_PW,  # noqa: E402
                       WAIT_START, Config, EVM, boot_to_voting)


def cast(m, voter_id, candidate):
    m.settle()
    m.type_keys(voter_id)
    assert m.phase == BALLOT, m.display
    m.settle()
    assert m.vote(candidate)


# ---------------------------------------------------------------- admin login
def test_wrong_password_loops_then_correct_grants_access():
    m = EVM()
    m.settle()
    m.type_keys("0000")
    assert m.display == "Wrong Password!" and m.phase == LOGIN
    m.settle()
    m.type_keys("1357")
    assert m.display == "Access Granted!" and m.phase == WAIT_START


def test_voting_cannot_start_without_login_and_star():
    m = EVM()
    m.settle()
    for k in "1111*#":
        m.press(k)
    assert m.phase == LOGIN and not m.voting_on
    m.type_keys("1357")
    m.settle()
    m.type_keys("5")
    assert m.phase == WAIT_START
    m.type_keys("*")
    assert m.phase == ID_ENTRY and m.voting_on


def test_keys_during_delay_window_are_dropped():
    m = EVM()
    m.settle()
    m.type_keys("0000")
    assert m.busy
    assert m.press("1") is False


# ------------------------------------------------------------ one vote per ID
def test_valid_vote_counts_once_and_duplicate_is_rejected():
    m = boot_to_voting()
    cast(m, "1111", 0)
    m.settle()
    m.type_keys("1111")
    assert m.display == "Already Voted" and m.votes == [1, 0, 0]
    assert m.votes_cast == 1


def test_invalid_id_rejected():
    m = boot_to_voting()
    m.type_keys("9999")
    assert m.display == "Invalid ID" and m.votes_cast == 0


def test_star_inside_id_makes_it_invalid_not_a_crash():
    m = boot_to_voting()
    m.type_keys("11*1")
    assert m.display == "Invalid ID"


# ------------------------------------------------------------------- results
@pytest.mark.parametrize("votes", list(itertools.product(range(6), repeat=3)))
def test_winner_logic_matches_reference_for_all_small_tallies(votes):
    m = EVM()
    m.votes = list(votes)
    winner, tie = m.result()
    top = max(votes)
    leaders = [i for i, v in enumerate(votes) if v == top]
    if top == 0 or len(leaders) > 1:
        assert tie and winner is None
    else:
        assert not tie and winner == leaders[0]


def test_report_scenario_clear_winner_bnp1_jmt1_ncp3():
    m = boot_to_voting()
    for vid, cand in [("1111", 0), ("2222", 1), ("3333", 2), ("4444", 2), ("5555", 2)]:
        cast(m, vid, cand)
    m.settle()
    assert m.phase == ENDED and m.votes == [1, 1, 3]
    assert m.result() == (2, False)


def test_report_scenario_draw_when_nobody_votes():
    m = boot_to_voting()
    m.advance(125_000)
    assert m.phase == ENDED and m.votes == [0, 0, 0]
    assert m.result() == (None, True)


# --------------------------------------------------------------------- timer
def test_session_ends_early_when_all_voters_have_voted():
    m = boot_to_voting()
    for n, vid in enumerate(m.cfg.voters):
        cast(m, vid, n % 3)
    m.settle()
    assert m.phase == ENDED and m.votes_cast == 5


def test_voting_window_is_just_under_119_seconds():
    """Model-predicted consequence of the latched Timer1 flag at sei().

    The first ISR fires immediately, so the 2:00 budget is consumed one tick
    early; the window is (118, 119] s rather than 120 s. Worth confirming in
    Proteus - see README section 9, finding K6.
    """
    for phase in range(0, 1000, 50):
        m = EVM(Config(tick_phase_ms=phase))
        m.settle()
        m.type_keys("1357")
        m.settle()
        m.press("*")                      # sei() happens here
        t0 = m.now_ms
        while m.phase != ENDED:
            m.advance(10)
        window = (m.now_ms - t0) / 1000
        assert 118.0 <= window <= 119.05, (phase, window)


# ---------------------------------------------------------------- pause/resume
def test_pause_freezes_clock_and_star_resumes():
    m = boot_to_voting()
    m.advance(10_000)
    m.type_keys("#1357")
    assert m.phase == PAUSED and m.is_paused
    snapshot = (m.minutes, m.seconds)
    m.advance(60_000)
    assert (m.minutes, m.seconds) == snapshot
    m.press("*")
    assert m.phase == ID_ENTRY and not m.is_paused
    m.advance(2_000)
    assert (m.minutes, m.seconds) != snapshot


def test_pause_wrong_password_does_not_pause():
    m = boot_to_voting()
    m.type_keys("#0000")
    assert m.display == "Wrong Password!" and not m.is_paused


def test_pause_password_entry_times_out_after_about_30_seconds():
    m = boot_to_voting()
    m.press("#")                          # timeoutCounter = 30 starts here
    t0 = m.now_ms
    m.type_keys("13")
    while m.phase == PAUSE_PW:
        m.advance(10)
    assert 29.0 <= (m.now_ms - t0) / 1000 <= 30.1
    assert m.display == "Timeout!" and not m.is_paused


def test_hash_midway_through_id_discards_partial_id():
    m = boot_to_voting()
    m.type_keys("11")
    m.type_keys("#")
    assert m.phase == PAUSE_PW and m.buf == []


# --------------------------------------------------------------------- fuzzing
def test_fuzz_invariants_hold_for_random_operator_behaviour():
    """S2 (single vote per ID) and tally conservation under random input."""
    rng = random.Random(373)
    for _ in range(300):
        m = boot_to_voting(Config(tick_phase_ms=rng.randrange(1000)))
        for _ in range(rng.randrange(20, 400)):
            r = rng.random()
            if r < 0.70:
                m.press(rng.choice("0123456789*#1111222233334444"))
            elif r < 0.85:
                m.vote(rng.randrange(3))
            else:
                m.advance(rng.randrange(1, 5000))
            assert sum(m.votes) == m.votes_cast == sum(m.voted) <= len(m.cfg.voters)
            if m.phase == ENDED:
                break


# ------------------------------------------------------------------- hardening
def test_lockout_blocks_input_after_n_wrong_attempts_but_not_after_success():
    cfg = Config(lockout_after=3, lockout_base_s=30)
    m = EVM(cfg)
    m.settle()
    for _ in range(3):
        m.type_keys("0000")
        m.settle() if m.busy_until - m.now_ms < 5_000 else None
    assert m.busy_until - m.now_ms >= 25_000  # locked for ~30 s
    assert m.press("1") is False
    m.settle()
    m.type_keys("1357")
    assert m.phase == WAIT_START
