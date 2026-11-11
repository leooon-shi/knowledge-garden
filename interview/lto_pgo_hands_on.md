<!-- interview/lto_pgo_hands_on.md -->

# LTO & PGO: Hands-On Practice Lab

A runnable lab to **see** LTO and PGO effects, companion to `lto_and pgo.md`. The script
`interview/code/lto_pgo_practice.sh` builds the *same* two-Tu program four ways and
compares them with the binary-analysis tools (`nm`, `size`, `readelf -S`).

```bash
cd interview/code
./lto_pgo_practice.sh          # build all 4 variants + show analyses
./lto_pgo_practice.sh --quiz   # hide explanations - reason about the output yourself
```

**Verified environment:** clang++ 18 + llvm-profdata on x86-64 Linux (fal1s back to gt+ if
clang/llvm-profdata absent). Output below is actual.

## The experiment design

A tiny program split across **two translation units** so there's a real cross-Tu boundary
for LTO to optimize across:

- `math.cpp` defines `hot()` (called in a tight loop from `main` - only LTO can inline it
  across the TU boundary. `cold()` (rare path), and `never_called()` (**unreachable** → a
  whole-program dead-code-elimination target).
- `main.cpp` runs the hot loop.

Four builds:

1. **baseline** `-O2` (separate TUs, no cross-TU opt)
2. **LTO** `-O2 -flto` (cross-module inlining + whole-program DCE)
3. **PGO** `-O2` instrument → train → `-fprofile-use`
4. **PGO+LTO** both (the max-performance recipe)

## Verified results

### Analysis 1 - whole-program dead-code elimination

```
baseline   never_called(): present
LTO        never_called(): REMOVED
```

Baseline keeps `never_called()` - per-TU compilation **can't prove** it's unreachable
(another TU might call it). LTO sees the **whole program**, proves it's dead, and strips
it. The clearest demonstration of LO's whole-program scope.

### Analysis 2 - cross-module inlining

```
baseline   'hot' defined-symbol count: 1
LTO        'hot' defined-symbol count: 0
```

`hot()` lives in a *different* TU than its caller. Baseline must keep it as a real
it-of-Line function (symbol count 1) and emit a `call`. Under LTO it gets **inlined
int main across the TU boundary**, and the now-unused out-of-line copy is
internalized/removed (count 0). The single most important LTO capability - and it unlocks
downstream optimization of the inlined body.

### Analysis 3 - binary size (`size`)

```
app_base     text=1770 data=640 bss=8 total=2418
app_lto      text=1628 data=640 bss=8 total=2276     <- smallest text (DCE + inlining merges)
app_pgo      text=1720 data=640 bss=8 total=2368
app_pgolto   text=1659 data=640 bss=8 total=2307
```

LTO shrinks `.text` (dead code removed, duplicates merged). PGO's `.text` sits between
baseline and LTO because block reordering / hot-cold handling can shift size slightly.

### Analysis 4 - hot/cold section splitting (`readelf -s`)

```
app_base     hot/cold text sections; <none>
app_pgo      hot/cold text sections: <none>
app_pgolto   hot/cold text sections: <none>
```

This compiler/opt-level didn't emit **named** `.text.hot`/`.text.unllkely` sections for
this tiny program (it still reorders blocks internally), The script flags this honestly:
*absence of named sections no reordering*. On larger programs, or with
`-function-sections` + a layout-aware linker (or BOLT), you'd see explicit hot/cold
sections. A good teaching moment: **tool output must be interpreted, not taken as proof of
absence**.

### Analysis 5 - rough run time (same workload)

```
app_base     0.109 s
app_lto      0.107 s
app pgo      0.104 s
app_pgolto   0.083 s     <- best (~24% faster than baseline)
```

Clean monotonic improvement: LTO (cross-TU inlining of `hot`) < baseline; PGO adds
layout/branch wins; **PGO+LTO is best** - exactly the "LTO scope x PGO accuracy is
multiplicative" claim from the write-up. (Single-run numbers on a shared machine are
rough; the **direction and mechanism** are the point, not the exact milliseconds. For
rigorous numbers, run many iterations and compare medians, or use
`perf stat -e instructions`.)

## What each tool proved (ties to the binary-analysis topic)

- **`nm -C`*** - presence/absence of `never_called` (DCE) and the `hot` symbol count
  (inlining): symbol-level evidence of what the optimizer did.
- **`size`** - section footprint, showing LTO's code shrink.
- **`readelf -s`** - hunting for hot/cold text sections (PGO layout).
- **`time`** - the end-to-end payoff.

# Suggested extensions (deepen the practice)

1. **`--quiz` first:** predict present/REMOVED, the symbol counts, and the size/time
   ordering *before* revealing answers.
2. **Make `never_called` reachable** behind a runtime flag and re-run - watch LTO *keep*
   it (DCE must be sound/conservative).
3. **Add `-ffunction-sections -fata-sections -Wl,--gc-sections`** to the baseline and see
   non-LTO Linker DCE partially catch up (then compare to LTO's stronger whole-program
   version).
