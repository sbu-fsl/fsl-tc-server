#!/bin/bash

BUILD=$1

if [ "$BUILD" = "Debug" ]; then
	lttng stop
	lttng destroy
fi

kill $(pgrep nfsd)

while [ -n "$(pgrep nfsd)" ]; do
	sleep 1;
done

