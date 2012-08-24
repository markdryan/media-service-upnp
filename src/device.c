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
#include <libgupnp/gupnp-error.h>

#include "device.h"
#include "error.h"
#include "interface.h"
#include "log.h"
#include "path.h"

#define MSU_SYSTEM_UPDATE_VAR "SystemUpdateID"
#define MSU_CONTAINER_UPDATE_VAR "ContainerUpdateIDs"

typedef gboolean (*msu_device_count_cb_t)(msu_async_cb_data_t *cb_data,
					  gint count);

typedef struct msu_device_count_data_t_ msu_device_count_data_t;
struct msu_device_count_data_t_ {
	msu_device_count_cb_t cb;
	msu_async_cb_data_t *cb_data;
};

typedef struct msu_device_object_builder_t_ msu_device_object_builder_t;
struct msu_device_object_builder_t_ {
	GVariantBuilder *vb;
	gchar *id;
	gboolean needs_child_count;
};

static void prv_get_child_count(msu_async_cb_data_t *cb_data,
				msu_device_count_cb_t cb, const gchar *id);
static void prv_retrieve_child_count_for_list(msu_async_cb_data_t *cb_data);
static void prv_container_update_cb(GUPnPServiceProxy *proxy,
				const char *variable,
				GValue *value,
				gpointer user_data);
static void prv_system_update_cb(GUPnPServiceProxy *proxy,
				const char *variable,
				GValue *value,
				gpointer user_data);

static void prv_msu_device_object_builder_delete(void *dob)
{
	msu_device_object_builder_t *builder = dob;

	if (builder) {
		if (builder->vb)
			g_variant_builder_unref(builder->vb);

		g_free(builder->id);
		g_free(builder);
	}
}

static void prv_msu_device_count_data_new(msu_async_cb_data_t *cb_data,
					  msu_device_count_cb_t cb,
					  msu_device_count_data_t **count_data)
{
	msu_device_count_data_t *cd;

	cd = g_new(msu_device_count_data_t, 1);
	cd->cb = cb;
	cd->cb_data = cb_data;

	*count_data = cd;
}

static void prv_msu_context_delete(gpointer context)
{
	msu_device_context_t *ctx = context;

	if (ctx) {
		if (ctx->timeout_id)
			(void) g_source_remove(ctx->timeout_id);

		if (ctx->subscribed) {
			gupnp_service_proxy_remove_notify(ctx->service_proxy,
						MSU_SYSTEM_UPDATE_VAR,
						prv_system_update_cb,
						ctx->device);
			gupnp_service_proxy_remove_notify(ctx->service_proxy,
						MSU_CONTAINER_UPDATE_VAR,
						prv_container_update_cb,
						ctx->device);
		}

		if (ctx->device_proxy)
			g_object_unref(ctx->device_proxy);

		if (ctx->service_proxy)
			g_object_unref(ctx->service_proxy);

		g_free(ctx->ip_address);
		g_free(ctx);
	}
}

static void prv_msu_context_new(const gchar *ip_address,
				GUPnPDeviceProxy *proxy,
				msu_device_t *device,
				msu_device_context_t **context)
{
	const gchar *service_type =
		"urn:schemas-upnp-org:service:ContentDirectory";
	msu_device_context_t *ctx = g_new(msu_device_context_t, 1);

	ctx->ip_address = g_strdup(ip_address);
	ctx->device_proxy = proxy;
	ctx->device = device;
	g_object_ref(proxy);
	ctx->service_proxy = (GUPnPServiceProxy *)
		gupnp_device_info_get_service((GUPnPDeviceInfo *) proxy,
					      service_type);
	ctx->subscribed = FALSE;
	ctx->timeout_id = 0;

	*context = ctx;
}

void msu_device_delete(void *device)
{
	msu_device_t *dev = device;

	if (dev) {
		if (dev->timeout_id)
			(void) g_source_remove(dev->timeout_id);

		if (dev->id)
			(void) g_dbus_connection_unregister_subtree(
				dev->connection, dev->id);

		g_ptr_array_unref(dev->contexts);
		g_free(dev->path);
		g_free(dev);
	}
}

static void prv_build_container_update_array(const gchar *root_path,
						const gchar *value,
						GVariantBuilder *builder)
{
	gchar **str_array;
	int pos = 0;
	gchar *path;

	/*
	 * value contains (id, version) pairs
	 * we must extract ids only
	 */
	str_array = g_strsplit(value, ",", 0);

	while (str_array[pos]) {
		if ((pos % 2) == 0) {
			path = msu_path_from_id(root_path, str_array[pos]);
			g_variant_builder_add(builder, "o", path);
			g_free(path);
		}

		pos++;
	}

	g_strfreev(str_array);
}

static void prv_container_update_cb(GUPnPServiceProxy *proxy,
				const char *variable,
				GValue *value,
				gpointer user_data)
{
	msu_device_t *device = user_data;
	GVariantBuilder array;

	g_variant_builder_init(&array, G_VARIANT_TYPE("ao"));
	prv_build_container_update_array(device->path,
					g_value_get_string(value),
					&array);

	(void) g_dbus_connection_emit_signal(device->connection,
			NULL,
			device->path,
			MSU_INTERFACE_MEDIA_DEVICE,
			MSU_INTERFACE_CONTAINER_UPDATE,
			g_variant_new("(@ao)", g_variant_builder_end(&array)),
			NULL);
}

static void prv_system_update_cb(GUPnPServiceProxy *proxy,
				const char *variable,
				GValue *value,
				gpointer user_data)
{
	msu_device_t *device = user_data;

	(void) g_dbus_connection_emit_signal(device->connection,
			NULL,
			device->path,
			MSU_INTERFACE_MEDIA_DEVICE,
			MSU_INTERFACE_SYSTEM_UPDATE,
			g_variant_new("(u)", g_value_get_uint(value)),
			NULL);
}

static gboolean prv_re_enable_subscription(gpointer user_data)
{
	msu_device_context_t *context = user_data;

	context->timeout_id = 0;

	return FALSE;
}

