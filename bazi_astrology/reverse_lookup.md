# Four-pillar reverse lookup / 八字反查

Unreleased development API. No release/tag is implied by this document.

`taiyin::bazi::reverse_lookup_at_ut1()` accepts a calendar, a
`BaziReverseRequest`, and an output vector. Include
`taiyin/bazi/reverse_lookup.h`. All four packed Ganzhi pillars are required;
`0xff` is not a wildcard. The C ABI is
`taiyin_bazi_reverse_lookup_at_ut1()` in `taiyin/c/bazi.h`, with request/result
initializers and a count-then-fill buffer contract.

Results are ordered, maximal half-open intervals `[start_jd_ut1,end_jd_ut1)`,
clipped to the requested `[start,end)` range. They are not exact reconstructed
birth instants. Empty ranges and no matches succeed with zero results. Native
errors propagate; no approximate replacement is returned. C++ output is
unchanged on error; C short-buffer calls report the required count and write
no candidate records. Flags include all operations in the search.

The day pillar is a conservative prefilter only. Candidates are verified by
the existing `calculate_four_pillars()` engine and subdivided at the existing
effective `next_pillar_transition()` boundaries, including assigned historical civil
midnights. Astronomical transitions include the forward engine's existing
root-equality tolerance (1e-10 day); the event's physical JD is unchanged.
Assigned historical Li Chun switches year and month strictly together at its
assigned civil midnight; it does not borrow this astronomical-root tolerance.
The existing `next_pillar_jie()` point-search contract is preserved for Ziwei.
No new calendar or solar-term algorithm is implemented.

Clock modes are `FixedOffset=0`, `MeanSolar=1`, `ApparentSolar=2`. FixedOffset
adds the calendar's configured offset to **UT1**, not UTC. Solar modes use
east-positive longitude in radians and the existing solar-time solver, with
each endpoint separately inverted. The calendar's lunar-month day-boundary
mode is independent. The physical Jie instant is never shifted to the
virtual chart clock. Unsupported coverage or a non-converging clock inverse
returns the existing native error instead of a guessed interval.

中文：输入物理 UT1 搜索范围和完整四柱，输出所有符合条件的时间区间，不是
“出生分钟”。三种早晚子时规则沿用正排干支引擎；不分早晚子的 23:00–01:00
在四柱相同时合并，另两种规则仍按各自日干／时干行为判断。真太阳时逐个端点
反解；定气仍按真实物理时刻和所选历史规则，不按虚拟钟表时间移动。

UTC 用户须先显式转换成 UT1，缺 EOP 时是否允许估算由调用者的时间策略决定。
这里的固定偏移不是 IANA 时区／夏令时服务。JS/Python 都直接调用这份 C++ 搜索，
没有第二套反查实现。Python 在复制历法上下文后释放 GIL，结果回到 Python 后
才构造对象，不共享全局搜索状态。
