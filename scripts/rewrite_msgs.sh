#!/usr/bin/env bash
#
# Cut every commit message down to its subject line (drops bodies, and with
# them the Co-Authored-By trailers agents keep adding). Merges, trees, authors,
# committers and both timestamps survive: filter-branch re-exports the original
# idents and dates, and remaps merge parents itself.
#
#   scripts/rewrite_msgs.sh                  every local branch and tag
#   scripts/rewrite_msgs.sh origin/main..    just what is not pushed yet
#
# A message already on one line rewrites to the same SHA, so re-running only
# churns what slipped in since. Undo is refs/original/*, left behind by
# filter-branch. Verifying and pushing are yours.

set -euo pipefail

[ $# -gt 0 ] || set -- --branches --tags

FILTER_BRANCH_SQUELCH_WARNING=1 exec git filter-branch -f \
	--msg-filter 'sed -e "1q"' \
	--tag-name-filter cat \
	-- "$@"