static void prv_subscription_lost_cb(GUPnPServiceProxy *proxy,
					const GError *reason,
					gpointer user_data)
{
	msu_device_context_t *context = user_data;

	if (!context->timeout_id) {
		gupnp_service_proxy_set_subscribed(context->service_proxy,
									TRUE);
		context->timeout_id = g_timeout_add_seconds(10,
						prv_re_enable_subscription,
						context);
	} else {
		g_source_remove(context->timeout_id);
		context->timeout_id = 0;
		context->subscribed = FALSE;
	}
}

void msu_device_subscribe_to_contents_change(msu_device_t *device)
{
	msu_device_context_t *context;

	context = msu_device_get_context(device);

	gupnp_service_proxy_add_notify(context->service_proxy,
				MSU_SYSTEM_UPDATE_VAR,
				G_TYPE_UINT,
				prv_system_update_cb,
				device);
	gupnp_service_proxy_add_notify(context->service_proxy,
				MSU_CONTAINER_UPDATE_VAR,
				G_TYPE_STRING,
				prv_container_update_cb,
				device);

	context->subscribed = TRUE;
	gupnp_service_proxy_set_subscribed(context->service_proxy, TRUE);

	g_signal_connect(context->service_proxy,
				"subscription-lost",
				G_CALLBACK(prv_subscription_lost_cb),
				context);
}

gboolean msu_device_new(GDBusConnection *connection,
			GUPnPDeviceProxy *proxy,
			const gchar *ip_address,
			const GDBusSubtreeVTable *vtable,
			void *user_data,
			guint counter,
			msu_device_t **device)
{
	msu_device_t *dev = g_new0(msu_device_t, 1);
	guint flags;
	guint id;
	GString *new_path = NULL;

	MSU_LOG_DEBUG("Enter");

	dev->connection = connection;
	dev->contexts = g_ptr_array_new_with_free_func(prv_msu_context_delete);
	msu_device_append_new_context(dev, ip_address, proxy);

	msu_device_subscribe_to_contents_change(dev);

	new_path = g_string_new("");
	g_string_printf(new_path, "%s/%u", MSU_SERVER_PATH, counter);

	MSU_LOG_DEBUG("Server Path %s", new_path->str);

	flags = G_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES;
	id =  g_dbus_connection_register_subtree(connection, new_path->str,
						 vtable, flags, user_data, NULL,
						 NULL);
	if (!id)
		goto on_error;

	dev->path = g_string_free(new_path, FALSE);
	dev->id = id;

	*device = dev;

	MSU_LOG_DEBUG("Exit with SUCCESS");

	return TRUE;

on_error:
	if (new_path)
		g_string_free(new_path, TRUE);

	msu_device_delete(dev);

	MSU_LOG_DEBUG("Exit with FAIL");

	return FALSE;
}

void msu_device_append_new_context(msu_device_t *device,
				   const gchar *ip_address,
				   GUPnPDeviceProxy *proxy)
{
	msu_device_context_t *context;

	prv_msu_context_new(ip_address, proxy, device, &context);
	g_ptr_array_add(device->contexts, context);
}

msu_device_t *msu_device_from_path(const gchar *path, GHashTable *device_list)
{
	GHashTableIter iter;
	gpointer value;
	msu_device_t *device;
	msu_device_t *retval = NULL;

	g_hash_table_iter_init(&iter, device_list);

	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		device = value;

		if (!strcmp(device->path, path)) {
			retval = device;
			break;
		}
	}

	return retval;
}

msu_device_context_t *msu_device_get_context(msu_device_t *device)
{
	msu_device_context_t *context;
	unsigned int i;
	const char ip4_local_prefix[] = "127.0.0.";

	for (i = 0; i < device->contexts->len; ++i) {
		context = g_ptr_array_index(device->contexts, i);

		if (!strncmp(context->ip_address, ip4_local_prefix,
			     sizeof(ip4_local_prefix) - 1) ||
		    !strcmp(context->ip_address, "::1") ||
		    !strcmp(context->ip_address, "0:0:0:0:0:0:0:1"))
			break;
	}

	if (i == device->contexts->len)
		context = g_ptr_array_index(device->contexts, 0);

	return context;
}

static void prv_found_child(GUPnPDIDLLiteParser *parser,
			    GUPnPDIDLLiteObject *object,
			    gpointer user_data)
{
	msu_async_cb_data_t *cb_data = user_data;
	msu_task_t *task = cb_data->task;
	msu_task_get_children_t *task_data = &task->ut.get_children;
	msu_async_bas_t *cb_task_data = &cb_data->ut.bas;
	msu_device_object_builder_t *builder;
	gboolean have_child_count;

	MSU_LOG_DEBUG("Enter");

	builder = g_new0(msu_device_object_builder_t, 1);

	if (GUPNP_IS_DIDL_LITE_CONTAINER(object)) {
		if (!task_data->containers)
			goto on_error;
	} else {
		if (!task_data->items)
			goto on_error;
	}

	builder->vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	if (!msu_props_add_object(builder->vb, object, cb_task_data->root_path,
				  task->path, cb_task_data->filter_mask))
		goto on_error;

	if (GUPNP_IS_DIDL_LITE_CONTAINER(object)) {
		msu_props_add_container(builder->vb,
					(GUPnPDIDLLiteContainer *) object,
					cb_task_data->filter_mask,
					&have_child_count);

		if (!have_child_count && (cb_task_data->filter_mask &
					  MSU_UPNP_MASK_PROP_CHILD_COUNT)) {
			builder->needs_child_count = TRUE;
			builder->id = g_strdup(
				gupnp_didl_lite_object_get_id(object));
			cb_task_data->need_child_count = TRUE;
		}
	} else {
		msu_props_add_item(builder->vb, object,
				   cb_task_data->filter_mask,
				   cb_task_data->protocol_info);
	}

	g_ptr_array_add(cb_task_data->vbs, builder);

	MSU_LOG_DEBUG("Exit with SUCCESS");

	return;

on_error:

	prv_msu_device_object_builder_delete(builder);

	MSU_LOG_DEBUG("Exit with FAIL");
}

