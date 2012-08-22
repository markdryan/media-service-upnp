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

#include <string.h>

#include <libgupnp/gupnp-control-point.h>
#include <libgupnp/gupnp-error.h>

#include "async.h"
#include "device.h"
#include "error.h"
#include "interface.h"
#include "log.h"
#include "path.h"
#include "search.h"
#include "sort.h"
#include "upnp.h"

struct msu_upnp_t_ {
	GDBusConnection *connection;
	msu_interface_info_t *interface_info;
	GHashTable *filter_map;
	msu_upnp_callback_t found_server;
	msu_upnp_callback_t lost_server;
	GUPnPContextManager *context_manager;
	void *user_data;
	GHashTable *server_udn_map;
	guint counter;
};

static gchar **prv_subtree_enumerate(GDBusConnection *connection,
				     const gchar *sender,
				     const gchar *object_path,
				     gpointer user_data);

static GDBusInterfaceInfo **prv_subtree_introspect(
	GDBusConnection *connection,
	const gchar *sender,
	const gchar *object_path,
	const gchar *node,
	gpointer user_data);

static const GDBusInterfaceVTable *prv_subtree_dispatch(
	GDBusConnection *connection,
	const gchar *sender,
	const gchar *object_path,
	const gchar *interface_name,
	const gchar *node,
	gpointer *out_user_data,
	gpointer user_data);


static const GDBusSubtreeVTable gSubtreeVtable = {
	prv_subtree_enumerate,
	prv_subtree_introspect,
	prv_subtree_dispatch
};

static gchar **prv_subtree_enumerate(GDBusConnection *connection,
				     const gchar *sender,
				     const gchar *object_path,
				     gpointer user_data)
{
	return g_malloc0(sizeof(gchar *));
}

static GDBusInterfaceInfo **prv_subtree_introspect(
	GDBusConnection *connection,
	const gchar *sender,
	const gchar *object_path,
	const gchar *node,
	gpointer user_data)
{
	msu_upnp_t *upnp = user_data;
	GDBusInterfaceInfo **retval = g_new(GDBusInterfaceInfo *,
					    MSU_INTERFACE_INFO_MAX + 1);
	GDBusInterfaceInfo *info;
	unsigned int i;
	gboolean root_object = FALSE;
	const gchar *slash;

	/* All objects in the hierarchy support the same interface.  Strictly
	   speaking this is not correct as it will allow ListChildren to be
	   executed on a mediaitem object.  However, returning the correct
	   interface here would be too inefficient.  We would need to either
	   cache the type of all objects encountered so far or issue a UPnP
	   request here to determine the objects type.  Best to let the client
	   call ListChildren on a item.  This will lead to an error when we
	   execute the UPnP command and we can return an error then.

	   We do know however that the root objects are containers.  Therefore
	   we can remove the MediaItem2 interface from the root containers.  We
	   also know that only the root objects suport the MediaDevice
	   interface.
	*/

	if (msu_path_get_non_root_id(object_path, &slash))
		root_object = !slash;

	i = 0;
	info = upnp->interface_info[MSU_INTERFACE_INFO_PROPERTIES].interface;
	retval[i++] =  g_dbus_interface_info_ref(info);

	info = upnp->interface_info[MSU_INTERFACE_INFO_OBJECT].interface;
	retval[i++] =  g_dbus_interface_info_ref(info);

	info = upnp->interface_info[MSU_INTERFACE_INFO_CONTAINER].interface;
	retval[i++] =  g_dbus_interface_info_ref(info);

	if (!root_object) {
		info = upnp->interface_info[MSU_INTERFACE_INFO_ITEM].interface;
		retval[i++] =  g_dbus_interface_info_ref(info);
	} else {
		info = upnp->interface_info[
			MSU_INTERFACE_INFO_DEVICE].interface;
		retval[i++] =  g_dbus_interface_info_ref(info);
	}

	retval[i] = NULL;

	return retval;
}

