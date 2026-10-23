"""Behavioural reference model of the EVM firmware (firmware/evm_final.c).

This is a *model*, not an emulator: it reproduces the control flow, the
1 Hz Timer1 ISR, and the blocking ``delay_ms`` windows of the C source so that
the system-level properties claimed in the report (one-vote-per-ID, tie
handling, timer expiry, pause/resume) can be checked automatically and so that
the security analysis in ``analysis/`` rests on executable numbers rather than
hand arithmetic.

Time is kept in milliseconds. ``busy_until`` models ``delay_ms()``: while the
firmware is inside a delay it is not polling the keypad, so key presses made in
that window are lost (``press`` returns False).

Optional hardening (NOT in the original firmware, off by default):
``Config.lockout_after`` / ``lockout_base_s`` / ``lockout_cap_s`` add an
escalating lock-out after repeated wrong admin passwords.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional, Tuple

CANDIDATES = ("BNP", "JMT", "NCP")

# Firmware phases
LOGIN, WAIT_START, ID_ENTRY, PAUSE_PW, PAUSED, BALLOT, ENDED = (
    "LOGIN", "WAIT_START", "ID_ENTRY", "PAUSE_PW", "PAUSED", "BALLOT", "ENDED",
)


@dataclass
class Config:
    admin_pass: str = "1357"
    voters: Tuple[str, ...] = ("1111", "2222", "3333", "4444", "5555")
    time_limit_min: int = 2          # #define timeLimit
    msg_delay_ms: int = 1500         # delay_ms(1500) after wrong pw / bad ID / thanks
    pause_timeout_ticks: int = 30    # timeoutCounter = 30 (decremented at 1 Hz)
    tick_ms: int = 1000              # Timer1 CTC period: OCR1A=15624, /1024 @ 16 MHz
    tick_phase_ms: int = 0           # where the free-running timer is at boot
    # --- optional hardening, disabled by default (= firmware behaviour) ---
    lockout_after: int = 0           # wrong attempts before a lock-out; 0 disables
    lockout_base_s: int = 30
    lockout_cap_s: int = 900


@dataclass
class EVM:
    cfg: Config = field(default_factory=Config)

    def __post_init__(self) -> None:
        c = self.cfg
        self.now_ms = 0
        self.busy_until = 0
        self.phase = LOGIN
        self.buf: List[str] = []
        self.minutes = 0
        self.seconds = 0
        self.voting_on = False
        self.is_paused = False
        self.timeout_counter = 0
        self.voted = [False] * len(c.voters)
        self.votes = [0, 0, 0]
        self.votes_cast = 0
        self.voter_index = -1
        self.display = "Nirbachon-2026"
        self.log: List[str] = []
        self.wrong_attempts = 0
        self.lock_episodes = 0
        self.total_wrong = 0
        # splash screen is a 2 s delay_ms in main()
        self.busy_until = 2000
        self._next_tick = c.tick_ms - c.tick_phase_ms or c.tick_ms

    # ------------------------------------------------------------------ time
    def _isr(self) -> None:
        """timer1_compa_isr(): 1 Hz."""
        if self.voting_on and not self.is_paused:
            if self.seconds == 0:
                if self.minutes == 0:
                    self.voting_on = False
                else:
                    self.minutes -= 1
                    self.seconds = 59
            else:
                self.seconds -= 1
        if self.timeout_counter > 0:
            self.timeout_counter -= 1

    def advance(self, ms: int) -> None:
        """Advance wall-clock time, firing the 1 Hz tick at each boundary."""
        target = self.now_ms + ms
        while self._next_tick <= target:
            self.now_ms = self._next_tick
            self._next_tick += self.cfg.tick_ms
            # The ISR only runs once the I-bit is set (sei() at voting start);
            # before that the compare flag just stays latched (see _start_voting).
            if self.phase not in (LOGIN, WAIT_START):
                self._isr()
                self._pause_timeout_check()
            self._poll_end()
        self.now_ms = target
        self._poll_end()

    def settle(self) -> None:
        """Wait until the firmware is out of any delay_ms() window."""
        if self.busy_until > self.now_ms:
            self.advance(self.busy_until - self.now_ms)

    @property
    def busy(self) -> bool:
        return self.now_ms < self.busy_until

    def _delay(self, ms: int) -> None:
        self.busy_until = max(self.busy_until, self.now_ms + ms)

    def _pause_timeout_check(self) -> None:
        """`while (passIndex < idLen && timeoutCounter > 0)` fell through."""
        if self.phase == PAUSE_PW and self.timeout_counter == 0:
            self._back_to_id("Timeout!")

    # -------------------------------------------------------------- end test
    def _poll_end(self) -> None:
        """Top-of-loop test in main(): only evaluated while polling the keypad."""
        if self.phase == ID_ENTRY and not self.busy:
            if self.votes_cast >= len(self.cfg.voters) or (
                self.minutes == 0 and self.seconds == 0
            ) or not self.voting_on:
                self.voting_on = False
                self.phase = ENDED
                self.display = "Voting Ended!"

    # ----------------------------------------------------------- admin guard
    def _record_wrong(self) -> None:
        self.total_wrong += 1
        self.wrong_attempts += 1
        c = self.cfg
        if c.lockout_after and self.wrong_attempts >= c.lockout_after:
            lock_s = min(c.lockout_base_s * (2 ** self.lock_episodes), c.lockout_cap_s)
            self.lock_episodes += 1
            self.wrong_attempts = 0
            self._delay(int(lock_s * 1000))

    def _record_right(self) -> None:
        self.wrong_attempts = 0

    # ---------------------------------------------------------------- inputs
    def press(self, key: str) -> bool:
        """One keypad press+release. Returns False if the firmware was busy."""
        assert len(key) == 1 and key in "0123456789*#"
        if self.busy:
            return False
        ph = self.phase
        if ph == LOGIN:
            self.buf.append(key)
            if len(self.buf) == 4:
                ok = "".join(self.buf) == self.cfg.admin_pass
                self.buf = []
                if ok:
                    self._record_right()
                    self.phase = WAIT_START
                    self.display = "Access Granted!"
                else:
                    self._record_wrong()
                    self.display = "Wrong Password!"
                self._delay(self.cfg.msg_delay_ms)
        elif ph == WAIT_START:
            if key == "*":
                self._start_voting()
        elif ph == ID_ENTRY:
            if key == "#":
                self.phase, self.buf = PAUSE_PW, []
                self.timeout_counter = self.cfg.pause_timeout_ticks
                self.display = "Enter Pass"
            else:
                self.buf.append(key)
                if len(self.buf) == 4:
                    self._check_id("".join(self.buf))
                    self.buf = []
        elif ph == PAUSE_PW:
            if self.timeout_counter > 0:
                self.buf.append(key)
                if len(self.buf) == 4:
                    ok = "".join(self.buf) == self.cfg.admin_pass
                    self.buf = []
                    self.timeout_counter = 0
                    if ok:
                        self._record_right()
                        self.is_paused = True
                        self.phase = PAUSED
                        self.display = "System Paused"
                    else:
                        self._record_wrong()
                        self._back_to_id("Wrong Password!")
        elif ph == PAUSED:
            if key == "*":
                self.is_paused = False
                self._back_to_id("Enter ID or # to Pause", delay=False)
        return True

    def vote(self, candidate: int) -> bool:
        """Push button PA0/PA1/PA2 (active low) while on the ballot screen."""
        if self.busy or self.phase != BALLOT:
            return False
        self.votes[candidate] += 1
        self.voted[self.voter_index] = True
        self.votes_cast += 1
        self.display = "Thank You!"
        self.phase = ID_ENTRY
        self._delay(self.cfg.msg_delay_ms)
        return True

    # ------------------------------------------------------------ internals
    def _start_voting(self) -> None:
        self.minutes, self.seconds = self.cfg.time_limit_min, 0
        self.voting_on = True
        self.phase = ID_ENTRY
        self.display = "Enter ID or # to Pause"
        # sei(): the Timer1 compare flag has been latched since boot (login
        # takes far longer than 1 s), so the ISR fires once immediately.
        self._isr()

    def _check_id(self, ident: str) -> None:
        try:
            i = self.cfg.voters.index(ident)
        except ValueError:
            self.display = "Invalid ID"
            self._delay(self.cfg.msg_delay_ms)
            return
        if self.voted[i]:
            self.display = "Already Voted"
            self._delay(self.cfg.msg_delay_ms)
            return
        self.voter_index = i
        self.phase = BALLOT
        self.display = "Use Buttons to Vote"

    def _back_to_id(self, msg: str, delay: bool = True) -> None:
        self.phase, self.buf = ID_ENTRY, []
        self.display = msg
        if delay:
            self._delay(self.cfg.msg_delay_ms)

    # --------------------------------------------------------------- results
    def result(self) -> Tuple[Optional[int], bool]:
        """(winner_index, is_tie) exactly as computed at the end of main()."""
        max_votes = max(self.votes)
        winner, count = -1, 0
        for j, v in enumerate(self.votes):
            if v == max_votes:
                count += 1
                winner = j
        is_tie = count > 1 or max_votes == 0
        return (None if is_tie else winner), is_tie

    # --------------------------------------------------------------- helpers
    def type_keys(self, keys: str, key_ms: int = 300) -> None:
        """Type a key sequence, waiting out delay windows like a patient user."""
        for k in keys:
            self.settle()
            self.press(k)
            self.advance(key_ms)


def boot_to_voting(cfg: Optional[Config] = None) -> EVM:
    """Convenience: power on, log in, press '*'. Returns a running session."""
    m = EVM(cfg or Config())
    m.settle()
    m.type_keys(m.cfg.admin_pass)
    m.settle()
    m.type_keys("*")
    return m