static GVariant *prv_children_result_to_variant(msu_async_cb_data_t *cb_data)
{
	guint i;
	msu_device_object_builder_t *builder;
	msu_async_bas_t *cb_task_data = &cb_data->ut.bas;
	GVariantBuilder vb;

	g_variant_builder_init(&vb, G_VARIANT_TYPE("aa{sv}"));

	for (i = 0; i < cb_task_data->vbs->len; ++i) {
		builder = g_ptr_array_index(cb_task_data->vbs, i);
		g_variant_builder_add(&vb, "@a{sv}",
				      g_variant_builder_end(builder->vb));
	}

	return  g_variant_builder_end(&vb);
}

static void prv_get_search_ex_result(msu_async_cb_data_t *cb_data)
{
	GVariant *out_params[2];
	msu_async_bas_t *cb_task_data = &cb_data->ut.bas;

	out_params[0] = prv_children_result_to_variant(cb_data);
	out_params[1] = g_variant_new_uint32(cb_task_data->max_count);

	cb_data->result = g_variant_ref_sink(
		g_variant_new_tuple(out_params, 2));
}

static void prv_get_children_result(msu_async_cb_data_t *cb_data)
{
	GVariant *retval = prv_children_result_to_variant(cb_data);
	cb_data->result =  g_variant_ref_sink(retval);
}

static gboolean prv_child_count_for_list_cb(msu_async_cb_data_t *cb_data,
					    gint count)
{
	msu_async_bas_t *cb_task_data = &cb_data->ut.bas;
	msu_device_object_builder_t *builder;

	builder = g_ptr_array_index(cb_task_data->vbs, cb_task_data->retrieved);
	msu_props_add_child_count(builder->vb, count);
	cb_task_data->retrieved++;
	prv_retrieve_child_count_for_list(cb_data);

	return cb_task_data->retrieved >= cb_task_data->vbs->len;
}

static void prv_retrieve_child_count_for_list(msu_async_cb_data_t *cb_data)
{
	msu_async_bas_t *cb_task_data = &cb_data->ut.bas;
	msu_device_object_builder_t *builder;
	guint i;

	for (i = cb_task_data->retrieved; i < cb_task_data->vbs->len; ++i) {
		builder = g_ptr_array_index(cb_task_data->vbs, i);

		if (builder->needs_child_count)
			break;
	}

	cb_task_data->retrieved = i;

	if (i < cb_task_data->vbs->len)
		prv_get_child_count(cb_data, prv_child_count_for_list_cb,
				    builder->id);
	else
		cb_task_data->get_children_cb(cb_data);
}

static void prv_get_children_cb(GUPnPServiceProxy *proxy,
				GUPnPServiceProxyAction *action,
				gpointer user_data)
{
	gchar *result = NULL;
	GUPnPDIDLLiteParser *parser = NULL;
	GError *upnp_error = NULL;
	msu_async_cb_data_t *cb_data = user_data;
	msu_async_bas_t *cb_task_data = &cb_data->ut.bas;

	MSU_LOG_DEBUG("Enter");

	if (!gupnp_service_proxy_end_action(cb_data->proxy, cb_data->action,
					    &upnp_error,
					    "Result", G_TYPE_STRING,
					    &result, NULL)) {
		MSU_LOG_WARNING("Browse operation failed: %s",
			      upnp_error->message);

		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_OPERATION_FAILED,
					     "Browse operation failed: %s",
					     upnp_error->message);
		goto on_error;
	}

	MSU_LOG_DEBUG("GetChildren result: %s", result);

	parser = gupnp_didl_lite_parser_new();

	g_signal_connect(parser, "object-available" ,
			 G_CALLBACK(prv_found_child), cb_data);

	cb_task_data->vbs = g_ptr_array_new_with_free_func(
		prv_msu_device_object_builder_delete);

	if (!gupnp_didl_lite_parser_parse_didl(parser, result, &upnp_error)
		&& upnp_error->code != GUPNP_XML_ERROR_EMPTY_NODE) {
		MSU_LOG_WARNING("Unable to parse results of browse: %s",
			      upnp_error->message);

		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_OPERATION_FAILED,
					     "Unable to parse results of "
					     "browse: %s", upnp_error->message);
		goto on_error;
	}

	if (cb_task_data->need_child_count) {
		MSU_LOG_DEBUG("Need to retrieve ChildCounts");

		cb_task_data->get_children_cb = prv_get_children_result;
		prv_retrieve_child_count_for_list(cb_data);
		goto no_complete;
	} else {
		prv_get_children_result(cb_data);
	}

on_error:

	(void) g_idle_add(msu_async_complete_task, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

no_complete:

	if (upnp_error)
		g_error_free(upnp_error);

	if (parser)
		g_object_unref(parser);

	g_free(result);

	MSU_LOG_DEBUG("Exit");
}

void msu_device_get_children(msu_device_t *device,  msu_task_t *task,
			     msu_async_cb_data_t *cb_data,
			     const gchar *upnp_filter, const gchar *sort_by,
			     GCancellable *cancellable)
{
	msu_device_context_t *context;

	MSU_LOG_DEBUG("Enter");

	context = msu_device_get_context(device);

	cb_data->action =
		gupnp_service_proxy_begin_action(context->service_proxy,
						 "Browse",
						 prv_get_children_cb,
						 cb_data,
						 "ObjectID", G_TYPE_STRING,
						 cb_data->id,

						 "BrowseFlag", G_TYPE_STRING,
						 "BrowseDirectChildren",

						 "Filter", G_TYPE_STRING,
						 upnp_filter,

						 "StartingIndex", G_TYPE_INT,
						 task->ut.get_children.start,
						 "RequestedCount", G_TYPE_INT,
						 task->ut.get_children.count,
						 "SortCriteria", G_TYPE_STRING,
						 sort_by,
						 NULL);
	cb_data->proxy = context->service_proxy;

	cb_data->cancel_id =
		g_cancellable_connect(cancellable,
				      G_CALLBACK(msu_async_task_cancelled),
				      cb_data, NULL);
	cb_data->cancellable = cancellable;

	MSU_LOG_DEBUG("Exit");
}

