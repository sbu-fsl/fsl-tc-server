/*
 * This test is used to perform clone and range clone on XFS
 * file system. To perform the test use the following command
 * ./test_clone2 --log <path> --config /src/config_samples/xfs-test.conf
 */

#include <sys/types.h>
#include <iostream>
#include <thread>
#include <boost/filesystem.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/program_options.hpp>

extern "C" {
/* Manually forward this, as 9P is not C++ safe */
void admin_halt(void);
/* Ganesha headers */
#include "export_mgr.h"
#include "nfs_exports.h"
#include "sal_data.h"
#include "fsal.h"
#include "common_utils.h"
/* For MDCACHE bypass.  Use with care */
#include "../FSAL/Stackable_FSALs/FSAL_MDCACHE/mdcache_debug.h"
}

#include "gtest.hh"

#define TEST_ROOT "clone"
#define TEST_FILE "original_name"
#define TEST_FILE_CLONE "clone_name"
#define OFFSET 0

namespace {

  char* ganesha_conf = nullptr;
  char* lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;
  char* event_list = nullptr;
  char* profile_out = nullptr;

  class CloneTest : public gtest::GaneshaFSALBaseTest {
  protected:

    virtual void SetUp() {
      gtest::GaneshaFSALBaseTest::SetUp();
      fsal_prepare_attrs(&attrs_in, 0);
    }

    virtual void TearDown() {
      fsal_release_attrs(&attrs_in);
      gtest::GaneshaFSALBaseTest::TearDown();
    }
    struct attrlist attrs_in;
  };

  static void callback(struct fsal_obj_handle *obj, fsal_status_t ret,
                        void *write_data, void *caller_data)
  {
    if (ret.major == ERR_FSAL_SHARE_DENIED)
      ret = fsalstat(ERR_FSAL_LOCKED, 0);

    EXPECT_EQ(ret.major, 0);
  }

} /* namespace */

TEST_F(CloneTest, SIMPLE_CLONE)
{
  fsal_status_t status;
  bool caller_perm_check = false;
  struct state_t *file_state1;
  struct state_t *file_state2;
  struct fsal_obj_handle *src_obj = nullptr;
  struct fsal_obj_handle *dst_obj = nullptr;
  char *w_databuffer, *r_databuffer;
  struct fsal_io_arg *write_arg, *read_arg;
  int bytes = 64;
  int ret = -1;
  w_databuffer = (char *) malloc(bytes);
  r_databuffer = (char *) malloc(bytes);

  memset(w_databuffer, 'a', bytes);

  /* Create src file for the test */
  file_state1 = op_ctx->fsal_export->exp_ops.alloc_state(op_ctx->fsal_export,
							STATE_TYPE_SHARE,
							NULL);
  ASSERT_NE(file_state1, nullptr);

  status = test_root->obj_ops->open2(test_root, file_state1, FSAL_O_RDWR,
	     FSAL_UNCHECKED, TEST_FILE, &attrs_in, NULL, &src_obj, NULL,
	     &caller_perm_check);
  ASSERT_EQ(status.major, 0);

  write_arg = (struct fsal_io_arg*)alloca(sizeof(struct fsal_io_arg) +
					  sizeof(struct iovec));
  write_arg->info = NULL;
  write_arg->state = NULL;
  write_arg->offset = OFFSET;
  write_arg->iov_count = 1;
  write_arg->iov[0].iov_len = bytes;
  write_arg->iov[0].iov_base = w_databuffer;
  write_arg->io_amount = 0;
  write_arg->fsal_stable = false;

  src_obj->obj_ops->write2(src_obj, true, callback, write_arg, NULL);

  file_state2 = op_ctx->fsal_export->exp_ops.alloc_state(op_ctx->fsal_export,
							STATE_TYPE_SHARE,
							NULL);
  ASSERT_NE(file_state2, nullptr);
  // create and open a file for clone
  status = test_root->obj_ops->open2(test_root, file_state2, FSAL_O_RDWR,
	     FSAL_UNCHECKED, TEST_FILE_CLONE, &attrs_in, NULL, &dst_obj, NULL,
	     &caller_perm_check);
  ASSERT_EQ(status.major, 0);

  /* Clone the src file */
  status = test_root->obj_ops->clone2(src_obj, NULL, dst_obj, NULL, -1, 0);
  EXPECT_EQ(status.major, 0);

  // Validate clone
  read_arg = (struct fsal_io_arg*)alloca(sizeof(struct fsal_io_arg) +
					 sizeof(struct iovec));
  read_arg->info = NULL;
  read_arg->state = NULL;
  read_arg->offset = OFFSET;
  read_arg->iov_count = 1;
  read_arg->iov[0].iov_len = bytes;
  read_arg->iov[0].iov_base = r_databuffer;
  read_arg->io_amount = 0;

  dst_obj->obj_ops->read2(dst_obj, true, callback, read_arg, NULL);

  ret = memcmp(r_databuffer, w_databuffer, bytes);
  EXPECT_EQ(ret, 0);

  free(w_databuffer);
  free(r_databuffer);

  status = src_obj->obj_ops->close2(src_obj, file_state1);
  EXPECT_EQ(status.major, 0);

  status = dst_obj->obj_ops->close2(dst_obj, file_state2);
  EXPECT_EQ(status.major, 0);
  /* Remove files created while running test */
  status = fsal_remove(test_root, TEST_FILE);
  ASSERT_EQ(status.major, 0);
  status = fsal_remove(test_root, TEST_FILE_CLONE);
  ASSERT_EQ(status.major, 0);
  op_ctx->fsal_export->exp_ops.free_state(op_ctx->fsal_export, file_state1);
  op_ctx->fsal_export->exp_ops.free_state(op_ctx->fsal_export, file_state2);
}

