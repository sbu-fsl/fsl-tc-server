#!/bin/bash

# Usage: ./launch-branch.sh branch=<branch> conf=<path/to/config> build=<Debug|Release> loglevel=<INFO|DEBUG|...> trace=<session_name>

# evaluate arguments

for kv in "$@"; do
	eval "$kv";
done

LOG="/tmp/launch-branch.log"
ERRLOG="/tmp/launch-branch.err.log"
BRANCH=$branch
CONF=$conf
SRVLOG="/var/log/tcserver.log"
BUILD=$build
LOGLEVEL=$loglevel
TRACE_SESSION=$trace
# Please change this if the server is located somewhere else
BASEDIR="$HOME/txcompound/nfs4tc-server"
EXTRA_OPT=

if [ -z "$BRANCH" ]; then
	BRANCH="tc-server-2.7"
fi

if [ -z "$CONF" ]; then
	CONF="/etc/ganesha/tcserver.txnfs.conf"
fi

if [ -z "$BUILD" ]; then
	BUILD="Release"
fi

if [ -z "$LOGLEVEL" ]; then
	LOGLEVEL="INFO"
fi

pushd $BASEDIR
git checkout $BRANCH
if [ $? -ne 0 ]; then
	echo "Cannot checkout branch: $BRANCH";
	exit 2;
fi

if [ "$BUILD" = "Release" ]; then
	pushd release
	EXTRA_OPT="$EXTRA_OPT -DUSE_LTTNG=OFF -DUSE_GTEST=ON"
else
	EXTRA_OPT="$EXTRA_OPT -DUSE_LTTNG=ON -DUSE_GTEST=OFF"
	if [ -z "$TRACE_SESSION" ]; then
		echo "Please specify a trace session name";
		exit 1;
	fi
	pushd build
fi

cmake -DCMAKE_BUILD_TYPE=$BUILD -DUSE_FSAL_CEPH=OFF -DUSE_FSAL_PROXY=OFF -DUSE_FSAL_GPFS=OFF -DUSE_FSAL_LUSTRE=OFF -DUSE_FSAL_GLUSTER=OFF -DUSE_9P=OFF -DUSE_ADMIN_TOOLS=OFF -DLTTNG_PATH_HINT=/usr/ -DUSE_FSAL_RGW=OFF -DUSE_9P_RDMA=OFF -D_USE_9P_RDMA=OFF -DUSE_NFS_RDMA=OFF -DUSE_RADOS_RECOV=OFF -DRADOS_URLS=OFF $EXTRA_OPT ../src

if [ $? -ne 0 ]; then
	echo "CMake failed.";
	exit 3;
fi

make -j$(nproc)

if [ $? -ne 0 ]; then
	echo "Make failed";
	exit 4;
fi

make install

if [ $? -ne 0 ]; then
	echo "Make install failed - permission?";
	exit 5;
fi

rm $SRVLOG;

if [ "$BUILD" = "Debug" ]; then
	lttng create $TRACE_SESSION;
	lttng enable-event -u 'txnfs:*';
	lttng enable-event -u 'nfs_rpc:v4op_start';
	lttng enable-event -u 'nfs_rpc:v4op_end';
	lttng start
	LD_PRELOAD=/usr/lib/ganesha/libganesha_trace.so $BASEDIR/build/MainNFSD/ganesha.nfsd -f $CONF -L $SRVLOG -N $LOGLEVEL 
else
	$BASEDIR/release/MainNFSD/ganesha.nfsd -f $CONF -L $SRVLOG -N $LOGLEVEL;
fi

popd
popd