static const GDBusInterfaceVTable *prv_subtree_dispatch(
	GDBusConnection *connection,
	const gchar *sender,
	const gchar *object_path,
	const gchar *interface_name,
	const gchar *node,
	gpointer *out_user_data,
	gpointer user_data)
{
	msu_upnp_t *upnp = user_data;
	const GDBusInterfaceVTable *retval = NULL;

	*out_user_data = upnp->user_data;

	if (!strcmp(MSU_INTERFACE_MEDIA_CONTAINER, interface_name))
		retval = upnp->interface_info[
			MSU_INTERFACE_INFO_CONTAINER].vtable;
	else if (!strcmp(MSU_INTERFACE_PROPERTIES, interface_name))
		retval = upnp->interface_info[
			MSU_INTERFACE_INFO_PROPERTIES].vtable;
	else if (!strcmp(MSU_INTERFACE_MEDIA_ITEM, interface_name))
		retval = upnp->interface_info[
			MSU_INTERFACE_INFO_ITEM].vtable;

	return retval;
}

static void prv_server_available_cb(GUPnPControlPoint *cp,
				    GUPnPDeviceProxy *proxy,
				    gpointer user_data)
{
	msu_upnp_t *upnp = user_data;
	const char *udn;
	msu_device_t *device;
	const gchar *ip_address;
	msu_device_context_t *context;
	unsigned int i;

	MSU_LOG_DEBUG("Enter");

	udn = gupnp_device_info_get_udn((GUPnPDeviceInfo *) proxy);
	if (!udn)
		goto on_error;

	ip_address = gupnp_context_get_host_ip(
		gupnp_control_point_get_context(cp));

	MSU_LOG_DEBUG("UDN %s", udn);
	MSU_LOG_DEBUG("IP Address %s", ip_address);

	device = g_hash_table_lookup(upnp->server_udn_map, udn);

	if (!device) {
		MSU_LOG_DEBUG("Device not found. Adding");

		if (msu_device_new(upnp->connection, proxy,
				   ip_address, &gSubtreeVtable, upnp,
				   upnp->counter, &device)) {
			upnp->counter++;
			g_hash_table_insert(upnp->server_udn_map, g_strdup(udn),
					    device);
			upnp->found_server(device->path, upnp->user_data);
		}
	} else {
		MSU_LOG_DEBUG("Device Found");

		for (i = 0; i < device->contexts->len; ++i) {
			context = g_ptr_array_index(device->contexts, i);
			if (!strcmp(context->ip_address, ip_address))
				break;
		}

		if (i == device->contexts->len) {
			MSU_LOG_DEBUG("Adding Context");
			msu_device_append_new_context(device, ip_address,
						      proxy);
		}
	}

on_error:

	MSU_LOG_DEBUG("Exit");
	MSU_LOG_DEBUG_NL();

	return;
}

static void prv_server_unavailable_cb(GUPnPControlPoint *cp,
				      GUPnPDeviceProxy *proxy,
				      gpointer user_data)
{
	msu_upnp_t *upnp = user_data;
	const char *udn;
	msu_device_t *device;
	const gchar *ip_address;
	unsigned int i;
	msu_device_context_t *context;

	MSU_LOG_DEBUG("Enter");

	udn = gupnp_device_info_get_udn((GUPnPDeviceInfo *) proxy);
	if (!udn)
		goto on_error;

	ip_address = gupnp_context_get_host_ip(
		gupnp_control_point_get_context(cp));

	MSU_LOG_DEBUG("UDN %s", udn);
	MSU_LOG_DEBUG("IP Address %s", ip_address);

	device = g_hash_table_lookup(upnp->server_udn_map, udn);
	if (!device) {
		MSU_LOG_WARNING("Device not found. Ignoring");
		goto on_error;
	}

	for (i = 0; i < device->contexts->len; ++i) {
		context = g_ptr_array_index(device->contexts, i);
		if (!strcmp(context->ip_address, ip_address))
			break;
	}

	if (i < device->contexts->len) {
		(void) g_ptr_array_remove_index(device->contexts, i);
		if (device->contexts->len == 0) {
			MSU_LOG_DEBUG("Last Context lost. Delete device");
			upnp->lost_server(device->path, upnp->user_data);
			g_hash_table_remove(upnp->server_udn_map, udn);
		}
	}

on_error:

	MSU_LOG_DEBUG("Exit");
	MSU_LOG_DEBUG_NL();

	return;
}

