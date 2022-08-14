#!/bin/bash

# Wrapper to run cmake with simplified management of configuration options.
# This takes care of selecting the build type and will purge the cache if the
# toolchain is changed.

PLATFORM=stm32
DEBUG=0
RELEASE=0
BUILD_DIR=.
LIST_OPTS=0

USE_TINYUSB=off

usage()
{
  echo "Usage: configure [ -d | --debug ] [ -r | --release ]
                 [ -p | --platform stm32|hosted ]
                 [ -B BUILD_DIR ]
                 [ USE_TINYUSB ]"
  exit 2
}

PARSED_ARGS=$(getopt -n configure -o drlB:p: --long debug,release,list,platform: -- "$@")
VALID_ARGS=$?
if [ "$VALID_ARGS" != "0" ]; then
  usage
fi


eval set -- "$PARSED_ARGS"

while :
do
  case "$1" in
    -p | --platform) PLATFORM="$2"; shift 2 ;;
    -B)             BUILD_DIR="$2"; shift 2 ;;
    -d | --debug)   DEBUG=1       ; shift   ;;
    -r | --release) RELEASE=1     ; shift   ;;
    -l | --list)    LIST_OPTS=1   ; shift   ;;
    --) shift; break ;; # End of option arguments
  esac
done

# Parse non-option arguments
for opt in "$@"
do
  case "$opt" in
    USE_TINYUSB)  USE_TINYUSB=on  ;;
  esac
done

if [ "$DEBUG" == "1" ] && [ "$RELEASE" == "1" ]; then
  BUILD_TYPE=RelWithDebInfo
elif [ "$RELEASE" == "1" ]; then
  BUILD_TYPE=Release
else
  BUILD_TYPE=Debug
fi

if [ "$LIST_OPTS" == "1" ]; then  # Print cached variables
  cmake -L -B $BUILD_DIR | awk '{if(f) print} /-- Cache values/ {f=1}'

else # Configure build
  echo "Build type: $BUILD_TYPE"
  echo "Build dir:  $BUILD_DIR"
  echo "Platform:   $PLATFORM"

  case "$PLATFORM" in
    stm32)  TOOLCHAIN=scripts/toolchain_arm_generic.cmake ;;
    hosted) TOOLCHAIN="" ;;
  esac


  NEW_TOOLCHAIN=1
  if [ -f "CMakeCache.txt" ]; then # Check if toolchain is already cached
    TOOLCHAIN_CACHE=`cmake -L -B $BUILD_DIR | awk '{ if (match($0,/CMAKE_TOOLCHAIN_FILE.*=(.*)/,m)) print m[1] }' | sed -e 's/^.*scripts\//scripts\//'`

#echo "## TC cache: '$TOOLCHAIN_CACHE'"
#echo "## TC: '$TOOLCHAIN'"
    if [[ "$TOOLCHAIN" == "$TOOLCHAIN_CACHE" ]]; then
      NEW_TOOLCHAIN=0
    fi
  fi


#echo "## New TC: $NEW_TOOLCHAIN"

  TOOLCHAIN_OPT=""
  if [ "$NEW_TOOLCHAIN" == "1" ]; then
    if [ ${#TOOLCHAIN} -gt 0 ]; then
      TOOLCHAIN_OPT="--toolchain $TOOLCHAIN"
    else # Using system toolchain
      TOOLCHAIN="(system)"
    fi

    echo "New toolchain: $TOOLCHAIN"
    rm CMakeCache.txt # New toolchain requires a cache purge
  fi

#echo "## TC: $TOOLCHAIN_OPT"

  cmake -S . -B $BUILD_DIR $TOOLCHAIN_OPT \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DBUILD_PLATFORM=$PLATFORM \
    -DUSE_TINYUSB=$USE_TINYUSB
fi
