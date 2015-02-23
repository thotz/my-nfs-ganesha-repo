/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Red Hat  Inc., 2013
 * Author: Anand Subramanian anands@redhat.com
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * -------------
 */

/**
 * @file main.c
 * @author Anand Subramanian <anands@redhat.com>
 *
 * @author Shyamsundar R     <srangana@redhat.com>
 *
 * @brief Module core functions for FSAL_GLUSTER functionality, init etc.
 *
 */

#include "fsal.h"
#include "FSAL/fsal_init.h"
#include "gluster_internal.h"

/* GLUSTERFS FSAL module private storage
 */

const char glfsal_name[] = "GLUSTER";

/* filesystem info for GLUSTERFS */
static struct fsal_staticfsinfo_t default_gluster_info = {
	.maxfilesize = UINT64_MAX,
	.maxlink = _POSIX_LINK_MAX,
	.maxnamelen = 1024,
	.maxpathlen = 1024,
	.no_trunc = true,
	.chown_restricted = true,
	.case_insensitive = false,
	.case_preserving = true,
	.link_support = true,
	.symlink_support = true,
	.lock_support = true,
	.lock_support_owner = false,
	.lock_support_async_block = false,
	.named_attr = true,
	.unique_handles = true,
	.lease_time = {10, 0},
	.acl_support = FSAL_ACLSUPPORT_ALLOW | FSAL_ACLSUPPORT_DENY,
	.cansettime = true,
	.homogenous = true,
	.supported_attrs = GLUSTERFS_SUPPORTED_ATTRIBUTES,
	.maxread = 0,
	.maxwrite = 0,
	.umask = 0,
	.auth_exportpath_xdev = false,
	.xattr_access_rights = 0400,	/* root=RW, owner=R */
};


static struct config_item glfs_params[] = {
	CONF_ITEM_BOOL("link_support", true,
		       fsal_staticfsinfo_t, link_support),
	CONF_ITEM_BOOL("symlink_support", true,
		       fsal_staticfsinfo_t, symlink_support),
	CONF_ITEM_MODE("acl_support", FSAL_ACLSUPPORT_ALLOW|FSAL_ACLSUPPORT_DENY,
		       fsal_staticfsinfo_t, acl_support),
	CONF_ITEM_MODE("umask", 0,
		       fsal_staticfsinfo_t, umask),
	CONF_ITEM_BOOL("auth_xdev_export", false,
		       fsal_staticfsinfo_t, auth_exportpath_xdev),
	CONF_ITEM_MODE("xattr_access_rights", 0400,
		       fsal_staticfsinfo_t, xattr_access_rights),
	CONF_ITEM_BOOL("pnfs_mds", false,
		       fsal_staticfsinfo_t, pnfs_mds),
	CONF_ITEM_BOOL("pnfs_ds", false,
		       fsal_staticfsinfo_t, pnfs_ds),
	CONFIG_EOL
};

struct config_block glfs_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.gluster",
	.blk_desc.name = "GLUSTER",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = glfs_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

static fsal_status_t init_config(struct fsal_module *fsal_hdl,
				 config_file_t config_struct,
				 struct config_error_type *err_type)
{
	struct glusterfs_fsal_module *glfsal_module =
	    container_of(fsal_hdl, struct glusterfs_fsal_module, fsal);


	glfsal_module->fs_info = default_gluster_info;
	(void) load_config_from_parse(config_struct,
				      &glfs_param,
				      &glfsal_module->fs_info,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);
	display_fsinfo(&glfsal_module->fs_info);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static struct glusterfs_fsal_module *glfsal_module;
/* Module methods
 */

MODULE_INIT void glusterfs_init(void)
{
	/* register_fsal seems to expect zeroed memory. */
	glfsal_module = gsh_calloc(1, sizeof(struct glusterfs_fsal_module));
	if (glfsal_module == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate memory for Gluster FSAL module.");
		return;
	}

	if (register_fsal(&glfsal_module->fsal, glfsal_name, FSAL_MAJOR_VERSION,
			  FSAL_MINOR_VERSION, FSAL_ID_GLUSTER) != 0) {
		gsh_free(glfsal_module);
		LogCrit(COMPONENT_FSAL,
			"Gluster FSAL module failed to register.");
	}

	/* set up module operations */
	glfsal_module->fsal.m_ops.create_export = glusterfs_create_export;

	/* setup global handle internals */
	glfsal_module->fsal.m_ops.init_config = init_config;
	LogDebug(COMPONENT_FSAL, "FSAL Gluster initialized");
}

MODULE_FINI void glusterfs_unload(void)
{
	if (unregister_fsal(&glfsal_module->fsal) != 0) {
		LogCrit(COMPONENT_FSAL,
			"FSAL Gluster unable to unload.  Dying ...");
		abort();
	}

	gsh_free(glfsal_module);
	glfsal_module = NULL;

	LogDebug(COMPONENT_FSAL, "FSAL Gluster unloaded");
}
