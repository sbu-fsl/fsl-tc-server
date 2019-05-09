// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (C) 2018 Red Hat, Inc.
 * Contributor : Frank Filz <ffilzlnx@mindspring.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

#include <boost/filesystem.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <iostream>
#include <map>
#include <random>
#include <sys/types.h>
#include <thread>
#include <vector>

#include "gtest_nfs4.hh"
#include "nfs_creds.h"

extern "C" {
/* Manually forward this, an 9P is not C++ safe */
void admin_halt(void);
/* Ganesha headers */
//#include "sal_data.h"
//#include "common_utils.h"
/* For MDCACHE bypass.  Use with care */
#include "../FSAL/Stackable_FSALs/FSAL_MDCACHE/mdcache_debug.h"
}

#define TEST_ROOT "nfs4_lookup_latency"
#define FILE_COUNT 100000
#define LOOP_COUNT 1000000

namespace {

char *event_list = nullptr;
char *profile_out = nullptr;

class GaneshaCompoundBaseTest : public gtest::GaeshaNFS4BaseTest {
protected:
  virtual void SetUp() {
    gtest::GaneshaFSALBaseTest::SetUp();

    memset(&data, 0, sizeof(struct compound_data));
    memset(&arg, 0, sizeof(nfs_arg_t));
    memset(&resp, 0, sizeof(struct nfs_resop4));

    ops = (struct nfs_argop4 *)gsh_calloc(3, sizeof(struct nfs_argop4));
    arg.arg_compound4.argarray.argarray_len = 3;
    arg.arg_compound4.argarray.argarray_val = ops;

    // caller_addr.sin_family = AF_INET;
    // caller_addr.sin_port = 100;
    // inet_pton(AF_INET, "127.0.0.1", &caller_addr.sin_addr);
    // req_ctx.caller_addr = (sockaddr_t *)&caller_addr;

    /* Setup some basic stuff (that will be overrode) so TearDown works. */
    data.minorversion = 0;
  }

  virtual void TearDown() {
    bool rc;

    set_current_entry(&data, nullptr);

    nfs4_Compound_FreeOne(&resp);

    /* Free the compound data and response */
    compound_data_Free(&data);

    /* Free the args structure. */
    rc = xdr_free((xdrproc_t)xdr_COMPOUND4args, &arg);
    EXPECT_EQ(rc, true);

    gtest::GaneshaFSALBaseTest::TearDown();
  }
};
} /* namespace */

TEST_F(GaneshaCompoundBaseTest, SimpleLookup) {
  int rc;

  struct svc_req req = {0};
  nfs_res_t res;

  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  EXPECT_NE(fd, -1);

  SVCXPRT *xprt =
      svc_vc_ncreatef(fd, 1024 * 1024, 1024 * 1024,
                      SVC_CREATE_FLAG_CLOSE | SVC_CREATE_FLAG_LISTEN);

  req.rq_msg.cb_cred.oa_flavor = AUTH_NONE;
  req.rq_xprt = xprt;
  // EXPECT_EQ(NFS4_OK, nfs4_export_check_access(&req));
  setup_rootfh(0);
  setup_putfh(1, root_entry);
  setup_lookup(2, TEST_ROOT);

  enableEvents(event_list);

  rc = nfs4_Compound(&arg, &req, &res);

  EXPECT_EQ(rc, NFS_REQ_OK);
  EXPECT_EQ(3, res.res_compound4.resarray.resarray_len);
  EXPECT_EQ(NFS_OK, res.res_compound4.resarray.resarray_val[0]
                        .nfs_resop4_u.opputrootfh.status);
  EXPECT_EQ(
      NFS_OK,
      res.res_compound4.resarray.resarray_val[1].nfs_resop4_u.opputfh.status);
  EXPECT_EQ(
      NFS_OK,
      res.res_compound4.resarray.resarray_val[2].nfs_resop4_u.oplookup.status);

  cleanup_lookup(2);
  cleanup_putfh(1);
  disableEvents(event_list);
}

