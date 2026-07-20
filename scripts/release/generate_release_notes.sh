#!/usr/bin/env bash

set -euo pipefail

output_path="${1:-release-notes.md}"
repository="${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is required}"
current_tag="${GITHUB_REF_NAME:?GITHUB_REF_NAME is required}"
target_commit="${GITHUB_SHA:-$current_tag}"

if ! git rev-parse --verify --quiet "refs/tags/${current_tag}^{commit}" >/dev/null; then
  echo "::error::Tag ${current_tag} is not available in the checkout"
  exit 1
fi

previous_tag="$(
  git describe --tags --match 'v*' --abbrev=0 "${current_tag}^" 2>/dev/null || true
)"

revision_range="$current_tag"
api_arguments=(
  --method POST
  "repos/${repository}/releases/generate-notes"
  -f "tag_name=${current_tag}"
  -f "target_commitish=${target_commit}"
)

if [[ -n "$previous_tag" ]]; then
  revision_range="${previous_tag}..${current_tag}"
  api_arguments+=(-f "previous_tag_name=${previous_tag}")
  echo "Generating release notes for ${revision_range}"
else
  echo "No previous version tag found; including all commits through ${current_tag}"
fi

generated_body="$(gh api "${api_arguments[@]}" --jq '.body')"
commit_list="$(
  git log \
    --format="- [\`%h\`](https://github.com/${repository}/commit/%H) %s" \
    "$revision_range"
)"

if [[ -z "$commit_list" ]]; then
  echo "::error::No commits found in ${revision_range}"
  exit 1
fi

mkdir -p "$(dirname "$output_path")"
{
  echo "## What's Changed"
  echo
  echo "### Commits"
  echo
  printf '%s\n' "$commit_list"

  if [[ -n "$generated_body" ]]; then
    echo
    renamed_heading=false
    while IFS= read -r line || [[ -n "$line" ]]; do
      if [[ "$renamed_heading" == false && "$line" == "## What's Changed" ]]; then
        echo "### Pull Requests"
        renamed_heading=true
      else
        printf '%s\n' "$line"
      fi
    done <<< "$generated_body"
  fi
} > "$output_path"

echo "Release notes written to ${output_path}"
