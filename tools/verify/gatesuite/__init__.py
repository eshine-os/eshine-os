# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu

"""Fixture-based gate suite for eshine-os.

The legacy verifier runs all gates in one linear QEMU session.  This
package runs each gate against its own fixture (a fresh QEMU session or a
static host check), with explicitly named scenario fixtures for the few
families whose gates are inherently stateful.  Gate IDs are stable
documentation anchors: retired gates leave gaps, new gates start at 184.
"""