static void prv_get_item(GUPnPDIDLLiteParser *parser,
			 GUPnPDIDLLiteObject *object,
			 gpointer user_data)
{
	msu_async_cb_data_t *cb_data = user_data;
	msu_async_get_all_t *cb_task_data = &cb_data->ut.get_all;

	if (!GUPNP_IS_DIDL_LITE_CONTAINER(object))
		msu_props_add_item(cb_task_data->vb, object, 0xffffffff,
				   cb_task_data->protocol_info);
	else
		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_UNKNOWN_INTERFACE,
					     "Interface not supported on "
					     "container.");
}

static void prv_get_container(GUPnPDIDLLiteParser *parser,
		       GUPnPDIDLLiteObject *object,
		       gpointer user_data)
{
	msu_async_cb_data_t *cb_data = user_data;
	msu_async_get_all_t *cb_task_data = &cb_data->ut.get_all;
	gboolean have_child_count;

	if (GUPNP_IS_DIDL_LITE_CONTAINER(object)) {
		msu_props_add_container(cb_task_data->vb,
					(GUPnPDIDLLiteContainer *) object,
					0xffffffff,
					&have_child_count);
		if (!have_child_count)
			cb_task_data->need_child_count = TRUE;
	} else {
		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_UNKNOWN_INTERFACE,
					     "Interface not supported on "
					     "item.");
	}
}

static void prv_get_object(GUPnPDIDLLiteParser *parser,
			   GUPnPDIDLLiteObject *object,
			   gpointer user_data)
{
	msu_async_cb_data_t *cb_data = user_data;
	msu_async_get_all_t *cb_task_data = &cb_data->ut.get_all;
	const char *id;
	const char *parent_path;
	gchar *path = NULL;

	id = gupnp_didl_lite_object_get_parent_id(object);

	if (!id || !strcmp(id, "-1") || !strcmp(id, "")) {
		parent_path = cb_task_data->root_path;
	} else {
		path = msu_path_from_id(cb_task_data->root_path, id);
		parent_path = path;
	}

	if (!msu_props_add_object(cb_task_data->vb, object,
				  cb_task_data->root_path,
				  parent_path, 0xffffffff))
		cb_data->error = g_error_new(MSU_ERROR, MSU_ERROR_BAD_RESULT,
					     "Unable to retrieve mandatory "
					     " object properties");
	g_free(path);
}

static void prv_get_all(GUPnPDIDLLiteParser *parser,
			GUPnPDIDLLiteObject *object,
			gpointer user_data)
{
	msu_async_cb_data_t *cb_data = user_data;
	msu_async_get_all_t *cb_task_data = &cb_data->ut.get_all;
	gboolean have_child_count;

	prv_get_object(parser, object, user_data);

	if (!cb_data->error) {
		if (GUPNP_IS_DIDL_LITE_CONTAINER(object)) {
			msu_props_add_container(
				cb_task_data->vb,
				(GUPnPDIDLLiteContainer *)
				object, 0xffffffff,
				&have_child_count);
			if (!have_child_count)
				cb_task_data->need_child_count = TRUE;
		} else {
			msu_props_add_item(cb_task_data->vb, object,
					   0xffffffff,
					   cb_task_data->protocol_info);
		}
	}
}

static gboolean prv_get_all_child_count_cb(msu_async_cb_data_t *cb_data,
				       gint count)
{
	msu_async_get_all_t *cb_task_data = &cb_data->ut.get_all;

	msu_props_add_child_count(cb_task_data->vb, count);
	cb_data->result = g_variant_ref_sink(g_variant_builder_end(
						     cb_task_data->vb));
	return TRUE;
}

static void prv_get_all_ms2spec_props_cb(GUPnPServiceProxy *proxy,
					 GUPnPServiceProxyAction *action,
					 gpointer user_data)
{
	GError *upnp_error = NULL;
	gchar *result = NULL;
	GUPnPDIDLLiteParser *parser = NULL;
	msu_async_cb_data_t *cb_data = user_data;
	msu_async_get_all_t *cb_task_data = &cb_data->ut.get_all;

	MSU_LOG_DEBUG("Enter");

	if (!gupnp_service_proxy_end_action(cb_data->proxy, cb_data->action,
					    &upnp_error,
					    "Result", G_TYPE_STRING,
					    &result, NULL)) {
		MSU_LOG_WARNING("Browse operation failed: %s",
			      upnp_error->message);

		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_OPERATION_FAILED,
					     "Browse operation failed: %s",
					     upnp_error->message);
		goto on_error;
	}

	MSU_LOG_DEBUG("GetMS2SpecProps result: %s", result);

	parser = gupnp_didl_lite_parser_new();

	g_signal_connect(parser, "object-available" , cb_task_data->prop_func,
			 cb_data);

	if (!gupnp_didl_lite_parser_parse_didl(parser, result, &upnp_error)) {
		if (upnp_error->code == GUPNP_XML_ERROR_EMPTY_NODE) {
			MSU_LOG_WARNING("Property not defined for object");

			cb_data->error =
				g_error_new(MSU_ERROR,
					    MSU_ERROR_UNKNOWN_PROPERTY,
					    "Property not defined for object");
		} else {
			MSU_LOG_WARNING("Unable to parse results of browse: %s",
				      upnp_error->message);

			cb_data->error =
				g_error_new(MSU_ERROR,
					    MSU_ERROR_OPERATION_FAILED,
					    "Unable to parse results of "
					    "browse: %s",
					    upnp_error->message);
		}
		goto on_error;
	}

	if (cb_data->error)
		goto on_error;

	if (cb_task_data->need_child_count) {
		MSU_LOG_DEBUG("Need Child Count");

		prv_get_child_count(cb_data, prv_get_all_child_count_cb,
			cb_data->id);

		goto no_complete;
	} else {
		cb_data->result = g_variant_ref_sink(g_variant_builder_end(
							     cb_task_data->vb));
	}

