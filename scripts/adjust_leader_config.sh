#!/bin/bash

# Script to automatically adjust CONFIG_ZMK_LEADER_MAX_SEQUENCES based on actual usage
# This ensures the config always has enough capacity for defined sequences + safety margin

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_DIR="${SCRIPT_DIR}/../config"
LEADER_FILE="${CONFIG_DIR}/leader.dtsi"

# Function to count actual leader sequences in leader.dtsi
count_leader_sequences() {
    if [[ ! -f "$LEADER_FILE" ]]; then
        echo "0"
        return
    fi

    # Count all ZMK_LEADER_SEQUENCE calls that are not commented out
    # This includes both direct calls and macro-generated calls
    local count=0

    # Count ZMK_LEADER_SEQUENCE calls (direct)
    count=$(grep -v "^#\|^/\*\|^\*\|^$\|^\s*//\|^\s*/\*" "$LEADER_FILE" | \
            grep -c "ZMK_LEADER_SEQUENCE(" || echo "0")

    # Count ZMK_LEADER_UNICODE_* macros that generate sequences
    local unicode_count=0
    unicode_count=$(grep -v "^#\|^/\*\|^\*\|^$\|^\s*//\|^\s*/\*" "$LEADER_FILE" | \
                   grep -c "ZMK_LEADER_UNICODE_" || echo "0")

    # Each unicode macro generates one sequence
    count=$((count + unicode_count))

    echo "$count"
}

# Function to count the bindings in the longest macro.
#
# Macros default to MACRO_MODE_TAP, which enqueues TWO behaviour-queue slots per
# binding (a press and a release). A macro longer than half the queue therefore
# loses its tail silently -- no error, it just stops partway. The long address
# macros hit this: home_address_web needs 82 slots against a default queue of 64
# and never reached the phone number.
count_longest_macro() {
    python3 - "$CONFIG_DIR" <<'PYEOF'
import glob, os, re, sys

config_dir = sys.argv[1]
longest = 0

for path in glob.glob(os.path.join(config_dir, "*.dtsi")) + \
            glob.glob(os.path.join(config_dir, "*.keymap")):
    try:
        text = open(path, errors="ignore").read()
    except OSError:
        continue

    # Join line continuations so multi-line macro bodies count as one.
    text = text.replace("\\\n", " ")

    for line in text.splitlines():
        if "ZMK_SIMPLE_MACRO" not in line and "macro_tap" not in line:
            continue
        # Commented-out definitions do not generate anything.
        if line.lstrip().startswith("//"):
            continue
        longest = max(longest, len(re.findall(r"&kp\b", line)))

print(longest)
PYEOF
}

# Function to update an arbitrary CONFIG_ symbol, raising it only.
update_conf_symbol() {
    local config_file="$1" symbol="$2" value="$3"

    if grep -q "^${symbol}=" "$config_file"; then
        local current
        current=$(grep "^${symbol}=" "$config_file" | cut -d= -f2)
        if [[ $value -gt $current ]]; then
            sed -i "s/^${symbol}=.*/${symbol}=${value}/" "$config_file"
            echo "Updated ${symbol} to ${value} in $(basename "$config_file")"
        fi
    else
        echo "${symbol}=${value}" >> "$config_file"
        echo "Added ${symbol}=${value} to $(basename "$config_file")"
    fi
}

# Function to update config file with new sequence limit
update_config_file() {
    local config_file="$1"
    local new_limit="$2"

    if [[ ! -f "$config_file" ]]; then
        echo "Config file not found: $config_file"
        return 1
    fi

    # Check if CONFIG_ZMK_LEADER_MAX_SEQUENCES exists
    if grep -q "CONFIG_ZMK_LEADER_MAX_SEQUENCES" "$config_file"; then
        # Update existing line
        sed -i "s/CONFIG_ZMK_LEADER_MAX_SEQUENCES=.*/CONFIG_ZMK_LEADER_MAX_SEQUENCES=${new_limit}/" "$config_file"
        echo "Updated CONFIG_ZMK_LEADER_MAX_SEQUENCES to $new_limit in $config_file"
    else
        # Add new line after other ZMK configs or at the end
        if grep -q "CONFIG_ZMK_" "$config_file"; then
            # Insert after last ZMK config line
            sed -i "/CONFIG_ZMK_.*=/a CONFIG_ZMK_LEADER_MAX_SEQUENCES=${new_limit}" "$config_file"
        else
            # Add at the end
            echo "CONFIG_ZMK_LEADER_MAX_SEQUENCES=${new_limit}" >> "$config_file"
        fi
        echo "Added CONFIG_ZMK_LEADER_MAX_SEQUENCES=$new_limit to $config_file"
    fi
}

