# 0008: WebExtensions

**Status:** Open

## Background

roytam1's tree keeps the WebExtensions support that upstream UXP removed.
Upstream Basilisk's maintainer has said it won't support them. The
implementation dates from Firefox 52–55, so many current add-ons won't work.

## Options

1. **Keep as-is,** documented as best effort. (Current lean.)
2. **Keep and backport** the APIs a few important add-ons need, for example
   legacy builds of uBlock Origin.
3. **Remove,** to save memory and maintenance.

## How to decide

Measure the memory and start-up cost of the WebExtensions code on NT 4.0 in
M4, then choose. XUL extensions are supported in every case.