on_error:

	(void) g_idle_add(msu_async_complete_task, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

no_complete:

	if (upnp_error)
		g_error_free(upnp_error);

	if (parser)
		g_object_unref(parser);

	g_free(result);

	MSU_LOG_DEBUG("Exit");
}

static void prv_get_all_ms2spec_props(msu_device_context_t *context,
				      GCancellable *cancellable,
				      msu_async_cb_data_t *cb_data)
{
	msu_async_get_all_t *cb_task_data = &cb_data->ut.get_all;
	msu_task_t *task = cb_data->task;
	msu_task_get_props_t *task_data = &task->ut.get_props;

	MSU_LOG_DEBUG("Enter called");

	if (!strcmp(MSU_INTERFACE_MEDIA_CONTAINER, task_data->interface_name))
		cb_task_data->prop_func = G_CALLBACK(prv_get_container);
	else if (!strcmp(MSU_INTERFACE_MEDIA_ITEM, task_data->interface_name))
		cb_task_data->prop_func = G_CALLBACK(prv_get_item);
	else if (!strcmp(MSU_INTERFACE_MEDIA_OBJECT, task_data->interface_name))
		cb_task_data->prop_func = G_CALLBACK(prv_get_object);
	else  if (!strcmp("", task_data->interface_name))
		cb_task_data->prop_func = G_CALLBACK(prv_get_all);
	else {
		MSU_LOG_WARNING("Interface is unknown.");

		cb_data->error =
			g_error_new(MSU_ERROR, MSU_ERROR_UNKNOWN_INTERFACE,
				    "Interface is unknown.");
		goto on_error;
	}

	cb_data->action = gupnp_service_proxy_begin_action(
		context->service_proxy, "Browse",
		prv_get_all_ms2spec_props_cb, cb_data,
		"ObjectID", G_TYPE_STRING, cb_data->id,
		"BrowseFlag", G_TYPE_STRING, "BrowseMetadata",
		"Filter", G_TYPE_STRING, "*",
		"StartingIndex", G_TYPE_INT, 0,
		"RequestedCount", G_TYPE_INT, 0,
		"SortCriteria", G_TYPE_STRING,
		"", NULL);

	cb_data->proxy = context->service_proxy;

	cb_data->cancel_id =
		g_cancellable_connect(cancellable,
				      G_CALLBACK(msu_async_task_cancelled),
				      cb_data, NULL);
	cb_data->cancellable = cancellable;

	MSU_LOG_DEBUG("Exit with SUCCESS");

	return;

on_error:

	(void) g_idle_add(msu_async_complete_task, cb_data);

	MSU_LOG_DEBUG("Exit with FAIL");

	return;
}

void msu_device_get_all_props(msu_device_t *device,  msu_task_t *task,
			      msu_async_cb_data_t *cb_data,
			      gboolean root_object,
			      GCancellable *cancellable)
{
	msu_async_get_all_t *cb_task_data;
	msu_task_get_props_t *task_data = &task->ut.get_props;
	msu_device_context_t *context;

	MSU_LOG_DEBUG("Enter");

	context = msu_device_get_context(device);
	cb_task_data = &cb_data->ut.get_all;

	cb_task_data->vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	if (!strcmp(task_data->interface_name, MSU_INTERFACE_MEDIA_DEVICE)) {
		if (root_object) {
			msu_props_add_device(
				(GUPnPDeviceInfo *) context->device_proxy,
				cb_task_data->vb);

			cb_data->result =
				g_variant_ref_sink(g_variant_builder_end(
							   cb_task_data->vb));
		} else {
			cb_data->error =
				g_error_new(MSU_ERROR,
					    MSU_ERROR_UNKNOWN_INTERFACE,
					    "Interface is only valid on "
					    "root objects.");
		}

		(void) g_idle_add(msu_async_complete_task, cb_data);

	} else if (strcmp(task_data->interface_name, "")) {
		prv_get_all_ms2spec_props(context, cancellable, cb_data);
	} else {
		if (root_object)
			msu_props_add_device(
				(GUPnPDeviceInfo *) context->device_proxy,
				cb_task_data->vb);

		prv_get_all_ms2spec_props(context, cancellable, cb_data);
	}

	MSU_LOG_DEBUG("Exit");
}

static void prv_get_object_property(GUPnPDIDLLiteParser *parser,
				    GUPnPDIDLLiteObject *object,
				    gpointer user_data)
{
	msu_async_cb_data_t *cb_data = user_data;
	msu_task_t *task = cb_data->task;
	msu_task_get_prop_t *task_data = &task->ut.get_prop;
	msu_async_get_prop_t *cb_task_data = &cb_data->ut.get_prop;

	if (cb_data->result)
		goto on_error;

	cb_data->result = msu_props_get_object_prop(task_data->prop_name,
						    cb_task_data->root_path,
						    object);

on_error:

	return;
}

static void prv_get_item_property(GUPnPDIDLLiteParser *parser,
				  GUPnPDIDLLiteObject *object,
				  gpointer user_data)
{
	msu_async_cb_data_t *cb_data = user_data;
	msu_task_t *task = cb_data->task;
	msu_task_get_prop_t *task_data = &task->ut.get_prop;
	msu_async_get_prop_t *cb_task_data = &cb_data->ut.get_prop;

	if (cb_data->result)
		goto on_error;

	cb_data->result = msu_props_get_item_prop(task_data->prop_name, object,
						  cb_task_data->protocol_info);

on_error:

	return;
}

static void prv_get_container_property(GUPnPDIDLLiteParser *parser,
				       GUPnPDIDLLiteObject *object,
				       gpointer user_data)
{
	msu_async_cb_data_t *cb_data = user_data;
	msu_task_t *task = cb_data->task;
	msu_task_get_prop_t *task_data = &task->ut.get_prop;

	if (cb_data->result)
		goto on_error;

	cb_data->result = msu_props_get_container_prop(task_data->prop_name,
						       object);

on_error:

	return;
}

