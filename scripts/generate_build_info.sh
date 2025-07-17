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

# Parse command-line arguments
COMMIT_PREFIX=""
while [[ $# -gt 0 ]]; do
  case $1 in
    --commit)
      if [ -z "$2" ]; then
        # If no commit hash is provided, try to get the current one
        COMMIT_HASH=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
      else
        COMMIT_HASH="$2"
      fi
      COMMIT_PREFIX="Built from commit ${COMMIT_HASH}"
      shift 2
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: $0 [--commit [hash]]"
      exit 1
      ;;
  esac
done

# Generate timestamp
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

# Set message based on whether commit prefix is provided
if [ -n "$COMMIT_PREFIX" ]; then
  MESSAGE="${COMMIT_PREFIX} ${TIMESTAMP}"
else
  MESSAGE="ZMK built ${TIMESTAMP}"
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

echo "Generated build_info.dtsi with timestamp: $TIMESTAMP"
if [ -n "$COMMIT_PREFIX" ]; then
  echo "Commit prefix: $COMMIT_PREFIX"
fi
