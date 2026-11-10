# Next Generation Keyer (NextGen) Development

## Introduction

This page documents an ongoing effort to modernize the core CW timing engine of this keyer. For fifteen years, the keyer's timing (dits, dahs, spacing, PTT sequencing, memory playback, beacon mode, etc.) has been built around blocking code: functions like `loop_element_lengths()` and the old `play_memory()` busy-wait or `delay()` while an element, memory, or PTT sequence plays out. That approach is simple and has served well, but it means the keyer can be unresponsive to new input (paddle taps, serial commands, button presses) while it's "stuck" inside one of those waits.

The Next Generation Keyer effort converts this blocking architecture to a non-blocking, `micros()`/`millis()`-based state machine, piece by piece, while preserving every existing feature and keeping keying fidelity identical to what operators are used to today. The goal is a keyer that's more responsive under the hood, easier to extend going forward, and no less reliable than the code it replaces.

## Why AI

From a post to the K3NG keyer group explaining the approach:

> I started writing the keyer code 15 years ago or so. I no longer have the brainpower or patience that I did a decade ago to code five or six nights a week, often into the wee hours of the morning. Claude Code can do the heavy lifting while I concentrate on functionality and testing. Claude also does a wonderful job at documentation, and I'm going to use it to write a brand new Wiki in GitHub.
>
> With the code I'm going to publish instructions for others using Claude Code or other AI tools. These instructions will "bootstrap" an LLM looking at the code for the first time. This will enable others who want to modify or enhance the code to use AI easily and effectively. Users could also use AI to configure pins, select features and options, etc.
>
> Furthermore, although I'm not planning to go SK anytime soon, this new version and the accompanying documentation should allow someone to run with the code without me if I ever get hit by the celestial bus. Sometimes those buses come out of nowhere.

In practical terms, this means:

* AI (Claude Code) does the mechanical, tedious parts of the refactor - tracing every call site, converting blocking loops to state machines, keeping dozens of feature-flag combinations compiling - under direction and review.
* K3NG focuses on what only he can do: deciding what the keyer *should* do, testing on real hardware, and making the judgment calls that require decades of operating and building experience.
* Documentation (this wiki included) gets written and kept up to date as a natural byproduct of the process, instead of being the thing that never quite gets finished.
* Alongside the code, AI-bootstrap instructions will be published so that anyone using Claude Code or another AI coding tool can get that AI up to speed on this codebase quickly - its architecture, its conventions, and the reasoning behind design decisions - lowering the barrier for others to safely contribute, extend, or configure the keyer (including letting AI help with pin assignments and feature/option selection).
* The project becomes less dependent on any one person's continued availability. Good documentation plus AI-assisted onboarding means someone else could pick up development if they ever needed to.

## Goals

* Convert the keyer's CW timing engine from blocking to non-blocking, without changing *what* the keyer does - only *how* it waits while doing it.
* Do the conversion incrementally, in five milestones (below), each one scoped narrowly enough to reason about and test in isolation.
* Snapshot each milestone separately so that if a regression is found, it's possible to tell exactly which milestone introduced it by testing the previous milestone's snapshot.
* Test every milestone on real hardware before moving to the next - compiling cleanly is necessary but not sufficient.
* Preserve exact keying fidelity: iambic A/B timing, CMOS Super Keyer timing, ultimatic mode, weighting, keying compensation, and every other existing feature should sound and behave identically to an operator afterward.
* Publish the process, the reasoning, and eventually AI-bootstrap instructions, so this isn't a one-time rewrite but a repeatable pattern others can learn from and build on.

## The Five Milestones

| Milestone | What It Converts |
|---|---|
| M1 | Manual/paddle keying (the element scheduler) |
| M2 | Automatic/typed CW sending (CLI, serial) |
| M3 | Memory playback (`play_memory()` and macros) |
| M4 | PTT lead-time and sequencer timing for CW elements |
| M5 | Hold-down keying (straight key/bug mode), PTT-only commands, and beacon mode delays |

Each milestone is a superset of the previous one's behavior - by M5, a full regression pass effectively covers all of M1-M4 as well. Development and testing are proceeding in order, M1 first.

### M1 - Manual/paddle keying

