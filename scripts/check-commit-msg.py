#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Validate eshine-os commit messages (hook + CI share this file).

Real semantics:
  Model: <model>            primary model, if any (one line)
  Assisted-by: <model>      additional model that actually contributed
  Reviewed-by: <reviewer>   only after a real review; requires Review-Ref
  Review-Ref: docs/reviews/<file>.md
  Signed-off-by: Julian Chu <rootuliux@gmail.com>   mandatory, last line

The legacy uniform four-line AI block is rejected on new commits. Set
ALLOW_HISTORICAL_TRAILERS=1 for history rewrites/cherry-picks.
Set ALLOW_NO_MODEL=1 for a genuinely human-only commit.

Usage:
  scripts/check-commit-msg.py --file .git/COMMIT_EDITMSG
  scripts/check-commit-msg.py --range BASE..HEAD
"""
import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIGN = 'Signed-off-by: Julian Chu <rootuliux@gmail.com>'
SUBJECT_RE = re.compile(r'^[a-z][a-z0-9._+/-]*: .+$')
TRAILER_RE = re.compile(r'^([A-Za-z][A-Za-z0-9-]*):[ \t]*(.+)$')
REVIEW_REF_RE = re.compile(r'^docs/reviews/[A-Za-z0-9._/-]+\.md$')

ALLOWED_KEYS = {
    'Signed-off-by', 'Model', 'Assisted-by', 'Reviewed-by', 'Review-Ref',
    'Co-Authored-By', 'Reported-by', 'Tested-by', 'Acked-by', 'Fixes',
    'Closes', 'Link',
}
MODELS = {
    'claude code', 'claude',
    'claude-opus-5.5', 'claude-fable-5.1', 'claude-sonnet-5.5',
    'claude-haiku-5.5', 'glm-5.3', 'deepseek-v4.1-flash', 'deepseek-v4',
    'qwen3.8-max', 'gemini-4-neon', 'gemini-4-flash', 'antigravity',
}
BANNED_VALUES = {'', 'tbd', 'n/a', 'na', 'unknown', 'none'}


def fail(msg):
    print('commit-msg: ' + msg, file=sys.stderr)
    return 1


def identity_name(value):
    """Return lowercased display name from 'Name <email>' or bare name."""
    return value.split('<', 1)[0].strip().lower()


def parse_trailers(lines, errors):
    """Return (body_lines, [(key, value), ...]) for the final paragraph."""
    end = len(lines)
    while end > 0 and not lines[end - 1].strip():
        end -= 1
    start = end
    while start > 1:          # never treat the subject line as a trailer
        m = TRAILER_RE.match(lines[start - 1].strip())
        if not m:
            break
        start -= 1
    trailers = []
    for line in lines[start:end]:
        m = TRAILER_RE.match(line.strip())
        if m:
            trailers.append((m.group(1), m.group(2).strip()))
    body = lines[:start]
    if start > 0 and start == end:
        errors.append('no trailer block (Signed-off-by is mandatory)')
    if start > 0 and lines[start - 1].strip():
        errors.append('missing blank line before trailer block')
    return body, trailers


def check_review_ref(ref, commit=None):
    if not REVIEW_REF_RE.match(ref) or '..' in ref:
        return 'Review-Ref must be docs/reviews/<name>.md'
    if commit is None:
        path = os.path.join(REPO, ref)
        if not os.path.isfile(path):
            return 'Review-Ref file does not exist: ' + ref
        return None
    r = subprocess.run(['git', '-C', REPO, 'cat-file', '-e',
                        '%s:%s' % (commit, ref)], capture_output=True)
    if r.returncode != 0:
        return 'Review-Ref not in commit %s: %s' % (commit[:7], ref)
    return None


def validate(message, commit=None):
    errors = []
    if not message.isascii():
        errors.append('message must be ASCII only')
    text = message.replace('\r\n', '\n').rstrip('\n')
    lines = text.split('\n')
    if not lines or not lines[0].strip():
        return ['empty subject']
    subject = lines[0]
    if len(subject.encode()) > 72:
        errors.append('subject exceeds 72 bytes')
    if not SUBJECT_RE.match(subject):
        errors.append('subject must be "subsystem: summary"')
    if subject.rstrip().endswith('.'):
        errors.append('subject must not end with a period')
    body, trailers = parse_trailers(lines, errors)

    # A trailer-looking attribution line buried in the body is almost always
    # a copied legacy block; reject it so attribution stays in the trailer
    # paragraph. Other kernel trailers (Fixes:, Link:, ...) may legitimately
    # appear in prose and are not policed here.
    attribution = {'Model', 'Assisted-by', 'Reviewed-by', 'Review-Ref',
                   'Co-Authored-By'}
    for line in body[1:]:
        m = TRAILER_RE.match(line.strip())
        if m and m.group(1) in attribution:
            errors.append('trailer-like line outside final trailer block: '
                          + m.group(1))
            break

    counts = {}
    values = {}
    for key, value in trailers:
        if key not in ALLOWED_KEYS:
            errors.append('unknown trailer key: ' + key)
            continue
        counts[key] = counts.get(key, 0) + 1
        values.setdefault(key, []).append(value)
        if value.strip().lower() in BANNED_VALUES:
            errors.append('empty/placeholder value for ' + key)
        if 'example.com' in value:
            errors.append('example.com address is not a real identity')

    if counts.get('Signed-off-by', 0) != 1:
        errors.append('exactly one Signed-off-by is required')
    if trailers and trailers[-1] != ('Signed-off-by', SIGN.split(': ', 1)[1]):
        errors.append('Signed-off-by must be the final trailer line')
    if 'Signed-off-by' in values and values['Signed-off-by'][0] != \
            SIGN.split(': ', 1)[1]:
        errors.append('Signed-off-by must be: ' + SIGN)

    if counts.get('Model', 0) > 1:
        errors.append('at most one Model trailer')
    if counts.get('Review-Ref', 0) > 1:
        errors.append('at most one Review-Ref trailer')

    historical = bool(os.environ.get('ALLOW_HISTORICAL_TRAILERS'))
    if not os.environ.get('ALLOW_NO_MODEL') and not historical:
        if counts.get('Model', 0) != 1:
            errors.append('exactly one Model trailer is required '
                          '(set ALLOW_NO_MODEL=1 for a human-only commit)')

    if not historical:
        for value in values.get('Model', []):
            if '<' in value or value.strip().lower() not in MODELS:
                errors.append('Model value not on the allowlist: ' + value)

    for key in ('Assisted-by', 'Co-Authored-By', 'Reviewed-by'):
        for value in values.get(key, []):
            name = identity_name(value)
            if '<' in value:
                if name not in MODELS:
                    # human co-author/reviewer is allowed
                    if not re.match(r'^[^<>]+ <[^<>@ ]+@[^<> ]+>$', value):
                        errors.append('bad %s identity: %s' % (key, value))
            elif name not in MODELS:
                errors.append('%s value not on the allowlist: %s'
                              % (key, value))

    if not os.environ.get('ALLOW_HISTORICAL_TRAILERS'):
        for value in values.get('Co-Authored-By', []):
            if identity_name(value) in MODELS:
                errors.append('Co-Authored-By is for human co-authors; '
                              'use Assisted-by for models')

    if counts.get('Reviewed-by', 0) and not historical:
        if counts.get('Review-Ref', 0) != 1:
            errors.append('Reviewed-by requires exactly one Review-Ref')
        else:
            err = check_review_ref(values['Review-Ref'][0], commit)
            if err:
                errors.append(err)

    if not historical:
        names = {identity_name(v) for v in
                 values.get('Co-Authored-By', [])}
        if {'claude code', 'glm-5.3'} <= names and \
                {'deepseek-v4.1-flash'} <= {identity_name(v) for v in
                                            values.get('Reviewed-by', [])} and \
                counts.get('Model', 0) == 0:
            errors.append('legacy four-line AI template detected; list only '
                          'actual participants (Model/Assisted-by/Reviewed-by)')

    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--file')
    ap.add_argument('--range')
    ap.add_argument('--message-stdin', action='store_true')
    args = ap.parse_args()

    if args.range:
        r = subprocess.run(['git', '-C', REPO, 'rev-list', '--no-merges',
                            args.range], capture_output=True, text=True)
        if r.returncode != 0:
            return fail('bad range %s: %s' % (args.range, r.stderr.strip()))
        bad = 0
        for sha in r.stdout.split():
            msg = subprocess.run(['git', '-C', REPO, 'show', '-s',
                                  '--format=%B', sha],
                                 capture_output=True, text=True).stdout
            errs = validate(msg, commit=sha)
            if errs:
                bad += 1
                print('%s: %s' % (sha[:7], '; '.join(errs)),
                      file=sys.stderr)
        if bad:
            print('commit-msg: %d commit(s) failed' % bad, file=sys.stderr)
            return 1
        print('commit-msg: range ok (%d commits)' %
              len(r.stdout.split()))
        return 0

    if args.file:
        with open(args.file, encoding='utf-8', errors='replace') as f:
            msg = f.read()
    elif args.message_stdin:
        msg = sys.stdin.read()
    else:
        msg = sys.stdin.read()

    # Strip git comment lines in commit-editmsg files.
    msg = '\n'.join(line for line in msg.split('\n')
                    if not line.startswith('#'))
    errs = validate(msg)
    if errs:
        for e in errs:
            print('commit-msg: ' + e, file=sys.stderr)
        print('\nExpected trailer block (real semantics):\n'
              'Model: <primary model>\n'
              'Assisted-by: <model>        # only if actually involved\n'
              'Reviewed-by: <reviewer>     # only after a real review\n'
              'Review-Ref: docs/reviews/<file>.md\n'
              'Signed-off-by: Julian Chu <rootuliux@gmail.com>',
              file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())