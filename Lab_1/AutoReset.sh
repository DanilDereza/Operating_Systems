#!/bin/bash

PROJECT_DIR="$HOME/Desktop/OS/Lab_1/wd_lnx/src"
BRANCH="main"
BUILD_DIR="$HOME/Desktop/OS/Lab_1/wd_lnx/build"
BUILD_COMMAND="cmake ../"
SLEEP_INTERVAL=5

while true
do
    LOCAL_COMMIT=$(git -C "$PROJECT_DIR" rev-parse "$BRANCH")
    REMOTE_COMMIT=$(git -C "$PROJECT_DIR" ls-remote origin | awk '{print $1}')
    
    if [ "$LOCAL_COMMIT" != "$REMOTE_COMMIT" ]; then
        git -C "$PROJECT_DIR" fetch origin || exit 1
        git -C "$PROJECT_DIR" reset --hard origin/"$BRANCH" || exit 1
        cd "$BUILD_DIR" || exit 1
        $BUILD_COMMAND || true
    fi
    sleep $SLEEP_INTERVAL
done