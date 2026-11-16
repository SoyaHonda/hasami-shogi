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

opponent "race3" is Race plus a search for captures of two or three placements.

    duel = hasami_env.DuelEnv(n_envs=256, seed=1, speed_scale=1.0)  # both sides from Python
    obs, scal, mask, side = duel.reset()
    obs, scal, mask, side, reward, dt_ms, done, info = duel.step(actions)

DuelEnv (plan 2026-11-12 7.3): side int8 [N] is the side to act in each game (0 machine, 1 human);
obs, scal and mask are that side's view (lib/core/Features.h: buildFeatures, buildHumanFeatures),
and actions[i] is taken for that side. reward and dt_ms are the returned side's since its previous
decision point (ignore them at its first one in a game). When done, info["final_reward"] and
info["final_dt_ms"] ([N, 2], machine then human) give both sides' reward and time from their last
decision points to the end. The human lifts the chosen piece at once and puts it down
human_move_ms (500) +-40% later; it is asked again human_react_ms (400) after its placement lands,
or human_wait_ms (250) after "do nothing". read_actions() gives the hand-written read's moves at the
machine's decision points (STAY elsewhere).

The planes, scalars and actions are documented in lib/core/Features.h. SOURCE_HASH is the hash of
the C++ sources the module was built from (hasami_env/srchash.py).
"""

from ._core import BOARD, N_ACTIONS, N_CHANNELS, N_SCALARS, SOURCE_HASH, STAY, BatchEnv, DuelEnv

__all__ = ["BatchEnv", "DuelEnv", "BOARD", "N_ACTIONS", "N_CHANNELS", "N_SCALARS", "SOURCE_HASH", "STAY"]
