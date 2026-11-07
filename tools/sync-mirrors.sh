#!/usr/bin/env bash
# Sync the private Helium mirrors from upstream: every branch and tag.
#
#   tools/sync-mirrors.sh                    from a workstation (pushes over ssh)
#   MIRROR_TOKEN=... tools/sync-mirrors.sh   from CI (pushes over https)
#
# The mirror exists to survive upstream going wrong, so it only ever adds:
#   - a branch or tag deleted upstream stays in the mirror;
#   - a branch fast-forwarded upstream is fast-forwarded in the mirror;
#   - a branch rewritten upstream (force push) is updated, but its old tip is
#     first kept as refs/backup/<date>/<branch>-<sha12> in the mirror (the
#     push is atomic, so the branch never moves without its backup);
#   - a tag moved upstream is NOT moved in the mirror; the new target is kept as
#     refs/backup/<date>/moved-tags/<tag>-<sha12>.
# Nothing is pushed if the upstream fetch fails or returns no branches.
#
# Test hooks: UPSTREAM_BASE (default https://github.com/), MIRROR_PUSH_BASE
# (replaces the push URL prefix), MIRROR_DATE (default today, UTC).
set -euo pipefail

OWNER="${MIRROR_OWNER:-pedrosousa13}"
UPSTREAM_BASE="${UPSTREAM_BASE:-https://github.com/}"
DATE="${MIRROR_DATE:-$(date -u +%Y%m%d)}"
# "<upstream owner/repo> <mirror repo name>"
PAIRS=(
  "imputnet/helium helium-mirror"
  "imputnet/helium-linux helium-linux-mirror"
)

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

push_url() {
  if [[ -n "${MIRROR_PUSH_BASE:-}" ]]; then
    echo "${MIRROR_PUSH_BASE}$1.git"
  elif [[ -n "${MIRROR_TOKEN:-}" ]]; then
    echo "https://github.com/$OWNER/$1.git"
  else
    echo "git@github.com:$OWNER/$1.git"
  fi
}

# Every command that talks to a mirror goes through here: the mirrors are
# private, so reads need the token as much as pushes do. The token goes in through the environment, not the command line, so it
# does not show in `ps`.
git_remote() {
  if [[ -n "${MIRROR_TOKEN:-}" ]]; then
    local auth
    auth="$(printf 'x-access-token:%s' "$MIRROR_TOKEN" | base64 -w0)"
    echo "::add-mask::$auth" 2>/dev/null || true
    GIT_CONFIG_COUNT=1 \
      GIT_CONFIG_KEY_0="http.https://github.com/.extraheader" \
      GIT_CONFIG_VALUE_0="AUTHORIZATION: basic $auth" \
      git "$@"
  else
    git "$@"
  fi
}

sync_one() {
  local upstream="$1" mirror="$2" repo="$work/$2.git" url
  url="$(push_url "$mirror")"
  git init --bare -q "$repo"
  if ! git -C "$repo" fetch -q --no-tags "${UPSTREAM_BASE}$upstream.git" \
        '+refs/heads/*:refs/up/heads/*' '+refs/tags/*:refs/up/tags/*'; then
    echo "ERROR: cannot fetch $upstream; leaving $mirror untouched" >&2
    return 1
  fi
  if [[ -z "$(git -C "$repo" for-each-ref refs/up/heads)" ]]; then
    echo "ERROR: $upstream returned no branches; leaving $mirror untouched" >&2
    return 1
  fi
  # The mirror's current state, for comparison. Any failure to read it stops
  # the sync (a failed read would make every branch look new and skip the
  # backups). Only a mirror with no refs at all is skipped.
  local listing
  if ! listing="$(cd "$repo" && git_remote ls-remote "$url")"; then
    echo "ERROR: cannot read mirror $mirror; pushing nothing" >&2
    return 1
  fi
  if [[ -n "$listing" ]]; then
    if ! (cd "$repo" && git_remote fetch -q --no-tags "$url" \
          '+refs/heads/*:refs/mir/heads/*' '+refs/tags/*:refs/mir/tags/*' \
          '+refs/backup/*:refs/backup/*'); then
      echo "ERROR: cannot fetch mirror $mirror; pushing nothing" >&2
      return 1
    fi
  fi

  local refspecs=() ref name up old backup
  while read -r up ref; do
    name="${ref#refs/up/heads/}"
    old="$(git -C "$repo" rev-parse -q --verify "refs/mir/heads/$name" || true)"
    if [[ -z "$old" || "$old" == "$up" ]]; then
      refspecs+=("$ref:refs/heads/$name")
    elif git -C "$repo" merge-base --is-ancestor "$old" "$up"; then
      refspecs+=("$ref:refs/heads/$name")
    else
      backup="refs/backup/$DATE/$name-${old:0:12}"
      echo "branch $name was rewritten upstream; keeping old tip $old as $backup"
      git -C "$repo" update-ref "$backup" "$old"
      refspecs+=("+$ref:refs/heads/$name" "$backup:$backup")
    fi
  done < <(git -C "$repo" for-each-ref --format='%(objectname) %(refname)' refs/up/heads)

  while read -r up ref; do
    name="${ref#refs/up/tags/}"
    old="$(git -C "$repo" rev-parse -q --verify "refs/mir/tags/$name" || true)"
    if [[ -z "$old" ]]; then
      refspecs+=("$ref:refs/tags/$name")
    elif [[ "$old" != "$up" ]]; then
      backup="refs/backup/$DATE/moved-tags/$name-${up:0:12}"
      echo "tag $name moved upstream; mirror keeps its tag, new target kept as $backup"
      git -C "$repo" update-ref "$backup" "$up"
      refspecs+=("$backup:$backup")
    fi
  done < <(git -C "$repo" for-each-ref --format='%(objectname) %(refname)' refs/up/tags)

  # Every refspec is a fast-forward or a new ref, except the "+" ones that
  # were backed up above. There is no --prune and no blanket force. --atomic:
  # a rewritten branch moves only if its backup ref lands in the same push.
  (cd "$repo" && git_remote push --atomic "$url" "${refspecs[@]}")
}

status=0
for pair in "${PAIRS[@]}"; do
  read -r upstream mirror <<<"$pair"
  echo "== $upstream -> $mirror"
  sync_one "$upstream" "$mirror" || status=1
done
exit "$status"