Replaces the blocking busy-wait that used to run while a dit or dah was being keyed with a `micros()`-based state machine. This is the foundation milestone - automatic/typed sending, memory playback, and PTT timing are all still on the old blocking path at this stage; only paddle-driven keying itself is converted.

**What gets tested:** every paddle-driven keying mode - Iambic A and B squeeze behavior (including the trailing-element difference between A and B), single-element dit/dah timing at multiple speeds, Ultimatic mode, single-paddle mode, Bug mode, straight key mode, CMOS Super Keyer B timing (the most intricate part of this milestone), speed changes made mid-sending, weighting/ratio/compensation settings, and the dead-operator watchdog. PTT lead/tail time is also checked, though it isn't touched until M4 - it should behave exactly as before at this stage.

**Status:** M1 hardware testing is complete, as of October 2026. Nine real bugs were found and fixed along the way - issues that only showed up on actual hardware, not in compilation or code review, because they depended on the exact timing of a loop that no longer blocks (plus one pre-existing, non-refactor bug in CMOS Super Keyer timing that hardware testing happened to surface). That's exactly why every milestone gets tested on real hardware before moving on. M2 hardware testing is starting next.

### M2 - Automatic/typed CW sending

Converts the CLI/serial typed-sending path so it enqueues characters through a non-blocking element buffer instead of blocking character-by-character.

**What gets tested:** typed sentences with full punctuation, prosigns sending as one continuous run, a paddle correctly interrupting (breaking in on) typed sending mid-transmission, Farnsworth timing, autospace, QLF "humanized" timing, and high-speed typed sending for dropped or garbled characters.

### M3 - Memory playback

Rewrites `play_memory()` - previously one large blocking function - into a persistent, non-blocking step-machine, while preserving every macro (delays, timed transmit, serial numbers, jump-to-memory, insert-memory, speed/mode/PTT control codes, and more).

**What gets tested:** every memory macro individually, paddle break-in during playback, the insert-memory macro resuming correctly afterward, triggering two memories at once (should be rejected cleanly, not corrupt playback), and beacon mode's repeat timing. This is expected to be the highest-risk milestone, since it's the largest mechanical port of intricate logic.

### M4 - PTT lead-time and sequencer timing

Converts PTT lead-time and sequencer activation (the wait before the first element of a transmission, while PTT/sequencer lines settle) to non-blocking, instead of busy-waiting for that settling time before every first element.

**What gets tested:** no clipping of the first character after a period of silence, correct sequencer pin ordering/timing, PTT staying up correctly through a multi-character transmission, and that the keyer stays responsive (CLI, paddle, display) during the lead-time wait instead of feeling "frozen."

### M5 - Hold-down keying, PTT-only commands, and beacon delays

Finishes the conversion: straight key and bug-mode hold-down, the dedicated hardware straight-key pin, USB keyboard/mouse straight-key emulation, PTT-only serial/memory commands, and beacon mode's remaining `delay()` calls all move onto the same non-blocking PTT-lead mechanism M4 built.

**What gets tested:** the same hold-down and straight-key tests as M1, now specifically after a period of silence (to exercise the PTT-lead-wait path), the dedicated straight-key pin and USB keyboard/mouse emulation if used, the timed-transmit and PTT-only memory macros, beacon mode responding promptly when told to stop mid-wait, and finally a full regression pass across everything from M1-M4 as a combined check.

## What comes after the port

The five milestones above are a port, not a rewrite - the goal through M5 is a keyer that does exactly what the current one does, just on a non-blocking foundation. Once all of the existing functionality has been ported and tested on real hardware, the plan is to start adding genuinely new functionality on top of that foundation.

That new functionality will be AI-coded, the same way the port itself has been - K3NG directing and testing, AI doing the implementation. Pull requests from the community will also be evaluated and integrated with AI assistance, so they should no longer sit for months or years waiting on review - a problem that's affected this project in the past simply because there's one maintainer and limited time.

## Following along

Development is happening on the [`next_gen_version_development`](https://github.com/k3ng/k3ng_cw_keyer/tree/next_gen_version_development) branch. Each milestone is also kept as its own isolated snapshot during development so that a regression can always be traced back to the specific milestone that introduced it.

This page will be updated as each milestone completes hardware testing and the project moves on to the next one.
