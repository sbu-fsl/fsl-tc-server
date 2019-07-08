// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (C) Stony Brook University 2019
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

//#include "fsal.h"
#include "gtest_nfs4.hh"
#include "nfs_creds.h"
//#include "nfs_proto_functions.h"
//#include "sal_functions.h"

extern "C" {
/* Manually forward this, an 9P is not C++ safe */
void admin_halt(void);
void nfs_end_grace(void);

clientid_status_t nfs_client_id_confirm(nfs_client_id_t *clientid,
                                        log_components_t component);

clientid4 new_clientid(void);
nfs_client_id_t *create_client_id(clientid4 clientid,
                                  nfs_client_record_t *client_record,
                                  nfs_client_cred_t *credential,
                                  uint32_t minorversion);

clientid_status_t nfs_client_id_insert(nfs_client_id_t *clientid);

nfs_client_record_t *get_client_record(const char *const value,
                                       const size_t len,
                                       const uint32_t pnfs_flags,
                                       const uint32_t server_addr);
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
  void init_args(int nops) {
    ops = (struct nfs_argop4 *)gsh_calloc(nops, sizeof(struct nfs_argop4));
    arg.arg_compound4.argarray.argarray_len = nops;
    arg.arg_compound4.argarray.argarray_val = ops;
  }

  virtual void SetUp() {
    gtest::GaneshaFSALBaseTest::SetUp();

    memset(&data, 0, sizeof(struct compound_data));
    memset(&arg, 0, sizeof(nfs_arg_t));
    memset(&resp, 0, sizeof(struct nfs_resop4));

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

  init_args(3 /*nops*/);
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

  init_args(3 /*nops*/);
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

TEST_F(GaneshaCompoundBaseTest, SimpleRemove) {
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

  init_args(3 /*nops*/);
  setup_rootfh(0);
  setup_putfh(1, root_entry);
  setup_remove(2, TEST_ROOT);
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
      res.res_compound4.resarray.resarray_val[2].nfs_resop4_u.opremove.status);

  cleanup_remove(2);
  cleanup_putfh(1);
  disableEvents(event_list);
}

TEST_F(GaneshaCompoundBaseTest, SimpleWrite) {
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
  req_ctx.client = get_gsh_client((sockaddr_t *)&caller_addr, false);

  // create client entry
  clientid4 clientid = new_clientid();
  nfs_client_record_t *client_record = get_client_record("client", 6, 0, 0);
  nfs_client_id_t *unconf =
      create_client_id(clientid, client_record, &data.credential, 0);
  rc = nfs_client_id_insert(unconf);
  rc = nfs_client_id_confirm(unconf, COMPONENT_CLIENTID);
  nfs_end_grace();
  init_args(5 /*nops*/);
  setup_putfh(0, root_entry);
  setup_open(1, "foo", clientid);
  setup_write(2, "foo");
  setup_write(3, "bar");
  setup_write(4, "baz");
  enableEvents(event_list);

  rc = nfs4_Compound(&arg, &req, &res);

  EXPECT_EQ(rc, NFS_REQ_OK);
  EXPECT_EQ(5, res.res_compound4.resarray.resarray_len);
  EXPECT_EQ(NFS_OK, res.res_compound4.resarray.resarray_val[0]
                        .nfs_resop4_u.opputrootfh.status);
  EXPECT_EQ(
      NFS_OK,
      res.res_compound4.resarray.resarray_val[1].nfs_resop4_u.opputfh.status);
  EXPECT_EQ(
      NFS_OK,
      res.res_compound4.resarray.resarray_val[2].nfs_resop4_u.opopen.status);
  EXPECT_EQ(
      NFS_OK,
      res.res_compound4.resarray.resarray_val[3].nfs_resop4_u.opopen.status);
  EXPECT_EQ(
      NFS_OK,
      res.res_compound4.resarray.resarray_val[4].nfs_resop4_u.opopen.status);

  // cleanup_open(2);
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
