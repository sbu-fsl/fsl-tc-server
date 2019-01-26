#!/bin/bash - 
#=============================================================================
# Install dependency needed by the TC server on ubuntu. Tested on both 16.04
# and 18.04.
# 
# by Ming Chen, v.mingchen@gmail.com
#=============================================================================

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOTDIR="${DIR}/.."
cd "$ROOTDIR"
git submodule update --init --recursive

echo 'Install package'
sudo apt-get install -y cmake
sudo apt-add-repository -y ppa:lttng/ppa
sudo apt-get update -q
sudo apt-get install -y libnfsidmap2
sudo apt-get install -y libnfsidmap-dev
sudo apt-get install -y libkrb5-3
sudo apt-get install -y libkrb5-dev
sudo apt-get install -y libk5crypto3
sudo apt-get install -y libgssapi-krb5-2
sudo apt-get install -y libgssglue1
sudo apt-get install -y libdbus-1-3
sudo apt-get install -y libattr1-dev
sudo apt-get install -y libacl1-dev
sudo apt-get install -y dbus
sudo apt-get install -y bison flex
sudo apt-get install -y libdbus-1-dev
sudo apt-get install -y libcap-dev
sudo apt-get install -y libjemalloc-dev
sudo apt-get install -y uuid-dev
sudo apt-get install -y libblkid-dev
sudo apt-get install -y xfslibs-dev
sudo apt-get install -y libwbclient-dev
sudo apt-get install -y --allow-unauthenticated lttng-tools
sudo apt-get install -y --allow-unauthenticated liblttng-ust-dev
sudo apt-get install -y --allow-unauthenticated liblttng-ctl-dev
sudo apt-get install -y --allow-unauthenticated lttng-modules-dkms
sudo apt-get install -y pyqt4-dev-tools
sudo apt-get install -y rpm2cpio
sudo apt-get install -y libaio-dev
sudo apt-get install -y libibverbs-dev
sudo apt-get install -y librdmacm-dev
sudo apt-get install -y rpcbind
sudo apt-get install -y nfs-common
sudo apt-get install -y libboost-all-dev
sudo apt-get install -y google-perftools
sudo apt-get install -y libgoogle-perftools-dev
sudo apt-get install -y libprotobuf-dev protobuf-compiler
sudo apt-get install -y libleveldb-dev

# install libgtest:
cd /tmp
git clone  https://github.com/google/googletest.git
cd googletest
git checkout v1.8.x
mkdir build
cd build
cmake ..
make
sudo make install

# install google-benchmark
cd /tmp
git clone https://github.com/google/benchmark.git
cd benchmark
git checkout cmake-3_5_1
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DBENCHMARK_ENABLE_GTEST_TESTS=OFF ../
make
sudo make install

cd "$ROOTDIR/src/abseil-cpp"
git checkout lts_2018_12_18
cd ../..
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DUSE_FSAL_CEPH=OFF -DUSE_FSAL_PROXY=OFF -DUSE_FSAL_GPFS=OFF -DUSE_FSAL_LUSTRE=OFF -DUSE_FSAL_GLUSTER=OFF -DUSE_9P=OFF -DUSE_ADMIN_TOOLS=OFF -DLTTNG_PATH_HINT=/usr/ -DUSE_LTTNG=ON -DUSE_FSAL_RGW=OFF -DUSE_9P_RDMA=OFF -D_USE_9P_RDMA=OFF -DUSE_NFS_RDMA=OFF -DUSE_GTEST=ON -DUSE_RADOS_RECOV=OFF -DRADOS_URLS=OFF ../src/
make
sudo make install