static void prv_on_context_available(GUPnPContextManager *context_manager,
				     GUPnPContext *context,
				     gpointer user_data)
{
	msu_upnp_t *upnp = user_data;
	GUPnPControlPoint *cp;

	cp = gupnp_control_point_new(
		context,
		"urn:schemas-upnp-org:device:MediaServer:1");

	g_signal_connect(cp, "device-proxy-available",
			 G_CALLBACK(prv_server_available_cb), upnp);

	g_signal_connect(cp, "device-proxy-unavailable",
			 G_CALLBACK(prv_server_unavailable_cb), upnp);

	gssdp_resource_browser_set_active(GSSDP_RESOURCE_BROWSER(cp), TRUE);
	gupnp_context_manager_manage_control_point(upnp->context_manager, cp);
	g_object_unref(cp);
}

msu_upnp_t *msu_upnp_new(GDBusConnection *connection,
			 msu_interface_info_t *interface_info,
			 msu_upnp_callback_t found_server,
			 msu_upnp_callback_t lost_server,
			 void *user_data)
{
	msu_upnp_t *upnp = g_new0(msu_upnp_t, 1);

	upnp->connection = connection;
	upnp->interface_info = interface_info;
	upnp->user_data = user_data;
	upnp->found_server = found_server;
	upnp->lost_server = lost_server;

	upnp->server_udn_map = g_hash_table_new_full(g_str_hash, g_str_equal,
						     g_free,
						     msu_device_delete);
	upnp->filter_map = msu_prop_maps_new();
	upnp->context_manager = gupnp_context_manager_create(0);

	g_signal_connect(upnp->context_manager, "context-available",
			 G_CALLBACK(prv_on_context_available),
			 upnp);

	return upnp;
}

void msu_upnp_delete(msu_upnp_t *upnp)
{
	if (upnp) {
		g_object_unref(upnp->context_manager);
		g_hash_table_unref(upnp->filter_map);
		g_hash_table_unref(upnp->server_udn_map);
		g_free(upnp->interface_info);
		g_free(upnp);
	}
}

GVariant *msu_upnp_get_server_ids(msu_upnp_t *upnp)
{
	GVariantBuilder vb;
	GHashTableIter iter;
	gpointer value;
	msu_device_t *device;
	GVariant *retval;

	MSU_LOG_DEBUG("Enter");

	g_variant_builder_init(&vb, G_VARIANT_TYPE("ao"));

	g_hash_table_iter_init(&iter, upnp->server_udn_map);
	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		device = value;
		MSU_LOG_DEBUG("Have device %s", device->path);
		g_variant_builder_add(&vb, "o", device->path);
	}

	retval = g_variant_ref_sink(g_variant_builder_end(&vb));

	MSU_LOG_DEBUG("Exit");

	return retval;
}

