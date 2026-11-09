#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Create weekly development tags (YY.MM-Nw) on master.

Rule (docs/VERSIONING.md):
- Week = ISO week, Monday-Sunday.
- YY.MM = month of the week's Monday.
- N = ordinal of that Monday within its month (1 = first Monday).
- The tag points to the earliest commit of the week on master (author date).
- Tags are lightweight; release tags vYY.MM remain annotated.

Usage:
  scripts/tag-dev-week.py              # current week
  scripts/tag-dev-week.py --backfill   # every week with commits (idempotent)
  scripts/tag-dev-week.py --dry-run
"""
import datetime
import subprocess
import sys


def git(*args):
    return subprocess.run(['git'] + list(args), check=True,
                          capture_output=True, text=True).stdout


def week_groups():
    out = git('log', '--format=%H%x00%aI', 'master')
    groups = {}
    for line in out.splitlines():
        if not line:
            continue
        sha, iso = line.split('\0', 1)
        dt = datetime.datetime.fromisoformat(iso)
        day = dt.astimezone().date()
        monday = day - datetime.timedelta(days=day.weekday())
        groups.setdefault(monday, []).append((dt, sha))
    for monday in groups:
        groups[monday].sort()
    return groups


def tag_name(monday, groups):
    # natural week ordinal: which Monday of the month this Monday is
    ordinal = (monday.day - 1) // 7 + 1
    return '%s-%dw' % (monday.strftime('%y.%m'), ordinal)


def create_tag(name, sha, dry_run):
    existing = subprocess.run(['git', 'rev-parse', '-q', '--verify',
                               'refs/tags/' + name],
                              capture_output=True, text=True)
    if existing.returncode == 0:
        pointed = existing.stdout.strip()
        if pointed == sha:
            print('%s already exists at %s' % (name, sha[:7]))
        else:
            print('ERROR: %s exists at %s, expected %s' %
                  (name, pointed[:7], sha[:7]))
            return 1
        return 0
    if dry_run:
        print('would tag %s -> %s' % (name, sha[:7]))
        return 0
    git('tag', name, sha)
    print('tagged %s -> %s' % (name, sha[:7]))
    return 0


def main():
    args = sys.argv[1:]
    dry_run = '--dry-run' in args
    backfill = '--backfill' in args
    groups = week_groups()
    if not groups:
        print('no commits on master')
        return 0
    if backfill:
        targets = sorted(groups)
    else:
        today = datetime.date.today()
        monday = today - datetime.timedelta(days=today.weekday())
        if monday not in groups:
            print('no commits in the current week (%s)' % monday)
            return 0
        targets = [monday]
    rc = 0
    for monday in targets:
        rc |= create_tag(tag_name(monday, groups),
                         groups[monday][0][1], dry_run)
    return rc


if __name__ == '__main__':
    sys.exit(main())
