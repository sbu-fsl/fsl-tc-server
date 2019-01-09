#!/bin/bash - 
#=============================================================================
# Hello world test. Create a simple file and export it; try mount and read it.
# 
# by Ming Chen, v.mingchen@gmail.com
#=============================================================================

set -o nounset                          # treat unset variables as an error
set -o errexit                          # stop script if command fail
export PATH="/bin:/usr/bin:/sbin"             
IFS=$' \t\n'                            # reset IFS
unset -f unalias                        # make sure unalias is not a function
\unalias -a                             # unset all aliases
ulimit -H -c 0 --                       # disable core dump
hash -r                                 # clear the command path hash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
CONF="${DIR}/../src/config_samples/vfs-test.conf"
NFSROOT='/nfsroot'
MNT='/tmp/mnt'
LOG=`mktemp /tmp/log.XXXXXX`
echo "NFS-Ganesha writing logs to ${LOG}."

if [ -n "${1:-}" ]; then
  BUILDDIR="$1"
  echo "Using provided build directory: ${BUILDDIR}" 
else
  BUILDDIR="${DIR}/../build"
  echo "Using default build directory: ${BUILDDIR}"
fi

function onerr() {
  tail -n 100 ${LOG}
}

trap onerr ERR

if [ -d ${NFSROOT} ]; then
  rm -rf ${NFSROOT}
fi

mkdir ${NFSROOT}
echo 'hello world' > ${NFSROOT}/hello.txt
${BUILDDIR}/MainNFSD/ganesha.nfsd -f ${CONF} -L ${LOG} -N NIV_INFO
echo "Waiting for grace period..."
sleep 10
echo "Grace period ended."
if [ -d ${MNT} ]; then
  rm -rf ${MNT}
fi
mkdir ${MNT}
mount -t nfs 127.0.0.1:${NFSROOT} ${MNT}
cat ${MNT}/hello.txt
umount ${MNT}