void msu_upnp_get_children(msu_upnp_t *upnp, msu_task_t *task,
			   const gchar *protocol_info,
			   GCancellable *cancellable,
			   msu_upnp_task_complete_t cb,
			   void *user_data)
{
	msu_async_cb_data_t *cb_data;
	msu_async_bas_t *cb_task_data;
	msu_device_t *device;
	gchar *upnp_filter = NULL;
	gchar *sort_by = NULL;

	MSU_LOG_DEBUG("Enter");

	MSU_LOG_DEBUG("Path: %s", task->path);
	MSU_LOG_DEBUG("Start: %u", task->ut.get_children.start);
	MSU_LOG_DEBUG("Count: %u", task->ut.get_children.count);

	cb_data = msu_async_cb_data_new(task, cb, user_data);
	cb_task_data = &cb_data->ut.bas;

	if (!msu_path_get_path_and_id(task->path, &cb_task_data->root_path,
				      &cb_data->id, &cb_data->error)) {
		MSU_LOG_WARNING("Bad path %s", task->path);

		goto on_error;
	}

	device = msu_device_from_path(cb_task_data->root_path,
				      upnp->server_udn_map);
	if (!device) {
		MSU_LOG_WARNING("Cannot locate device for %s",
			      cb_task_data->root_path);

		cb_data->error =
			g_error_new(MSU_ERROR, MSU_ERROR_OBJECT_NOT_FOUND,
				    "Cannot locate device corresponding to"
				    " the specified path");
		goto on_error;
	}

	cb_task_data->filter_mask =
		msu_props_parse_filter(upnp->filter_map,
					task->ut.get_children.filter,
					&upnp_filter);

	MSU_LOG_DEBUG("Filter Mask 0x%x", cb_task_data->filter_mask);

	sort_by = msu_sort_translate_sort_string(upnp->filter_map,
						 task->ut.get_children.sort_by);
	if (!sort_by) {
		MSU_LOG_WARNING("Invalid Sort Criteria");

		cb_data->error = g_error_new(MSU_ERROR, MSU_ERROR_BAD_QUERY,
					     "Sort Criteria are not valid");
		goto on_error;
	}

	MSU_LOG_DEBUG("Sort By %s", sort_by);

	cb_task_data->protocol_info = protocol_info;

	msu_device_get_children(device, task, cb_data,
				upnp_filter, sort_by, cancellable);

on_error:

	if (!cb_data->action)
		(void) g_idle_add(msu_async_complete_task, cb_data);

	g_free(sort_by);
	g_free(upnp_filter);

	MSU_LOG_DEBUG("Exit with %s", !cb_data->action ? "FAIL" : "SUCCESS");
}

void msu_upnp_get_all_props(msu_upnp_t *upnp, msu_task_t *task,
			    const gchar *protocol_info,
			    GCancellable *cancellable,
			    msu_upnp_task_complete_t cb,
			    void *user_data)
{
	gboolean root_object;
	msu_async_cb_data_t *cb_data;
	msu_async_get_all_t *cb_task_data;
	msu_device_t *device;

	MSU_LOG_DEBUG("Enter");

	MSU_LOG_DEBUG("Path: %s", task->path);
	MSU_LOG_DEBUG("Interface %s", task->ut.get_prop.interface_name);

	cb_data = msu_async_cb_data_new(task, cb, user_data);
	cb_task_data = &cb_data->ut.get_all;

	if (!msu_path_get_path_and_id(task->path, &cb_task_data->root_path,
				      &cb_data->id, &cb_data->error)) {
		MSU_LOG_WARNING("Bad path %s", task->path);

		goto on_error;
	}

	root_object = cb_data->id[0] == '0' && cb_data->id[1] == 0;

	MSU_LOG_DEBUG("Root Object = %d", root_object);

	device = msu_device_from_path(cb_task_data->root_path,
				      upnp->server_udn_map);
	if (!device) {
		MSU_LOG_WARNING("Cannot locate device for %s",
			      cb_task_data->root_path);

		cb_data->error =
			g_error_new(MSU_ERROR, MSU_ERROR_OBJECT_NOT_FOUND,
				    "Cannot locate device corresponding to"
				    " the specified path");
		goto on_error;
	}

	cb_task_data->protocol_info = protocol_info;

	msu_device_get_all_props(device, task, cb_data, root_object,
				 cancellable);

	MSU_LOG_DEBUG("Exit with SUCCESS");

	return;

on_error:

	(void) g_idle_add(msu_async_complete_task, cb_data);

	MSU_LOG_DEBUG("Exit with FAIL");
}

