#!/bin/bash

# Function to convert character to ZMK keycode
char_to_keycode() {
   case $1 in
       '0') echo "N0" ;;
       '1') echo "N1" ;;
       '2') echo "N2" ;;
       '3') echo "N3" ;;
       '4') echo "N4" ;;
       '5') echo "N5" ;;
       '6') echo "N6" ;;
       '7') echo "N7" ;;
       '8') echo "N8" ;;
       '9') echo "N9" ;;
       'a'|'A') echo "A" ;;
       'b'|'B') echo "B" ;;
       'c'|'C') echo "C" ;;
       'd'|'D') echo "D" ;;
       'e'|'E') echo "E" ;;
       'f'|'F') echo "F" ;;
       'g'|'G') echo "G" ;;
       'h'|'H') echo "H" ;;
       'i'|'I') echo "I" ;;
       'j'|'J') echo "J" ;;
       'k'|'K') echo "K" ;;
       'l'|'L') echo "L" ;;
       'm'|'M') echo "M" ;;
       'n'|'N') echo "N" ;;
       'o'|'O') echo "O" ;;
       'p'|'P') echo "P" ;;
       'q'|'Q') echo "Q" ;;
       'r'|'R') echo "R" ;;
       's'|'S') echo "S" ;;
       't'|'T') echo "T" ;;
       'u'|'U') echo "U" ;;
       'v'|'V') echo "V" ;;
       'w'|'W') echo "W" ;;
       'x'|'X') echo "X" ;;
       'y'|'Y') echo "Y" ;;
       'z'|'Z') echo "Z" ;;
       '-') echo "MINUS" ;;
       '_') echo "UNDER" ;;
       ':') echo "COLON" ;;
       ' ') echo "SPACE" ;;
       '.') echo "DOT" ;;
       '/') echo "SLASH" ;;
       '<') echo "LT" ;;
       '>') echo "GT" ;;
       *) echo "SPACE" ;; # fallback
   esac
}

# Parse command-line arguments.
#
# The commit is included BY DEFAULT. It used to be opt-in, and nothing passed
# the flag, so every build said only when it was made -- which is the one thing
# that cannot answer "what is on this keyboard". A binary's mtime says when it
# was compiled, not whether it was ever flashed, and on 2026-08-31 that misled
# two sessions at once: one reflashed 08-28 artifacts for an afternoon, the
# other read a NEWER file that had never been written to the hardware and drew
# the opposite conclusion. The commit is the thing worth carrying.
COMMIT_HASH=""
while [[ $# -gt 0 ]]; do
  case $1 in
    --commit)
      # An explicit hash is still honoured; otherwise take the current one.
      if [ -n "$2" ] && [[ "$2" != --* ]]; then
        COMMIT_HASH="$2"
        shift 2
      else
        shift
      fi
      ;;
    --no-commit)
      COMMIT_HASH="none"
      shift
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: $0 [--commit [hash]] [--no-commit]"
      exit 1
      ;;
  esac
done

if [ -z "$COMMIT_HASH" ]; then
  COMMIT_HASH=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")

  # A SHA on its own is a lie when the tree has uncommitted changes: it names a
  # commit whose contents are NOT what got built. Say so. Note this asks about
  # the tracked tree only -- config/build_info.dtsi is itself generated and
  # gitignored, so regenerating it cannot make the tree look dirty.
  if ! git diff --quiet HEAD 2>/dev/null; then
    COMMIT_HASH="${COMMIT_HASH}+"
  fi
fi

# Generate timestamp
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

# The keycode table has no plus sign, so a dirty marker would silently become a
# space. Spell it instead, and keep the whole thing typeable.
MESSAGE_HASH="${COMMIT_HASH/+/ dirty}"

if [ "$COMMIT_HASH" = "none" ]; then
  MESSAGE="ZMK built ${TIMESTAMP}"
else
  MESSAGE="ZMK ${MESSAGE_HASH} built ${TIMESTAMP}"
fi

# Convert to keycode sequence
KEYCODES=""
for (( i=0; i<${#MESSAGE}; i++ )); do
   char="${MESSAGE:$i:1}"
   keycode=$(char_to_keycode "$char")
   KEYCODES="$KEYCODES &kp $keycode"
done

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Generate build_info.dtsi in the correct location
OUTPUT_FILE="${SCRIPT_DIR}/../config/build_info.dtsi"

cat > "${OUTPUT_FILE}" << EOF
/ {
   macros {
       build_time: build_time {
           compatible = "zmk,behavior-macro";
           #binding-cells = <0>;
           bindings = <&macro_tap$KEYCODES>;
       };
   };
};
EOF

echo "Generated build_info.dtsi: $MESSAGE"
