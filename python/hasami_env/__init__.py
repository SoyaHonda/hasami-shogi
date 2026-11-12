"""Hasami shogi learning environment (plan 2026-11-12 section 4).

    env = hasami_env.BatchEnv(n_envs=256, seed=1, opponent="b1",  # "b1" | "race" | "random"
                              speed_scale=1.0)
    obs, scal, mask = env.reset()
    obs, scal, mask, reward, dt_ms, done, info = env.step(actions)

obs int16 [N, 12, 7, 7], scal int16 [N, 6], mask bool [N, 1177], actions int32 [N] (each in its
mask, otherwise ValueError), reward float32 [N], dt_ms int32 [N], done bool [N]. A finished game
restarts by itself: obs, scal and mask are then the new game's first decision point, while reward,
dt_ms and info describe the finished game. info: winner int8 [N] (0 none: running, a draw or cut
off; 1 human; 2 machine), captures int16 [N, 2] (by the machine, by the human), truncated bool
[N], game_ms int32 [N] and activity, a dict of [N] arrays (outside, mean_row, play_ms, threats,
moves, back_forth, first_capture_ms, lead_changes).

Further keywords: level ("hard"; the machine level the bot reads the machine with and the default
of read_actions), capture_reward (0.2 per piece of capture difference), n_threads (0: one per
CPU), track_activity (True), tick_ms (10). env.read_actions(level=None) returns the actions the
hand-written read (Ai think) picks at the current decision points.

The planes, scalars and actions are documented in lib/core/Features.h. SOURCE_HASH is the hash of
the C++ sources the module was built from (hasami_env/srchash.py).
"""

from ._core import BOARD, N_ACTIONS, N_CHANNELS, N_SCALARS, SOURCE_HASH, STAY, BatchEnv

__all__ = ["BatchEnv", "BOARD", "N_ACTIONS", "N_CHANNELS", "N_SCALARS", "SOURCE_HASH", "STAY"]
