#!/bin/sh
#
# check_clean_tree.sh — refuse to produce a user-facing build from a tree that
# does not correspond to a pushed commit.
#
# Usage:  scripts/check_clean_tree.sh "<what is being built>"
#
# Why this exists: user-facing builds are identified by their commit (nightly
# tarballs carry "-<short hash>"; milestone tarballs carry a version that is
# expected to sit on a pushed tag). That identification is only worth anything
# if the commit actually describes what shipped. An uncommitted edit, or a
# commit that exists only on this machine, breaks the mapping from a user's bug
# report back to source -- silently, and only once someone else is affected.
# Releases are hand-rolled here, so there is no CI to catch it.
#
# Exit status: 0 = tree matches a pushed commit; 1 = it does not (message on
# stderr). Set BANDICOOT_ALLOW_DIRTY=1 to downgrade every check to a warning,
# for the cases where the mapping genuinely does not matter (a one-off tarball
# for yourself).
#
set -eu

WHAT="${1:-this build}"
PAD="                  "

cd "$(dirname "$0")/.."

# complain <problem> <remedy> [pre-indented detail block]
complain() {
    if [ "${BANDICOOT_ALLOW_DIRTY:-0}" = "1" ]; then
        echo "check_clean_tree: WARNING — $1" >&2
        echo "${PAD}Continuing anyway (BANDICOOT_ALLOW_DIRTY=1)." >&2
        return 0
    fi
    echo "check_clean_tree: $WHAT requires a clean, pushed tree." >&2
    echo "${PAD}$1" >&2
    if [ -n "${3:-}" ]; then
        printf '%s\n' "$3" >&2
    fi
    echo "${PAD}$2" >&2
    echo "${PAD}Override with BANDICOOT_ALLOW_DIRTY=1." >&2
    exit 1
}

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    complain "This is not a git working tree, so the commit cannot be recorded." \
             "Build from a checkout of the repository."
fi

# 1. Uncommitted changes, tracked or not. Generated build products
#    (src/bandicoot-build-id.h, .build-counter, *.o, ...) and .DS_Store are
#    already in .gitignore, so anything showing up here is real.
DIRTY="$(git status --porcelain)"
if [ -n "$DIRTY" ]; then
    COUNT="$(printf '%s\n' "$DIRTY" | wc -l | tr -d ' ')"
    LISTING="$(printf '%s\n' "$DIRTY" | head -20 | sed -e "s/^/${PAD}  /")"
    if [ "$COUNT" -gt 20 ]; then
        LISTING="${LISTING}
${PAD}  ... and $((COUNT - 20)) more"
    fi
    complain "The working tree has ${COUNT} uncommitted change(s)." \
             "Commit them, then push." \
             "$LISTING"
fi

# 2. A branch with no upstream has never been pushed anywhere.
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
if ! UPSTREAM="$(git rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null)"; then
    complain "Branch '${BRANCH}' has no upstream, so HEAD exists only on this machine." \
             "Push it: git push -u origin ${BRANCH}"
    UPSTREAM=""
fi

# 3. Commits ahead of the upstream are likewise local-only. Being *behind* is
#    fine -- HEAD is still a commit anyone can fetch, which is all we need.
if [ -n "${UPSTREAM:-}" ]; then
    AHEAD="$(git rev-list --count "${UPSTREAM}..HEAD")"
    if [ "$AHEAD" -gt 0 ]; then
        complain "${AHEAD} commit(s) on '${BRANCH}' are not in '${UPSTREAM}' yet." \
                 "Push them: git push"
    fi
fi

exit 0
