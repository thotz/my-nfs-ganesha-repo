/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *              nfs_fh4 *    Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * ---------------------------------------
 */

/**
 * @file    nfs_proto_tools.c
 * @brief   A set of functions used to managed NFS.
 *
 * A set of functions used to managed NFS.
 */
#include "log.h"
#include "fsal.h"
#include "cache_inode.h"
#include "fsal_convert.h"
#include "nfs_core.h"
#include "nfs_convert.h"
#include "nfs_exports.h"
#include "nfs_proto_tools.h"
#include "idmapper.h"
#include "export_mgr.h"

/* Define mapping of NFS4 who name and type. */
static struct {
	char *string;
	int stringlen;
	int type;
} whostr_2_type_map[] = {
	{
	.string = "OWNER@",
	.stringlen = sizeof("OWNER@") - 1,
	.type = FSAL_ACE_SPECIAL_OWNER,
	},
	{
	.string = "GROUP@",
	.stringlen = sizeof("GROUP@") - 1,
	.type = FSAL_ACE_SPECIAL_GROUP,
	},
	{
	.string = "EVERYONE@",
	.stringlen = sizeof("EVERYONE@") - 1,
	.type = FSAL_ACE_SPECIAL_EVERYONE,
	},
};

/**
 * Converts FSAL Attributes to NFSv3 PostOp Attributes structure.
 *
 * This function converts FSAL Attributes to NFSv3 PostOp Attributes
 * structure.
 *
 * @param[in]  entry Cache entry
 * @param[out] attr  NFSv3 PostOp structure attributes.
 *
 */
void nfs_SetPostOpAttr(cache_entry_t *entry, post_op_attr *attr)
{
	attr->attributes_follow =
	    cache_entry_to_nfs3_Fattr(entry,
				      &(attr->post_op_attr_u.attributes));
}				/* nfs_SetPostOpAttr */

/**
 * @brief Converts FSAL Attributes to NFSv3 PreOp Attributes structure.
 *
 * This function Converts FSAL Attributes to NFSv3 PreOp Attributes
 * structure.
 *
 * @param[in]  entry Cache entry
 * @param[out] attr  NFSv3 PreOp structure attributes.
 */

void nfs_SetPreOpAttr(cache_entry_t *entry, pre_op_attr *attr)
{
	if ((entry == NULL) || (cache_inode_lock_trust_attrs(entry, false)
				!= CACHE_INODE_SUCCESS)) {
		attr->attributes_follow = false;
	} else {
		attr->pre_op_attr_u.attributes.size =
		    entry->obj_handle->attributes.filesize;
		attr->pre_op_attr_u.attributes.mtime.tv_sec =
		    entry->obj_handle->attributes.mtime.tv_sec;
		attr->pre_op_attr_u.attributes.mtime.tv_nsec =
		    entry->obj_handle->attributes.mtime.tv_nsec;
		attr->pre_op_attr_u.attributes.ctime.tv_sec =
		    entry->obj_handle->attributes.ctime.tv_sec;
		attr->pre_op_attr_u.attributes.ctime.tv_nsec =
		    entry->obj_handle->attributes.ctime.tv_nsec;
		attr->attributes_follow = TRUE;
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	}
}				/* nfs_SetPreOpAttr */

/**
 * @brief Set NFSv3 Weak Cache Coherency structure
 *
 * This function sets NFSv3 Weak Cache Coherency structure.
 *
 * @param[in]  before_attr Pre-op attrs for before state
 * @param[in]  entry       The cache entry after operation
 * @param[out] wcc_data    the Weak Cache Coherency structure
 *
 */
void nfs_SetWccData(const struct pre_op_attr *before_attr,
		    cache_entry_t *entry,
		    wcc_data *wcc_data)
{
	if (before_attr == NULL)
		wcc_data->before.attributes_follow = false;

	/* Build directory post operation attributes */
	nfs_SetPostOpAttr(entry, &(wcc_data->after));
}				/* nfs_SetWccData */

/**
 * @brief Indicate if an error is retryable
 *
 * This function indicates if an error is retryable or not.
 *
 * @param cache_status [IN] input Cache Inode Status value, to be tested.
 *
 * @return true if retryable, false otherwise.
 *
 * @todo: Not implemented for NOW BUGAZEOMEU
 *
 */
bool nfs_RetryableError(cache_inode_status_t cache_status)
{
	switch (cache_status) {
	case CACHE_INODE_IO_ERROR:
		if (nfs_param.core_param.drop_io_errors) {
			/* Drop the request */
			return true;
		} else {
			/* Propagate error to the client */
			return false;
		}
		break;

	case CACHE_INODE_INVALID_ARGUMENT:
		if (nfs_param.core_param.drop_inval_errors) {
			/* Drop the request */
			return true;
		} else {
			/* Propagate error to the client */
			return false;
		}
		break;

	case CACHE_INODE_DELAY:
		if (nfs_param.core_param.drop_delay_errors) {
			/* Drop the request */
			return true;
		} else {
			/* Propagate error to the client */
			return false;
		}
		break;

	case CACHE_INODE_SUCCESS:
		LogCrit(COMPONENT_NFSPROTO,
			"Possible implementation error: CACHE_INODE_SUCCESS managed as an error");
		return false;
		break;

	case CACHE_INODE_MALLOC_ERROR:
	case CACHE_INODE_POOL_MUTEX_INIT_ERROR:
	case CACHE_INODE_GET_NEW_LRU_ENTRY:
	case CACHE_INODE_INIT_ENTRY_FAILED:
	case CACHE_INODE_FSAL_ERROR:
	case CACHE_INODE_LRU_ERROR:
	case CACHE_INODE_HASH_SET_ERROR:
	case CACHE_INODE_INCONSISTENT_ENTRY:
	case CACHE_INODE_HASH_TABLE_ERROR:
	case CACHE_INODE_INSERT_ERROR:
		/* Internal error, should be dropped and retryed */
		return true;
		break;

	case CACHE_INODE_NOT_A_DIRECTORY:
	case CACHE_INODE_BAD_TYPE:
	case CACHE_INODE_ENTRY_EXISTS:
	case CACHE_INODE_DIR_NOT_EMPTY:
	case CACHE_INODE_NOT_FOUND:
	case CACHE_INODE_FSAL_EACCESS:
	case CACHE_INODE_IS_A_DIRECTORY:
	case CACHE_INODE_FSAL_EPERM:
	case CACHE_INODE_NO_SPACE_LEFT:
	case CACHE_INODE_READ_ONLY_FS:
	case CACHE_INODE_ESTALE:
	case CACHE_INODE_FSAL_ERR_SEC:
	case CACHE_INODE_QUOTA_EXCEEDED:
	case CACHE_INODE_NOT_SUPPORTED:
	case CACHE_INODE_UNION_NOTSUPP:
	case CACHE_INODE_NAME_TOO_LONG:
	case CACHE_INODE_STATE_CONFLICT:
	case CACHE_INODE_ASYNC_POST_ERROR:
	case CACHE_INODE_STATE_ERROR:
	case CACHE_INODE_BAD_COOKIE:
	case CACHE_INODE_FILE_BIG:
	case CACHE_INODE_FILE_OPEN:
	case CACHE_INODE_FSAL_XDEV:
	case CACHE_INODE_FSAL_MLINK:
	case CACHE_INODE_TOOSMALL:
	case CACHE_INODE_FSAL_SHARE_DENIED:
	case CACHE_INODE_SERVERFAULT:
	case CACHE_INODE_BADNAME:
	case CACHE_INODE_CROSS_JUNCTION:
	case CACHE_INODE_IN_GRACE:
	case CACHE_INODE_BADHANDLE:
		/* Non retryable error, return error to client */
		return false;
		break;
	}

	/* Should never reach this */
	LogDebug(COMPONENT_NFSPROTO,
		 "cache_inode_status=%u not managed properly in nfs_RetryableError, line %u should never be reached",
		 cache_status, __LINE__);
	return false;
}
/**
 * @brief Returns the maximun attribute index possbile for a 4.x protocol.
 *
 * This function returns the maximun attribute index possbile for a
 * 4.x protocol.
 * returns -1 if the protocol is not recognized.
 *
 * @param minorversion[IN] input minorversion of the protocol.
 *
 * @return the max index possbile for the minorversion, -1 if minorversion is
 * not recognized.
 *
 */

static inline int nfs4_max_attr_index(compound_data_t *data)
{
	if (data) {
		enum nfs4_minor_vers minorversion = data->minorversion;
		switch (minorversion) {
		case NFS4_MINOR_VERS_0:
			return FATTR4_MOUNTED_ON_FILEID;
		case NFS4_MINOR_VERS_1:
			return FATTR4_FS_CHARSET_CAP;
		case NFS4_MINOR_VERS_2:
			return FATTR4_SEC_LABEL;
		}
	} else {
		return FATTR4_SEC_LABEL;
	}
	/* Should never be here */
	LogFatal(COMPONENT_NFS_V4, "Unexpected minor version for NFSv4");
	return -1;
}


/* NFSv4.0+ Attribute management
 * XDR encode/decode/compare functions for FSAL <-> Fattr4 translations
 * There is a set of functions for each and every attribute in the tables
 * on page 39-46 of RFC3530.  The translate between internal and the wire.
 * Ordered by attribute number
 */

/*
 * FATTR4_SUPPORTED_ATTRS
 */

/** supported attributes
 *  this drives off of the table but it really should drive off of the
 *  fs_supports in the export
 */

static fattr_xdr_result encode_supported_attrs(XDR *xdr,
					       struct xdr_attrs_args *args)
{
	struct bitmap4 bits;
	int attr, offset;
	int max_attr_idx;

	memset(&bits, 0, sizeof(bits));
	max_attr_idx = nfs4_max_attr_index(args->data);