TEST_F(CloneTest, RANGE_CLONE)
{
  fsal_status_t status;
  bool caller_perm_check = false;
  struct state_t *file_state1;
  struct state_t *file_state2;
  struct fsal_obj_handle *src_obj = nullptr;
  struct fsal_obj_handle *dst_obj = nullptr;
  char *w_databuffer, *r_databuffer;
  struct fsal_io_arg *write_arg, *read_arg;
  int bytes = 4096 + 1024;
  int cpy_bytes = 1024;
  // For range copy the offset should be aligned with block size
  long int off_in = 4096;
  long int off_out = 0;
  int ret = -1;
  w_databuffer = (char *) malloc(bytes);
  r_databuffer = (char *) malloc(cpy_bytes);

  memset(w_databuffer, 'a', bytes);

  /* Create src file for the test */

  file_state1 = op_ctx->fsal_export->exp_ops.alloc_state(op_ctx->fsal_export,
							STATE_TYPE_SHARE,
							NULL);
  ASSERT_NE(file_state1, nullptr);

  status = test_root->obj_ops->open2(test_root, file_state1, FSAL_O_RDWR,
	     FSAL_UNCHECKED, TEST_FILE, &attrs_in, NULL, &src_obj, NULL,
	     &caller_perm_check);
  ASSERT_EQ(status.major, 0);

  write_arg = (struct fsal_io_arg*)alloca(sizeof(struct fsal_io_arg) +
					  sizeof(struct iovec));
  write_arg->info = NULL;
  write_arg->state = NULL;
  write_arg->offset = OFFSET;
  write_arg->iov_count = 1;
  write_arg->iov[0].iov_len = bytes;
  write_arg->iov[0].iov_base = w_databuffer;
  write_arg->io_amount = 0;
  write_arg->fsal_stable = false;

  src_obj->obj_ops->write2(src_obj, true, callback, write_arg, NULL);

  file_state2 = op_ctx->fsal_export->exp_ops.alloc_state(op_ctx->fsal_export,
							STATE_TYPE_SHARE,
							NULL);
  ASSERT_NE(file_state2, nullptr);
  // create and open a file for clone
  status = test_root->obj_ops->open2(test_root, file_state2, FSAL_O_RDWR,
	     FSAL_UNCHECKED, TEST_FILE_CLONE, &attrs_in, NULL, &dst_obj, NULL,
	     &caller_perm_check);
  ASSERT_EQ(status.major, 0);

  /* Clone the src file */
  status = test_root->obj_ops->clone2(src_obj, &off_in, dst_obj, &off_out, cpy_bytes, 0);
  EXPECT_EQ(status.major, 0);

  // Validate clone
  read_arg = (struct fsal_io_arg*)alloca(sizeof(struct fsal_io_arg) +
					 sizeof(struct iovec));
  read_arg->info = NULL;
  read_arg->state = NULL;
  read_arg->offset = OFFSET;
  read_arg->iov_count = 1;
  read_arg->iov[0].iov_len = cpy_bytes;
  read_arg->iov[0].iov_base = r_databuffer;
  read_arg->io_amount = 0;

  dst_obj->obj_ops->read2(dst_obj, true, callback, read_arg, NULL);

  ret = memcmp(r_databuffer, w_databuffer + off_in, cpy_bytes);
  EXPECT_EQ(ret, 0);

  free(w_databuffer);
  free(r_databuffer);

  status = src_obj->obj_ops->close2(src_obj, file_state1);
  EXPECT_EQ(status.major, 0);

  status = dst_obj->obj_ops->close2(dst_obj, file_state2);
  EXPECT_EQ(status.major, 0);
  /* Remove files created while running test */
  status = fsal_remove(test_root, TEST_FILE);
  ASSERT_EQ(status.major, 0);
  status = fsal_remove(test_root, TEST_FILE_CLONE);
  ASSERT_EQ(status.major, 0);
  op_ctx->fsal_export->exp_ops.free_state(op_ctx->fsal_export, file_state1);
  op_ctx->fsal_export->exp_ops.free_state(op_ctx->fsal_export, file_state2);
}

