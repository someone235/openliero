#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd -- "$project_dir"

if [[ -f .env ]]; then
    set -a
    source "$project_dir/.env"
    set +a
fi

cd -- "$project_dir/server"
if command -v go >/dev/null 2>&1; then
    go build -o openliero-relay .
elif [[ ! -x ./openliero-relay ]]; then
    echo "Install Go to build the server, or provide server/openliero-relay." >&2
    exit 1
fi

exec ./openliero-relay --port "${OPENLIERO_SIGNALING_PORT:-19533}" "$@"