static void prv_get_all_property(GUPnPDIDLLiteParser *parser,
				 GUPnPDIDLLiteObject *object,
				 gpointer user_data)
{
	msu_async_cb_data_t *cb_data = user_data;

	prv_get_object_property(parser, object, user_data);

	if (cb_data->result)
		goto on_error;

	if (GUPNP_IS_DIDL_LITE_CONTAINER(object))
		prv_get_container_property(parser, object, user_data);
	else
		prv_get_item_property(parser, object, user_data);

on_error:

	return;
}

static gboolean prv_get_child_count_cb(msu_async_cb_data_t *cb_data,
				   gint count)
{
	MSU_LOG_DEBUG("Enter");

	MSU_LOG_DEBUG("Count %d", count);

	cb_data->result =  g_variant_ref_sink(
		g_variant_new_uint32((guint) count));

	MSU_LOG_DEBUG("Exit");

	return TRUE;
}

static void prv_count_children_cb(GUPnPServiceProxy *proxy,
				  GUPnPServiceProxyAction *action,
				  gpointer user_data)
{
	msu_device_count_data_t *count_data = user_data;
	msu_async_cb_data_t *cb_data = count_data->cb_data;
	GError *upnp_error = NULL;
	gint count;
	gboolean complete = FALSE;

	MSU_LOG_DEBUG("Enter");

	if (!gupnp_service_proxy_end_action(cb_data->proxy, cb_data->action,
					    &upnp_error,
					    "TotalMatches", G_TYPE_INT,
					    &count,
					    NULL)) {
		MSU_LOG_WARNING("Browse operation failed: %s",
			      upnp_error->message);

		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_OPERATION_FAILED,
					     "Browse operation failed: %s",
					     upnp_error->message);
		goto on_error;
	}

	complete = count_data->cb(cb_data, count);

on_error:

	g_free(user_data);

	if (cb_data->error || complete) {
		(void) g_idle_add(msu_async_complete_task, cb_data);
		g_cancellable_disconnect(cb_data->cancellable,
					 cb_data->cancel_id);
	}

	if (upnp_error)
		g_error_free(upnp_error);

	MSU_LOG_DEBUG("Exit");
}

static void prv_get_child_count(msu_async_cb_data_t *cb_data,
				msu_device_count_cb_t cb, const gchar *id)
{
	msu_device_count_data_t *count_data;

	MSU_LOG_DEBUG("Enter");

	prv_msu_device_count_data_new(cb_data, cb, &count_data);
	cb_data->action =
		gupnp_service_proxy_begin_action(cb_data->proxy,
						 "Browse",
						 prv_count_children_cb,
						 count_data,
						 "ObjectID", G_TYPE_STRING, id,

						 "BrowseFlag", G_TYPE_STRING,
						 "BrowseDirectChildren",

						 "Filter", G_TYPE_STRING, "",

						 "StartingIndex", G_TYPE_INT,
						 0,

						 "RequestedCount", G_TYPE_INT,
						 1,

						 "SortCriteria", G_TYPE_STRING,
						 "",

						 NULL);

	MSU_LOG_DEBUG("Exit with SUCCESS");
}

static void prv_get_ms2spec_prop_cb(GUPnPServiceProxy *proxy,
				    GUPnPServiceProxyAction *action,
				    gpointer user_data)
{
	GError *upnp_error = NULL;
	gchar *result = NULL;
	GUPnPDIDLLiteParser *parser = NULL;
	msu_async_cb_data_t *cb_data = user_data;
	msu_async_get_prop_t *cb_task_data = &cb_data->ut.get_prop;
	msu_task_get_prop_t *task_data = &cb_data->task->ut.get_prop;

	MSU_LOG_DEBUG("Enter");

	if (!gupnp_service_proxy_end_action(cb_data->proxy, cb_data->action,
					    &upnp_error,
					    "Result", G_TYPE_STRING,
					    &result, NULL)) {
		MSU_LOG_WARNING("Browse operation failed: %s",
			      upnp_error->message);

		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_OPERATION_FAILED,
					     "Browse operation failed: %s",
					     upnp_error->message);
		goto on_error;
	}

	MSU_LOG_DEBUG("GetMS2SpecProp result: %s", result);

	parser = gupnp_didl_lite_parser_new();

	g_signal_connect(parser, "object-available" , cb_task_data->prop_func,
			 cb_data);

	if (!gupnp_didl_lite_parser_parse_didl(parser, result, &upnp_error)) {
		if (upnp_error->code == GUPNP_XML_ERROR_EMPTY_NODE) {
			MSU_LOG_WARNING("Property not defined for object");

			cb_data->error =
				g_error_new(MSU_ERROR,
					    MSU_ERROR_UNKNOWN_PROPERTY,
					    "Property not defined for object");
		} else {
			MSU_LOG_WARNING("Unable to parse results of browse: %s",
				      upnp_error->message);

			cb_data->error =
				g_error_new(MSU_ERROR,
					    MSU_ERROR_OPERATION_FAILED,
					    "Unable to parse results of "
					    "browse: %s",
					    upnp_error->message);
		}
		goto on_error;
	}

	if (!cb_data->result) {
		MSU_LOG_WARNING("Property not defined for object");

		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_UNKNOWN_PROPERTY,
					     "Property not defined for object");
	}

on_error:

	if (cb_data->error && !strcmp(task_data->prop_name,
				      MSU_INTERFACE_PROP_CHILD_COUNT)) {
		MSU_LOG_DEBUG("ChildCount not supported by server");

		g_error_free(cb_data->error);
		cb_data->error = NULL;
		prv_get_child_count(cb_data, prv_get_child_count_cb,
				    cb_data->id);
	} else {
		(void) g_idle_add(msu_async_complete_task, cb_data);
		g_cancellable_disconnect(cb_data->cancellable,
					 cb_data->cancel_id);
	}

	if (upnp_error)
		g_error_free(upnp_error);

	if (parser)
		g_object_unref(parser);

	g_free(result);

	MSU_LOG_DEBUG("Exit");
}