int main(int argc, char *argv[])
{
  int code = 0;
  char* session_name = NULL;

  using namespace std;
  using namespace std::literals;
  namespace po = boost::program_options;

  po::options_description opts("program options");
  po::variables_map vm;

  try {

    opts.add_options()
      ("config", po::value<string>(),
       "path to Ganesha conf file")

      ("logfile", po::value<string>(),
       "log to the provided file path")

      ("export", po::value<uint16_t>(),
       "id of export on which to operate (must exist)")

      ("debug", po::value<string>(),
       "ganesha debug level")

      ("session", po::value<string>(),
	   "LTTng session name")

      ("event-list", po::value<string>(),
	   "LTTng event list, comma separated")

      ("profile", po::value<string>(),
	   "Enable profiling and set output file.")
      ;

    po::variables_map::iterator vm_iter;
    po::command_line_parser parser{argc, argv};
    parser.options(opts).allow_unregistered();
    po::store(parser.run(), vm);
    po::notify(vm);

    // use config vars--leaves them on the stack
    vm_iter = vm.find("config");
    if (vm_iter != vm.end()) {
      ganesha_conf = (char*) vm_iter->second.as<std::string>().c_str();
    }
    vm_iter = vm.find("logfile");
    if (vm_iter != vm.end()) {
      lpath = (char*) vm_iter->second.as<std::string>().c_str();
    }
    vm_iter = vm.find("debug");
    if (vm_iter != vm.end()) {
      dlevel = ReturnLevelAscii(
	(char*) vm_iter->second.as<std::string>().c_str());
    }
    vm_iter = vm.find("export");
    if (vm_iter != vm.end()) {
      export_id = vm_iter->second.as<uint16_t>();
    }
    vm_iter = vm.find("session");
    if (vm_iter != vm.end()) {
      session_name = (char*) vm_iter->second.as<std::string>().c_str();
    }
    vm_iter = vm.find("event-list");
    if (vm_iter != vm.end()) {
      event_list = (char*) vm_iter->second.as<std::string>().c_str();
    }
    vm_iter = vm.find("profile");
    if (vm_iter != vm.end()) {
      profile_out = (char*) vm_iter->second.as<std::string>().c_str();
    }

    ::testing::InitGoogleTest(&argc, argv);
    gtest::env = new gtest::Environment(ganesha_conf, lpath, dlevel,
					session_name, TEST_ROOT, export_id);
    ::testing::AddGlobalTestEnvironment(gtest::env);

    code  = RUN_ALL_TESTS();
  }

  catch(po::error& e) {
    cout << "Error parsing opts " << e.what() << endl;
  }

  catch(...) {
    cout << "Unhandled exception in main()" << endl;
  }

  return code;
}