void msu_upnp_get_prop(msu_upnp_t *upnp, msu_task_t *task,
		       const gchar *protocol_info,
		       GCancellable *cancellable,
		       msu_upnp_task_complete_t cb,
		       void *user_data)
{
	gboolean root_object;
	msu_async_cb_data_t *cb_data;
	msu_async_get_prop_t *cb_task_data;
	msu_device_t *device;
	msu_prop_map_t *prop_map;
	msu_task_get_prop_t *task_data;

	MSU_LOG_DEBUG("Enter");

	MSU_LOG_DEBUG("Path: %s", task->path);
	MSU_LOG_DEBUG("Interface %s", task->ut.get_prop.interface_name);
	MSU_LOG_DEBUG("Prop.%s", task->ut.get_prop.prop_name);

	task_data = &task->ut.get_prop;
	cb_data = msu_async_cb_data_new(task, cb, user_data);
	cb_task_data = &cb_data->ut.get_prop;

	if (!msu_path_get_path_and_id(task->path, &cb_task_data->root_path,
				      &cb_data->id,
				      &cb_data->error)) {
		MSU_LOG_WARNING("Bad path %s", task->path);

		goto on_error;
	}

	root_object = cb_data->id[0] == '0' && cb_data->id[1] == 0;

	MSU_LOG_DEBUG("Root Object = %d", root_object);

	device = msu_device_from_path(cb_task_data->root_path,
				      upnp->server_udn_map);
	if (!device) {
		MSU_LOG_WARNING("Cannot locate device for %s",
			      cb_task_data->root_path);

		cb_data->error =
			g_error_new(MSU_ERROR, MSU_ERROR_OBJECT_NOT_FOUND,
				    "Cannot locate device corresponding to"
				    " the specified path");
		goto on_error;
	}

	cb_task_data->protocol_info = protocol_info;
	prop_map = g_hash_table_lookup(upnp->filter_map, task_data->prop_name);

	msu_device_get_prop(device, task, cb_data, prop_map,
			    root_object, cancellable);

	MSU_LOG_DEBUG("Exit with SUCCESS");

	return;

on_error:

	(void) g_idle_add(msu_async_complete_task, cb_data);

	MSU_LOG_DEBUG("Exit with FAIL");
}

