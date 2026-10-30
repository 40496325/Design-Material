"""Jerk-limited (S-curve) speed ramp for the trolley and hoist commands.

ICD IF10.0 operating sequence: "the PC calculates smooth acceleration steps (to
prevent load swing)". Decision D-04 proposes an S-curve baseline with a ZV input
shaper once the sway frequency is identified - this is that baseline. The
operator sets a target speed in %, and the command sent to the crane follows it
with bounded acceleration (%/s) and bounded jerk (%/s^2), so the speed profile has
no corners.

    python ramp.py      # prints a 0 -> 100 % -> 0 profile
"""

from __future__ import annotations

import math


class SCurveRamp:
    def __init__(self, accel_pct_s: float = 100.0, jerk_pct_s2: float = 400.0):
        self.amax = accel_pct_s
        self.jmax = jerk_pct_s2
        self.v = 0.0      # commanded speed, %
        self.a = 0.0      # current acceleration, %/s
        self.steps = 0    # ramp steps taken (sent as segment_idx)

    def reset(self, value: float = 0.0) -> None:
        self.v, self.a = value, 0.0

    @property
    def settled(self) -> bool:
        return self.a == 0.0

    def step(self, target: float, dt: float) -> float:
        err = target - self.v
        if abs(err) < 0.5 and abs(self.a) < self.jmax * dt:
            self.v, self.a = float(target), 0.0
            return self.v
        # acceleration that still lets us stop exactly on target under the jerk limit
        # (one step of look-ahead so the approach to the target stays smooth in discrete time)
        rem = max(0.0, abs(err) - abs(self.a) * dt)
        a_des = math.copysign(min(self.amax, math.sqrt(2.0 * self.jmax * rem)), err)
        da = max(-self.jmax * dt, min(self.jmax * dt, a_des - self.a))
        self.a += da
        nv = self.v + self.a * dt
        if (target - self.v) * (target - nv) <= 0:   # crossed the target this step
            nv, self.a = float(target), 0.0
        self.v = nv
        self.steps += 1
        return self.v


if __name__ == "__main__":
    r = SCurveRamp()
    t, dt = 0.0, 0.05
    for target in (100, 0):
        for _ in range(50):
            r.step(target, dt)
            t += dt
            print(f"{t:5.2f} s  target {target:4d} %  cmd {r.v:6.1f} %  accel {r.a:6.1f} %/s")