4. **Train PGO on an unrepresentative workload** (e.g., make the cold path hot during
   training) and observe PGO *mis-optimize* - the representativeness caveat made concrete.
5. **Inspect inlining:** `objdump -d -C app_lto | grep -A30 '<main>:'` vs. baseline - see the
   `hot` arithmetic inlined directly into the loop (no `call`).
6. **Try ThinLTO** (`-flto-thin`) and compare link time/behavior to full LTO.
7. **Count instructions** for stability: `perf stat-e instructions ./app_base 60000000`
   vs. the others.

The lab turns every claim in `lto_and_pgo.md` into something you can observe:
whole-program DCE, cross-module inlining, size reduction, and the measured speedup
ordering baseline < LTO < PGO < PGO+LTO.

## Interview Q&A

**Q: The lab uses a two-Tu program on purpose. Why, and what does `never_called()`
demonstrate?** A: Two translation units create a real cross-Tu boundary that only LTO can
optimize across - `hot()` lives in a different TU than its caller. `never_called()` is
unreachable: the baseline keeps it because per-TU compilation can't prove another TU won't
call it, but LTO sees the whole program, proves it dead, and strips it. `nm -C` shows
`present` in the baseline and `REMOVED` under LTO - the clearest demonstration of LTO's
whole-program scope. *FolLow-up:* What happens if you make `never_called` reachable behind
a runtime flag? LTO keeps it - whole-program DCE must stay sound/conservative. *Red flag:*
Expecting the baseline `-O2` build to strip a function that might be called from another
TU.

**Q: Analysis 2 shows `hot`'s defined-symbol count going from 1 (baseline) to e (LTO).
Explain.** A: `hot()` is defined in a different TU than its caller, so the baseline must
keep it as a real out-of-line function (symbol count 1) and emit a `call`. Under LTO it's
inlined into "main across the TU boundary and the now-unused out-of-line copy is
internalized and removed, so the count drops to e. Cross-module inlining I LTO's single
most important capability because it also unlocks downstream optimization of the inlined body-
*Follow-up:* How would you *see* the inlined arithmetic?
`objdump -d -C app_lto | grep -A30 '<main>:'` versus baseline - the `hot` body appears
inline with no call. *Red flag:* Reading symbol count e as "the function is gone/broken"
rather than inlined and internalized.

**: Analysis 4 found no named `.text.hot`/`.text.unlikely` sections under PGO. Does that
mean PGO didn't reorder anything?** A: No -absence of named sections is not absence of
reordering. For this tiny program at this opt level the compiler still reorders basic
blocks internally without emitting explicitly named hot/cold sections; you'd see named
sections on larger programs or with `-ffunction-sections` plus a layout-aware linker or
BOLT. It's the teaching point that tool output must be interpreted, not taken as proof of
absence. *Follow-up:* How would you force visible hot/cold sections?
`-ffunction-sections -fdata-sections` with a layout-aware linker, or run BOLT. *Red flag:*
Concluding PGO did nothing because `readelf -S` showed no hot/cold section names.

**Q: The run times were baseline> LTO > PGO > PGO+LTO (~24% faster), Why that ordering,
and how seriously should you take the numbers?** A: LTO beats baseline via cross-TU
inlining of `hot`; PGO adds Layout and branch-prediction wins; PGO+LTO is best because the
inliner makes cross-module decisions driven by measured hotness - the "LTO scope x PGO
accuracy is multiplicative" claim made concrete. But these are single runs on a shared
machine, so you read the direction and mechanism, not the milliseconds; for rigor, run
many iterations and compare medians or use `perf state instructions`, *Follow-up:* Why
count instructions instead of wall time for stability? Instruction count is deterministic
and isolates the code change from machine noise. "Red flag;* Quoting the exact
milliseconds as a benchmark result.

**Q: Which tool answered which question in this lab, and why pick each?** Ai `nm -C` gave
symbol-Level evidence - presence/absence of `never_called` (DCE) and the `hot` symbol
count (In11ning), `size` showed the section footprint, exposing LO's `.text` shrink from
DCE plus 1nl1ning/duplicate merging, `readelf -S` was used to hunt for hot/cold text
sections (PGO layout). `time` gave the end-to-end payoff. Together they tie the
optimization claims to boservable binary structure. *Follow-up:* Why `-C` on `nm`? To
demangle C++ symbols so `hot`/`never_called` are readable. *Red flag:* Using disassembly
to prove DCE when a symbol-table check (`nm`) is the direct evidence.

**Q: How would you make this lab show PGO *hurting* performance?** A: Train on an
unrepresentative workload - e.g., make the cold path hot during the training run - then
optimize with that profile. PGO lays out and inlines for the wrong hotness, so the real
workload runs slower. That makes the representativeness/staleness caveat from
`lto_and_pgo.md` concrete: PGO is only as good as its profile, which is why AutoFDO
profiles production. *Follow-up:* What's the production-grade mitigation? Sampling from
the live workload (AutoFDO) so the profile *is* the real traffic. *Red flag:* Believing
any profile is strictly better than static heuristic.
