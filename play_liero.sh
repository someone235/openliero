#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd -- "$project_dir"

if [[ -f .env ]]; then
    set -a
    source "$project_dir/.env"
    set +a
fi

if [[ ! -x "$project_dir/LIERO/openliero" ]]; then
    echo "Game launcher missing: $project_dir/LIERO/openliero" >&2
    exit 1
fi

exec "$project_dir/LIERO/openliero" "$@"
