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

#ifndef MSU_DEVICE_H__
#define MSU_DEVICE_H__

#include <libgupnp/gupnp-control-point.h>

#include "async.h"
#include "chain-task.h"
#include "client.h"
#include "props.h"

typedef struct msu_device_context_t_ msu_device_context_t;
struct msu_device_context_t_ {
	gchar *ip_address;
	GUPnPDeviceProxy *device_proxy;
	GUPnPServiceProxy *service_proxy;
	msu_device_t *device;
	gboolean subscribed;
	guint timeout_id;
};

struct msu_device_t_ {
	GDBusConnection *connection;
	guint id;
	gchar *path;
	GPtrArray *contexts;
	guint timeout_id;
	GHashTable *uploads;
	GHashTable *upload_jobs;
	guint upload_id;
	guint system_update_id;
	GVariant *search_caps;
	GVariant *sort_caps;
	GVariant *sort_ext_caps;
	gboolean shutting_down;
};

void msu_device_append_new_context(msu_device_t *device,
				   const gchar *ip_address,
				   GUPnPDeviceProxy *proxy);
void msu_device_delete(void *device);
msu_device_t *msu_device_new(GDBusConnection *connection,
			     GUPnPDeviceProxy *proxy,
			     const gchar *ip_address,
			     const GDBusSubtreeVTable *vtable,
			     void *user_data,
			     GHashTable *filter_map,
			     guint counter,
			     msu_chain_task_t *chain);

msu_device_t *msu_device_from_path(const gchar *path, GHashTable *device_list);
msu_device_context_t *msu_device_get_context(msu_device_t *device,
					     msu_client_t *client);
void msu_device_get_children(msu_device_t *device, msu_client_t *client,
			     msu_task_t *task, msu_async_cb_data_t *cb_data,
			     const gchar *upnp_filter, const gchar *sort_by,
			     GCancellable *cancellable);
void msu_device_get_all_props(msu_device_t *device, msu_client_t *client,
			      msu_task_t *task,
			      msu_async_cb_data_t *cb_data,
			      gboolean root_object,
			      GCancellable *cancellable);
void msu_device_get_prop(msu_device_t *device, msu_client_t *client,
			 msu_task_t *task, msu_async_cb_data_t *cb_data,
			 msu_prop_map_t *prop_map, gboolean root_object,
			 GCancellable *cancellable);
void msu_device_search(msu_device_t *device, msu_client_t *client,
		       msu_task_t *task, msu_async_cb_data_t *cb_data,
		       const gchar *upnp_filter, const gchar *upnp_query,
		       const gchar *sort_by, GCancellable *cancellable);
void msu_device_get_resource(msu_device_t *device, msu_client_t *client,
			     msu_task_t *task,
			     msu_async_cb_data_t *cb_data,
			     const gchar *upnp_filter,
			     GCancellable *cancellable);
void msu_device_subscribe_to_contents_change(msu_device_t *device);
void msu_device_upload(msu_device_t *device, msu_client_t *client,
		       msu_task_t *task, const gchar *parent_id,
		       msu_async_cb_data_t *cb_data, GCancellable *cancellable);
gboolean msu_device_get_upload_status(msu_device_t *device,
				      msu_task_t *task, GError **error);
gboolean msu_device_cancel_upload(msu_device_t *device, msu_task_t *task,
				  GError **error);
void msu_device_get_upload_ids(msu_device_t *device, msu_task_t *task);
void msu_device_delete_object(msu_device_t *device, msu_client_t *client,
			      msu_task_t *task,
			      msu_async_cb_data_t *cb_data,
			      GCancellable *cancellable);
void msu_device_create_container(msu_device_t *device, msu_client_t *client,
				 msu_task_t *task,
				 const gchar *parent_id,
				 msu_async_cb_data_t *cb_data,
				 GCancellable *cancellable);

#endif
