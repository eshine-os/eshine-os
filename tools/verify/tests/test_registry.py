# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Registry and selection invariants (no QEMU)."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))

from gatesuite import registry, selection  # noqa: E402
from gatesuite.runner import shard_groups  # noqa: E402


class RegistryTest(unittest.TestCase):
    def test_validate(self):
        self.assertEqual(registry.validate(), [])

    def test_ids_unique_and_names(self):
        gates = registry.collect()
        self.assertGreater(len(gates), 0)
        for gid, g in gates.items():
            self.assertEqual(gid, g.id)
            self.assertTrue(g.name)
        for gid in registry.RETIRED:
            self.assertNotIn(gid, gates)

    def test_scenario_membership(self):
        for key, sc in registry.SCENARIOS.items():
            for gid in sc.gate_ids:
                self.assertEqual(registry.GATE[gid].scenario, key)



    def test_shared_boot_batches(self):
        gates = registry.collect()
        for key, ids in registry.BATCHES.items():
            self.assertLessEqual(len(ids), registry.BATCH_SIZE)
            for gid in ids:
                g = gates[gid]
                self.assertEqual(g.batch, key)
                self.assertEqual(g.fixture, "vm0")
                self.assertIsNone(g.scenario)
                self.assertNotIn(gid, registry.BATCH_EXCLUDE)
        for gid in registry.BATCH_EXCLUDE:
            self.assertIsNone(gates[gid].batch,
                              "excluded gate %d must keep a private boot"
                              % gid)


    def test_shard_partition(self):
        groups = [(i, "gate", None) for i in range(10)]
        for n in (2, 3, 4):
            seen = []
            for k in range(1, n + 1):
                seen.extend(shard_groups(groups, k, n))
            self.assertEqual(sorted(seen), groups)

class SelectionTest(unittest.TestCase):
    def test_smoke_is_1_to_18(self):
        sel = selection.select(suite="smoke")
        self.assertEqual([g.id for g in sel.gates], list(range(1, 19)))

    def test_retired_excluded(self):
        sel = selection.select(suite="green")
        ids = {g.id for g in sel.gates}
        self.assertNotIn(58, ids)
        self.assertNotIn(61, ids)

    def test_stop_after_partial(self):
        sel = selection.select(stop_after=18)
        self.assertTrue(sel.partial)
        self.assertEqual([g.id for g in sel.gates], list(range(1, 19)))

    def test_parse_spec(self):
        self.assertEqual(selection.parse_gate_spec("1-3,7"),
                         [1, 2, 3, 7])


if __name__ == "__main__":
    unittest.main()