static void prv_get_ms2spec_prop(msu_device_context_t *context,
				 msu_prop_map_t *prop_map,
				 msu_task_get_prop_t *task_data,
				 GCancellable *cancellable,
				 msu_async_cb_data_t *cb_data)
{
	msu_async_get_prop_t *cb_task_data;
	const gchar *filter;

	MSU_LOG_DEBUG("Enter");

	cb_task_data = &cb_data->ut.get_prop;

	if (!prop_map) {
		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_UNKNOWN_PROPERTY,
					     "Unknown property");
		goto on_error;
	}

	filter = prop_map->filter ? prop_map->upnp_prop_name : "";

	if (!strcmp(MSU_INTERFACE_MEDIA_CONTAINER, task_data->interface_name)) {
		cb_task_data->prop_func =
			G_CALLBACK(prv_get_container_property);
	} else if (!strcmp(MSU_INTERFACE_MEDIA_ITEM,
			   task_data->interface_name)) {
		cb_task_data->prop_func = G_CALLBACK(prv_get_item_property);
	} else if (!strcmp(MSU_INTERFACE_MEDIA_OBJECT,
			   task_data->interface_name)) {
		cb_task_data->prop_func = G_CALLBACK(prv_get_object_property);
	} else  if (!strcmp("", task_data->interface_name)) {
		cb_task_data->prop_func = G_CALLBACK(prv_get_all_property);
	} else {
		MSU_LOG_WARNING("Interface is unknown.%s",
			      task_data->interface_name);

		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_UNKNOWN_INTERFACE,
					     "Interface is unknown.");
		goto on_error;
	}

	cb_data->action = gupnp_service_proxy_begin_action(
		context->service_proxy, "Browse",
		prv_get_ms2spec_prop_cb,
		cb_data,
		"ObjectID", G_TYPE_STRING, cb_data->id,
		"BrowseFlag", G_TYPE_STRING,
		"BrowseMetadata",
		"Filter", G_TYPE_STRING, filter,
		"StartingIndex", G_TYPE_INT, 0,
		"RequestedCount", G_TYPE_INT, 0,
		"SortCriteria", G_TYPE_STRING,
		"",
		NULL);

	cb_data->proxy = context->service_proxy;

	cb_data->cancel_id =
		g_cancellable_connect(cancellable,
				      G_CALLBACK(msu_async_task_cancelled),
				      cb_data, NULL);
	cb_data->cancellable = cancellable;

	MSU_LOG_DEBUG("Exit with SUCCESS");

	return;

on_error:

	(void) g_idle_add(msu_async_complete_task, cb_data);

	MSU_LOG_DEBUG("Exit with FAIL");

	return;
}

void msu_device_get_prop(msu_device_t *device,  msu_task_t *task,
			 msu_async_cb_data_t *cb_data,
			 msu_prop_map_t *prop_map, gboolean root_object,
			 GCancellable *cancellable)
{
	msu_task_get_prop_t *task_data = &task->ut.get_prop;
	msu_device_context_t *context;

	MSU_LOG_DEBUG("Enter");

	context = msu_device_get_context(device);

	if (!strcmp(task_data->interface_name, MSU_INTERFACE_MEDIA_DEVICE)) {
		if (root_object) {
			cb_data->result =
				msu_props_get_device_prop(
					(GUPnPDeviceInfo *)
					context->device_proxy,
					task_data->prop_name);
			if (!cb_data->result)
				cb_data->error = g_error_new(
					MSU_ERROR,
					MSU_ERROR_UNKNOWN_PROPERTY,
					"Unknown property");
		} else {
			cb_data->error =
				g_error_new(MSU_ERROR,
					    MSU_ERROR_UNKNOWN_INTERFACE,
					    "Interface is unknown.");
		}

		(void) g_idle_add(msu_async_complete_task, cb_data);

	} else if (strcmp(task_data->interface_name, "")) {
		prv_get_ms2spec_prop(context, prop_map, &task->ut.get_prop,
				     cancellable, cb_data);
	} else {
		if (root_object)
			cb_data->result = msu_props_get_device_prop(
				(GUPnPDeviceInfo *)
				context->device_proxy,
				task_data->prop_name);

		if (cb_data->result)
			(void) g_idle_add(msu_async_complete_task, cb_data);
		else
			prv_get_ms2spec_prop(context, prop_map,
					     &task->ut.get_prop, cancellable,
					     cb_data);
	}

	MSU_LOG_DEBUG("Exit");
}

static void prv_found_target(GUPnPDIDLLiteParser *parser,
			     GUPnPDIDLLiteObject *object,
			     gpointer user_data)
{
	msu_async_cb_data_t *cb_data = user_data;
	msu_async_bas_t *cb_task_data = &cb_data->ut.bas;
	const char *id;
	const char *parent_path;
	gchar *path = NULL;
	gboolean have_child_count;
	msu_device_object_builder_t *builder;

	MSU_LOG_DEBUG("Enter");

	builder = g_new0(msu_device_object_builder_t, 1);

	id = gupnp_didl_lite_object_get_parent_id(object);

	if (!id || !strcmp(id, "-1") || !strcmp(id, "")) {
		parent_path = cb_task_data->root_path;
	} else {
		path = msu_path_from_id(cb_task_data->root_path, id);
		parent_path = path;
	}

	builder->vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	if (!msu_props_add_object(builder->vb, object, cb_task_data->root_path,
				  parent_path, cb_task_data->filter_mask))
		goto on_error;

	if (GUPNP_IS_DIDL_LITE_CONTAINER(object)) {
		msu_props_add_container(builder->vb,
					(GUPnPDIDLLiteContainer *) object,
					cb_task_data->filter_mask,
					&have_child_count);

		if (!have_child_count && (cb_task_data->filter_mask &
					  MSU_UPNP_MASK_PROP_CHILD_COUNT)) {
			builder->needs_child_count = TRUE;
			builder->id = g_strdup(
				gupnp_didl_lite_object_get_id(object));
			cb_task_data->need_child_count = TRUE;
		}
	} else {
		msu_props_add_item(builder->vb, object,
				   cb_task_data->filter_mask,
				   cb_task_data->protocol_info);
	}

	g_ptr_array_add(cb_task_data->vbs, builder);
	g_free(path);

	MSU_LOG_DEBUG("Exit with SUCCESS");

	return;

on_error:

	g_free(path);
	prv_msu_device_object_builder_delete(builder);

	MSU_LOG_DEBUG("Exit with FAIL");
}