# Function to get current config value
get_current_config() {
    local config_file="$1"
    if [[ -f "$config_file" ]] && grep -q "CONFIG_ZMK_LEADER_MAX_SEQUENCES" "$config_file"; then
        grep "CONFIG_ZMK_LEADER_MAX_SEQUENCES" "$config_file" | cut -d'=' -f2
    else
        echo "32" # Default from Kconfig
    fi
}

# Main logic
main() {
    local target_config="${1:-}"
    local safety_margin=16  # Extra sequences for future expansion

    # Count current sequences
    local sequence_count
    sequence_count=$(count_leader_sequences)

    # Calculate required limit with safety margin
    local required_limit=$((sequence_count + safety_margin))

    echo "Found $sequence_count leader sequences in $LEADER_FILE"
    echo "Calculated required limit: $required_limit (with $safety_margin safety margin)"

    # Behaviour-queue guard: two slots per macro binding, plus room for whatever
    # else is queued alongside a running macro.
    local longest_macro required_queue
    longest_macro=$(count_longest_macro)
    required_queue=$(( longest_macro * 2 + 32 ))
    if [[ $required_queue -lt 64 ]]; then
        required_queue=64  # never drop below ZMK's own default
    fi
    echo "Longest macro: $longest_macro bindings -> needs $(( longest_macro * 2 )) queue slots; setting $required_queue"

    # If no specific config file specified, update all .conf files
    if [[ -z "$target_config" ]]; then
        local updated_files=0
        for config_file in "$CONFIG_DIR"/*.conf; do
            if [[ -f "$config_file" ]]; then
                update_conf_symbol "$config_file" CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE "$required_queue"

                local current_limit
                current_limit=$(get_current_config "$config_file")

                if [[ $required_limit -gt $current_limit ]]; then
                    update_config_file "$config_file" "$required_limit"
                    updated_files=$((updated_files + 1))
                else
                    echo "$(basename "$config_file"): current limit $current_limit is sufficient"
                fi
            fi
        done

        if [[ $updated_files -eq 0 ]]; then
            echo "All config files have sufficient limits"
        else
            echo "Updated $updated_files config files"
        fi
    else
        # Update specific config file
        local config_file="${CONFIG_DIR}/${target_config}"
        if [[ ! "$config_file" == *.conf ]]; then
            config_file="${config_file}.conf"
        fi

        if [[ ! -f "$config_file" ]]; then
            echo "Error: Config file not found: $config_file"
            exit 1
        fi

        local current_limit
        current_limit=$(get_current_config "$config_file")

        if [[ $required_limit -gt $current_limit ]]; then
            update_config_file "$config_file" "$required_limit"
        else
            echo "Current limit $current_limit is sufficient for $sequence_count sequences"
        fi
    fi
}

# Show usage if requested
if [[ "${1:-}" == "--help" ]] || [[ "${1:-}" == "-h" ]]; then
    echo "Usage: $0 [config_name]"
    echo ""
    echo "Automatically adjusts CONFIG_ZMK_LEADER_MAX_SEQUENCES based on sequences defined in leader.dtsi"
    echo ""
    echo "Arguments:"
    echo "  config_name    Optional. Name of specific .conf file to update (without .conf extension)"
    echo "                 If not provided, updates all .conf files in config/ directory"
    echo ""
    echo "Examples:"
    echo "  $0                    # Update all config files"
    echo "  $0 eyelash_corne      # Update only eyelash_corne.conf"
    echo ""
    echo "The script adds a safety margin of sequences for future expansion."
    exit 0
fi

main "$@"