void msu_upnp_search(msu_upnp_t *upnp, msu_task_t *task,
		     const gchar *protocol_info,
		     GCancellable *cancellable,
		     msu_upnp_task_complete_t cb,
		     void *user_data)
{
	gchar *upnp_filter = NULL;
	gchar *upnp_query = NULL;
	gchar *sort_by = NULL;
	msu_async_cb_data_t *cb_data;
	msu_async_bas_t *cb_task_data;
	msu_device_t *device;

	MSU_LOG_DEBUG("Enter");

	MSU_LOG_DEBUG("Path: %s", task->path);
	MSU_LOG_DEBUG("Query: %s", task->ut.search.query);
	MSU_LOG_DEBUG("Start: %u", task->ut.search.start);
	MSU_LOG_DEBUG("Count: %u", task->ut.search.count);

	cb_data = msu_async_cb_data_new(task, cb, user_data);
	cb_task_data = &cb_data->ut.bas;

	if (!msu_path_get_path_and_id(task->path, &cb_task_data->root_path,
				      &cb_data->id, &cb_data->error)) {
		MSU_LOG_WARNING("Bad path %s", task->path);

		goto on_error;
	}

	device = msu_device_from_path(cb_task_data->root_path,
				      upnp->server_udn_map);
	if (!device) {
		MSU_LOG_WARNING("Cannot locate device for %s",
			      cb_task_data->root_path);

		cb_data->error =
			g_error_new(MSU_ERROR, MSU_ERROR_OBJECT_NOT_FOUND,
				    "Cannot locate device corresponding to"
				    " the specified path");
		goto on_error;
	}

	cb_task_data->filter_mask =
		msu_props_parse_filter(upnp->filter_map,
				       task->ut.search.filter, &upnp_filter);

	MSU_LOG_DEBUG("Filter Mask 0x%x", cb_task_data->filter_mask);

	upnp_query = msu_search_translate_search_string(upnp->filter_map,
							task->ut.search.query);
	if (!upnp_query) {
		MSU_LOG_WARNING("Query string is not valid:%s",
			      task->ut.search.query);

		cb_data->error = g_error_new(MSU_ERROR, MSU_ERROR_BAD_QUERY,
					     "Query string is not valid.");
		goto on_error;
	}

	MSU_LOG_DEBUG("UPnP Query %s", upnp_query);

	sort_by = msu_sort_translate_sort_string(upnp->filter_map,
						 task->ut.search.sort_by);
	if (!sort_by) {
		MSU_LOG_WARNING("Invalid Sort Criteria");

		cb_data->error = g_error_new(MSU_ERROR, MSU_ERROR_BAD_QUERY,
					     "Sort Criteria are not valid");
		goto on_error;
	}

	MSU_LOG_DEBUG("Sort By %s", sort_by);

	cb_task_data->protocol_info = protocol_info;

	msu_device_search(device, task, cb_data, upnp_filter,
			  upnp_query, sort_by, cancellable);
on_error:

	if (!cb_data->action)
		(void) g_idle_add(msu_async_complete_task, cb_data);

	g_free(sort_by);
	g_free(upnp_query);
	g_free(upnp_filter);

	MSU_LOG_DEBUG("Exit with %s", !cb_data->action ? "FAIL" : "SUCCESS");
}

void msu_upnp_get_resource(msu_upnp_t *upnp, msu_task_t *task,
			   GCancellable *cancellable,
			   msu_upnp_task_complete_t cb,
			   void *user_data)
{
	msu_async_cb_data_t *cb_data;
	msu_async_get_all_t *cb_task_data;
	msu_device_t *device;
	gchar *upnp_filter = NULL;
	gchar *root_path = NULL;

	MSU_LOG_DEBUG("Enter");

	MSU_LOG_DEBUG("Protocol Info: %s ", task->ut.resource.protocol_info);

	cb_data = msu_async_cb_data_new(task, cb, user_data);
	cb_task_data = &cb_data->ut.get_all;

	if (!msu_path_get_path_and_id(task->path, &root_path, &cb_data->id,
				      &cb_data->error)) {
		MSU_LOG_WARNING("Bad path %s", task->path);

		goto on_error;
	}

	MSU_LOG_DEBUG("Root Path %s Id %s", root_path, cb_data->id);

	device = msu_device_from_path(root_path, upnp->server_udn_map);
	if (!device) {
		MSU_LOG_WARNING("Cannot locate device for %s", root_path);

		cb_data->error =
			g_error_new(MSU_ERROR, MSU_ERROR_OBJECT_NOT_FOUND,
				    "Cannot locate device corresponding to"
				    " the specified path");
		goto on_error;
	}

	cb_task_data->filter_mask =
		msu_props_parse_filter(upnp->filter_map,
				       task->ut.resource.filter, &upnp_filter);

	MSU_LOG_DEBUG("Filter Mask 0x%x", cb_task_data->filter_mask);

	msu_device_get_resource(device, task, cb_data, upnp_filter,
				cancellable);

on_error:

	if (!cb_data->action)
		(void) g_idle_add(msu_async_complete_task, cb_data);

	g_free(upnp_filter);
	g_free(root_path);

	MSU_LOG_DEBUG("Exit with %s", !cb_data->action ? "FAIL" : "SUCCESS");
}
