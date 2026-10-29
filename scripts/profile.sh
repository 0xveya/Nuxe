#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd -- "$root"

if (($#)); then
    if ! command -v perf >/dev/null; then
        printf 'perf is required to record a profile.\n' >&2
        exit 1
    fi
    xmake f -m releasedbg -y
    xmake build -y

    mkdir -p -- .cache/profiles
    directory=$(mktemp -d "$root/.cache/profiles/run.XXXXXX")
    ln -sfnT -- "$(basename -- "$directory")" .cache/profiles/latest
    binary="$root/build/linux/x86_64/releasedbg/nuxe"
    printf '%q ' "$binary" "$@" > "$directory/command.txt"
    printf '\n' >> "$directory/command.txt"
    printf 'Profile artifacts: %s\n' "$directory" >&2
    perf record -e cycles:u -F 199 --call-graph fp --user-callchains \
        -o "$directory/perf.data" -- "$binary" "$@"
else
    directory="$root/.cache/profiles/latest"
    if [[ ! -s "$directory/perf.data" ]]; then
        printf 'No recording yet. Run: mise run profile -- <nuxe command and arguments>\n' >&2
        exit 2
    fi
fi

exec samply import --profile-name nuxe --output "$directory/profile.json.gz" "$directory/perf.data"
