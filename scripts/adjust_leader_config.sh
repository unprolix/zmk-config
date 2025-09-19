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

    # If no specific config file specified, update all .conf files
    if [[ -z "$target_config" ]]; then
        local updated_files=0
        for config_file in "$CONFIG_DIR"/*.conf; do
            if [[ -f "$config_file" ]]; then
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