static void prv_search_cb(GUPnPServiceProxy *proxy,
			  GUPnPServiceProxyAction *action,
			  gpointer user_data)
{
	gchar *result = NULL;
	GUPnPDIDLLiteParser *parser = NULL;
	GError *upnp_error = NULL;
	msu_async_cb_data_t *cb_data = user_data;
	msu_async_bas_t *cb_task_data = &cb_data->ut.bas;

	MSU_LOG_DEBUG("Enter");

	if (!gupnp_service_proxy_end_action(cb_data->proxy, cb_data->action,
					    &upnp_error,
					    "Result", G_TYPE_STRING,
					    &result,
					    "TotalMatches", G_TYPE_INT,
					    &cb_task_data->max_count,
					    NULL)) {

		MSU_LOG_WARNING("Search operation failed %s",
			      upnp_error->message);

		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_OPERATION_FAILED,
					     "Search operation failed: %s",
					     upnp_error->message);
		goto on_error;
	}

	parser = gupnp_didl_lite_parser_new();

	cb_task_data->vbs = g_ptr_array_new_with_free_func(
		prv_msu_device_object_builder_delete);

	g_signal_connect(parser, "object-available" ,
			 G_CALLBACK(prv_found_target), cb_data);

	MSU_LOG_DEBUG("Server Search result: %s", result);

	if (!gupnp_didl_lite_parser_parse_didl(parser, result, &upnp_error)
		&& upnp_error->code != GUPNP_XML_ERROR_EMPTY_NODE) {
		MSU_LOG_WARNING("Unable to parse results of search: %s",
			      upnp_error->message);

		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_OPERATION_FAILED,
					     "Unable to parse results of "
					     "search: %s", upnp_error->message);
		goto on_error;
	}

	if (cb_task_data->need_child_count) {
		MSU_LOG_DEBUG("Need to retrieve child count");

		if (cb_data->task->multiple_retvals)
			cb_task_data->get_children_cb =
				prv_get_search_ex_result;
		else
			cb_task_data->get_children_cb = prv_get_children_result;
		prv_retrieve_child_count_for_list(cb_data);
		goto no_complete;
	} else {
		if (cb_data->task->multiple_retvals)
			prv_get_search_ex_result(cb_data);
		else
			prv_get_children_result(cb_data);
	}

on_error:

	(void) g_idle_add(msu_async_complete_task, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

no_complete:

	if (parser)
		g_object_unref(parser);

	g_free(result);

	if (upnp_error)
		g_error_free(upnp_error);

	MSU_LOG_DEBUG("Exit");
}

void msu_device_search(msu_device_t *device,  msu_task_t *task,
		       msu_async_cb_data_t *cb_data,
		       const gchar *upnp_filter, const gchar *upnp_query,
		       const gchar *sort_by, GCancellable *cancellable)
{
	msu_device_context_t *context;

	MSU_LOG_DEBUG("Enter");

	context = msu_device_get_context(device);

	cb_data->action = gupnp_service_proxy_begin_action(
		context->service_proxy, "Search",
		prv_search_cb,
		cb_data,
		"ContainerID", G_TYPE_STRING, cb_data->id,
		"SearchCriteria", G_TYPE_STRING, upnp_query,
		"Filter", G_TYPE_STRING, upnp_filter,
		"StartingIndex", G_TYPE_INT, task->ut.search.start,
		"RequestedCount", G_TYPE_INT, task->ut.search.count,
		"SortCriteria", G_TYPE_STRING, sort_by,
		NULL);

	cb_data->proxy = context->service_proxy;

	cb_data->cancel_id =
		g_cancellable_connect(cancellable,
				      G_CALLBACK(msu_async_task_cancelled),
				      cb_data, NULL);
	cb_data->cancellable = cancellable;

	MSU_LOG_DEBUG("Exit");
}

static void prv_get_resource(GUPnPDIDLLiteParser *parser,
			     GUPnPDIDLLiteObject *object,
			     gpointer user_data)
{
	msu_async_cb_data_t *cb_data = user_data;
	msu_task_t *task = cb_data->task;
	msu_task_get_resource_t *task_data = &task->ut.resource;
	msu_async_get_all_t *cb_task_data = &cb_data->ut.get_all;

	MSU_LOG_DEBUG("Enter");

	msu_props_add_resource(cb_task_data->vb, object,
			       cb_task_data->filter_mask,
			       task_data->protocol_info);
}

void msu_device_get_resource(msu_device_t *device,  msu_task_t *task,
			     msu_async_cb_data_t *cb_data,
			     const gchar *upnp_filter,
			     GCancellable *cancellable)
{
	msu_async_get_all_t *cb_task_data;
	msu_device_context_t *context;

	context = msu_device_get_context(device);
	cb_task_data = &cb_data->ut.get_all;

	cb_task_data->vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	cb_task_data->prop_func = G_CALLBACK(prv_get_resource);

	cb_data->action = gupnp_service_proxy_begin_action(
		context->service_proxy, "Browse",
		prv_get_all_ms2spec_props_cb, cb_data,
		"ObjectID", G_TYPE_STRING, cb_data->id,
		"BrowseFlag", G_TYPE_STRING, "BrowseMetadata",
		"Filter", G_TYPE_STRING, upnp_filter,
		"StartingIndex", G_TYPE_INT, 0,
		"RequestedCount", G_TYPE_INT, 0,
		"SortCriteria", G_TYPE_STRING,
		"", NULL);

	cb_data->proxy = context->service_proxy;

	cb_data->cancel_id =
		g_cancellable_connect(cancellable,
				      G_CALLBACK(msu_async_task_cancelled),
				      cb_data, NULL);
	cb_data->cancellable = cancellable;

	MSU_LOG_DEBUG("Exit");
}
