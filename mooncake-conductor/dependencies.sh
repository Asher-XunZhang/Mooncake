#!/bin/bash

get_git_repo_path() {
    local input_path="${1:-.}"
    local target_dir
    
    if [ ! -e "$input_path" ]; then
        echo "Error: The path '$input_path' does not exist." >&2
        return 1
    fi
    
    if [ -d "$input_path" ]; then
        target_dir="$input_path"
    else
        target_dir=$(dirname "$input_path")
    fi
    
    if command -v git >/dev/null 2>&1; then
        local git_root
        if git_root=$(git -C "$target_dir" rev-parse --show-toplevel 2>/dev/null); then
            echo "$git_root"
            return 0
        fi
    fi
    
    local current_dir=$(cd "$target_dir" && pwd)
    local max_depth=50
    local depth=0
    
    while [ "$current_dir" != "/" ] && [ $depth -lt $max_depth ]; do
        if [ -d "$current_dir/.git" ]; then
            echo "$current_dir"
            return 0
        fi

        if [ -f "$current_dir/.git" ]; then
            local git_file_content
            git_file_content=$(head -n1 "$current_dir/.git" 2>/dev/null)
            if [[ "$git_file_content" == "gitdir:"* ]]; then
                echo "$current_dir"
                return 0
            fi
        fi
        
        current_dir=$(dirname "$current_dir")
        ((depth++))
    done
    
    echo "Error: The path '$input_path' is not within a git repository." >&2
    return 1
}

# Color definitions
GREEN="\033[0;32m"
BLUE="\033[0;34m"
YELLOW="\033[0;33m"
RED="\033[0;31m"
NC="\033[0m" # No Color

# Configuration

GITHUB_PROXY=${GITHUB_PROXY:-"https://github.com"}
GOVER=1.23.8

# Function to print section headers
print_section() {
    echo -e "\n${BLUE}=== $1 ===${NC}"
}

# Function to print success messages
print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

# Function to print error messages and exit
print_error() {
    echo -e "${RED}✗ ERROR: $1${NC}"
    exit 1
}

# Function to check command success
check_success() {
    if [ $? -ne 0 ]; then
        print_error "$1"
    fi
}

print_section "Installing nlohmann/json"

REPO_ROOT=$(get_git_repo_path)
THIRDPARTIES_DIR="${REPO_ROOT}/thirdparties"
NLOHMANN_DIR="${THIRDPARTIES_DIR}/nlohmann"

# Ensure thirdparties directory exists
if [ ! -d "${THIRDPARTIES_DIR}" ]; then
    mkdir -p "${THIRDPARTIES_DIR}"
    check_success "Failed to create thirdparties directory"
fi

# Clean existing installation
if [ -d "${NLOHMANN_DIR}" ]; then
    echo -e "${YELLOW}Existing nlohmann directory found. Cleaning up...${NC}"
    rm -rf "${NLOHMANN_DIR}"
    check_success "Failed to remove existing nlohmann directory"
fi

mkdir -p "${NLOHMANN_DIR}"
check_success "Failed to create nlohmann directory"

# Download single header file (v3.11.3)
JSON_URL="${GITHUB_PROXY}/nlohmann/json/releases/download/v3.11.3/json.hpp"
echo "Downloading nlohmann/json from ${JSON_URL}"
wget -q --show-progress -O "${NLOHMANN_DIR}/json.hpp" "${JSON_URL}"
check_success "Failed to download nlohmann/json header"

# Verify file integrity (basic check)
if [ ! -s "${NLOHMANN_DIR}/json.hpp" ]; then
    print_error "Downloaded json.hpp is empty"
fi

print_success "nlohmann/json header installed successfully at ${NLOHMANN_DIR}/json.hpp"