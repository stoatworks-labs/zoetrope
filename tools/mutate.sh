#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders would pass every GLSL mutant here.
#
# Each mutant is a copy of the tree in a temporary directory (the FFGL SDK is
# symlinked, not copied), built arm64-only, with only the named check run at
# CI's raster, under a time limit (a mutant can hang: honeydew's lesson; a
# timeout counts as caught).
#
#     tools/mutate.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap).
MUTANTS=(
	"source/Shaders.cpp|sum += cardAt( r, a, angleAt( mix( seg.x, seg.y, ( float( j ) + offset ) / float( n ) ) ) );|sum += cardAt( r, a, angleAt( mix( seg.x, seg.y, ( float( j ) - offset ) / float( n ) ) ) );|--hand|GLSL: the taps half a tap early, + -> -"
	"source/Shaders.cpp|return mix( Angles[ i ], Angles[ i + 1 ], x - float( i ) );|return mix( Angles[ i ], Angles[ i + 1 ], x + float( i ) );|--hand|GLSL: the angle table read off its piece, - -> +"
	"source/Shaders.cpp|float x     = ( a - turned ) / Pitch - PhaseFrac;|float x     = ( a + turned ) / Pitch - PhaseFrac;|--smear|GLSL: the disc turning backwards within the exposure, - -> +"
	"source/Shaders.cpp|int card     = ( int( kf ) - StationOffset + 64 * Stations ) % Stations;|int card     = ( int( kf ) + StationOffset + 64 * Stations ) % Stations;|--delay|GLSL: the cards dealt to the wrong stations, - -> +"
	"source/Shaders.cpp|fragColor = vec4( sum / float( Taps * Taps ), 1.0 );|fragColor = vec4( sum / float( Taps + Taps ), 1.0 );|--energy|GLSL: a print's average over the wrong count, * -> +"
	"source/Shaders.cpp|float tFar  = ( Eye + root ) / A;|float tFar  = ( Eye - root ) / A;|--drum|GLSL: the drum's far wall where the near wall is, + -> -"
	"source/Disc.cpp|const double decay  = -std::expm1( -k * s ) / k;|const double decay  = -std::expm1( +k * s ) / k;|--bearing|C++: the viscous decay growing, - -> +"
	"source/Disc.cpp|const int card    = static_cast< int >( ( ( -m ) % n + n ) % n );|const int card    = static_cast< int >( ( ( +m ) % n + n ) % n );|--delay|C++: the head printing the wrong card, - -> +"
	"source/Strobe.cpp|double weight = strobe * ( b - a ) / f.length;|double weight = strobe * ( b - b ) / f.length;|--energy|C++: a flash's overlap measured as nothing, a -> b"
	"source/Strobe.cpp|( boundary - p0 ) / ( p1 - p0 );|( boundary + p0 ) / ( p1 - p0 );|--beat|C++: the beat's place in its frame from the sum of phases, - -> +"
	"source/Strobe.cpp|return std::max( now - length, now - std::max( dt, 0.0 ) );|return std::max( now + length, now - std::max( dt, 0.0 ) );|--audio|C++: an onset's flash after its frame, - -> +"
	"source/Zoetrope.cpp|( 2.0 * kPi / counted ) / period;|( 3.0 * kPi / counted ) / period;|--lock|C++: Sync's speed a station and a half per flash, 2 -> 3"
	"source/Zoetrope.cpp|const double shutter = ShutterFromParam( params[ PT_SHUTTER ] ) * period;|const double shutter = ShutterFromParam( params[ PT_SHUTTER ] ) / period;|--alias|C++: the exposure the shutter over the frame's length, * -> /"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

caught=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	tree="$WORK/tree"
	rm -rf "$tree"
	mkdir -p "$tree/external"
	cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
	ln -s "$REPO/external/ffgl" "$tree/external/ffgl"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
	cmake --build "$tree/build" --target zttest -j"$(sysctl -n hw.ncpu)" >/dev/null 2>&1
	status=0
	extra=()
	[[ "$check" == "--bearing" || "$check" == "--beat" ]] || extra=( --size 320x180 )
	limit 300 "$tree/build/zttest" "$check" "${extra[@]}" >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant (exit %d):\n' "$check" "$status"
		# A mutant that timed out printed no FAIL: grep finding none must not end the run.
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "${#MUTANTS[@]}" "$caught"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]
