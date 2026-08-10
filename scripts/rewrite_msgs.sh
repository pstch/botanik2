#!/usr/bin/env bash
#
# Cut every commit message down to its subject line, dropping bodies and their
# trailers. Merges, trees, idents and both timestamps survive.
#
#   scripts/rewrite_msgs.sh                  every local branch and tag
#   scripts/rewrite_msgs.sh origin/main..    just what is not pushed yet
#
# A one-line message rewrites to the same SHA, so re-running churns only what
# slipped in since. Undo is refs/original/*. Verifying and pushing are yours.

set -euo pipefail

[ $# -gt 0 ] || set -- --branches --tags

FILTER_BRANCH_SQUELCH_WARNING=1 exec git filter-branch -f \
	--msg-filter 'sed -e "1q"' \
	--tag-name-filter cat \
	-- "$@"
