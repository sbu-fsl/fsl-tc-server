#!/bin/bash -
# Launch the NFS server running VFS FSAL
#
# This script should be placed in the build directory.
#
#       cp run-tcserver.sh <root-to-nfs-ganesha>/<build-directory>
#
# Usage:
#
#       cd <root-to-nfs-ganesha>/<build-directory>
#       ./run-tcserver.sh [debug|release]

set -e

# set the max nubmer of open files
ulimit -n 409600

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

running_mode="${1:-release}"

ulimit -c 0

if [[ ${running_mode} == 'debug' ]]; then
  CONFFILE=/etc/ganesha/tcserver.debug.ganesha.conf
  LOGLEVEL=DEBUG
elif [[ ${running_mode} == 'release' ]]; then
  CONFFILE=/etc/ganesha/tcserver.ganesha.conf
  LOGLEVEL=EVENT
else
  echo "usage: $0 [debug|release]"
  exit 1
fi

PATHPROG=$DIR/MainNFSD/ganesha.nfsd

LOGFILE=/var/log/tcserver.ganesha.log

prog=ganesha.nfsd
PID_FILE=${PID_FILE:=/var/run/${prog}.pid}
LOCK_FILE=${LOCK_FILE:=/var/lock/subsys/${prog}}

while pgrep ${prog}; do
  echo "NFS-Ganesha server is already running. Killing it ..."
  if pkill -9 ${prog}; then
    echo "Ganesha killed. Restarting..";
  fi
done

$PATHPROG -L ${LOGFILE} -f ${CONFFILE} -N ${LOGLEVEL}
