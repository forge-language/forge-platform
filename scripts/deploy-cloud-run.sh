#!/usr/bin/env bash
set -euo pipefail
project=forge-lang-514802
region=asia-northeast3
service=forge-platform
registry="$region-docker.pkg.dev/$project/$service"
run=(--project "$project" --region "$region" --quiet)
commit=$(GIT_MASTER=1 git rev-parse HEAD)
names=(site api repository-checker)
inputs() {
  case "$1" in
    site) printf '%s\n' frontend playground content vendor/toolchain scripts/build-playground.sh scripts/sync-content.py scripts/install.sh ;;
    api) printf '%s\n' backend cli vendor/toolchain vendor/forge-postgres vendor/forge-web vendor/forge-storage scripts/build.sh deployment/cloud-run/forge-postgres-neon.patch ;;
    repository-checker) printf '%s\n' repository-checker ;;
    *) return 1 ;;
  esac
  echo .dockerignore
}
source_tag() {
  local paths key
  mapfile -t paths < <(inputs "$1")
  key=$(GIT_MASTER=1 git ls-tree -r HEAD -- "${paths[@]}" | sha256sum | cut -d' ' -f1)
  printf '%s/%s:src-%s\n' "$registry" "$1" "$key"
}
if [[ "${1:-}" == --source-tags ]]; then
  for name in "${names[@]}"; do source_tag "$name"; done
  exit 0
fi
current_main() {
  local ref
  ref=$(GIT_MASTER=1 git ls-remote --exit-code origin refs/heads/main) || exit 1
  [[ "${ref%%[[:space:]]*}" == "$commit" ]]
}
current_main || exit 0
state=$(gcloud run services describe "$service" "${run[@]}" --format=json)
previous=$(jq -er '[.status.traffic[] | select(.percent == 100)] | if length == 1 then .[0].revisionName else error("unexpected traffic split") end' <<<"$state")
jq -e --arg previous "$previous" '
  .status.latestReadyRevisionName == .status.latestCreatedRevisionName
  and ([.spec.template.spec.containers[].name] | sort) == ["api","repository-checker","site"]
  and any(.spec.template.spec.containers[] | select(.name=="api") | .env[]; .name=="DATABASE_URL" and .valueFrom.secretKeyRef.name=="forge-neon-database-url")
' <<<"$state" >/dev/null
latest=$(jq -er '.status.latestReadyRevisionName' <<<"$state")
serving=$(jq '.spec.template.spec.containers' <<<"$state")
if [[ "$latest" != "$previous" ]]; then
  # A rolled-back candidate remains the latest revision even after traffic moves back.
  serving_revision=$(gcloud run revisions describe "$previous" "${run[@]}" --format=json)
  latest_revision=$(gcloud run revisions describe "$latest" "${run[@]}" --format=json)
  comparable() { jq -S '.spec | .containers |= map(del(.image))' <<<"$1"; }
  [[ "$(comparable "$serving_revision")" == "$(comparable "$latest_revision")" ]]
  serving=$(jq '.spec.containers' <<<"$serving_revision")
fi
gcloud auth configure-docker "$region-docker.pkg.dev" --quiet
updates=()
for name in "${names[@]}"; do
  tag=$(source_tag "$name")
  if ! digest=$(gcloud artifacts docker images describe "$tag" --project "$project" --format='value(image_summary.digest)' 2>/dev/null); then
    case "$name" in site) file=frontend/Containerfile ;; api) file=backend/Containerfile ;; repository-checker) file=repository-checker/Containerfile ;; esac
    options=()
    if [[ "$name" == site ]]; then options+=(--build-arg ASSET_BASE=https://storage.googleapis.com/forge-lang-514802-frontend/); fi
    docker buildx build "${options[@]}" --platform=linux/amd64 --provenance=false --push -t "$tag" -f "$file" .
    digest=$(gcloud artifacts docker images describe "$tag" --project "$project" --format='value(image_summary.digest)')
  fi
  [[ "$digest" =~ ^sha256:[0-9a-f]{64}$ ]]
  image="$registry/$name@$digest"
  if [[ "$name" == site ]]; then site_image=$image; fi
  old=$(jq -er --arg name "$name" '.[] | select(.name==$name) | .image' <<<"$serving")
  if [[ "$image" != "$old" ]]; then updates+=(--container "$name" --image "$image"); fi
done
if (( ${#updates[@]} == 0 )); then
  echo "Verified source tags match all three serving image digests."
  python3 scripts/publish-site.py "$site_image" "$commit"
  exit 0
fi
current_main || exit 0
tag="ci-${GITHUB_RUN_ID:?}-${GITHUB_RUN_ATTEMPT:?}"
remove_tag() { gcloud run services update-traffic "$service" "${run[@]}" --remove-tags="$tag"; }
trap remove_tag EXIT
gcloud run services update "$service" "${run[@]}" --no-traffic --tag "$tag" "${updates[@]}"
state=$(gcloud run services describe "$service" "${run[@]}" --format=json)
candidate=$(jq -er --arg tag "$tag" '.status.traffic[] | select(.tag==$tag) | .revisionName' <<<"$state")
url=$(jq -er --arg tag "$tag" '.status.traffic[] | select(.tag==$tag) | .url' <<<"$state")
check() {
  local url=$1 path
  for path in / /api/health /api/packages /releases/latest.json; do
    curl --fail --silent --show-error --max-time 30 --retry 3 --retry-all-errors "$url$path" -o /dev/null || return 1
  done
}
check "$url"
current_main || exit 0
gcloud run services update-traffic "$service" "${run[@]}" --to-revisions "$candidate=100"
if ! check "$(jq -er '.status.url' <<<"$state")"; then
  gcloud run services update-traffic "$service" "${run[@]}" --to-revisions "$previous=100"
  exit 1
fi
if ! python3 scripts/publish-site.py "$site_image" "$commit"; then
  gcloud run services update-traffic "$service" "${run[@]}" --to-revisions "$previous=100"
  exit 1
fi
echo "Serving $candidate"