TEST_F(GaneshaCompoundBaseTest, SimpleCreate) {
  int rc;

  struct svc_req req = {0};
  nfs_res_t res;

  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  EXPECT_NE(fd, -1);

  SVCXPRT *xprt =
      svc_vc_ncreatef(fd, 1024 * 1024, 1024 * 1024,
                      SVC_CREATE_FLAG_CLOSE | SVC_CREATE_FLAG_LISTEN);

  req.rq_msg.cb_cred.oa_flavor = AUTH_NONE;
  req.rq_xprt = xprt;
  // EXPECT_EQ(NFS4_OK, nfs4_export_check_access(&req));
  setup_rootfh(0);
  setup_putfh(1, root_entry);
  setup_create(2, "foo");
  enableEvents(event_list);

  rc = nfs4_Compound(&arg, &req, &res);

  EXPECT_EQ(rc, NFS_REQ_OK);
  EXPECT_EQ(3, res.res_compound4.resarray.resarray_len);
  EXPECT_EQ(NFS_OK, res.res_compound4.resarray.resarray_val[0]
                        .nfs_resop4_u.opputrootfh.status);
  EXPECT_EQ(
      NFS_OK,
      res.res_compound4.resarray.resarray_val[1].nfs_resop4_u.opputfh.status);
  EXPECT_EQ(
      NFS_OK,
      res.res_compound4.resarray.resarray_val[2].nfs_resop4_u.opcreate.status);

  cleanup_create(2);
  cleanup_putfh(1);
  disableEvents(event_list);
}

int main(int argc, char *argv[]) {
  int code = 0;
  char *session_name = NULL;
  char *ganesha_conf = nullptr;
  char *lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;

  using namespace std;
  using namespace std::literals;
  namespace po = boost::program_options;

  po::options_description opts("program options");
  po::variables_map vm;

  try {

    opts.add_options()("config", po::value<string>(),
                       "path to Ganesha conf file")

        ("logfile", po::value<string>(), "log to the provided file path")

            ("export", po::value<uint16_t>(),
             "id of export on which to operate (must exist)")

                ("debug", po::value<string>(), "ganesha debug level")

                    ("session", po::value<string>(), "LTTng session name")

                        ("event-list", po::value<string>(),
                         "LTTng event list, comma separated")

                            ("profile", po::value<string>(),
                             "Enable profiling and set output file.");

    po::variables_map::iterator vm_iter;
    po::command_line_parser parser{argc, argv};
    parser.options(opts).allow_unregistered();
    po::store(parser.run(), vm);
    po::notify(vm);

    // use config vars--leaves them on the stack
    vm_iter = vm.find("config");
    if (vm_iter != vm.end()) {
      ganesha_conf = (char *)vm_iter->second.as<std::string>().c_str();
    }
    vm_iter = vm.find("logfile");
    if (vm_iter != vm.end()) {
      lpath = (char *)vm_iter->second.as<std::string>().c_str();
    }
    vm_iter = vm.find("debug");
    if (vm_iter != vm.end()) {
      dlevel =
          ReturnLevelAscii((char *)vm_iter->second.as<std::string>().c_str());
    }
    vm_iter = vm.find("export");
    if (vm_iter != vm.end()) {
      export_id = vm_iter->second.as<uint16_t>();
    }
    vm_iter = vm.find("session");
    if (vm_iter != vm.end()) {
      session_name = (char *)vm_iter->second.as<std::string>().c_str();
    }
    vm_iter = vm.find("event-list");
    if (vm_iter != vm.end()) {
      event_list = (char *)vm_iter->second.as<std::string>().c_str();
    }
    vm_iter = vm.find("profile");
    if (vm_iter != vm.end()) {
      profile_out = (char *)vm_iter->second.as<std::string>().c_str();
    }

    ::testing::InitGoogleTest(&argc, argv);
    gtest::env = new gtest::Environment(ganesha_conf, lpath, dlevel,
                                        session_name, TEST_ROOT, export_id);
    ::testing::AddGlobalTestEnvironment(gtest::env);

    code = RUN_ALL_TESTS();
  }

  catch (po::error &e) {
    cout << "Error parsing opts " << e.what() << endl;
  }

  catch (...) {
    cout << "Unhandled exception in main()" << endl;
  }

  return code;
}