	for (attr = FATTR4_SUPPORTED_ATTRS; attr <= max_attr_idx;
	     attr++) {
		if (fattr4tab[attr].supported) {
			bool res = set_attribute_in_bitmap(&bits, attr);
			assert(res);
		}
	}
	if (!inline_xdr_u_int32_t(xdr, &bits.bitmap4_len))
		return FATTR_XDR_FAILED;
	for (offset = 0; offset < bits.bitmap4_len; offset++) {
		if (!inline_xdr_u_int32_t(xdr, &bits.map[offset]))
			return FATTR_XDR_FAILED;
	}
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_supported_attrs(XDR *xdr,
					       struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_TYPE
 */

static fattr_xdr_result encode_type(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t file_type;

	switch (args->attrs->type) {
	case REGULAR_FILE:
	case EXTENDED_ATTR:
		file_type = NF4REG;	/* Regular file */
		break;
	case DIRECTORY:
		file_type = NF4DIR;	/* Directory */
		break;
	case BLOCK_FILE:
		file_type = NF4BLK;	/* Special File - block device */
		break;
	case CHARACTER_FILE:
		file_type = NF4CHR;	/* Special File - character device */
		break;
	case SYMBOLIC_LINK:
		file_type = NF4LNK;	/* Symbolic Link */
		break;
	case SOCKET_FILE:
		file_type = NF4SOCK;	/* Special File - socket */
		break;
	case FIFO_FILE:
		file_type = NF4FIFO;	/* Special File - fifo */
		break;
	default:		/* includes NO_FILE_TYPE & FS_JUNCTION: */
		return FATTR_XDR_FAILED;	/* silently skip bogus? */
	}			/* switch( pattr->type ) */
	if (!xdr_u_int32_t(xdr, &file_type))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_type(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t t;

	if (!xdr_u_int32_t(xdr, &t))
		return FATTR_XDR_FAILED;
	switch (t) {
	case NF4REG:
		args->attrs->type = REGULAR_FILE;
		break;
	case NF4DIR:
		args->attrs->type = DIRECTORY;
		break;
	case NF4BLK:
		args->attrs->type = BLOCK_FILE;
		break;
	case NF4CHR:
		args->attrs->type = CHARACTER_FILE;
		break;
	case NF4LNK:
		args->attrs->type = SYMBOLIC_LINK;
		break;
	case NF4SOCK:
		args->attrs->type = SOCKET_FILE;
		break;
	case NF4FIFO:
		args->attrs->type = FIFO_FILE;
		break;
	default:
		/* For wanting of a better solution */
		return FATTR_XDR_FAILED;
	}
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_FH_EXPIRE_TYPE
 */

static fattr_xdr_result encode_expiretype(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	uint32_t expire_type;

	/* For the moment, we handle only the persistent filehandle */
	expire_type = FH4_PERSISTENT;
	if (!xdr_u_int32_t(xdr, &expire_type))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_expiretype(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_CHANGE
 */

static fattr_xdr_result encode_change(XDR *xdr, struct xdr_attrs_args *args)
{
	if (!xdr_u_int64_t(xdr, &args->attrs->change))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_change(XDR *xdr, struct xdr_attrs_args *args)
{
	uint64_t change;

	if (!xdr_u_int64_t(xdr, &change))
		return FATTR_XDR_FAILED;
	args->attrs->chgtime.tv_sec = (uint32_t) change;
	args->attrs->chgtime.tv_nsec = 0;
	args->attrs->change = change;
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_SIZE
 */

static fattr_xdr_result encode_filesize(XDR *xdr, struct xdr_attrs_args *args)
{
	if (!xdr_u_int64_t(xdr, &args->attrs->filesize))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_filesize(XDR *xdr, struct xdr_attrs_args *args)
{
	if (!xdr_u_int64_t(xdr, &args->attrs->filesize))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_LINK_SUPPORT
 */

static fattr_xdr_result encode_linksupport(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	int linksupport = FALSE;

	if (args->data != NULL) {
		export = op_ctx->fsal_export;
		linksupport =
		    export->exp_ops.fs_supports(export, fso_link_support);
	}
	if (!xdr_bool(xdr, &linksupport))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_linksupport(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_SYMLINK_SUPPORT
 */

static fattr_xdr_result encode_symlinksupport(XDR *xdr,
					      struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	int symlinksupport = FALSE;

	if (args->data != NULL) {
		export = op_ctx->fsal_export;
		symlinksupport =
		    export->exp_ops.fs_supports(export, fso_symlink_support);
	}
	if (!xdr_bool(xdr, &symlinksupport))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_symlinksupport(XDR *xdr,
					      struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_NAMED_ATTR
 */

/* For this version of the binary, named attributes is not supported */

static fattr_xdr_result encode_namedattrsupport(XDR *xdr,
						struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	int namedattrsupport = FALSE;

	if (args->data != NULL) {
		export = op_ctx->fsal_export;
		namedattrsupport =
		    export->exp_ops.fs_supports(export, fso_named_attr);
	}
	if (!xdr_bool(xdr, &namedattrsupport))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_namedattrsupport(XDR *xdr,
						struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_FSID
 */

static fattr_xdr_result encode_fsid(XDR *xdr, struct xdr_attrs_args *args)
{
	fsid4 fsid = {0, 0};

	if (args->data != NULL &&
	    (op_ctx->export->options_set &
	     EXPORT_OPTION_FSID_SET) != 0) {
		fsid.major = op_ctx->export->filesystem_id.major;
		fsid.minor = op_ctx->export->filesystem_id.minor;
	} else {
		fsid.major = args->attrs->fsid.major;
		fsid.minor = args->attrs->fsid.minor;
	}

	if (!xdr_u_int64_t(xdr, &fsid.major))
		return FATTR_XDR_FAILED;
	if (!xdr_u_int64_t(xdr, &fsid.minor))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_fsid(XDR *xdr, struct xdr_attrs_args *args)
{
	if (!xdr_u_int64_t(xdr, &args->attrs->fsid.major))
		return FATTR_XDR_FAILED;
	if (!xdr_u_int64_t(xdr, &args->attrs->fsid.minor))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_UNIQUE_HANDLES
 */

static fattr_xdr_result encode_uniquehandles(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	int uniquehandles = FALSE;

	if (args->data != NULL) {
		export = op_ctx->fsal_export;
		uniquehandles =
		    export->exp_ops.fs_supports(export, fso_unique_handles);
	}
	if (!inline_xdr_bool(xdr, &uniquehandles))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_uniquehandles(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_LEASE_TIME
 */

static fattr_xdr_result encode_leaselife(XDR *xdr, struct xdr_attrs_args *args)
{
	if (!inline_xdr_u_int32_t(xdr, &nfs_param.nfsv4_param.lease_lifetime))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_leaselife(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_RDATTR_ERROR
 */

/** todo we don't really do anything with rdattr_error.  It is needed for full
 * readdir error handling.  Check this to be correct when we do...
 */

static fattr_xdr_result encode_rdattr_error(XDR *xdr,
					    struct xdr_attrs_args *args)
{
	if (!inline_xdr_u_int32_t(xdr, &args->rdattr_error))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_rdattr_error(XDR *xdr,
					    struct xdr_attrs_args *args)
{
	if (!inline_xdr_u_int32_t(xdr, &args->rdattr_error))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_ACL
 */

static fattr_xdr_result encode_acl(XDR *xdr, struct xdr_attrs_args *args)
{
	if (args->attrs->acl) {
		fsal_ace_t *ace;
		int i;
		char *name = NULL;

		LogFullDebug(COMPONENT_NFS_V4, "Number of ACEs = %u",
			     args->attrs->acl->naces);

		if (!inline_xdr_u_int32_t(xdr, &(args->attrs->acl->naces)))
			return FATTR_XDR_FAILED;
		for (ace = args->attrs->acl->aces;
		     ace < args->attrs->acl->aces + args->attrs->acl->naces;
		     ace++) {
			LogFullDebug(COMPONENT_NFS_V4,
				     "type=0X%x, flag=0X%x, perm=0X%x",
				     ace->type, ace->flag, ace->perm);
			if (!inline_xdr_u_int32_t(xdr, &ace->type))
				return FATTR_XDR_FAILED;
			if (!inline_xdr_u_int32_t(xdr, &ace->flag))
				return FATTR_XDR_FAILED;
			if (!inline_xdr_u_int32_t(xdr, &ace->perm))
				return FATTR_XDR_FAILED;
			if (IS_FSAL_ACE_SPECIAL_ID(*ace)) {
				for (i = 0;
				     i < FSAL_ACE_SPECIAL_EVERYONE;
				     i++) {
					if (whostr_2_type_map[i].type ==
					    ace->who.uid) {
						name = whostr_2_type_map[i]
							.string;
						break;
					}
				}
				if (name == NULL ||
				    !xdr_string(xdr, &name, MAXNAMLEN))
					return FATTR_XDR_FAILED;
			} else if (IS_FSAL_ACE_GROUP_ID(*ace)) {
				/* Encode group name. */
				if (!xdr_encode_nfs4_group(xdr, ace->who.gid))
					return FATTR_XDR_FAILED;
			} else {
				if (!xdr_encode_nfs4_owner
				    (xdr, ace->who.uid)) {
					return FATTR_XDR_FAILED;
				}
			}
		}		/* for ace... */
	} else {
		uint32_t noacls = 0;
		if (!inline_xdr_u_int32_t(xdr, &noacls))
			return FATTR_XDR_FAILED;
	}
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_acl(XDR *xdr, struct xdr_attrs_args *args)
{
	fsal_acl_status_t status;
	fsal_acl_data_t acldata;
	fsal_ace_t *ace;
	char buffer[MAXNAMLEN + 1];
	char *buffp = buffer;
	utf8string utf8buffer;
	int who = 0;		/* not ACE_SPECIAL anything */

	acldata.naces = 0;

	if (!inline_xdr_u_int32_t(xdr, &acldata.naces))
		return FATTR_XDR_FAILED;
	if (acldata.naces == 0)
		return FATTR_XDR_SUCCESS;	/* no acls is not a crime */
	acldata.aces = (fsal_ace_t *) nfs4_ace_alloc(acldata.naces);
	if (acldata.aces == NULL) {
		LogCrit(COMPONENT_NFS_V4, "Failed to allocate ACEs");
		args->nfs_status = NFS4ERR_SERVERFAULT;
		return FATTR_XDR_FAILED;
	}
	memset(acldata.aces, 0, acldata.naces * sizeof(fsal_ace_t));
	for (ace = acldata.aces; ace < acldata.aces + acldata.naces; ace++) {
		int i;
		who = 0;

		if (!inline_xdr_u_int32_t(xdr, &ace->type))
			goto baderr;
		if (!inline_xdr_u_int32_t(xdr, &ace->flag))
			goto baderr;
		if (!inline_xdr_u_int32_t(xdr, &ace->perm))
			goto baderr;
		if (!inline_xdr_string(xdr, &buffp, MAXNAMLEN))
			goto baderr;
		for (i = 0; i < FSAL_ACE_SPECIAL_EVERYONE; i++) {
			if (strncmp
			    (buffer, whostr_2_type_map[i].string,
			     strlen(buffer)) == 0) {
				who = whostr_2_type_map[i].type;
				break;
			}
		}
		if (who != 0) {
			/* Clear group flag for special users */
			ace->flag &= ~(FSAL_ACE_FLAG_GROUP_ID);
			ace->iflag |= FSAL_ACE_IFLAG_SPECIAL_ID;
			ace->who.uid = who;
			LogFullDebug(COMPONENT_NFS_V4,
				     "ACE special who.uid = 0x%x",
				     ace->who.uid);
		} else {
			utf8buffer.utf8string_val = buffer;
			utf8buffer.utf8string_len = strlen(buffer);
			if (IS_FSAL_ACE_GROUP_ID(*ace)) {
				/* Decode group. */
				struct gsh_buffdesc gname = {
					.addr = utf8buffer.utf8string_val,
					.len = utf8buffer.utf8string_len
				};
				if (!name2gid(
					&gname,
					&ace->who.gid,
					args->data
					? op_ctx->export
					  ->export_perms .anonymous_gid
					: -1))
					goto baderr;

				LogFullDebug(COMPONENT_NFS_V4,
					     "ACE who.gid = 0x%x",
					     ace->who.gid);
			} else {	/* Decode user. */
				struct gsh_buffdesc uname = {
					.addr = utf8buffer.utf8string_val,
					.len = utf8buffer.utf8string_len
				};
				if (!name2uid(
					&uname,
					&ace->who.uid,
					args->data
					? op_ctx->export
					  ->export_perms .anonymous_uid
					: -1))
					goto baderr;

				LogFullDebug(COMPONENT_NFS_V4,
					     "ACE who.uid = 0x%x",
					     ace->who.uid);
			}
		}

		/* Check if we can map a name string to uid or gid. If we
		 * can't, do cleanup and bubble up NFS4ERR_BADOWNER.
		 */
		if ((IS_FSAL_ACE_GROUP_ID(*ace) ?
		     ace->who.gid : ace->who.uid) == -1) {
			LogFullDebug(COMPONENT_NFS_V4, "ACE bad owner");
			args->nfs_status = NFS4ERR_BADOWNER;
			goto baderr;
		}
	}
	args->attrs->acl = nfs4_acl_new_entry(&acldata, &status);
	if (args->attrs->acl == NULL) {
		LogCrit(COMPONENT_NFS_V4,
			"Failed to create a new entry for ACL");
		args->nfs_status = NFS4ERR_SERVERFAULT;
		/* acldata has already been freed */
		return FATTR_XDR_FAILED;
	} else {
		LogFullDebug(COMPONENT_NFS_V4,
			     "Successfully created a new entry for ACL, status = %u",
			     status);
	}
	/* Set new ACL */
	LogFullDebug(COMPONENT_NFS_V4, "new acl = %p", args->attrs->acl);
	return FATTR_XDR_SUCCESS;

 baderr:
	nfs4_ace_free(acldata.aces);
	return FATTR_XDR_FAILED;
}

/*
 * FATTR4_ACLSUPPORT
 */

static fattr_xdr_result encode_aclsupport(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t aclsupport = 0;

	if (args->data != NULL) {
		export = op_ctx->fsal_export;
		aclsupport = export->exp_ops.fs_acl_support(export);
	}
	if (!inline_xdr_u_int32_t(xdr, &aclsupport))
		return FATTR_XDR_FAILED;

	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_aclsupport(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_ARCHIVE
 */

static fattr_xdr_result encode_archive(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t archive;

	archive = FALSE;
	if (!inline_xdr_bool(xdr, &archive))
		return FATTR_XDR_FAILED;

	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_archive(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_CANSETTIME
 */

static fattr_xdr_result encode_cansettime(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t cansettime = FALSE;

	if (args->data != NULL) {
		export = op_ctx->fsal_export;
		cansettime = export->exp_ops.
				fs_supports(export, fso_cansettime);
	}
	if (!inline_xdr_bool(xdr, &cansettime))
		return FATTR_XDR_FAILED;

	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_cansettime(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_CASE_INSENSITIVE
 */

static fattr_xdr_result encode_case_insensitive(XDR *xdr,
						struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t caseinsensitive = FALSE;

	if (args->data != NULL) {
		export = op_ctx->fsal_export;
		caseinsensitive =
		    export->exp_ops.fs_supports(export, fso_case_insensitive);
	}
	if (!inline_xdr_bool(xdr, &caseinsensitive))
		return FATTR_XDR_FAILED;

	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_case_insensitive(XDR *xdr,
						struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_CASE_PRESERVING
 */

static fattr_xdr_result encode_case_preserving(XDR *xdr,
					       struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t casepreserving = FALSE;

	if (args->data != NULL) {
		export = op_ctx->fsal_export;
		casepreserving =
		    export->exp_ops.fs_supports(export, fso_case_preserving);
	}
	if (!inline_xdr_bool(xdr, &casepreserving))
		return FATTR_XDR_FAILED;

	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_case_preserving(XDR *xdr,
					       struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_CHOWN_RESTRICTED
 */

static fattr_xdr_result encode_chown_restricted(XDR *xdr,
						struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t chownrestricted = FALSE;

	if (args->data != NULL) {
		export = op_ctx->fsal_export;
		chownrestricted =
		    export->exp_ops.fs_supports(export, fso_chown_restricted);
	}
	if (!inline_xdr_bool(xdr, &chownrestricted))
		return FATTR_XDR_FAILED;

	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_chown_restricted(XDR *xdr,
						struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_FILEHANDLE
 */

static fattr_xdr_result encode_filehandle(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	if (args->hdl4 == NULL || args->hdl4->nfs_fh4_val == NULL)
		return FATTR_XDR_FAILED;
	if (!inline_xdr_bytes
	    (xdr, &args->hdl4->nfs_fh4_val, &args->hdl4->nfs_fh4_len,
	     NFS4_FHSIZE))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

/* zero copy file handle reference dropped as potentially unsafe XDR */

static fattr_xdr_result decode_filehandle(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	uint32_t fhlen = 0, pos;

	if (args->hdl4 == NULL || args->hdl4->nfs_fh4_val == NULL) {
		if (!inline_xdr_u_int32_t(xdr, &fhlen))
			return FATTR_XDR_FAILED;
		pos = xdr_getpos(xdr);
		if (!xdr_setpos(xdr, pos + fhlen))
			return FATTR_XDR_FAILED;
	} else {
		if (!inline_xdr_bytes
		    (xdr, &args->hdl4->nfs_fh4_val, &args->hdl4->nfs_fh4_len,
		     NFS4_FHSIZE))
			return FATTR_XDR_FAILED;
	}
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_FILEID
 */

static fattr_xdr_result encode_fileid(XDR *xdr, struct xdr_attrs_args *args)
{
	if (!inline_xdr_u_int64_t(xdr, &args->attrs->fileid))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_fileid(XDR *xdr, struct xdr_attrs_args *args)
{
	if (!inline_xdr_u_int64_t(xdr, &args->attrs->fileid))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

/*
 * Dynamic file system info
 */

static fattr_xdr_result encode_fetch_fsinfo(struct xdr_attrs_args *args)
{
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;

	if (args->data != NULL && args->data->current_entry != NULL) {
		cache_status =
		    cache_inode_statfs(args->data->current_entry,
				       args->dynamicinfo);
	} else {
		args->dynamicinfo->avail_files = 512;
		args->dynamicinfo->free_files = 512;
		args->dynamicinfo->total_files = 512;
		args->dynamicinfo->total_bytes = 1024000;
		args->dynamicinfo->free_bytes = 512000;
		args->dynamicinfo->avail_bytes = 512000;
	}
	if (cache_status == CACHE_INODE_SUCCESS) {
		args->statfscalled = true;
		return TRUE;
	} else {
		return FATTR_XDR_FAILED;
	}
}

/*
 * FATTR4_FILES_AVAIL
 */

static fattr_xdr_result encode_files_avail(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	if (!args->statfscalled)
		if (!encode_fetch_fsinfo(args))
			return FATTR_XDR_FAILED;
	if (!inline_xdr_u_int64_t(xdr, &args->dynamicinfo->avail_files))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_files_avail(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	return inline_xdr_u_int64_t(xdr,
				    &args->dynamicinfo->
				    avail_files) ? FATTR_XDR_SUCCESS :
	    FATTR_XDR_FAILED;
}

/*
 * FATTR4_FILES_FREE
 */

static fattr_xdr_result encode_files_free(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	if (!args->statfscalled)
		if (!encode_fetch_fsinfo(args))
			return FATTR_XDR_FAILED;
	if (!inline_xdr_u_int64_t(xdr, &args->dynamicinfo->free_files))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_files_free(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	return inline_xdr_u_int64_t(xdr,
				    &args->dynamicinfo->
				    free_files) ? FATTR_XDR_SUCCESS :
	    FATTR_XDR_FAILED;
}

/*
 * FATTR4_FILES_TOTAL
 */

static fattr_xdr_result encode_files_total(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	if (!args->statfscalled)
		if (!encode_fetch_fsinfo(args))
			return FATTR_XDR_FAILED;
	if (!inline_xdr_u_int64_t(xdr, &args->dynamicinfo->total_files))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_files_total(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	return xdr_u_int64_t(xdr,
			     &args->dynamicinfo->
			     total_files) ? FATTR_XDR_SUCCESS :
	    FATTR_XDR_FAILED;
}

/*
 * FATTR4_FS_LOCATIONS
 */

static fattr_xdr_result encode_fs_locations(XDR *xdr,
					    struct xdr_attrs_args *args)
{
	fsal_status_t st;
	fs_locations4 fs_locs;
	fs_location4 fs_loc;
	component4 fs_path;
	component4 fs_root;
	component4 fs_server;
	char root[MNTNAMLEN];
	char path[MNTNAMLEN];
	char server[MNTNAMLEN];

	if (args->data == NULL || args->data->current_entry == NULL)
		return FATTR_XDR_NOOP;

	if (args->data->current_entry->type != DIRECTORY)
		return FATTR_XDR_NOOP;

	fs_root.utf8string_len = MNTNAMLEN;
	fs_root.utf8string_val = root;
	fs_path.utf8string_len = MNTNAMLEN;
	fs_path.utf8string_val = path;
	fs_locs.fs_root.pathname4_len = 1;
	fs_locs.fs_root.pathname4_val = &fs_path;
	fs_server.utf8string_len = MNTNAMLEN;
	fs_server.utf8string_val = server;
	fs_loc.server.server_len = 1;
	fs_loc.server.server_val = &fs_server;
	fs_loc.rootpath.pathname4_len = 1;
	fs_loc.rootpath.pathname4_val = &fs_root;
	fs_locs.locations.locations_len = 1;
	fs_locs.locations.locations_val = &fs_loc;

	/* For now allow for one fs locations, fs_locations() should set:
	   root and update its length, can not be bigger than MNTNAMLEN
	   path and update its length, can not be bigger than MNTNAMLEN
	   server and update its length, can not be bigger than MNTNAMELEN
	*/
	st = args->data->current_entry->obj_handle->obj_ops.fs_locations(
					args->data->current_entry->obj_handle,
					&fs_locs);
	if (FSAL_IS_ERROR(st))
		return FATTR_XDR_NOOP;

	if (!xdr_fs_locations4(xdr, &fs_locs))
		return FATTR_XDR_FAILED;

	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_fs_locations(XDR *xdr,
					    struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_HIDDEN
 */

static fattr_xdr_result encode_hidden(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t hidden = FALSE;

	if (!inline_xdr_bool(xdr, &hidden))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_hidden(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_HOMOGENEOUS
 */

/* Unix semantic is homogeneous (all objects have the same kind of attributes)
 */

static fattr_xdr_result encode_homogeneous(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t homogeneous = FALSE;

	if (args->data != NULL) {
		export = op_ctx->fsal_export;
		homogeneous = export->exp_ops.
				fs_supports(export, fso_homogenous);
	}
	if (!inline_xdr_bool(xdr, &homogeneous))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_homogeneous(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MAXFILESIZE
 */

static fattr_xdr_result encode_maxfilesize(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint64_t maxfilesize = 0;

	if (args->data != NULL) {
		export = op_ctx->fsal_export;
		maxfilesize = export->exp_ops.fs_maxfilesize(export);
	}
	if (!inline_xdr_u_int64_t(xdr, &maxfilesize))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_maxfilesize(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MAXLINK
 */

static fattr_xdr_result encode_maxlink(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t maxlink = 0;

	if (args->data != NULL) {
		export = op_ctx->fsal_export;
		maxlink = export->exp_ops.fs_maxlink(export);
	}
	if (!inline_xdr_u_int32_t(xdr, &maxlink))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_maxlink(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MAXNAME
 */

static fattr_xdr_result encode_maxname(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t maxname = 0;

	if (args->data != NULL) {
		export = op_ctx->fsal_export;
		maxname = export->exp_ops.fs_maxnamelen(export);
	}
	if (!inline_xdr_u_int32_t(xdr, &maxname))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_maxname(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MAXREAD
 */

/* The exports.c MAXREAD-MAXWRITE code establishes these semantics:
 *  a. If you set the MaxWrite and MaxRead defaults in an export file
 *  they apply.
 *  b. If you set the MaxWrite and MaxRead defaults in the main.conf
 *  file they apply unless overwritten by an export file setting.
 *  c. If no settings are present in the export file or the main.conf
 *  file then the defaults values in the FSAL apply.
 */

/** todo make these conditionals go away.  The old code was the 'else' part of
 * this. this is a fast path. Do both read and write conditionals.
 */

static fattr_xdr_result encode_maxread(XDR *xdr, struct xdr_attrs_args *args)
{
	if (!inline_xdr_u_int64_t(xdr, &op_ctx->export->MaxRead))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_maxread(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MAXWRITE
 */

static fattr_xdr_result encode_maxwrite(XDR *xdr, struct xdr_attrs_args *args)
{
	if (!inline_xdr_u_int64_t(xdr, &op_ctx->export->MaxWrite))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_maxwrite(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MIMETYPE
 */

static fattr_xdr_result encode_mimetype(XDR *xdr, struct xdr_attrs_args *args)
{
	int mimetype = FALSE;

	if (!inline_xdr_bool(xdr, &mimetype))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_mimetype(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MODE
 */

static fattr_xdr_result encode_mode(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t file_mode = fsal2unix_mode(args->attrs->mode);

	if (!inline_xdr_u_int32_t(xdr, &file_mode))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_mode(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t file_mode = 0;

	if (!inline_xdr_u_int32_t(xdr, &file_mode))
		return FATTR_XDR_FAILED;
	args->attrs->mode = unix2fsal_mode(file_mode);
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_NO_TRUNC
 */

static fattr_xdr_result encode_no_trunc(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t no_trunc = FALSE;

	if (args->data != NULL) {
		export = op_ctx->fsal_export;
		no_trunc = export->exp_ops.fs_supports(export, fso_no_trunc);
	}
	if (!inline_xdr_bool(xdr, &no_trunc))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_no_trunc(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_NUMLINKS
 */

static fattr_xdr_result encode_numlinks(XDR *xdr, struct xdr_attrs_args *args)
{
	if (!inline_xdr_u_int32_t(xdr, &args->attrs->numlinks))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_numlinks(XDR *xdr, struct xdr_attrs_args *args)
{
	if (!inline_xdr_u_int32_t(xdr, &args->attrs->numlinks))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_OWNER
 */

static fattr_xdr_result encode_owner(XDR *xdr, struct xdr_attrs_args *args)
{
	return xdr_encode_nfs4_owner(xdr, args->attrs->owner) ?
		FATTR_XDR_SUCCESS : FATTR_XDR_FAILED;
}

static fattr_xdr_result decode_owner(XDR *xdr, struct xdr_attrs_args *args)
{
	uid_t uid;
	uint32_t len = 0;
	struct gsh_buffdesc ownerdesc;
	unsigned int pos, newpos;

	if (!inline_xdr_u_int(xdr, &len))
		return FATTR_XDR_FAILED;

	pos = xdr_getpos(xdr);
	newpos = pos + len;
	if (len % 4 != 0)
		newpos += (4 - (len % 4));

	ownerdesc.len = len;
	ownerdesc.addr = xdr_inline(xdr, len);

	if (!ownerdesc.addr) {
		LogMajor(COMPONENT_NFSPROTO,
			 "xdr_inline on xdrmem stream failed!");
		return FATTR_XDR_FAILED;
	}

	if (!name2uid(&ownerdesc,
		      &uid,
		      args->data ?
			op_ctx->export->export_perms.anonymous_uid
			: -1)) {
		return FATTR_BADOWNER;
	}

	xdr_setpos(xdr, newpos);
	args->attrs->owner = uid;
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_OWNER_GROUP
 */

static fattr_xdr_result encode_group(XDR *xdr, struct xdr_attrs_args *args)
{
	return xdr_encode_nfs4_group(xdr, args->attrs->group) ?
		FATTR_XDR_SUCCESS : FATTR_XDR_FAILED;
}

static fattr_xdr_result decode_group(XDR *xdr, struct xdr_attrs_args *args)
{
	gid_t gid;
	uint32_t len = 0;
	struct gsh_buffdesc groupdesc;
	unsigned int pos, newpos;

	if (!inline_xdr_u_int(xdr, &len))
		return FATTR_XDR_FAILED;

	pos = xdr_getpos(xdr);
	newpos = pos + len;
	if (len % 4 != 0)
		newpos += (4 - (len % 4));

	groupdesc.len = len;
	groupdesc.addr = xdr_inline(xdr, len);

	if (!groupdesc.addr) {
		LogMajor(COMPONENT_NFSPROTO,
			 "xdr_inline on xdrmem stream failed!");
		return FATTR_XDR_FAILED;
	}

	if (!name2gid(&groupdesc,
		      &gid,
		      args->data ?
			op_ctx->export->export_perms.anonymous_gid
			: -1))
		return FATTR_BADOWNER;

	xdr_setpos(xdr, newpos);
	args->attrs->group = gid;
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_QUOTA_AVAIL_HARD
 */

static fattr_xdr_result encode_quota_avail_hard(XDR *xdr,
						struct xdr_attrs_args *args)
{
/** @todo: not the right answer, actual quotas should be implemented */
	uint64_t quota = NFS_V4_MAX_QUOTA_HARD;

	if (!inline_xdr_u_int64_t(xdr, &quota))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_quota_avail_hard(XDR *xdr,
						struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_QUOTA_AVAIL_SOFT
 */

static fattr_xdr_result encode_quota_avail_soft(XDR *xdr,
						struct xdr_attrs_args *args)
{
	uint64_t quota = NFS_V4_MAX_QUOTA_SOFT;

	if (!inline_xdr_u_int64_t(xdr, &quota))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_quota_avail_soft(XDR *xdr,
						struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_QUOTA_USED
 */

static fattr_xdr_result encode_quota_used(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	uint64_t quota = args->attrs->filesize;

	if (!inline_xdr_u_int64_t(xdr, &quota))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_quota_used(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_RAWDEV
 */

static fattr_xdr_result encode_rawdev(XDR *xdr, struct xdr_attrs_args *args)
{
	struct specdata4 specdata4;

	specdata4.specdata1 = args->attrs->rawdev.major;
	specdata4.specdata2 = args->attrs->rawdev.minor;

	if (!inline_xdr_u_int64_t(xdr, (uint64_t *) &specdata4))
		return FATTR_XDR_FAILED;

	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_rawdev(XDR *xdr, struct xdr_attrs_args *args)
{
	struct specdata4 specdata4 = {.specdata1 = 0, .specdata2 = 0 };

	if (!inline_xdr_u_int64_t(xdr, (uint64_t *) &specdata4))
		return FATTR_XDR_FAILED;

	args->attrs->rawdev.major = specdata4.specdata1;
	args->attrs->rawdev.minor = specdata4.specdata2;
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_SPACE_AVAIL
 */

static fattr_xdr_result encode_sace_avail(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	if (!args->statfscalled)
		if (!encode_fetch_fsinfo(args))
			return FATTR_XDR_FAILED;
	if (!inline_xdr_u_int64_t(xdr, &args->dynamicinfo->avail_bytes))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_sace_avail(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	return inline_xdr_u_int64_t(xdr,
				    &args->dynamicinfo->
				    avail_bytes) ? FATTR_XDR_SUCCESS :
	    FATTR_XDR_FAILED;
}

/*
 * FATTR4_SPACE_FREE
 */

static fattr_xdr_result encode_sace_free(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	if (!args->statfscalled)
		if (!encode_fetch_fsinfo(args))
			return FATTR_XDR_FAILED;
	if (!inline_xdr_u_int64_t(xdr, &args->dynamicinfo->free_bytes))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_sace_free(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	return inline_xdr_u_int64_t(xdr,
				    &args->dynamicinfo->
				    free_bytes) ? FATTR_XDR_SUCCESS :
	    FATTR_XDR_FAILED;
}

/*
 * FATTR4_SPACE_TOTAL
 */

static fattr_xdr_result encode_sace_total(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	if (!args->statfscalled)
		if (!encode_fetch_fsinfo(args))
			return FATTR_XDR_FAILED;
	if (!inline_xdr_u_int64_t(xdr, &args->dynamicinfo->total_bytes))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_sace_total(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	return inline_xdr_u_int64_t(xdr,
				    &args->dynamicinfo->
				    total_bytes) ? FATTR_XDR_SUCCESS :
	    FATTR_XDR_FAILED;
}

/*
 * FATTR4_SPACE_USED
 */

/* the number of bytes on the filesystem used by the object
 * which is slightly different
 * from the file's size (there can be hole in the file)
 */

static fattr_xdr_result encode_spaceused(XDR *xdr, struct xdr_attrs_args *args)
{
	uint64_t sace = args->attrs->spaceused;

	if (!inline_xdr_u_int64_t(xdr, &sace))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_spaceused(XDR *xdr, struct xdr_attrs_args *args)
{
	uint64_t sace = 0;

	if (!inline_xdr_u_int64_t(xdr, &sace))
		return FATTR_XDR_FAILED;
	args->attrs->spaceused = sace;
	return TRUE;
}

/*
 * FATTR4_SYSTEM
 */

/* This is not a windows system File-System with respect to the regarding API */

static fattr_xdr_result encode_system(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t system = FALSE;

	if (!inline_xdr_bool(xdr, &system))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_system(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * Time conversions
 */

static inline fattr_xdr_result encode_time(XDR *xdr, struct timespec *ts)
{
	uint64_t seconds = ts->tv_sec;
	uint32_t nseconds = ts->tv_nsec;
	if (!inline_xdr_u_int64_t(xdr, &seconds))
		return FATTR_XDR_FAILED;
	if (!inline_xdr_u_int32_t(xdr, &nseconds))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static inline fattr_xdr_result decode_time(XDR *xdr,
					   struct xdr_attrs_args *args,
					   struct timespec *ts)
{
	uint64_t seconds = 0;
	uint32_t nseconds = 0;

	if (!inline_xdr_u_int64_t(xdr, &seconds))
		return FATTR_XDR_FAILED;
	if (!inline_xdr_u_int32_t(xdr, &nseconds))
		return FATTR_XDR_FAILED;
	ts->tv_sec = (uint32_t) seconds;	/* !!! is this correct?? */
	ts->tv_nsec = nseconds;
	if (nseconds >= 1000000000) {	/* overflow */
		args->nfs_status = NFS4ERR_INVAL;
		return FATTR_XDR_FAILED;
	}
	return FATTR_XDR_SUCCESS;
}

static inline fattr_xdr_result encode_timeset_server(XDR *xdr)
{
	uint32_t how = SET_TO_SERVER_TIME4;

	return inline_xdr_u_int32_t(xdr, &how);
}

static inline fattr_xdr_result encode_timeset(XDR *xdr, struct timespec *ts)
{
	uint32_t how = SET_TO_CLIENT_TIME4;

	if (!inline_xdr_u_int32_t(xdr, &how))
		return FATTR_XDR_FAILED;
	return encode_time(xdr, ts);
}

static inline fattr_xdr_result decode_timeset(XDR *xdr,
					      struct xdr_attrs_args *args,
					      struct timespec *ts)
{
	uint32_t how = 0;

	if (!inline_xdr_u_int32_t(xdr, &how))
		return FATTR_XDR_FAILED;

	if (how == SET_TO_SERVER_TIME4)
		return FATTR_XDR_SUCCESS_EXP;
	else
		return decode_time(xdr, args, ts);
}

/*
 * FATTR4_TIME_ACCESS
 */

static fattr_xdr_result encode_accesstime(XDR *xdr,
					  struct xdr_attrs_args *args)
{

	return encode_time(xdr, &args->attrs->atime);
}

static fattr_xdr_result decode_accesstime(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	return decode_time(xdr, args, &args->attrs->atime);
}

/*
 * FATTR4_TIME_ACCESS_SET
 */

static fattr_xdr_result encode_accesstimeset(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	if (FSAL_TEST_MASK(args->attrs->mask, ATTR_ATIME_SERVER))
		return encode_timeset_server(xdr);
	else
		return encode_timeset(xdr, &args->attrs->atime);
}

static fattr_xdr_result decode_accesstimeset(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	return decode_timeset(xdr, args, &args->attrs->atime);
}

/*
 * FATTR4_TIME_BACKUP
 */

/* No time backup, return unix's beginning of time */

static fattr_xdr_result encode_backuptime(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	struct timespec ts;

	ts.tv_sec = 0LL;
	ts.tv_nsec = 0;
	return encode_time(xdr, &ts);
}

static fattr_xdr_result decode_backuptime(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_TIME_CREATE
 */

/* No time create, return unix's beginning of time */

static fattr_xdr_result encode_createtime(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	struct timespec ts;

	ts.tv_sec = 0LL;
	ts.tv_nsec = 0;
	return encode_time(xdr, &ts);
}

static fattr_xdr_result decode_createtime(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_TIME_DELTA
 */

/* According to RFC3530, this is "the smallest usefull server time granularity".
 * I set this to 1s.  note: dynamicfsinfo has this value.  use it???
 */

static fattr_xdr_result encode_deltatime(XDR *xdr, struct xdr_attrs_args *args)
{
	struct timespec ts;

	ts.tv_sec = 1LL;
	ts.tv_nsec = 0;
	return encode_time(xdr, &ts);
}

static fattr_xdr_result decode_deltatime(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_TIME_METADATA:
 */

static fattr_xdr_result encode_metatime(XDR *xdr, struct xdr_attrs_args *args)
{
	return encode_time(xdr, &args->attrs->ctime);
}

static fattr_xdr_result decode_metatime(XDR *xdr, struct xdr_attrs_args *args)
{
	return decode_time(xdr, args, &args->attrs->ctime);
}

/*
 * FATTR4_TIME_MODIFY
 */

static fattr_xdr_result encode_modifytime(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	return encode_time(xdr, &args->attrs->mtime);
}

static fattr_xdr_result decode_modifytime(XDR *xdr,
					  struct xdr_attrs_args *args)
{
	return decode_time(xdr, args, &args->attrs->mtime);
}

/*
 * FATTR4_TIME_MODIFY_SET
 */

static fattr_xdr_result encode_modifytimeset(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	if (FSAL_TEST_MASK(args->attrs->mask, ATTR_MTIME_SERVER))
		return encode_timeset_server(xdr);
	else
		return encode_timeset(xdr, &args->attrs->mtime);
}

static fattr_xdr_result decode_modifytimeset(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	return decode_timeset(xdr, args, &args->attrs->mtime);
}

/*
 * FATTR4_MOUNTED_ON_FILEID
 */

static fattr_xdr_result encode_mounted_on_fileid(XDR *xdr,
						 struct xdr_attrs_args *args)
{
	if (!inline_xdr_u_int64_t(xdr, &args->mounted_on_fileid))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_mounted_on_fileid(XDR *xdr,
						 struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_DIR_NOTIF_DELAY
 */

static fattr_xdr_result encode_dir_notif_delay(XDR *xdr,
					       struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_dir_notif_delay(XDR *xdr,
					       struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_DIRENT_NOTIF_DELAY
 */

static fattr_xdr_result encode_dirent_notif_delay(XDR *xdr,
						  struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_dirent_notif_delay(XDR *xdr,
						  struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_DACL
 */

static fattr_xdr_result encode_dacl(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_dacl(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_SACL
 */

static fattr_xdr_result encode_sacl(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_sacl(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_CHANGE_POLICY
 */

static fattr_xdr_result encode_change_policy(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_change_policy(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_FS_STATUS
 */

static fattr_xdr_result encode_fs_status(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_fs_status(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_FS_LAYOUT_TYPES:
 */

static fattr_xdr_result encode_fs_layout_types(XDR *xdr,
					       struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	const layouttype4 *layouttypes = NULL;
	layouttype4 layout_type;
	int32_t typecount = 0;
	int32_t index = 0;

	if (args->data == NULL)
		return FATTR_XDR_NOOP;
	export = op_ctx->fsal_export;
	export->exp_ops.fs_layouttypes(export, &typecount, &layouttypes);

	if (!inline_xdr_u_int32_t(xdr, (uint32_t *) &typecount))
		return FATTR_XDR_FAILED;

	for (index = 0; index < typecount; index++) {
		layout_type = layouttypes[index];
		if (!inline_xdr_u_int32_t(xdr, &layout_type))
			return FATTR_XDR_FAILED;
	}

	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_fs_layout_types(XDR *xdr,
					       struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_LAYOUT_HINT
 */

static fattr_xdr_result encode_layout_hint(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_layout_hint(XDR *xdr,
					   struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_LAYOUT_TYPES
 */

static fattr_xdr_result encode_layout_types(XDR *xdr,
					    struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_layout_types(XDR *xdr,
					    struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_LAYOUT_BLKSIZE
 */

static fattr_xdr_result encode_layout_blocksize(XDR *xdr,
						struct xdr_attrs_args *args)
{

	if (args->data == NULL) {
		return FATTR_XDR_NOOP;
	} else {
		struct fsal_export *export = op_ctx->fsal_export;
		uint32_t blocksize = export->exp_ops.
					fs_layout_blocksize(export);

		if (!inline_xdr_u_int32_t(xdr, &blocksize))
			return FATTR_XDR_FAILED;
	}
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_layout_blocksize(XDR *xdr,
						struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_LAYOUT_ALIGNMENT
 */

static fattr_xdr_result encode_layout_alignment(XDR *xdr,
						struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_layout_alignment(XDR *xdr,
						struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 *  FATTR4_FS_LOCATIONS_INFO
 */

static fattr_xdr_result encode_fs_locations_info(XDR *xdr,
						 struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_fs_locations_info(XDR *xdr,
						 struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MDSTHRESHOLD
 */

static fattr_xdr_result encode_mdsthreshold(XDR *xdr,
					    struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_mdsthreshold(XDR *xdr,
					    struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_RETENTION_GET
 */

static fattr_xdr_result encode_retention_get(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_retention_get(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_RETENTION_SET
 */

static fattr_xdr_result encode_retention_set(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_retention_set(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_RETENTEVT_GET
 */

static fattr_xdr_result encode_retentevt_get(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_retentevt_get(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_RETENTEVT_SET
 */

static fattr_xdr_result encode_retentevt_set(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_retentevt_set(XDR *xdr,
					     struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_RETENTION_HOLD
 */

static fattr_xdr_result encode_retention_hold(XDR *xdr,
					      struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_retention_hold(XDR *xdr,
					      struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MODE_SET_MASKED
 */

static fattr_xdr_result encode_mode_set_masked(XDR *xdr,
					       struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_mode_set_masked(XDR *xdr,
					       struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_SUPPATTR_EXCLCREAT
 */

static fattr_xdr_result encode_support_exclusive_create(XDR *xdr,
							struct xdr_attrs_args
							*args)
{
	struct bitmap4 bits;
	int attr, offset;

	memset(&bits, 0, sizeof(bits));
	for (attr = FATTR4_SUPPORTED_ATTRS; attr <= FATTR4_SEC_LABEL;
	     attr++) {
		if (fattr4tab[attr].supported) {
			bool res = set_attribute_in_bitmap(&bits, attr);
			assert(res);
		}
	}
	bool res = clear_attribute_in_bitmap(&bits, FATTR4_TIME_ACCESS_SET);
	assert(res);
	res = clear_attribute_in_bitmap(&bits, FATTR4_TIME_MODIFY_SET);
	assert(res);
	if (!inline_xdr_u_int32_t(xdr, &bits.bitmap4_len))
		return FATTR_XDR_FAILED;
	for (offset = 0; offset < bits.bitmap4_len; offset++) {
		if (!inline_xdr_u_int32_t(xdr, &bits.map[offset]))
			return FATTR_XDR_FAILED;
	}
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_support_exclusive_create(XDR *xdr,
							struct xdr_attrs_args
							*args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_FS_CHARSET_CAP
 */

static fattr_xdr_result encode_fs_charset_cap(XDR *xdr,
					      struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_fs_charset_cap(XDR *xdr,
					      struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/* NFS V4.0+ attributes
 * This array reflects the tables on page 39-46 of RFC3530
 * indexed by attribute number
 */

const struct fattr4_dent fattr4tab[FATTR4_SEC_LABEL + 1] = {
	[FATTR4_SUPPORTED_ATTRS] = {
		.name = "FATTR4_SUPPORTED_ATTRS",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_supported_attrs),
		.encode = encode_supported_attrs,
		.decode = decode_supported_attrs,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_TYPE] = {
		.name = "FATTR4_TYPE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_type),
		.attrmask = ATTR_TYPE,
		.encode = encode_type,
		.decode = decode_type,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_FH_EXPIRE_TYPE] = {
		.name = "FATTR4_FH_EXPIRE_TYPE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_fh_expire_type),
		.encode = encode_expiretype,
		.decode = decode_expiretype,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_CHANGE] = {
		.name = "FATTR4_CHANGE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_change),
		.attrmask = (ATTR_CHGTIME | ATTR_CHANGE),
		.encode = encode_change,
		.decode = decode_change,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_SIZE] = {
		.name = "FATTR4_SIZE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_size),
		.attrmask = ATTR_SIZE,
		.encode = encode_filesize,
		.decode = decode_filesize,
		.access = FATTR4_ATTR_READ_WRITE}
	,
	[FATTR4_LINK_SUPPORT] = {
		.name = "FATTR4_LINK_SUPPORT",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_link_support),
		.encode = encode_linksupport,
		.decode = decode_linksupport,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_SYMLINK_SUPPORT] = {
		.name = "FATTR4_SYMLINK_SUPPORT",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_symlink_support),
		.encode = encode_symlinksupport,
		.decode = decode_symlinksupport,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_NAMED_ATTR] = {
		.name = "FATTR4_NAMED_ATTR",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_named_attr),
		.encode = encode_namedattrsupport,
		.decode = decode_namedattrsupport,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_FSID] = {
		.name = "FATTR4_FSID",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_fsid),
		.encode = encode_fsid,
		.decode = decode_fsid,
		.attrmask = ATTR_FSID,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_UNIQUE_HANDLES] = {
		.name = "FATTR4_UNIQUE_HANDLES",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_unique_handles),
		.encode = encode_uniquehandles,
		.decode = decode_uniquehandles,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_LEASE_TIME] = {
		.name = "FATTR4_LEASE_TIME",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_lease_time),
		.encode = encode_leaselife,
		.decode = decode_leaselife,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_RDATTR_ERROR] = {
		.name = "FATTR4_RDATTR_ERROR",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_rdattr_error),
		.encode = encode_rdattr_error,
		.decode = decode_rdattr_error,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_ACL] = {
		.name = "FATTR4_ACL",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_acl),
		.encode = encode_acl,
		.decode = decode_acl,
		.attrmask = ATTR_ACL,
		.access = FATTR4_ATTR_READ_WRITE}
	,
	[FATTR4_ACLSUPPORT] = {
		.name = "FATTR4_ACLSUPPORT",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_aclsupport),
		.encode = encode_aclsupport,
		.decode = decode_aclsupport,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_ARCHIVE] = {
		.name = "FATTR4_ARCHIVE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_archive),
		.encode = encode_archive,
		.decode = decode_archive,
		.access = FATTR4_ATTR_READ_WRITE}
	,
	[FATTR4_CANSETTIME] = {
		.name = "FATTR4_CANSETTIME",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_cansettime),
		.encode = encode_cansettime,
		.decode = decode_cansettime,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_CASE_INSENSITIVE] = {
		.name = "FATTR4_CASE_INSENSITIVE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_case_insensitive),
		.encode = encode_case_insensitive,
		.decode = decode_case_insensitive,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_CASE_PRESERVING] = {
		.name = "FATTR4_CASE_PRESERVING",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_case_preserving),
		.encode = encode_case_preserving,
		.decode = decode_case_preserving,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_CHOWN_RESTRICTED] = {
		.name = "FATTR4_CHOWN_RESTRICTED",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_chown_restricted),
		.encode = encode_chown_restricted,
		.decode = decode_chown_restricted,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_FILEHANDLE] = {
		.name = "FATTR4_FILEHANDLE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_filehandle),
		.encode = encode_filehandle,
		.decode = decode_filehandle,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_FILEID] = {
		.name = "FATTR4_FILEID",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_fileid),
		.encode = encode_fileid,
		.decode = decode_fileid,
		.attrmask = ATTR_FILEID,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_FILES_AVAIL] = {
		.name = "FATTR4_FILES_AVAIL",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_files_avail),
		.encode = encode_files_avail,
		.decode = decode_files_avail,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_FILES_FREE] = {
		.name = "FATTR4_FILES_FREE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_files_free),
		.encode = encode_files_free,
		.decode = decode_files_free,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_FILES_TOTAL] = {
		.name = "FATTR4_FILES_TOTAL",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_files_total),
		.encode = encode_files_total,
		.decode = decode_files_total,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_FS_LOCATIONS] = {
		.name = "FATTR4_FS_LOCATIONS",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_fs_locations),
		.encode = encode_fs_locations,
		.decode = decode_fs_locations,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_HIDDEN] = {
		.name = "FATTR4_HIDDEN",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_hidden),
		.encode = encode_hidden,
		.decode = decode_hidden,
		.access = FATTR4_ATTR_READ_WRITE}
	,
	[FATTR4_HOMOGENEOUS] = {
		.name = "FATTR4_HOMOGENEOUS",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_homogeneous),
		.encode = encode_homogeneous,
		.decode = decode_homogeneous,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_MAXFILESIZE] = {
		.name = "FATTR4_MAXFILESIZE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_maxfilesize),
		.encode = encode_maxfilesize,
		.decode = decode_maxfilesize,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_MAXLINK] = {
		.name = "FATTR4_MAXLINK",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_maxlink),
		.encode = encode_maxlink,
		.decode = decode_maxlink,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_MAXNAME] = {
		.name = "FATTR4_MAXNAME",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_maxname),
		.encode = encode_maxname,
		.decode = decode_maxname,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_MAXREAD] = {
		.name = "FATTR4_MAXREAD",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_maxread),
		.encode = encode_maxread,
		.decode = decode_maxread,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_MAXWRITE] = {
		.name = "FATTR4_MAXWRITE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_maxwrite),
		.encode = encode_maxwrite,
		.decode = decode_maxwrite,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_MIMETYPE] = {
		.name = "FATTR4_MIMETYPE",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_mimetype),
		.encode = encode_mimetype,
		.decode = decode_mimetype,
		.access = FATTR4_ATTR_READ_WRITE}
	,
	[FATTR4_MODE] = {
		.name = "FATTR4_MODE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_mode),
		.encode = encode_mode,
		.decode = decode_mode,
		.attrmask = ATTR_MODE,
		.access = FATTR4_ATTR_READ_WRITE}
	,
	[FATTR4_NO_TRUNC] = {
		.name = "FATTR4_NO_TRUNC",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_no_trunc),
		.encode = encode_no_trunc,
		.decode = decode_no_trunc,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_NUMLINKS] = {
		.name = "FATTR4_NUMLINKS",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_numlinks),
		.encode = encode_numlinks,
		.decode = decode_numlinks,
		.attrmask = ATTR_NUMLINKS,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_OWNER] = {
		.name = "FATTR4_OWNER",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_owner),
		.encode = encode_owner,
		.decode = decode_owner,
		.attrmask = ATTR_OWNER,
		.access = FATTR4_ATTR_READ_WRITE}
	,
	[FATTR4_OWNER_GROUP] = {
		.name = "FATTR4_OWNER_GROUP",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_owner_group),
		.encode = encode_group,
		.decode = decode_group,
		.attrmask = ATTR_GROUP,
		.access = FATTR4_ATTR_READ_WRITE}
	,
	[FATTR4_QUOTA_AVAIL_HARD] = {
		.name = "FATTR4_QUOTA_AVAIL_HARD",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_quota_avail_hard),
		.encode = encode_quota_avail_hard,
		.decode = decode_quota_avail_hard,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_QUOTA_AVAIL_SOFT] = {
		.name = "FATTR4_QUOTA_AVAIL_SOFT",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_quota_avail_soft),
		.encode = encode_quota_avail_soft,
		.decode = decode_quota_avail_soft,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_QUOTA_USED] = {
		.name = "FATTR4_QUOTA_USED",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_quota_used),
		.encode = encode_quota_used,
		.decode = decode_quota_used,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_RAWDEV] = {
		.name = "FATTR4_RAWDEV",
		.supported = 1,
		/** @todo use FSAL attrs instead ??? */
		.size_fattr4 = sizeof(fattr4_rawdev),
		.encode = encode_rawdev,
		.decode = decode_rawdev,
		.attrmask = ATTR_RAWDEV,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_SPACE_AVAIL] = {
		.name = "FATTR4_SPACE_AVAIL",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_space_avail),
		.encode = encode_sace_avail,
		.decode = decode_sace_avail,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_SPACE_FREE] = {
		.name = "FATTR4_SPACE_FREE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_space_used),
		.encode = encode_sace_free,
		.decode = decode_sace_free,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_SPACE_TOTAL] = {
		.name = "FATTR4_SPACE_TOTAL",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_space_total),
		.encode = encode_sace_total,
		.decode = decode_sace_total,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_SPACE_USED] = {
		.name = "FATTR4_SPACE_USED",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_space_used),
		.encode = encode_spaceused,
		.decode = decode_spaceused,
		.attrmask = ATTR_SPACEUSED,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_SYSTEM] = {
		.name = "FATTR4_SYSTEM",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_system),
		.encode = encode_system,
		.decode = decode_system,
		.access = FATTR4_ATTR_READ_WRITE}
	,
	[FATTR4_TIME_ACCESS] = {
		.name = "FATTR4_TIME_ACCESS",
		.supported = 1,
		/* ( fattr4_time_access )  not aligned on 32 bits */
		.size_fattr4 = 12,
		.encode = encode_accesstime,
		.decode = decode_accesstime,
		.attrmask = ATTR_ATIME,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_TIME_ACCESS_SET] = {
		.name = "FATTR4_TIME_ACCESS_SET",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_time_access_set),
		.encode = encode_accesstimeset,
		.decode = decode_accesstimeset,
		.attrmask = ATTR_ATIME,
		.exp_attrmask = ATTR_ATIME_SERVER,
		.access = FATTR4_ATTR_WRITE}
	,
	[FATTR4_TIME_BACKUP] = {
		.name = "FATTR4_TIME_BACKUP",
		.supported = 0,
		/*( fattr4_time_backup ) not aligned on 32 bits */
		.size_fattr4 = 12,
		.encode = encode_backuptime,
		.decode = decode_backuptime,
		.access = FATTR4_ATTR_READ_WRITE}
	,
	[FATTR4_TIME_CREATE] = {
		.name = "FATTR4_TIME_CREATE",
		.supported = 0,
		/* ( fattr4_time_create ) not aligned on 32 bits */
		.size_fattr4 = 12,
		.encode = encode_createtime,
		.decode = decode_createtime,
		.access = FATTR4_ATTR_READ_WRITE}
	,
	[FATTR4_TIME_DELTA] = {
		.name = "FATTR4_TIME_DELTA",
		.supported = 1,
		/* ( fattr4_time_delta ) not aligned on 32 bits */
		.size_fattr4 = 12,
		.encode = encode_deltatime,
		.decode = decode_deltatime,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_TIME_METADATA] = {
		.name = "FATTR4_TIME_METADATA",
		.supported = 1,
		/* ( fattr4_time_metadata ) not aligned on 32 bits */
		.size_fattr4 = 12,
		.encode = encode_metatime,
		.decode = decode_metatime,
		.attrmask = ATTR_CTIME,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_TIME_MODIFY] = {
		.name = "FATTR4_TIME_MODIFY",
		.supported = 1,
		/* ( fattr4_time_modify ) not aligned on 32 bits */
		.size_fattr4 = 12,
		.encode = encode_modifytime,
		.decode = decode_modifytime,
		.attrmask = ATTR_MTIME,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_TIME_MODIFY_SET] = {
		.name = "FATTR4_TIME_MODIFY_SET",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_time_modify_set),
		.encode = encode_modifytimeset,
		.decode = decode_modifytimeset,
		.attrmask = ATTR_MTIME,
		.exp_attrmask = ATTR_MTIME_SERVER,
		.access = FATTR4_ATTR_WRITE}
	,
	[FATTR4_MOUNTED_ON_FILEID] = {
		.name = "FATTR4_MOUNTED_ON_FILEID",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_mounted_on_fileid),
		.encode = encode_mounted_on_fileid,
		.decode = decode_mounted_on_fileid,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_DIR_NOTIF_DELAY] = {
		.name = "FATTR4_DIR_NOTIF_DELAY",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_dir_notif_delay),
		.encode = encode_dir_notif_delay,
		.decode = decode_dir_notif_delay,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_DIRENT_NOTIF_DELAY] = {
		.name = "FATTR4_DIRENT_NOTIF_DELAY",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_dirent_notif_delay),
		.encode = encode_dirent_notif_delay,
		.decode = decode_dirent_notif_delay,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_DACL] = {
		.name = "FATTR4_DACL",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_dacl),
		.encode = encode_dacl,
		.decode = decode_dacl,
		.access = FATTR4_ATTR_READ_WRITE}
	,
	[FATTR4_SACL] = {
		.name = "FATTR4_SACL",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_sacl),
		.encode = encode_sacl,
		.decode = decode_sacl,
		.access = FATTR4_ATTR_READ_WRITE}
	,
	[FATTR4_CHANGE_POLICY] = {
		.name = "FATTR4_CHANGE_POLICY",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_change_policy),
		.encode = encode_change_policy,
		.decode = decode_change_policy,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_FS_STATUS] = {
		.name = "FATTR4_FS_STATUS",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_fs_status),
		.encode = encode_fs_status,
		.decode = decode_fs_status,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_FS_LAYOUT_TYPES] = {
		.name = "FATTR4_FS_LAYOUT_TYPES",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_fs_layout_types),
		.encode = encode_fs_layout_types,
		.decode = decode_fs_layout_types,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_LAYOUT_HINT] = {
		.name = "FATTR4_LAYOUT_HINT",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_layout_hint),
		.encode = encode_layout_hint,
		.decode = decode_layout_hint,
		.access = FATTR4_ATTR_WRITE}
	,
	[FATTR4_LAYOUT_TYPES] = {
		.name = "FATTR4_LAYOUT_TYPES",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_layout_types),
		.encode = encode_layout_types,
		.decode = decode_layout_types,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_LAYOUT_BLKSIZE] = {
		.name = "FATTR4_LAYOUT_BLKSIZE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_layout_blksize),
		.encode = encode_layout_blocksize,
		.decode = decode_layout_blocksize,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_LAYOUT_ALIGNMENT] = {
		.name = "FATTR4_LAYOUT_ALIGNMENT",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_layout_alignment),
		.encode = encode_layout_alignment,
		.decode = decode_layout_alignment,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_FS_LOCATIONS_INFO] = {
		.name = "FATTR4_FS_LOCATIONS_INFO",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_fs_locations_info),
		.encode = encode_fs_locations_info,
		.decode = decode_fs_locations_info,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_MDSTHRESHOLD] = {
		.name = "FATTR4_MDSTHRESHOLD",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_mdsthreshold),
		.encode = encode_mdsthreshold,
		.decode = decode_mdsthreshold,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_RETENTION_GET] = {
		.name = "FATTR4_RETENTION_GET",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_retention_get),
		.encode = encode_retention_get,
		.decode = decode_retention_get,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_RETENTION_SET] = {
		.name = "FATTR4_RETENTION_SET",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_retention_set),
		.encode = encode_retention_set,
		.decode = decode_retention_set,
		.access = FATTR4_ATTR_WRITE}
	,
	[FATTR4_RETENTEVT_GET] = {
		.name = "FATTR4_RETENTEVT_GET",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_retentevt_get),
		.encode = encode_retentevt_get,
		.decode = decode_retentevt_get,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_RETENTEVT_SET] = {
		.name = "FATTR4_RETENTEVT_SET",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_retentevt_set),
		.encode = encode_retentevt_set,
		.decode = decode_retentevt_set,
		.access = FATTR4_ATTR_WRITE}
	,
	[FATTR4_RETENTION_HOLD] = {
		.name = "FATTR4_RETENTION_HOLD",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_retention_hold),
		.encode = encode_retention_hold,
		.decode = decode_retention_hold,
		.access = FATTR4_ATTR_READ_WRITE}
	,
	[FATTR4_MODE_SET_MASKED] = {
		.name = "FATTR4_MODE_SET_MASKED",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_mode_set_masked),
		.encode = encode_mode_set_masked,
		.decode = decode_mode_set_masked,
		.access = FATTR4_ATTR_WRITE}
	,
	[FATTR4_SUPPATTR_EXCLCREAT] = {
		.name = "FATTR4_SUPPATTR_EXCLCREAT",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_suppattr_exclcreat),
		.encode = encode_support_exclusive_create,
		.decode = decode_support_exclusive_create,
		.access = FATTR4_ATTR_READ}
	,
	[FATTR4_FS_CHARSET_CAP] = {
		.name = "FATTR4_FS_CHARSET_CAP",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_fs_charset_cap),
		.encode = encode_fs_charset_cap,
		.decode = decode_fs_charset_cap,
		.access = FATTR4_ATTR_READ}
};

/* goes in a more global header?
 */

/** path_filter
 * scan the path we are given for bad filenames.
 *
 * scan control:
 *    UTF8_SCAN_NOSLASH - detect and reject '/' in names
 *    UTF8_NODOT - detect and reject "." and ".." as the name
 *    UTF8_SCAN_CKUTF8 - detect invalid utf8 sequences
 *
 * NULL termination is required.  It also speeds up the scan
 * UTF-8 scanner courtesy Markus Kuhn <http://www.cl.cam.ac.uk/~mgk25/>
 * GPL licensed per licensing referenced in source.
 *
 */

static nfsstat4 path_filter(const char *name, utf8_scantype_t scan)
{
	const unsigned char *np = (const unsigned char *)name;
	nfsstat4 status = NFS4_OK;
	unsigned int c, first;

	first = 1;
	c = *np++;
	while (c) {
		if (likely(c < 0x80)) {
			/* ascii */
			if (unlikely(c == '/' && (scan & UTF8_SCAN_NOSLASH))) {
				status = NFS4ERR_BADCHAR;
				goto error;
			}
			if (unlikely
			    (first && c == '.' && (scan & UTF8_SCAN_NODOT))) {
				if (np[0] == '\0'
				    || (np[0] == '.' && np[1] == '\0')) {
					status = NFS4ERR_BADNAME;
					goto error;
				}
			}
		} else if (likely(scan & UTF8_SCAN_CKUTF8)) {
			/* UTF-8 range */
			if ((c & 0xe0) == 0xc0) {
				/* 2 octet UTF-8 */
				if ((*np & 0xc0) != 0x80 ||
				    (c & 0xfe) == 0xc0) {
					/* overlong */
					goto badutf8;
				} else {
					np++;
				}
			} else if ((c & 0xf0) == 0xe0) {
				/* 3 octet UTF-8 */
				if (/* overlong */
				    (*np & 0xc0) != 0x80 ||
				    (np[1] & 0xc0) != 0x80 ||
				    (c == 0xe0 && (*np & 0xe0) == 0x80) ||
				    /* surrogate */
				    (c == 0xed && (*np & 0xe0) == 0xa0) ||
				    (c == 0xef && *np == 0xbf &&
				    (np[1] & 0xfe) == 0xbe)) {
					/* U+fffe - u+ffff */
					goto badutf8;
				} else {
					np += 2;
				}
			} else if ((c & 0xf8) == 0xf0) {
				/* 4 octet UTF-8 */
				if (/* overlong */
				    (*np & 0xc0) != 0x80 ||
				    (np[1] & 0xc0) != 0x80 ||
				    (np[2] & 0xc0) != 0x80 ||
				    (c == 0xf0 && (*np & 0xf0) == 0x80) ||
				    (c == 0xf4 && *np > 0x8f) || c > 0xf4) {
					/* > u+10ffff */
					goto badutf8;
				} else {
					np += 3;
				}
			} else {
				goto badutf8;
			}
		}
		c = *np++;
		first = 0;
	}
	return NFS4_OK;

 badutf8:
	status = NFS4ERR_INVAL;
 error:
	return status;
}

void nfs4_Fattr_Free(fattr4 *fattr)
{
	if (fattr->attr_vals.attrlist4_val != NULL) {
		gsh_free(fattr->attr_vals.attrlist4_val);
		fattr->attr_vals.attrlist4_val = NULL;
	}
}

/**
 * @brief Structure for Fattr_filler callback
 */

struct Fattr_filler_opaque {
	fattr4 *Fattr;		/*< Fattr to fill */
	compound_data_t *data;	/*< Compound data */
	nfs_fh4 *objFH;		/*< Object file handle */
	struct bitmap4 *Bitmap;	/*< Bitmap of entries to fill */
};

/**
 * @brief Callback to fill a fattr
 *
 * This function is the callback for cache_entry_To_Fattr.
 *
 * @param[in] opaque Opaque structure
 * @param[in] attr   Attribute list
 *
 * @retval CACHE_INODE_SUCCESS on success.
 * @retval CACHE_INODE_IO_ERROR on error.
 */

static cache_inode_status_t Fattr_filler(void *opaque,
					 cache_entry_t *entry,
					 const struct attrlist *attr,
					 uint64_t mounted_on_fileid,
					 enum cb_state cb_state)
{
	struct xdr_attrs_args args;
	struct Fattr_filler_opaque *f = (struct Fattr_filler_opaque *)opaque;

	memset(&args, 0, sizeof(args));
	args.attrs = (struct attrlist *)attr;
	args.data = f->data;
	args.hdl4 = f->objFH;
	args.mounted_on_fileid = mounted_on_fileid;

	if (nfs4_FSALattr_To_Fattr(&args, f->Bitmap, f->Fattr) != 0)
		return CACHE_INODE_IO_ERROR;

	return CACHE_INODE_SUCCESS;
}

/**
 * @brief Fill NFSv4 Fattr from cache entry
 *
 * This function fills an NFSv4 Fattr from a cache entry.
 *
 * @param[in]  entry   Cache entry
 * @param[out] Fattr   NFSv4 Fattr buffer
 *		       Memory for bitmap_val and attr_val is
 *                     dynamically allocated,
 *		       caller is responsible for freeing it.
 * @param[in]  data    NFSv4 compoud request's data.
 * @param[in]  objFH   The NFSv4 filehandle of the object whose
 *                     attributes are requested
 * @param[in]  Bitmap  Bitmap of attributes being requested
 *
 * @retval cache status
 */

nfsstat4 cache_entry_To_Fattr(cache_entry_t *entry, fattr4 *Fattr,
			      compound_data_t *data, nfs_fh4 *objFH,
			      struct bitmap4 *Bitmap)
{
	struct Fattr_filler_opaque f = {
		.Fattr = Fattr,
		.data = data,
		.objFH = objFH,
		.Bitmap = Bitmap
	};

	/* Permissiomn check only if ACL is asked for.
	 * NOTE: We intentionally do NOT check ACE4_READ_ATTR.
	 */
	if (attribute_is_set(Bitmap, ATTR_ACL)) {
		cache_inode_status_t status;

		LogDebug(COMPONENT_NFS_V4_ACL,
			 "Permission check for ACL for entry %p", entry);

		status =
		    cache_inode_access(entry,
				       FSAL_ACE4_MASK_SET
				       (FSAL_ACE_PERM_READ_ACL));

		if (status != CACHE_INODE_SUCCESS) {
			LogDebug(COMPONENT_NFS_V4_ACL,
				 "Permission check for ACL for entry %p failed with %s",
				 entry, cache_inode_err_str(status));
			return nfs4_Errno(status);
		}
	} else {
		LogDebug(COMPONENT_NFS_V4_ACL,
			 "No permission check for ACL for entry %p", entry);
	}

	return nfs4_Errno(
		cache_inode_getattr(entry, &f, Fattr_filler, CB_ORIGINAL));
}

int nfs4_Fattr_Fill_Error(fattr4 *Fattr, nfsstat4 rdattr_error)
{
	u_int LastOffset;
	XDR attr_body;
	struct xdr_attrs_args args;
	fattr_xdr_result xdr_res;

	/* basic init */
	memset(&Fattr->attrmask, 0, sizeof(Fattr->attrmask));
	Fattr->attr_vals.attrlist4_val =
	    gsh_malloc(fattr4tab[FATTR4_RDATTR_ERROR].size_fattr4);

	if (Fattr->attr_vals.attrlist4_val == NULL)
		return -1;

	LastOffset = 0;
	memset(&attr_body, 0, sizeof(attr_body));
	xdrmem_create(&attr_body, Fattr->attr_vals.attrlist4_val,
		      fattr4tab[FATTR4_RDATTR_ERROR].size_fattr4, XDR_ENCODE);
	memset(&args, 0, sizeof(args));
	args.rdattr_error = rdattr_error;

	xdr_res = fattr4tab[FATTR4_RDATTR_ERROR].encode(&attr_body, &args);
	if (xdr_res == FATTR_XDR_SUCCESS) {
		bool res = set_attribute_in_bitmap(&Fattr->attrmask,
						   FATTR4_RDATTR_ERROR);
		assert(res);
		LogFullDebug(COMPONENT_NFS_V4,
			     "Encoded attribute %d, name = %s",
			     FATTR4_RDATTR_ERROR,
			     fattr4tab[FATTR4_RDATTR_ERROR].name);

		/* mark the attribute in the bitmap should be new bitmap btw */

		LastOffset = xdr_getpos(&attr_body);	/* dumb but for now */
		xdr_destroy(&attr_body);

		if (LastOffset == 0) {	/* no supported attrs so we can free */
			assert(Fattr->attrmask.bitmap4_len == 0);
			gsh_free(Fattr->attr_vals.attrlist4_val);
			Fattr->attr_vals.attrlist4_val = NULL;
		}
		Fattr->attr_vals.attrlist4_len = LastOffset;
		return 0;
	} else {
		LogFullDebug(COMPONENT_NFS_V4,
			     "Encode FAILED for attribute %d, name = %s",
			     FATTR4_RDATTR_ERROR,
			     fattr4tab[FATTR4_RDATTR_ERROR].name);
		/* signal fail so if(LastOffset > 0) works right */

		gsh_free(Fattr->attr_vals.attrlist4_val);
		Fattr->attr_vals.attrlist4_val = NULL;
		return -1;
	}
}

/**
 * @brief Converts FSAL Attributes to NFSv4 Fattr buffer.
 *
 * Converts FSAL Attributes to NFSv4 Fattr buffer.
 *
 * @param[in]  args    XDR attribute arguments
 * @param[in]  Bitmap  Bitmap of attributes being requested
 * @param[out] Fattr   NFSv4 Fattr buffer
 *		       Memory for bitmap_val and attr_val is
 *                     dynamically allocated,
 *		       caller is responsible for freeing it.
 *
 * @return -1 if failed, 0 if successful.
 *
 */

int nfs4_FSALattr_To_Fattr(struct xdr_attrs_args *args, struct bitmap4 *Bitmap,
			   fattr4 *Fattr)
{
	int attribute_to_set = 0;
	int max_attr_idx;
	u_int LastOffset;
	fsal_dynamicfsinfo_t dynamicinfo;
	XDR attr_body;
	fattr_xdr_result xdr_res;

	/* basic init */
	memset(&Fattr->attrmask, 0, sizeof(Fattr->attrmask));

	if (Bitmap->bitmap4_len == 0)
		return 0;	/* they ask for nothing, they get nothing */

	Fattr->attr_vals.attrlist4_val = gsh_malloc(NFS4_ATTRVALS_BUFFLEN);

	if (Fattr->attr_vals.attrlist4_val == NULL)
		return -1;

	max_attr_idx = nfs4_max_attr_index(args->data);
	LogFullDebug(COMPONENT_NFS_V4, "Maximum allowed attr index = %d",
		 max_attr_idx);

	LastOffset = 0;
	memset(&attr_body, 0, sizeof(attr_body));
	xdrmem_create(&attr_body, Fattr->attr_vals.attrlist4_val,
		      NFS4_ATTRVALS_BUFFLEN, XDR_ENCODE);

	if (args->dynamicinfo == NULL)
		args->dynamicinfo = &dynamicinfo;

	for (attribute_to_set = next_attr_from_bitmap(Bitmap, -1);
	     attribute_to_set != -1;
	     attribute_to_set =
	     next_attr_from_bitmap(Bitmap, attribute_to_set)) {
		if (attribute_to_set > max_attr_idx)
			break;	/* skip out of bounds */

		xdr_res = fattr4tab[attribute_to_set].encode(&attr_body, args);
		if (xdr_res == FATTR_XDR_SUCCESS) {
			bool res = set_attribute_in_bitmap(&Fattr->attrmask,
							   attribute_to_set);
			assert(res);
			LogFullDebug(COMPONENT_NFS_V4,
				     "Encoded attr %d, name = %s",
				     attribute_to_set,
				     fattr4tab[attribute_to_set].name);
		} else if (xdr_res == FATTR_XDR_NOOP) {
			LogFullDebug(COMPONENT_NFS_V4,
				     "Attr not supported %d name=%s",
				     attribute_to_set,
				     fattr4tab[attribute_to_set].name);
			continue;
		} else {
			LogFullDebug(COMPONENT_NFS_V4,
				     "Encode FAILED for attr %d, name = %s",
				     attribute_to_set,
				     fattr4tab[attribute_to_set].name);

			/* signal fail so if(LastOffset > 0) works right */
			goto err;
		}
		/* mark the attribute in the bitmap should be new bitmap btw */
	}
	LastOffset = xdr_getpos(&attr_body);	/* dumb but for now */
	xdr_destroy(&attr_body);

	if (LastOffset == 0) {	/* no supported attrs so we can free */
		assert(Fattr->attrmask.bitmap4_len == 0);
		gsh_free(Fattr->attr_vals.attrlist4_val);
		Fattr->attr_vals.attrlist4_val = NULL;
	}
	Fattr->attr_vals.attrlist4_len = LastOffset;
	return 0;

 err:
	gsh_free(Fattr->attr_vals.attrlist4_val);
	Fattr->attr_vals.attrlist4_val = NULL;
	return -1;
}

/**
 *
 * nfs3_Sattr_To_FSALattr: Converts NFSv3 Sattr to FSAL Attributes.
 *
 * Converts NFSv3 Sattr to FSAL Attributes.
 *
 * @param FSAL_attr  [OUT]  computed FSAL attributes.
 * @param sattr      [IN]   NFSv3 sattr to be set.
 *
 * @retval true on success.
 * @retval false on failure.
 *
 */
bool nfs3_Sattr_To_FSALattr(struct attrlist *FSAL_attr, sattr3 *sattr)
{
	FSAL_attr->mask = 0;

	if (sattr->mode.set_it) {
		LogFullDebug(COMPONENT_NFSPROTO, "mode = %o",
			     sattr->mode.set_mode3_u.mode);
		FSAL_attr->mode = unix2fsal_mode(sattr->mode.set_mode3_u.mode);
		FSAL_attr->mask |= ATTR_MODE;
	}

	if (sattr->uid.set_it) {
		LogFullDebug(COMPONENT_NFSPROTO, "uid = %d",
			     sattr->uid.set_uid3_u.uid);
		FSAL_attr->owner = sattr->uid.set_uid3_u.uid;
		FSAL_attr->mask |= ATTR_OWNER;
	}

	if (sattr->gid.set_it) {
		LogFullDebug(COMPONENT_NFSPROTO, "gid = %d",
			     sattr->gid.set_gid3_u.gid);
		FSAL_attr->group = sattr->gid.set_gid3_u.gid;
		FSAL_attr->mask |= ATTR_GROUP;
	}

	if (sattr->size.set_it) {
		LogFullDebug(COMPONENT_NFSPROTO, "size = %lld",
			     sattr->size.set_size3_u.size);
		FSAL_attr->filesize = sattr->size.set_size3_u.size;
		FSAL_attr->spaceused = sattr->size.set_size3_u.size;
		/* Both ATTR_SIZE and ATTR_SPACEUSED are to be managed */
		FSAL_attr->mask |= ATTR_SIZE;
		FSAL_attr->mask |= ATTR_SPACEUSED;
	}

	if (sattr->atime.set_it != DONT_CHANGE) {
		LogFullDebug(COMPONENT_NFSPROTO, "set=%d atime = %d,%d",
			     sattr->atime.set_it,
			     sattr->atime.set_atime_u.atime.tv_sec,
			     sattr->atime.set_atime_u.atime.tv_nsec);
		if (sattr->atime.set_it == SET_TO_CLIENT_TIME) {
			FSAL_attr->atime.tv_sec =
			    sattr->atime.set_atime_u.atime.tv_sec;
			FSAL_attr->atime.tv_nsec = 0;
			FSAL_attr->mask |= ATTR_ATIME;
		} else if (sattr->atime.set_it == SET_TO_SERVER_TIME) {
			/* Use the server's current time */
			LogFullDebug(COMPONENT_NFSPROTO,
				     "nfs3_Sattr_To_FSALattr: SET_TO_SERVER_TIME atime");
			FSAL_attr->mask |= ATTR_ATIME_SERVER;
		} else {
			LogCrit(COMPONENT_NFSPROTO,
				"Unexpected value for sattr->atime.set_it = %d",
				sattr->atime.set_it);
		}

	}

	if (sattr->mtime.set_it != DONT_CHANGE) {
		LogFullDebug(COMPONENT_NFSPROTO, "set=%d mtime = %d",
			     sattr->atime.set_it,
			     sattr->mtime.set_mtime_u.mtime.tv_sec);
		if (sattr->mtime.set_it == SET_TO_CLIENT_TIME) {
			FSAL_attr->mtime.tv_sec =
			    sattr->mtime.set_mtime_u.mtime.tv_sec;
			FSAL_attr->mtime.tv_nsec = 0;
			FSAL_attr->mask |= ATTR_MTIME;
		} else if (sattr->mtime.set_it == SET_TO_SERVER_TIME) {
			/* Use the server's current time */
			LogFullDebug(COMPONENT_NFSPROTO,
				     "nfs3_Sattr_To_FSALattr: SET_TO_SERVER_TIME Mtime");
			FSAL_attr->mask |= ATTR_MTIME_SERVER;
		} else {
			LogCrit(COMPONENT_NFSPROTO,
				"Unexpected value for sattr->mtime.set_it = %d",
				sattr->mtime.set_it);
		}

	}

	return true;
}				/* nfs3_Sattr_To_FSALattr */

/**** Glue related functions ****/

/*
 * Conversion of attributes
 */

/**
 * @brief Converts FSAL Attributes to NFSv3 attributes.
 *
 * Fill in the fields in the fattr3 structure.
 *
 * @param[in]    FSAL_attr  FSAL attributes.
 * @param[out]   mask       Attributes which have been copied
 * @param[out]   Fattr      NFSv3 attributes.
 *
 */
static void nfs3_FSALattr_To_PartialFattr(const struct attrlist *FSAL_attr,
					  attrmask_t *mask, fattr3 *Fattr)
{
	*mask = 0;

	if (FSAL_attr->mask & ATTR_TYPE) {
		*mask |= ATTR_TYPE;
		switch (FSAL_attr->type) {
		case FIFO_FILE:
			Fattr->type = NF3FIFO;
			break;

		case CHARACTER_FILE:
			Fattr->type = NF3CHR;
			break;

		case DIRECTORY:
			Fattr->type = NF3DIR;
			break;

		case BLOCK_FILE:
			Fattr->type = NF3BLK;
			break;

		case REGULAR_FILE:
		case EXTENDED_ATTR:
			Fattr->type = NF3REG;
			break;

		case SYMBOLIC_LINK:
			Fattr->type = NF3LNK;
			break;

		case SOCKET_FILE:
			Fattr->type = NF3SOCK;
			break;

		default:
			LogEvent(COMPONENT_NFSPROTO,
				 "nfs3_FSALattr_To_Fattr: Bogus type = %d",
				 FSAL_attr->type);
			*mask &= ~ATTR_TYPE;
		}
	}

	if (FSAL_attr->mask & ATTR_MODE) {
		Fattr->mode = fsal2unix_mode(FSAL_attr->mode);
		*mask |= ATTR_MODE;
	}

	if (FSAL_attr->mask & ATTR_NUMLINKS) {
		Fattr->nlink = FSAL_attr->numlinks;
		*mask |= ATTR_NUMLINKS;
	}

	if (FSAL_attr->mask & ATTR_OWNER) {
		Fattr->uid = FSAL_attr->owner;
		*mask |= ATTR_OWNER;
	}

	if (FSAL_attr->mask & ATTR_GROUP) {
		Fattr->gid = FSAL_attr->group;
		*mask |= ATTR_GROUP;
	}

	if (FSAL_attr->mask & ATTR_SIZE) {
		Fattr->size = FSAL_attr->filesize;
		*mask |= ATTR_SIZE;
	}

	if (FSAL_attr->mask & ATTR_SPACEUSED) {
		Fattr->used = FSAL_attr->spaceused;
		*mask |= ATTR_SPACEUSED;
	}

	if (FSAL_attr->mask & ATTR_RAWDEV) {
		Fattr->rdev.specdata1 = FSAL_attr->rawdev.major;
		Fattr->rdev.specdata2 = FSAL_attr->rawdev.minor;
		*mask |= ATTR_RAWDEV;
	}

	if (FSAL_attr->mask & ATTR_FSID) {
		/* xor filesystem_id major and rotated minor to create unique
		 * on-wire fsid.
		 */
		Fattr->fsid = (nfs3_uint64) squash_fsid(&FSAL_attr->fsid);

		LogFullDebug(COMPONENT_NFSPROTO,
			     "Compressing fsid for NFS v3 from "
			     "fsid major %#" PRIX64 " (%" PRIu64 "), minor %#"
			     PRIX64 " (%" PRIu64 ") to nfs3_fsid = %#" PRIX64
			     " (%" PRIu64 ")",
			     FSAL_attr->fsid.major,
			     FSAL_attr->fsid.major,
			     FSAL_attr->fsid.minor,
			     FSAL_attr->fsid.minor,
			     (uint64_t) Fattr->fsid, (uint64_t) Fattr->fsid);

		*mask |= ATTR_FILEID;
	}

	if (FSAL_attr->mask & ATTR_FILEID) {
		Fattr->fileid = FSAL_attr->fileid;
		*mask |= ATTR_FILEID;
	}

	if (FSAL_attr->mask & ATTR_ATIME) {
		Fattr->atime.tv_sec = FSAL_attr->atime.tv_sec;
		Fattr->atime.tv_nsec = FSAL_attr->atime.tv_nsec;
		*mask |= ATTR_ATIME;
	}

	if (FSAL_attr->mask & ATTR_MTIME) {
		Fattr->mtime.tv_sec = FSAL_attr->mtime.tv_sec;
		Fattr->mtime.tv_nsec = FSAL_attr->mtime.tv_nsec;
		*mask |= ATTR_MTIME;
	}

	if (FSAL_attr->mask & ATTR_CTIME) {
		Fattr->ctime.tv_sec = FSAL_attr->ctime.tv_sec;
		Fattr->ctime.tv_nsec = FSAL_attr->ctime.tv_nsec;
		*mask |= ATTR_CTIME;
	}
}				/* nfs3_FSALattr_To_PartialFattr */

/**
 * @brief Fill out an NFSv3 Fattr from a cache entry
 *
 * This function locks and refreshes the cache entry then calls out to
 * fill the Fattr.
 *
 * @param[in]  entry The cache entry
 * @param[out] Fattr NFSv3 Fattr
 *
 * @retval true on success.
 * @retval false on failure.
 */

bool cache_entry_to_nfs3_Fattr(cache_entry_t *entry, fattr3 *Fattr)
{
	bool rc = false;
	if (entry && (cache_inode_lock_trust_attrs(entry, false)
		      == CACHE_INODE_SUCCESS)) {
		rc = nfs3_FSALattr_To_Fattr(op_ctx->export,
					    &entry->obj_handle->attributes,
					    Fattr);
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	}

	return rc;
}

/**
 * @brief Convert FSAL Attributes to NFSv3 attributes.
 *
 * This function converts FSAL Attributes to NFSv3 attributes.  The
 * callee is expecting the full compliment of FSAL attributes to fill
 * in all the fields in the fattr3 structure.
 *
 * @param export   [IN]  the related export entry
 * @param FSAL_attr [IN]  pointer to FSAL attributes.
 * @param Fattr     [OUT] pointer to NFSv3 attributes.
 *
 * @retval true if successful.
 * @retval false  otherwise.
 *
 */
bool nfs3_FSALattr_To_Fattr(struct gsh_export *export,
			    const struct attrlist *FSAL_attr, fattr3 *Fattr)
{
	/* We want to override the FSAL fsid with the export's configured fsid
	 */
	attrmask_t want = ATTRS_NFS3;
	attrmask_t got = 0;

	nfs3_FSALattr_To_PartialFattr(FSAL_attr, &got, Fattr);
	if (want & ~got) {
		LogCrit(COMPONENT_NFSPROTO,
			"Likely bug: FSAL did not fill in a standard NFSv3 "
			"attribute: missing %lx", want & ~got);
	}

	if ((export->options_set & EXPORT_OPTION_FSID_SET) != 0) {
		/* xor filesystem_id major and rotated minor to create unique
		 * on-wire fsid.
		 */
		Fattr->fsid = (nfs3_uint64) squash_fsid(
					&export->filesystem_id);

		LogFullDebug(COMPONENT_NFSPROTO,
			     "Compressing export filesystem_id for NFS v3 from "
			     "fsid major %#" PRIX64 " (%" PRIu64 "), minor %#"
			     PRIX64 " (%" PRIu64 ") to nfs3_fsid = %#" PRIX64
			     " (%" PRIu64 ")",
			     export->filesystem_id.major,
			     export->filesystem_id.major,
			     export->filesystem_id.minor,
			     export->filesystem_id.minor,
			     (uint64_t) Fattr->fsid, (uint64_t) Fattr->fsid);
	}

	return true;
}

/**
 * @brief Checks if attributes have READ or WRITE access
 *
 * @param[in] bitmap NFSv4 attribute bitmap
 * @param[in] access Access to be checked, either FATTR4_ATTR_READ or
 *                   FATTR4_ATTR_WRITE
 *
 * @return true if successful, false otherwise.
 *
 */

bool nfs4_Fattr_Check_Access_Bitmap(struct bitmap4 *bitmap, int access)
{
	int attribute;
	assert((access == FATTR4_ATTR_READ) || (access == FATTR4_ATTR_WRITE));

	for (attribute = next_attr_from_bitmap(bitmap, -1); attribute != -1;
	     attribute = next_attr_from_bitmap(bitmap, attribute)) {
		if (attribute > FATTR4_SEC_LABEL) {
			/* Erroneous value... skip */
			continue;
		}
		if (((int)fattr4tab[attribute].access & access) != access)
			return false;
	}

	return true;
}				/* nfs4_Fattr_Check_Access */

/**
 *
 * nfs4_Fattr_Check_Access: checks if attributes have READ or WRITE access.
 *
 * Checks if attributes have READ or WRITE access.
 *
 * @param Fattr      [IN] pointer to NFSv4 attributes.
 * @param access     [IN] access to be checked, either FATTR4_ATTR_READ or FATTR4_ATTR_WRITE
 *
 * @return true if successful, false otherwise.
 *
 */

bool nfs4_Fattr_Check_Access(fattr4 *Fattr, int access)
{
	return nfs4_Fattr_Check_Access_Bitmap(&Fattr->attrmask, access);
}				/* nfs4_Fattr_Check_Access */

/**
 * @brief Remove unsupported attributes from bitmap4
 *
 * @todo .supported is not nearly as good as actually checking with
 * the export.
 *
 * @param[in] bitmap NFSv4 attribute bitmap.
 */

void nfs4_bitmap4_Remove_Unsupported(struct bitmap4 *bitmap)
{
	int attribute;

	for (attribute = 0; attribute <= FATTR4_SEC_LABEL; attribute++) {
		if (!fattr4tab[attribute].supported) {
			if (!clear_attribute_in_bitmap(bitmap, attribute))
				break;
		}
	}
}

/**
 *
 * nfs4_Fattr_Supported: Checks if an attribute is supported.
 *
 * Checks if an attribute is supported.
 *
 * @param Fattr      [IN] pointer to NFSv4 attributes.
 *
 * @return true if successful, false otherwise.
 *
 */

bool nfs4_Fattr_Supported(fattr4 *Fattr)
{
	return nfs4_Fattr_Supported_Bitmap(&Fattr->attrmask);
}				/* nfs4_Fattr_Supported */

/**
 * @brief Check if an attribute is supported
 *
 * @param[in] bitmap NFSv4 attributes bitmap
 *
 * @return true if successful, false otherwise.
 *
 */

bool nfs4_Fattr_Supported_Bitmap(struct bitmap4 *bitmap)
{
	int attribute;

	for (attribute = next_attr_from_bitmap(bitmap, -1); attribute != -1;
	     attribute = next_attr_from_bitmap(bitmap, attribute)) {
		LogFullDebug(COMPONENT_NFS_V4,
			     "nfs4_Fattr_Supported  ==============> %s supported flag=%u | ",
			     fattr4tab[attribute].name,
			     fattr4tab[attribute].supported);

		if (!fattr4tab[attribute].supported)
			return false;
	}

	return true;
}				/* nfs4_Fattr_Supported */

/**
 *
 * nfs4_Fattr_cmp: compares 2 fattr4 buffers.
 *
 * Compares 2 fattr4 buffers.
 *
 * @param Fattr1      [IN] pointer to NFSv4 attributes.
 * @param Fattr2      [IN] pointer to NFSv4 attributes.
 *
 * @return 1 if attributes are the same, 0 otherwise, but -1 if RDATTR_ERROR is set
 *
 */
int nfs4_Fattr_cmp(fattr4 *Fattr1, fattr4 *Fattr2)
{
	u_int LastOffset;
	uint32_t i;
	uint32_t k;
	u_int len = 0;
	int attribute_to_set = 0;

	if (Fattr1->attrmask.bitmap4_len != Fattr2->attrmask.bitmap4_len) {
		/* different mask */
		return 0;
	}

	for (i = 0; i < Fattr1->attrmask.bitmap4_len; i++)
		if (Fattr1->attrmask.map[i] != Fattr2->attrmask.map[i])
			return FALSE;
	if (attribute_is_set(&Fattr1->attrmask, FATTR4_RDATTR_ERROR))
		return -1;

	LastOffset = 0;
	len = 0;

  /** There has got to be a better way to do this but we do have to cope
   *  with unaligned buffers for opaque data
   */
	for (attribute_to_set = next_attr_from_bitmap(&Fattr1->attrmask, -1);
	     attribute_to_set != -1;
	     attribute_to_set =
	     next_attr_from_bitmap(&Fattr1->attrmask, attribute_to_set)) {
		if (attribute_to_set > FATTR4_SEC_LABEL) {
			/* Erroneous value... skip */
			continue;
		}
		LogFullDebug(COMPONENT_NFS_V4,
			     "nfs4_Fattr_cmp ==============> %s",
			     fattr4tab[attribute_to_set].name);

		switch (attribute_to_set) {
		case FATTR4_SUPPORTED_ATTRS:
			memcpy(&len,
			       (char *)(Fattr1->attr_vals.attrlist4_val +
					LastOffset), sizeof(u_int));
			if (memcmp
			    ((char *)(Fattr1->attr_vals.attrlist4_val +
				      LastOffset),
			     (char *)(Fattr2->attr_vals.attrlist4_val +
				      LastOffset), sizeof(u_int)) != 0)
				return 0;

			len = htonl(len);
			LastOffset += sizeof(u_int);

			for (k = 0; k < len; k++) {
				if (memcmp
				    ((char *)(Fattr1->attr_vals.attrlist4_val +
					      LastOffset),
				     (char *)(Fattr2->attr_vals.attrlist4_val +
					      LastOffset),
				     sizeof(uint32_t)) != 0)
					return 0;
				LastOffset += sizeof(uint32_t);
			}

			break;

		case FATTR4_FILEHANDLE:
		case FATTR4_OWNER:
		case FATTR4_OWNER_GROUP:
			memcpy(&len,
			       (char *)(Fattr1->attr_vals.attrlist4_val +
					LastOffset), sizeof(u_int));
			len = ntohl(len);	/* xdr marshalling on fattr4 */
			if (memcmp
			    ((char *)(Fattr1->attr_vals.attrlist4_val +
				      LastOffset),
			     (char *)(Fattr2->attr_vals.attrlist4_val +
				      LastOffset), sizeof(u_int)) != 0)
				return 0;
			LastOffset += sizeof(u_int);
			if (memcmp
			    ((char *)(Fattr1->attr_vals.attrlist4_val +
				      LastOffset),
			     (char *)(Fattr2->attr_vals.attrlist4_val +
				      LastOffset), len) != 0)
				return 0;
			break;

		case FATTR4_TYPE:
		case FATTR4_FH_EXPIRE_TYPE:
		case FATTR4_CHANGE:
		case FATTR4_SIZE:
		case FATTR4_LINK_SUPPORT:
		case FATTR4_SYMLINK_SUPPORT:
		case FATTR4_NAMED_ATTR:
		case FATTR4_FSID:
		case FATTR4_UNIQUE_HANDLES:
		case FATTR4_LEASE_TIME:
		case FATTR4_RDATTR_ERROR:
		case FATTR4_ACL:
		case FATTR4_ACLSUPPORT:
		case FATTR4_ARCHIVE:
		case FATTR4_CANSETTIME:
		case FATTR4_CASE_INSENSITIVE:
		case FATTR4_CASE_PRESERVING:
		case FATTR4_CHOWN_RESTRICTED:
		case FATTR4_FILEID:
		case FATTR4_FILES_AVAIL:
		case FATTR4_FILES_FREE:
		case FATTR4_FILES_TOTAL:
		case FATTR4_FS_LOCATIONS:
		case FATTR4_HIDDEN:
		case FATTR4_HOMOGENEOUS:
		case FATTR4_MAXFILESIZE:
		case FATTR4_MAXLINK:
		case FATTR4_MAXNAME:
		case FATTR4_MAXREAD:
		case FATTR4_MAXWRITE:
		case FATTR4_MIMETYPE:
		case FATTR4_MODE:
		case FATTR4_NO_TRUNC:
		case FATTR4_NUMLINKS:
		case FATTR4_QUOTA_AVAIL_HARD:
		case FATTR4_QUOTA_AVAIL_SOFT:
		case FATTR4_QUOTA_USED:
		case FATTR4_RAWDEV:
		case FATTR4_SPACE_AVAIL:
		case FATTR4_SPACE_FREE:
		case FATTR4_SPACE_TOTAL:
		case FATTR4_SPACE_USED:
		case FATTR4_SYSTEM:
		case FATTR4_TIME_ACCESS:
		case FATTR4_TIME_ACCESS_SET:
		case FATTR4_TIME_BACKUP:
		case FATTR4_TIME_CREATE:
		case FATTR4_TIME_DELTA:
		case FATTR4_TIME_METADATA:
		case FATTR4_TIME_MODIFY:
		case FATTR4_TIME_MODIFY_SET:
		case FATTR4_MOUNTED_ON_FILEID:
			if (memcmp
			    ((char *)(Fattr1->attr_vals.attrlist4_val +
				      LastOffset),
			     (char *)(Fattr2->attr_vals.attrlist4_val +
				      LastOffset),
			     fattr4tab[attribute_to_set].size_fattr4) != 0)
				return 0;
			break;

		default:
			return 0;
			break;
		}
	}
	return 1;
}

/**
 *
 * @brief Converts NFSv4 attributes buffer to a FSAL attributes structure.
 *
 * Converts NFSv4 attributes buffer to a FSAL attributes structure.
 *
 * NB! If the pointer for the handle is provided the memory is not allocated,
 *     the handle's nfs_fh4_val points inside fattr4. The pointer is valid
 *     as long as fattr4 is valid.
 *
 * @param pFSAL_attr [OUT]  pointer to FSAL attributes.
 * @param Fattr      [IN] pointer to NFSv4 attributes.
 * @param hdl4       [OUT] optional pointer to return NFSv4 file handle
 * @param dinfo      [OUT] optional pointer to return 'dynamic info' about FS
 *
 * @return NFS4_OK if successful, NFS4ERR codes if not.
 *
 */

static int Fattr4_To_FSAL_attr(struct attrlist *attrs, fattr4 *Fattr,
			       nfs_fh4 *hdl4, fsal_dynamicfsinfo_t *dinfo,
			       compound_data_t *data)
{
	int attribute_to_set = next_attr_from_bitmap(&Fattr->attrmask, -1);
	int nfs_status = NFS4_OK;
	XDR attr_body;
	struct xdr_attrs_args args;
	fattr_xdr_result xdr_res;

	/* Check attributes data */
	if ((Fattr->attr_vals.attrlist4_val == NULL)
	    || (Fattr->attr_vals.attrlist4_len == 0))
		return attribute_to_set == -1 ? NFS4_OK : NFS4ERR_BADXDR;

	/* Init */
	memset(&attr_body, 0, sizeof(attr_body));
	xdrmem_create(&attr_body, Fattr->attr_vals.attrlist4_val,
		      Fattr->attr_vals.attrlist4_len, XDR_DECODE);
	if (attrs)
		FSAL_CLEAR_MASK(attrs->mask);
	memset(&args, 0, sizeof(args));
	args.attrs = attrs;
	args.hdl4 = hdl4;
	args.dynamicinfo = dinfo;
	args.nfs_status = NFS4_OK;
	args.data = data;

	for (attribute_to_set = next_attr_from_bitmap(&Fattr->attrmask, -1);
	     attribute_to_set != -1;
	     attribute_to_set =
	     next_attr_from_bitmap(&Fattr->attrmask, attribute_to_set)) {
		const struct fattr4_dent *f4e = fattr4tab + attribute_to_set;

		if (attribute_to_set > FATTR4_SEC_LABEL) {
			nfs_status = NFS4ERR_BADXDR;	/* undefined attr */
			break;
		}
		xdr_res = f4e->decode(&attr_body, &args);

		if (xdr_res == FATTR_XDR_SUCCESS) {
			if (attrs)
				FSAL_SET_MASK(attrs->mask, f4e->attrmask);
			LogFullDebug(COMPONENT_NFS_V4,
				     "Decode attr %d, name = %s",
				     attribute_to_set, f4e->name);
		} else if (xdr_res == FATTR_XDR_SUCCESS_EXP) {
			if (attrs)
				FSAL_SET_MASK(attrs->mask, f4e->exp_attrmask);
			LogFullDebug(COMPONENT_NFS_V4,
				     "Decode (exp) attr %d, name = %s",
				     attribute_to_set, f4e->name);
		} else if (xdr_res == FATTR_XDR_NOOP) {
			LogFullDebug(COMPONENT_NFS_V4,
				     "Attr not supported %d name=%s",
				     attribute_to_set, f4e->name);
			if (nfs_status == NFS4_OK) {
				/* preserve previous error */
				nfs_status = NFS4ERR_ATTRNOTSUPP;
			}
			break;
		} else {
			LogFullDebug(COMPONENT_NFS_V4,
				     "Decode attr FAILED: %d, name = %s",
				     attribute_to_set, f4e->name);
			if (args.nfs_status == NFS4_OK)
				nfs_status = NFS4ERR_BADXDR;
			else
				nfs_status = args.nfs_status;
			break;
		}
	}
	if (xdr_getpos(&attr_body) < Fattr->attr_vals.attrlist4_len)
		nfs_status = NFS4ERR_BADXDR;	/* underrun on attribute */
	xdr_destroy(&attr_body);
	return nfs_status;
}

/**
 * @brief Convert NFSv4 attribute buffer to an FSAL attribute list
 *
 * @param[out] FSAL_attr FSAL attributes
 * @param[in]  Fattr     NFSv4 attributes
 * @param[in]  data      Compound data
 *
 * @return NFS4_OK if successful, NFS4ERR codes if not.
 *
 */
int nfs4_Fattr_To_FSAL_attr(struct attrlist *FSAL_attr, fattr4 *Fattr,
			    compound_data_t *data)
{
	memset(FSAL_attr, 0, sizeof(struct attrlist));
	return Fattr4_To_FSAL_attr(FSAL_attr, Fattr, NULL, NULL, data);
}

/**
 *
 * nfs4_Fattr_To_fsinfo: Decode filesystem info out of NFSv4 attributes.
 *
 * Converts information encoded in NFSv4 attributes buffer to 'dynamic info'
 * about an exported filesystem.
 *
 * It is assumed and expected that the fattr4 blob is not
 * going to have any other attributes expect those necessary
 * to fill in details about sace and inode utilization.
 *
 * @param dinfo [OUT] pointer to dynamic info
 * @param Fattr [IN]  pointer to NFSv4 attributes.
 *
 * @return NFS4_OK if successful, NFS4ERR codes if not.
 *
 */
int nfs4_Fattr_To_fsinfo(fsal_dynamicfsinfo_t *dinfo, fattr4 *Fattr)
{
	return Fattr4_To_FSAL_attr(NULL, Fattr, NULL, dinfo, NULL);
}

/* nfs4_utf8string2dynamic
 * unpack the input string from the XDR into a null term'd string
 * scan for bad chars
 */

nfsstat4 nfs4_utf8string2dynamic(const utf8string *input,
				 utf8_scantype_t scan,
				 char **obj_name)
{
	nfsstat4 status = NFS4_OK;

	*obj_name = NULL;

	if (input->utf8string_val == NULL || input->utf8string_len == 0)
		return NFS4ERR_INVAL;

	if (input->utf8string_len > MAXNAMLEN)
		return NFS4ERR_NAMETOOLONG;

	char *name = gsh_malloc(input->utf8string_len + 1);

	if (name == NULL)
		return NFS4ERR_SERVERFAULT;

	memcpy(name, input->utf8string_val, input->utf8string_len);
	name[input->utf8string_len] = '\0';
	if (scan != UTF8_SCAN_NONE)
		status = path_filter(name, scan);
	if (status == NFS4_OK)
		*obj_name = name;
	else
		gsh_free(name);
	return status;
}

/**
 * @brief: is a directory's sticky bit set?
 *
 */
bool is_sticky_bit_set(const struct attrlist *attr)
{
	if (attr->mode & (S_IXUSR|S_IXGRP|S_IXOTH))
		return false;

	if (!(attr->mode & S_ISVTX))
		return false;

	LogDebug(COMPONENT_NFS_V4, "sticky bit is set on %ld", attr->fileid);
	return true;
}
