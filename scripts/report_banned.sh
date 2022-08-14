#!/bin/bash
# Scan elf binary with debug symbols for banned functions

: ${1?"USAGE: "`basename ${BASH_SOURCE[0]}`" [linked binary]"}

SCRIPT_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"

# Feed awk script through stdin
awk -f- ${SCRIPT_DIR}/banned_funcs.txt <(arm-none-eabi-nm $1) <<'EOF' |
# Read banned files into array
NR==FNR { # Match first file
  banned[$1]
  next
}

# Scan symbol names
$3 in banned {
  print $3
}
EOF
sort
