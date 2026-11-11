#!/bin/sh
# Rebuilds data/SpellChords.esp from plugin/build.cs. Needs the .NET 10 SDK (dotnet run fetches Mutagen) and the game's Data
# folder (only Skyrim.esm is read). The Papyrus scripts are compiled separately: see "Building from source" in README.md.
# Usage: plugin/build.sh "PATH/TO/Skyrim Special Edition/Data"
set -e
cd "$(dirname "$0")/.."
[ -f "${1:-}/Skyrim.esm" ] || { echo "usage: $0 PATH/TO/Data (the folder holding Skyrim.esm)"; exit 1; }
DATA=$(cd "$1" && pwd)
OUT="$(pwd)/data/SpellChords.esp"
cd plugin && dotnet run build.cs -- "$DATA" "$OUT"
