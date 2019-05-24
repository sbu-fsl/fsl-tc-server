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

#include "gtest.hh"

extern "C" {
/* Ganesha headers */
#include "nfs_file_handle.h"
#include "nfs_lib.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
}

#ifndef GTEST_GTEST_NFS4_HH
#define GTEST_GTEST_NFS4_HH

namespace gtest {

class GaeshaNFS4BaseTest : public gtest::GaneshaFSALBaseTest {
protected:
  virtual void SetUp() {
    gtest::GaneshaFSALBaseTest::SetUp();

    memset(&data, 0, sizeof(struct compound_data));
    memset(&arg, 0, sizeof(nfs_arg_t));
    memset(&resp, 0, sizeof(struct nfs_resop4));

    ops = (struct nfs_argop4 *)gsh_calloc(1, sizeof(struct nfs_argop4));
    arg.arg_compound4.argarray.argarray_len = 1;
    arg.arg_compound4.argarray.argarray_val = ops;

    /* Setup some basic stuff (that will be overrode) so TearDown works. */
    data.minorversion = 0;
    ops[0].argop = NFS4_OP_PUTROOTFH;
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

  void setCurrentFH(struct fsal_obj_handle *entry) {
    bool fhres;

    /* Convert root_obj to a file handle in the args */
    fhres = nfs4_FSALToFhandle(data.currentFH.nfs_fh4_val == NULL,
                               &data.currentFH, entry, op_ctx->ctx_export);
    EXPECT_EQ(fhres, true);

    set_current_entry(&data, entry);
  }

  void setSavedFH(struct fsal_obj_handle *entry) {
    bool fhres;

    /* Convert root_obj to a file handle in the args */
    fhres = nfs4_FSALToFhandle(data.savedFH.nfs_fh4_val == NULL, &data.savedFH,
                               entry, op_ctx->ctx_export);
    EXPECT_EQ(fhres, true);

    set_saved_entry(&data, entry);
  }

  void set_saved_export(void) {
    /* Set saved export from op_ctx */
    if (data.saved_export != NULL)
      put_gsh_export(data.saved_export);
    /* Save the export information and take reference. */
    get_gsh_export_ref(op_ctx->ctx_export);
    data.saved_export = op_ctx->ctx_export;
    data.saved_export_perms = *op_ctx->export_perms;
  }

  void setup_rootfh(int pos) { ops[pos].argop = NFS4_OP_PUTROOTFH; }

  void setup_lookup(int pos, const char *name) {
    gsh_free(ops[pos].nfs_argop4_u.oplookup.objname.utf8string_val);
    ops[pos].argop = NFS4_OP_LOOKUP;
    ops[pos].nfs_argop4_u.oplookup.objname.utf8string_len = strlen(name);
    ops[pos].nfs_argop4_u.oplookup.objname.utf8string_val = gsh_strdup(name);
  }

  void cleanup_lookup(int pos) {
    gsh_free(ops[pos].nfs_argop4_u.oplookup.objname.utf8string_val);
    ops[pos].nfs_argop4_u.oplookup.objname.utf8string_len = 0;
    ops[pos].nfs_argop4_u.oplookup.objname.utf8string_val = nullptr;
  }

  void setup_putfh(int pos, struct fsal_obj_handle *entry) {
    bool fhres;

    gsh_free(ops[pos].nfs_argop4_u.opputfh.object.nfs_fh4_val);

    ops[pos].argop = NFS4_OP_PUTFH;

    /* Convert root_obj to a file handle in the args */
    fhres = nfs4_FSALToFhandle(true, &ops[pos].nfs_argop4_u.opputfh.object,
                               entry, op_ctx->ctx_export);
    EXPECT_EQ(fhres, true);
  }

  void cleanup_putfh(int pos) {
    gsh_free(ops[pos].nfs_argop4_u.opputfh.object.nfs_fh4_val);
    ops[pos].nfs_argop4_u.opputfh.object.nfs_fh4_len = 0;
    ops[pos].nfs_argop4_u.opputfh.object.nfs_fh4_val = nullptr;
  }

  void setup_rename(int pos, const char *oldname, const char *newname) {
    gsh_free(ops[pos].nfs_argop4_u.oprename.oldname.utf8string_val);
    gsh_free(ops[pos].nfs_argop4_u.oprename.newname.utf8string_val);
    ops[pos].argop = NFS4_OP_RENAME;
    ops[pos].nfs_argop4_u.oprename.oldname.utf8string_len = strlen(oldname);
    ops[pos].nfs_argop4_u.oprename.oldname.utf8string_val = gsh_strdup(oldname);
    ops[pos].nfs_argop4_u.oprename.newname.utf8string_len = strlen(newname);
    ops[pos].nfs_argop4_u.oprename.newname.utf8string_val = gsh_strdup(newname);
  }

  void swap_rename(int pos) {
    component4 temp = ops[pos].nfs_argop4_u.oprename.newname;
    ops[pos].nfs_argop4_u.oprename.newname =
        ops[pos].nfs_argop4_u.oprename.oldname;
    ops[pos].nfs_argop4_u.oprename.oldname = temp;
  }

  void cleanup_rename(int pos) {
    gsh_free(ops[pos].nfs_argop4_u.oprename.oldname.utf8string_val);
    gsh_free(ops[pos].nfs_argop4_u.oprename.newname.utf8string_val);
    ops[pos].nfs_argop4_u.oprename.oldname.utf8string_len = 0;
    ops[pos].nfs_argop4_u.oprename.oldname.utf8string_val = nullptr;
    ops[pos].nfs_argop4_u.oprename.newname.utf8string_len = 0;
    ops[pos].nfs_argop4_u.oprename.newname.utf8string_val = nullptr;
  }

  void setup_link(int pos, const char *newname) {
    gsh_free(ops[pos].nfs_argop4_u.oplink.newname.utf8string_val);
    ops[pos].argop = NFS4_OP_LINK;
    ops[pos].nfs_argop4_u.oplink.newname.utf8string_len = strlen(newname);
    ops[pos].nfs_argop4_u.oplink.newname.utf8string_val = gsh_strdup(newname);
  }

  void setup_remove(int pos, const char *name) {
    gsh_free(ops[pos].nfs_argop4_u.opremove.target.utf8string_val);
    ops[pos].argop = NFS4_OP_REMOVE;
    ops[pos].nfs_argop4_u.opremove.target.utf8string_len = strlen(name);
    ops[pos].nfs_argop4_u.opremove.target.utf8string_val = gsh_strdup(name);
  }

  void setup_open(int pos, const char *name, clientid4 clientid) {
    op_ctx->export_perms->options = EXPORT_OPTION_ACCESS_MASK;
    ops[pos].argop = NFS4_OP_OPEN;
    ops[pos].nfs_argop4_u.opopen.share_access = OPEN4_SHARE_ACCESS_BOTH;
    ops[pos].nfs_argop4_u.opopen.openhow.opentype = OPEN4_CREATE;
    ops[pos].nfs_argop4_u.opopen.openhow.openflag4_u.how.mode = GUARDED4;
    ops[pos].nfs_argop4_u.opopen.owner.clientid = clientid;
    ops[pos].nfs_argop4_u.opopen.owner.owner.owner_val = "foo";
    ops[pos].nfs_argop4_u.opopen.owner.owner.owner_len = 3;
    ops[pos].nfs_argop4_u.opopen.claim.claim = CLAIM_NULL;
    ops[pos].nfs_argop4_u.opopen.claim.open_claim4_u.file.utf8string_val =
        gsh_strdup(name);
    ops[pos].nfs_argop4_u.opopen.claim.open_claim4_u.file.utf8string_len =
        strlen(name);
    setattr(&ops[pos]
                 .nfs_argop4_u.opopen.openhow.openflag4_u.how.createhow4_u
                 .createattrs);
  }

  void setup_write(int pos, const char *data) {
    ops[pos].argop = NFS4_OP_WRITE;
    // ops[pos].nfs_argop4_u.opwrite.stateid;
    ops[pos].nfs_argop4_u.opwrite.offset = 0;
    ops[pos].nfs_argop4_u.opwrite.stable = DATA_SYNC4;
    ops[pos].nfs_argop4_u.opwrite.data.data_len = strlen(data);
    ops[pos].nfs_argop4_u.opwrite.data.data_val = gsh_strdup(data);
  }

  void setattr(struct fattr4 *Fattr) {
    struct xdr_attrs_args args;
    struct attrlist attrs;
    XDR attr_body;
    fattr_xdr_result xdr_res;

    memset(&attr_body, 0, sizeof(attr_body));
    memset(&args, 0, sizeof(args));
    memset(&attrs, 0, sizeof(attrs));

    attrs.mode = 0777; /* XXX */
    attrs.owner = 0;
    attrs.group = 0;
    args.attrs = &attrs;
    args.data = NULL;

    set_attribute_in_bitmap(&Fattr->attrmask, FATTR4_OWNER);
    set_attribute_in_bitmap(&Fattr->attrmask, FATTR4_OWNER_GROUP);
    set_attribute_in_bitmap(&Fattr->attrmask, FATTR4_MODE);

    Fattr->attr_vals.attrlist4_val = (char *)gsh_malloc(NFS4_ATTRVALS_BUFFLEN);
    xdrmem_create(&attr_body, Fattr->attr_vals.attrlist4_val,
                  NFS4_ATTRVALS_BUFFLEN, XDR_ENCODE);
    int attribute_to_set = 0;
    u_int LastOffset;
    for (attribute_to_set = next_attr_from_bitmap(&Fattr->attrmask, -1);
         attribute_to_set != -1; attribute_to_set = next_attr_from_bitmap(
                                     &Fattr->attrmask, attribute_to_set)) {
      /*if (attribute_to_set > max_attr_idx)
              break;	[> skip out of bounds <]*/

      xdr_res = fattr4tab[attribute_to_set].encode(&attr_body, &args);
      if (xdr_res == FATTR_XDR_SUCCESS) {
        bool res = set_attribute_in_bitmap(&Fattr->attrmask, attribute_to_set);
        assert(res);
        LogFullDebug(COMPONENT_NFS_V4, "Encoded attr %d, name = %s",
                     attribute_to_set, fattr4tab[attribute_to_set].name);
      } else if (xdr_res == FATTR_XDR_NOOP) {
        LogFullDebug(COMPONENT_NFS_V4, "Attr not supported %d name=%s",
                     attribute_to_set, fattr4tab[attribute_to_set].name);
        continue;
      } else {
        LogEvent(COMPONENT_NFS_V4, "Encode FAILED for attr %d, name = %s",
                 attribute_to_set, fattr4tab[attribute_to_set].name);

        /* signal fail so if(LastOffset > 0) works right */
        assert(false);
      }
      /* mark the attribute in the bitmap should be new bitmap btw */
    }
    LastOffset = xdr_getpos(&attr_body); /* dumb but for now */
    xdr_destroy(&attr_body);

    if (LastOffset == 0) { /* no supported attrs so we can free */
      assert(Fattr->attrmask.bitmap4_len == 0);
      gsh_free(Fattr->attr_vals.attrlist4_val);
      Fattr->attr_vals.attrlist4_val = NULL;
    }
    Fattr->attr_vals.attrlist4_len = LastOffset;
  }

  void setup_create(int pos, const char *name) {
    gsh_free(ops[pos].nfs_argop4_u.opcreate.objname.utf8string_val);
    ops[pos].argop = NFS4_OP_CREATE;
    ops[pos].nfs_argop4_u.opcreate.objtype.type = NF4DIR;

    setattr(&ops[pos].nfs_argop4_u.opcreate.createattrs);
    ops[pos].nfs_argop4_u.opcreate.objname.utf8string_len = strlen(name);
    ops[pos].nfs_argop4_u.opcreate.objname.utf8string_val = gsh_strdup(name);
  }

  void cleanup_remove(int pos) {
    gsh_free(ops[pos].nfs_argop4_u.opremove.target.utf8string_val);
    ops[pos].nfs_argop4_u.opremove.target.utf8string_len = 0;
    ops[pos].nfs_argop4_u.opremove.target.utf8string_val = nullptr;
  }

  void cleanup_link(int pos) {
    gsh_free(ops[pos].nfs_argop4_u.oplink.newname.utf8string_val);
    ops[pos].nfs_argop4_u.oplink.newname.utf8string_len = 0;
    ops[pos].nfs_argop4_u.oplink.newname.utf8string_val = nullptr;
  }

  void cleanup_create(int pos) {
    gsh_free(ops[pos].nfs_argop4_u.opcreate.objname.utf8string_val);
    gsh_free(
        ops[pos].nfs_argop4_u.opcreate.createattrs.attr_vals.attrlist4_val);
    ops[pos].nfs_argop4_u.opcreate.objname.utf8string_len = 0;
    ops[pos].nfs_argop4_u.opcreate.createattrs.attrmask.bitmap4_len = 0;
    ops[pos].nfs_argop4_u.opcreate.createattrs.attr_vals.attrlist4_val =
        nullptr;
    ops[pos].nfs_argop4_u.opcreate.createattrs.attr_vals.attrlist4_len = 0;
    ops[pos].nfs_argop4_u.opcreate.objname.utf8string_val = nullptr;
  }

  struct compound_data data;
  struct nfs_argop4 *ops;
  nfs_arg_t arg;
  struct nfs_resop4 resp;
};
} // namespace gtest

#endif /* GTEST_GTEST_NFS4_HH */
