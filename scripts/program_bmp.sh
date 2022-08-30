#!/bin/sh

# Default device depends on udev rules here:
# https://github.com/blackmagic-debug/blackmagic/blob/main/driver/99-blackmagic-plugdev.rules

DEV="/dev/ttyBmpGdb"
ELF_IMAGE=""

usage()
{
  echo "Usage: program_bmp.sh -d [device] -e [elf image]"
  exit 2
}

PARSED_ARGS=$(getopt -n configure -o d:e: -- "$@")
VALID_ARGS=$?
if [ "$VALID_ARGS" != "0" ]; then
  usage
fi

eval set -- "$PARSED_ARGS"

while :
do
  case "$1" in
    -d) DEV="$2"; shift 2 ;;
    -e) ELF_IMAGE="$2"; shift 2 ;;
    --) shift; break ;; # End of option arguments
  esac
done

if [ -z "$DEV" ] || [ -z "$ELF_IMAGE" ]; then
  usage
fi


# Confirm ELF image exists
arm-none-eabi-size $ELF_IMAGE || exit 1

# GDB script for Black Magic Probe with TPWR (3.3V) provided by the probe
TMPFILE=`tempfile --prefix=gdb_`
cat > $TMPFILE <<EOF
target extended-remote $DEV
monitor tpwr enable
monitor swdp_scan
attach 1
load
kill
EOF

gdb-multiarch $ELF_IMAGE --batch -x $TMPFILE
rm -f $TMPFILE
