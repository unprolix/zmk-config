default:
    @just --list --unsorted

config := absolute_path('config')
build := absolute_path('.build')
out := absolute_path('firmware')
draw := absolute_path('draw')

_generate_build_info:
    scripts/generate_build_info.sh

# parse build.yaml and filter targets by expression
_parse_targets $expr:
    #!/usr/bin/env bash
    attrs="[.board, .shield, .snippet, .\"artifact-name\", .\"cmake-args\"]"
    filter="(($attrs | map(. // [.]) | combinations), ((.include // {})[] | $attrs)) | join(\",\")"
    echo "$(yq -r "$filter" build.yaml | grep -v "^," | grep -i "${expr/#all/.*}")"

# build firmware for single board & shield combination
_build_single $board $shield $snippet $artifact $cmake_args *west_args:
    #!/usr/bin/env bash
    set -euo pipefail
    artifact="${artifact:-${shield:+${shield// /+}-}${board}}"
    build_dir="{{ build / '$artifact' }}"

    echo "Building firmware for $artifact..."
    
    # Parse cmake-args and substitute GITHUB_WORKSPACE with current directory
    cmake_flags=""
    if [[ -n "${cmake_args}" && "${cmake_args}" != "null" ]]; then
        cmake_flags="${cmake_args//\$\{GITHUB_WORKSPACE\}/{{ invocation_directory() }}}"
        cmake_flags="${cmake_flags//-DZMK_EXTRA_CONF_FILE=/-DEXTRA_CONF_FILE=}"
        # Remove quotes since they'll be added by shell expansion
        cmake_flags="${cmake_flags//\"/}"
    fi
    
    west build -s zmk/app -d "$build_dir" -b $board {{ west_args }} ${snippet:+-S "$snippet"} -- \
        -DZMK_CONFIG="{{ config }}" ${shield:+-DSHIELD="$shield"} ${cmake_flags}

    if [[ -f "$build_dir/zephyr/zmk.uf2" ]]; then
        mkdir -p "{{ out }}" && cp "$build_dir/zephyr/zmk.uf2" "{{ out }}/$artifact.uf2"
    else
        mkdir -p "{{ out }}" && cp "$build_dir/zephyr/zmk.bin" "{{ out }}/$artifact.bin"
    fi

# check if west update is needed and run it if necessary
_check_west_update:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "Checking if west update is needed..."
    if [ ! -d "zmk-new_corne" ] || [ -n "$(west list -f '{name} {revision} {path}' | grep 'needs update')" ]; then
        echo "Running west update..."
        west update --fetch-opt=--filter=blob:none
    fi

# check for new upstream commits since last sync
_check_upstream:
    #!/usr/bin/env bash
    set -euo pipefail
    
    # Find the latest upstream-sync tag
    latest_tag=$(git tag -l "upstream-sync-*" | sort -V | tail -1 || echo "")
    
    if [[ -z "$latest_tag" ]]; then
        echo "⚠️  No upstream-sync tags found. Run 'just upstream-status' to see all upstream commits."
        return
    fi
    
    echo "🔍 Checking for upstream updates since $latest_tag..."
    git fetch upstream --quiet 2>/dev/null || echo "📡 Fetching upstream..."
    
    # Get commits we've integrated (from tag message and git log)
    integrated_shas=""
    if git tag -l "$latest_tag" >/dev/null 2>&1; then
        # Extract SHAs from tag message (format: local_sha->upstream_sha)
        integrated_shas=$(git tag -n99 "$latest_tag" | grep -oE '[a-f0-9]{7,}->[a-f0-9]{7,}' | cut -d'>' -f2 | tr '\n' ' ' || echo "")
    fi
    
    # Get upstream commits since divergence point
    merge_base=$(git merge-base main upstream/main 2>/dev/null || echo "")
    if [[ -n "$merge_base" ]]; then
        all_upstream=$(git log --pretty=format:"%ad %h %s" --date=short upstream/main ^$merge_base 2>/dev/null || echo "")
    else
        all_upstream=$(git log --pretty=format:"%ad %h %s" --date=short upstream/main -20 2>/dev/null || echo "")
    fi
    
    # Filter out integrated commits
    new_commits=""
    if [[ -n "$all_upstream" ]]; then
        while read -r line; do
            commit_sha=$(echo "$line" | cut -d' ' -f2)
            if [[ ! " $integrated_shas " =~ " $commit_sha " ]]; then
                new_commits="$new_commits$line"$'\n'
            fi
        done <<< "$all_upstream"
        new_commits=$(echo "$new_commits" | head -n -1) # remove trailing newline
    fi
    
    if [[ -z "$new_commits" ]]; then
        echo "✅ Up to date with upstream"
    else
        echo "🆕 New upstream commits available:"
        echo "$new_commits" | head -10
        if [[ $(echo "$new_commits" | wc -l) -gt 10 ]]; then
            echo "   ... and $(($(echo "$new_commits" | wc -l) - 10)) more"
        fi
        echo ""
        echo "💡 Run 'just upstream-status' for details or 'git cherry-pick <commit>' to integrate"
    fi

# build firmware for matching targets
build expr *west_args: _check_upstream _check_west_update _generate_build_info
    #!/usr/bin/env bash
    set -euo pipefail
    targets=$(just _parse_targets {{ expr }})

    [[ -z $targets ]] && echo "No matching targets found. Aborting..." >&2 && exit 1
    echo "$targets" | while IFS=, read -r board shield snippet artifact cmake_args; do
        just _build_single "$board" "$shield" "$snippet" "$artifact" "$cmake_args" {{ west_args }}
    done

# clear build cache and artifacts
clean:
    rm -rf {{ build }} {{ out }}

# clear all automatically generated files
clean-all: clean
    rm -rf .west zmk

# clear nix cache
clean-nix:
    nix-collect-garbage --delete-old

# parse & plot keymap
draw:
    #!/usr/bin/env bash
    set -euo pipefail
    keymap -c "{{ draw }}/config.yaml" parse -z "{{ config }}/eyelash_corne.keymap" --virtual-layers Combos >"{{ draw }}/base.yaml"
    yq -Yi '.combos.[].l = ["Combos"]' "{{ draw }}/base.yaml"
    keymap -c "{{ draw }}/config.yaml" draw "{{ draw }}/base.yaml" -d "zmk-new_corne/boards/arm/eyelash_corne/eyelash_corne-layouts.dtsi" >"{{ draw }}/keymap.png"

# initialize west
init:
    west init -l config
    west update --fetch-opt=--filter=blob:none
    west zephyr-export

# list build targets
list:
    @just _parse_targets all | awk -F, '{if ($4) print $4; else if ($2) print $2"-"$1; else print $1}' | sort

# update west
update:
    west update --fetch-opt=--filter=blob:none

# upgrade zephyr-sdk and python dependencies
upgrade-sdk:
    nix flake update --flake .

# check for new upstream commits (quick check)
upstream-check:
    @just _check_upstream

# show detailed upstream status and commits
upstream-status:
    #!/usr/bin/env bash
    set -euo pipefail
    
    latest_tag=$(git tag -l "upstream-sync-*" | sort -V | tail -1 || echo "")
    
    echo "📊 Upstream Status"
    echo "==================="
    
    if [[ -z "$latest_tag" ]]; then
        echo "⚠️  No upstream-sync tags found"
        echo "🔍 Showing last 10 upstream commits:"
        git log --pretty=format:"%ad %h %s" --date=short upstream/main -10
    else
        echo "🏷️  Last sync: $latest_tag"
        echo ""
        
        # Get upstream commits since divergence point
        merge_base=$(git merge-base main upstream/main 2>/dev/null || echo "")
        if [[ -n "$merge_base" ]]; then
            all_upstream=$(git log --pretty=format:"%ad %h %s" --date=short upstream/main ^$merge_base 2>/dev/null || echo "")
        else
            all_upstream=$(git log --pretty=format:"%ad %h %s" --date=short upstream/main -20 2>/dev/null || echo "")
        fi
        
        # Get commits we've integrated (from tag message)
        integrated_shas=""
        if git tag -l "$latest_tag" >/dev/null 2>&1; then
            # Extract SHAs from tag message (format: local_sha->upstream_sha)
            integrated_shas=$(git tag -n99 "$latest_tag" | grep -oE '[a-f0-9]{7,}->[a-f0-9]{7,}' | cut -d'>' -f2 | tr '\n' ' ' || echo "")
        fi
        
        # Filter out integrated commits
        new_commits=""
        if [[ -n "$all_upstream" ]]; then
            while read -r line; do
                commit_sha=$(echo "$line" | cut -d' ' -f2)
                if [[ ! " $integrated_shas " =~ " $commit_sha " ]]; then
                    new_commits="$new_commits$line"$'\n'
                fi
            done <<< "$all_upstream"
            new_commits=$(echo "$new_commits" | head -n -1) # remove trailing newline
        fi
        
        if [[ -z "$new_commits" ]]; then
            echo "✅ Up to date with upstream"
        else
            echo "🆕 New commits since $latest_tag:"
            echo "$new_commits"
            echo ""
            echo "💡 To integrate: git cherry-pick <commit-sha>"
            echo "💡 After integration: git tag -a upstream-sync-$(date +%Y-%m-%d) -m \"Integrated: <description>\""
        fi
    fi

[no-cd]
test $testpath *FLAGS:
    #!/usr/bin/env bash
    set -euo pipefail
    testcase=$(basename "$testpath")
    build_dir="{{ build / "tests" / '$testcase' }}"
    config_dir="{{ '$(pwd)' / '$testpath' }}"
    cd {{ justfile_directory() }}

    if [[ "{{ FLAGS }}" != *"--no-build"* ]]; then
        echo "Running $testcase..."
        rm -rf "$build_dir"
        west build -s zmk/app -d "$build_dir" -b native_posix_64 -- \
            -DCONFIG_ASSERT=y -DZMK_CONFIG="$config_dir"
    fi

    ${build_dir}/zephyr/zmk.exe | sed -e "s/.*> //" |
        tee ${build_dir}/keycode_events.full.log |
        sed -n -f ${config_dir}/events.patterns > ${build_dir}/keycode_events.log
    if [[ "{{ FLAGS }}" == *"--verbose"* ]]; then
        cat ${build_dir}/keycode_events.log
    fi

    if [[ "{{ FLAGS }}" == *"--auto-accept"* ]]; then
        cp ${build_dir}/keycode_events.log ${config_dir}/keycode_events.snapshot
    fi
    diff -auZ ${config_dir}/keycode_events.snapshot ${build_dir}/keycode_events.log
