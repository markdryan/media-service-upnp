/*
 * media-service-upnp
 *
 * Copyright (C) 2012 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Mark Ryan <mark.d.ryan@intel.com>
 *
 */

#ifndef MSU_UPNP_H__
#define MSU_UPNP_H__

#include "client.h"
#include "task.h"

typedef struct msu_upnp_t_ msu_upnp_t;

enum msu_interface_type_ {
	MSU_INTERFACE_INFO_PROPERTIES,
	MSU_INTERFACE_INFO_OBJECT,
	MSU_INTERFACE_INFO_CONTAINER,
	MSU_INTERFACE_INFO_ITEM,
	MSU_INTERFACE_INFO_DEVICE,
	MSU_INTERFACE_INFO_MAX
};

typedef struct msu_interface_info_t_ msu_interface_info_t;
struct msu_interface_info_t_ {
	GDBusInterfaceInfo *interface;
	const GDBusInterfaceVTable *vtable;
};

typedef void (*msu_upnp_callback_t)(const gchar *path, void *user_data);
typedef void (*msu_upnp_task_complete_t)(msu_task_t *task, GVariant *result,
					 GError *error);

msu_upnp_t *msu_upnp_new(GDBusConnection *connection,
			 msu_interface_info_t *interface_info,
			 msu_upnp_callback_t found_server,
			 msu_upnp_callback_t lost_server,
			 void *user_data);
void msu_upnp_delete(msu_upnp_t *upnp);
GVariant *msu_upnp_get_server_ids(msu_upnp_t *upnp);
void msu_upnp_get_children(msu_upnp_t *upnp, msu_client_t *client,
			   msu_task_t *task,
			   GCancellable *cancellable,
			   msu_upnp_task_complete_t cb);
void msu_upnp_get_all_props(msu_upnp_t *upnp, msu_client_t *client,
			    msu_task_t *task,
			    GCancellable *cancellable,
			    msu_upnp_task_complete_t cb);
void msu_upnp_get_prop(msu_upnp_t *upnp, msu_client_t *client,
		       msu_task_t *task,
		       GCancellable *cancellable,
		       msu_upnp_task_complete_t cb);
void msu_upnp_search(msu_upnp_t *upnp, msu_client_t *client,
		     msu_task_t *task,
		     GCancellable *cancellable,
		     msu_upnp_task_complete_t cb);
void msu_upnp_get_resource(msu_upnp_t *upnp, msu_client_t *client,
			   msu_task_t *task,
			   GCancellable *cancellable,
			   msu_upnp_task_complete_t cb);
void msu_upnp_upload_to_any(msu_upnp_t *upnp, msu_client_t *client,
			    msu_task_t *task,
			    GCancellable *cancellable,
			    msu_upnp_task_complete_t cb);
void msu_upnp_upload(msu_upnp_t *upnp, msu_client_t *client,
		     msu_task_t *task,
		     GCancellable *cancellable,
		     msu_upnp_task_complete_t cb);
void msu_upnp_get_upload_status(msu_upnp_t *upnp, msu_task_t *task);
void msu_upnp_get_upload_ids(msu_upnp_t *upnp, msu_task_t *task);
void msu_upnp_cancel_upload(msu_upnp_t *upnp, msu_task_t *task);
void msu_upnp_delete_object(msu_upnp_t *upnp, msu_client_t *client,
			    msu_task_t *task,
			    GCancellable *cancellable,
			    msu_upnp_task_complete_t cb);
void msu_upnp_create_container(msu_upnp_t *upnp, msu_client_t *client,
			       msu_task_t *task,
			       GCancellable *cancellable,
			       msu_upnp_task_complete_t cb);
void msu_upnp_create_container_in_any(msu_upnp_t *upnp, msu_client_t *client,
				      msu_task_t *task,
				      GCancellable *cancellable,
				      msu_upnp_task_complete_t cb);
#endif
