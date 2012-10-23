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
#include <libsoup/soup.h>

#include "chain-task.h"
#include "device.h"
#include "error.h"
#include "interface.h"
#include "log.h"
#include "path.h"

#define MSU_SYSTEM_UPDATE_VAR "SystemUpdateID"
#define MSU_CONTAINER_UPDATE_VAR "ContainerUpdateIDs"
#define MEDIA_SERVER_DEVICE_TYPE "urn:schemas-upnp-org:device:MediaServer:"

#define MSU_UPLOAD_STATUS_IN_PROGRESS "IN_PROGRESS"
#define MSU_UPLOAD_STATUS_CANCELLED "CANCELLED"
#define MSU_UPLOAD_STATUS_ERROR "ERROR"
#define MSU_UPLOAD_STATUS_COMPLETED "COMPLETED"

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

typedef struct msu_device_upload_job_t_ msu_device_upload_job_t;

typedef struct msu_device_upload_t_ msu_device_upload_t;
struct msu_device_upload_t_ {
	SoupSession *soup_session;
	SoupMessage *msg;
	GMappedFile *mapped_file;
	const gchar *status;
	guint64 bytes_uploaded;
	guint64 bytes_to_upload;
};

struct msu_device_upload_job_t_ {
	gint upload_id;
	msu_device_t *device;
	guint remove_idle;
};

/* Private structure used in chain task */
typedef struct prv_new_device_ct_t_ prv_new_device_ct_t;
struct prv_new_device_ct_t_ {
	msu_device_t *dev;
	GDBusConnection *connection;
	const GDBusSubtreeVTable *vtable;
	void *user_data;
	GHashTable *property_map;
	guint counter;
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
static void prv_msu_device_upload_delete(gpointer up);
static void prv_msu_upload_job_delete(gpointer up);
static void prv_get_sr_token_for_props(GUPnPServiceProxy *proxy,
			     msu_device_t *device,
			     GCancellable *cancellable,
			     msu_async_cb_data_t *cb_data);

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
		dev->shutting_down = TRUE;
		g_hash_table_unref(dev->upload_jobs);
		g_hash_table_unref(dev->uploads);

		if (dev->timeout_id)
			(void) g_source_remove(dev->timeout_id);

		if (dev->id)
			(void) g_dbus_connection_unregister_subtree(
				dev->connection, dev->id);

		g_ptr_array_unref(dev->contexts);
		g_free(dev->path);
		g_variant_unref(dev->search_caps);
		g_variant_unref(dev->sort_caps);
		g_variant_unref(dev->sort_ext_caps);
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

	MSU_LOG_DEBUG("Container Update %s", g_value_get_string(value));

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
	GVariantBuilder *array;
	GVariant *val;
	msu_device_t *device = user_data;
	guint suid = g_value_get_uint(value);

	MSU_LOG_DEBUG("System Update %u", suid);

	device->system_update_id = suid;

	array = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(array, "{sv}", MSU_INTERFACE_SYSTEM_UPDATE_ID,
			      g_variant_new_uint32(suid));
	val = g_variant_new("(s@a{sv}as)", MSU_INTERFACE_MEDIA_DEVICE,
			    g_variant_builder_end(array),
			    NULL);

	(void) g_dbus_connection_emit_signal(device->connection,
					     NULL,
					     device->path,
					     MSU_INTERFACE_PROPERTIES,
					     MSU_INTERFACE_PROPERTIES_CHANGED,
					     val,
					     NULL);

	g_variant_builder_unref(array);
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
		gupnp_service_proxy_remove_notify(context->service_proxy,
						  MSU_SYSTEM_UPDATE_VAR,
						  prv_system_update_cb,
						  context->device);
		gupnp_service_proxy_remove_notify(context->service_proxy,
						  MSU_CONTAINER_UPDATE_VAR,
						  prv_container_update_cb,
						  context->device);

		context->timeout_id = 0;
		context->subscribed = FALSE;
	}
}

void msu_device_subscribe_to_contents_change(msu_device_t *device)
{
	msu_device_context_t *context;

	context = msu_device_get_context(device, NULL);

	MSU_LOG_DEBUG("Subscribe for events on context: %s",
		      context->ip_address);

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

static void prv_get_sort_ext_capabilities_analyze(msu_device_t *device,
						  gchar *result)
{
	gchar **caps;
	gchar **saved;
#if MSU_LOG_LEVEL & MSU_LOG_LEVEL_DEBUG
	gchar *props;
#endif
	GVariantBuilder sort_ext_caps_vb;

	g_variant_builder_init(&sort_ext_caps_vb, G_VARIANT_TYPE("as"));

	caps = g_strsplit(result, ",", 0);
	saved = caps;

	while (caps && *caps) {
		g_variant_builder_add(&sort_ext_caps_vb, "s", *caps);
		caps++;
	}

	g_strfreev(saved);

	device->sort_ext_caps = g_variant_ref_sink(g_variant_builder_end(
							&sort_ext_caps_vb));

#if MSU_LOG_LEVEL & MSU_LOG_LEVEL_DEBUG
	props = g_variant_print(device->sort_ext_caps, FALSE);
	MSU_LOG_DEBUG("%s = %s", MSU_INTERFACE_PROP_SV_SORT_EXT_CAPABILITIES,
				 props);
	g_free(props);
#endif
}

static void prv_get_sort_ext_capabilities_cb(GUPnPServiceProxy *proxy,
					   GUPnPServiceProxyAction *action,
					   gpointer user_data)
{
	gchar *result = NULL;
	GError *error = NULL;
	prv_new_device_ct_t *priv_t = (prv_new_device_ct_t *) user_data;

	if (!gupnp_service_proxy_end_action(proxy, action, &error,
					    "SortExtensionCaps",
					    G_TYPE_STRING, &result, NULL)) {
		MSU_LOG_WARNING(
			"GetSortExtensionCapabilities operation failed: %s",
			error->message);
		goto on_error;
	}

	MSU_LOG_DEBUG("GetSortExtensionCapabilities result: %s", result);

	prv_get_sort_ext_capabilities_analyze(priv_t->dev, result);

on_error:

	if (error)
		g_error_free(error);

	g_free(result);
}

static GUPnPServiceProxyAction *prv_get_sort_ext_capabilities(
							msu_chain_task_t *chain,
							gboolean *failed)
{
	msu_device_t *device;
	msu_device_context_t *context;

	device = msu_chain_task_get_device(chain);
	context = msu_device_get_context(device, NULL);
	*failed = FALSE;

	return gupnp_service_proxy_begin_action(context->service_proxy,
						"GetSortExtensionCapabilities",
						msu_chain_task_begin_action_cb,
						chain, NULL);
}

static void prv_get_capabilities_analyze(GHashTable *property_map,
					 gchar *result,
					 GVariant **variant)
{
	gchar **caps;
	gchar **saved;
	gchar *prop_name;
	GVariantBuilder caps_vb;

	g_variant_builder_init(&caps_vb, G_VARIANT_TYPE("as"));

	caps = g_strsplit(result, ",", 0);
	saved = caps;

	while (caps && *caps) {
		prop_name = g_hash_table_lookup(property_map, *caps);

		if (prop_name)
			g_variant_builder_add(&caps_vb, "s", prop_name);

		caps++;
	}

	g_strfreev(saved);

	*variant = g_variant_ref_sink(g_variant_builder_end(&caps_vb));

#if MSU_LOG_LEVEL & MSU_LOG_LEVEL_DEBUG
	prop_name = g_variant_print(*variant, FALSE);
	MSU_LOG_DEBUG("%s = %s", MSU_INTERFACE_PROP_SV_SEARCH_CAPABILITIES,
				 prop_name);
	g_free(prop_name);
#endif
}

static void prv_get_sort_capabilities_cb(GUPnPServiceProxy *proxy,
					   GUPnPServiceProxyAction *action,
					   gpointer user_data)
{
	gchar *result = NULL;
	GError *error = NULL;
	prv_new_device_ct_t *priv_t = (prv_new_device_ct_t *) user_data;

	if (!gupnp_service_proxy_end_action(proxy, action, &error, "SortCaps",
					    G_TYPE_STRING, &result, NULL)) {
		MSU_LOG_WARNING("GetSortCapabilities operation failed: %s",
				error->message);
		goto on_error;
	}

	MSU_LOG_DEBUG("GetSortCapabilities result: %s", result);

	prv_get_capabilities_analyze(priv_t->property_map, result,
				     &priv_t->dev->sort_caps);

on_error:

	if (error)
		g_error_free(error);

	g_free(result);
}

static GUPnPServiceProxyAction *prv_get_sort_capabilities(
					msu_chain_task_t *chain,
					gboolean *failed)
{
	msu_device_t *device;
	msu_device_context_t *context;

	device = msu_chain_task_get_device(chain);
	context = msu_device_get_context(device, NULL);
	*failed = FALSE;

	return gupnp_service_proxy_begin_action(context->service_proxy,
						"GetSortCapabilities",
						msu_chain_task_begin_action_cb,
						chain, NULL);
}

static void prv_get_search_capabilities_cb(GUPnPServiceProxy *proxy,
					   GUPnPServiceProxyAction *action,
					   gpointer user_data)
{
	gchar *result = NULL;
	GError *error = NULL;
	prv_new_device_ct_t *priv_t = (prv_new_device_ct_t *) user_data;

	if (!gupnp_service_proxy_end_action(proxy, action, &error, "SearchCaps",
					    G_TYPE_STRING, &result, NULL)) {
		MSU_LOG_WARNING("GetSearchCapabilities operation failed: %s",
				error->message);
		goto on_error;
	}

	MSU_LOG_DEBUG("GetSearchCapabilities result: %s", result);

	prv_get_capabilities_analyze(priv_t->property_map, result,
				     &priv_t->dev->search_caps);

on_error:

	if (error)
		g_error_free(error);

	g_free(result);
}

static GUPnPServiceProxyAction *prv_get_search_capabilities(
					msu_chain_task_t *chain,
					gboolean *failed)
{
	msu_device_t *device;
	msu_device_context_t *context;

	device = msu_chain_task_get_device(chain);
	context = msu_device_get_context(device, NULL);
	*failed = FALSE;

	return gupnp_service_proxy_begin_action(context->service_proxy,
						"GetSearchCapabilities",
						msu_chain_task_begin_action_cb,
						chain, NULL);
}

static GUPnPServiceProxyAction *prv_subscribe(msu_chain_task_t *chain,
					      gboolean *failed)
{
	msu_device_t *device;

	device = msu_chain_task_get_device(chain);
	msu_device_subscribe_to_contents_change(device);

	*failed = FALSE;

	return NULL;
}

static GUPnPServiceProxyAction *prv_declare(msu_chain_task_t *chain,
					    gboolean *failed)
{
	guint flags;
	guint id;
	gchar *new_path = NULL;
	msu_device_t *device;
	prv_new_device_ct_t *priv_t;

	device = msu_chain_task_get_device(chain);

	priv_t = (prv_new_device_ct_t *) msu_chain_task_get_user_data(chain);

	new_path = g_strdup_printf("%s/%u", MSU_SERVER_PATH, priv_t->counter);

	MSU_LOG_DEBUG("Server Path %s", new_path);

	flags = G_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES;
	id =  g_dbus_connection_register_subtree(priv_t->connection,
						 new_path,
						 priv_t->vtable,
						 flags,
						 priv_t->user_data,
						 NULL, NULL);
	if (id) {
		device->path = new_path;
		device->id = id;

		device->uploads = g_hash_table_new_full(g_int_hash, g_int_equal,
						g_free,
						prv_msu_device_upload_delete);
		device->upload_jobs =
			g_hash_table_new_full(g_int_hash, g_int_equal,
					      g_free,
					      prv_msu_upload_job_delete);

	} else
		MSU_LOG_ERROR("g_dbus_connection_register_subtree FAILED");

	*failed = (!id);

	return NULL;
}

msu_device_t *msu_device_new(GDBusConnection *connection,
			     GUPnPDeviceProxy *proxy,
			     const gchar *ip_address,
			     const GDBusSubtreeVTable *vtable,
			     void *user_data,
			     GHashTable *property_map,
			     guint counter,
			     msu_chain_task_t *chain)
{
	msu_device_t *dev;
	prv_new_device_ct_t *priv_t;

	MSU_LOG_DEBUG("New Device on %s", ip_address);

	dev = g_new0(msu_device_t, 1);
	priv_t = g_new0(prv_new_device_ct_t, 1);

	dev->connection = connection;
	dev->contexts = g_ptr_array_new_with_free_func(prv_msu_context_delete);

	priv_t->dev = dev;
	priv_t->connection = connection;
	priv_t->vtable = vtable;
	priv_t->user_data = user_data;
	priv_t->property_map = property_map;
	priv_t->counter = counter;

	msu_device_append_new_context(dev, ip_address, proxy);

	msu_chain_task_add(chain, prv_get_search_capabilities, dev,
			   prv_get_search_capabilities_cb, NULL, priv_t);

	msu_chain_task_add(chain, prv_get_sort_capabilities, dev,
			   prv_get_sort_capabilities_cb, NULL, priv_t);

	msu_chain_task_add(chain, prv_get_sort_ext_capabilities, dev,
			   prv_get_sort_ext_capabilities_cb, NULL, priv_t);

	msu_chain_task_add(chain, prv_subscribe, dev, NULL, NULL, NULL);
	msu_chain_task_add(chain, prv_declare, dev, NULL, g_free, priv_t);

	msu_chain_task_start(chain);

	return dev;
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

msu_device_context_t *msu_device_get_context(msu_device_t *device,
					     msu_client_t *client)
{
	msu_device_context_t *context;
	unsigned int i;
	const char ip4_local_prefix[] = "127.0.0.";
	gboolean prefer_local;
	gboolean is_local;

	prefer_local = (client && client->prefer_local_addresses);

	for (i = 0; i < device->contexts->len; ++i) {
		context = g_ptr_array_index(device->contexts, i);

		is_local = (!strncmp(context->ip_address, ip4_local_prefix,
						sizeof(ip4_local_prefix) - 1) ||
			    !strcmp(context->ip_address, "::1") ||
			    !strcmp(context->ip_address, "0:0:0:0:0:0:0:1"));

		if (prefer_local == is_local)
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
				   cb_task_data->root_path,
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

void msu_device_get_children(msu_device_t *device, msu_client_t *client,
			     msu_task_t *task, msu_async_cb_data_t *cb_data,
			     const gchar *upnp_filter, const gchar *sort_by,
			     GCancellable *cancellable)
{
	msu_device_context_t *context;

	MSU_LOG_DEBUG("Enter");

	context = msu_device_get_context(device, client);

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
		msu_props_add_item(cb_task_data->vb, object,
				   cb_task_data->root_path,
				   0xffffffff,
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
			msu_props_add_item(cb_task_data->vb,
					   object,
					   cb_task_data->root_path,
					   0xffffffff,
					   cb_task_data->protocol_info);
		}
	}
}

static gboolean prv_device_subscribed(msu_device_t *device)
{
	msu_device_context_t *context;
	unsigned int i;
	gboolean subscribed = FALSE;

	for (i = 0; i < device->contexts->len; ++i) {
		context = g_ptr_array_index(device->contexts, i);
		if (context->subscribed) {
			subscribed = TRUE;
			break;
		}
	}

	return subscribed;
}

static void prv_system_update_id_for_prop_cb(GUPnPServiceProxy *proxy,
				    GUPnPServiceProxyAction *action,
				    gpointer user_data)
{
	GError *upnp_error = NULL;
	guint id;
	msu_async_cb_data_t *cb_data = user_data;

	MSU_LOG_DEBUG("Enter");

	if (!gupnp_service_proxy_end_action(proxy, action, &upnp_error,
					    "Id", G_TYPE_UINT,
					    &id,
					    NULL)) {
		MSU_LOG_ERROR("Unable to retrieve ServiceUpdateID: %s %s",
			       g_quark_to_string(upnp_error->domain),
			       upnp_error->message);

		cb_data->error = g_error_new(MSU_ERROR,
				MSU_ERROR_OPERATION_FAILED,
				"Unable to retrieve ServiceUpdateID: %s",
				upnp_error->message);

		goto on_complete;
	}

	cb_data->result = g_variant_ref_sink(g_variant_new_uint32(id));

on_complete:

	(void) g_idle_add(msu_async_complete_task, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

	if (upnp_error)
		g_error_free(upnp_error);

	MSU_LOG_DEBUG("Exit");
}

static void prv_get_system_update_id_for_prop(GUPnPServiceProxy *proxy,
				     msu_device_t *device,
				     GCancellable *cancellable,
				     msu_async_cb_data_t *cb_data)
{
	guint suid;

	MSU_LOG_DEBUG("Enter");

	if (prv_device_subscribed(device)) {
		suid = device->system_update_id;

		cb_data->result = g_variant_ref_sink(
					g_variant_new_uint32(suid));

		(void) g_idle_add(msu_async_complete_task, cb_data);

		goto on_complete;
	}

	gupnp_service_proxy_begin_action(proxy, "GetSystemUpdateID",
					 prv_system_update_id_for_prop_cb,
					 cb_data,
					 NULL);

	cb_data->proxy = proxy;

	cb_data->cancel_id =
	g_cancellable_connect(cancellable,
				      G_CALLBACK(msu_async_task_cancelled),
				      cb_data, NULL);
	cb_data->cancellable = cancellable;

on_complete:

	MSU_LOG_DEBUG("Exit");
}

static void prv_system_update_id_for_props_cb(GUPnPServiceProxy *proxy,
				    GUPnPServiceProxyAction *action,
				    gpointer user_data)
{
	GError *upnp_error = NULL;
	guint id;
	msu_async_cb_data_t *cb_data = user_data;
	msu_async_get_all_t *cb_task_data = &cb_data->ut.get_all;

	MSU_LOG_DEBUG("Enter");

	if (!gupnp_service_proxy_end_action(proxy, action, &upnp_error,
					    "Id", G_TYPE_UINT,
					    &id,
					    NULL)) {
		MSU_LOG_ERROR("Unable to retrieve ServiceUpdateID: %s %s",
			       g_quark_to_string(upnp_error->domain),
			       upnp_error->message);

		cb_data->error = g_error_new(MSU_ERROR,
				MSU_ERROR_OPERATION_FAILED,
				"Unable to retrieve ServiceUpdateID: %s",
				upnp_error->message);

		goto on_complete;
	}

	g_variant_builder_add(cb_task_data->vb, "{sv}",
			      MSU_SYSTEM_UPDATE_VAR,
			      g_variant_new_uint32(id));

	cb_data->result = g_variant_ref_sink(g_variant_builder_end(
						cb_task_data->vb));

on_complete:

	if (!cb_data->error)
		prv_get_sr_token_for_props(proxy, cb_task_data->device,
					   cb_data->cancellable, cb_data);
	else {
		(void) g_idle_add(msu_async_complete_task, cb_data);
		g_cancellable_disconnect(cb_data->cancellable,
					 cb_data->cancel_id);
	}

	if (upnp_error)
		g_error_free(upnp_error);

	MSU_LOG_DEBUG("Exit");
}

static void prv_get_system_update_id_for_props(GUPnPServiceProxy *proxy,
				     msu_device_t *device,
				     GCancellable *cancellable,
				     msu_async_cb_data_t *cb_data)
{
	msu_async_get_all_t *cb_task_data;
	guint suid;

	MSU_LOG_DEBUG("Enter");

	if (prv_device_subscribed(device)) {
		suid = device->system_update_id;

		cb_task_data = &cb_data->ut.get_all;

		g_variant_builder_add(cb_task_data->vb, "{sv}",
				      MSU_SYSTEM_UPDATE_VAR,
				      g_variant_new_uint32(suid));

		prv_get_sr_token_for_props(proxy, device, cancellable, cb_data);

		goto on_complete;
	}

	gupnp_service_proxy_begin_action(proxy, "GetSystemUpdateID",
					 prv_system_update_id_for_props_cb,
					 cb_data,
					 NULL);

	cb_data->proxy = proxy;

	cb_data->cancel_id =
	g_cancellable_connect(cancellable,
				      G_CALLBACK(msu_async_task_cancelled),
				      cb_data, NULL);
	cb_data->cancellable = cancellable;

on_complete:

	MSU_LOG_DEBUG("Exit");
}

static int prv_get_media_server_version(msu_device_t *device)
{
	msu_device_context_t *context;
	const char *device_type;
	const char *version;

	context = msu_device_get_context(device, NULL);
	device_type = gupnp_device_info_get_device_type((GUPnPDeviceInfo *)
							context->device_proxy);

	if (strncmp(device_type, MEDIA_SERVER_DEVICE_TYPE,
					sizeof(MEDIA_SERVER_DEVICE_TYPE) - 1))
		goto on_error;

	version = device_type + sizeof(MEDIA_SERVER_DEVICE_TYPE) - 1;

	return atoi(version);

on_error:

	return -1;
}

static void prv_service_reset_for_prop_cb(GUPnPServiceProxy *proxy,
					  GUPnPServiceProxyAction *action,
					  gpointer user_data)
{
	GError *upnp_error = NULL;
	gchar *token = NULL;
	msu_async_cb_data_t *cb_data = user_data;

	MSU_LOG_DEBUG("Enter");

	if (!gupnp_service_proxy_end_action(proxy, action, &upnp_error,
					    "ResetToken", G_TYPE_STRING,
					    &token,
					    NULL)) {
		MSU_LOG_ERROR("Unable to retrieve ServiceResetToken: %s %s",
				g_quark_to_string(upnp_error->domain),
				 upnp_error->message);


		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_OPERATION_FAILED,
					     "GetServiceResetToken failed: %s",
					     upnp_error->message);

		goto on_complete;
	}

	cb_data->result = g_variant_ref_sink(g_variant_new_string(token));

	g_free(token);

	MSU_LOG_DEBUG("Service Reset %s", token);

on_complete:

	(void) g_idle_add(msu_async_complete_task, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

	if (upnp_error)
		g_error_free(upnp_error);

	MSU_LOG_DEBUG("Exit");
}

static void prv_get_sr_token_for_prop(GUPnPServiceProxy *proxy,
			     msu_device_t *device,
			     GCancellable *cancellable,
			     msu_async_cb_data_t *cb_data)
{
	MSU_LOG_DEBUG("Enter");

	if (prv_get_media_server_version(device) < 3) {
		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_UNKNOWN_PROPERTY,
					     "Unknown property");

		(void) g_idle_add(msu_async_complete_task, cb_data);

		goto on_error;
	}

	gupnp_service_proxy_begin_action(proxy, "GetServiceResetToken",
					 prv_service_reset_for_prop_cb,
					 cb_data,
					 NULL);

	cb_data->proxy = proxy;

	cb_data->cancel_id = g_cancellable_connect(cancellable,
					G_CALLBACK(msu_async_task_cancelled),
					cb_data, NULL);
	cb_data->cancellable = cancellable;

on_error:

	MSU_LOG_DEBUG("Exit");
}

static void prv_service_reset_for_props_cb(GUPnPServiceProxy *proxy,
					  GUPnPServiceProxyAction *action,
					  gpointer user_data)
{
	GError *upnp_error = NULL;
	gchar *token = NULL;
	msu_async_cb_data_t *cb_data = user_data;
	msu_async_get_all_t *cb_task_data;

	MSU_LOG_DEBUG("Enter");

	if (!gupnp_service_proxy_end_action(proxy, action, &upnp_error,
					    "ResetToken", G_TYPE_STRING,
					    &token,
					    NULL)) {
		MSU_LOG_ERROR("Unable to retrieve ServiceResetToken: %s %s",
			      g_quark_to_string(upnp_error->domain),
			      upnp_error->message);

		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_OPERATION_FAILED,
					     "GetServiceResetToken failed: %s",
					     upnp_error->message);

		goto on_complete;
	}

	cb_task_data = &cb_data->ut.get_all;
	g_variant_builder_add(cb_task_data->vb, "{sv}",
			      MSU_INTERFACE_PROP_ESV_SERVICE_RESET_TOKEN,
			      g_variant_new_string(token));

	cb_data->result = g_variant_ref_sink(g_variant_builder_end(
						cb_task_data->vb));

	g_free(token);

	MSU_LOG_DEBUG("Service Reset %s", token);

on_complete:

	(void) g_idle_add(msu_async_complete_task, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

	if (upnp_error)
		g_error_free(upnp_error);

	MSU_LOG_DEBUG("Exit");
}

static void prv_get_sr_token_for_props(GUPnPServiceProxy *proxy,
			     msu_device_t *device,
			     GCancellable *cancellable,
			     msu_async_cb_data_t *cb_data)
{
	msu_async_get_all_t *cb_task_data;

	MSU_LOG_DEBUG("Enter");

	if (prv_get_media_server_version(device) < 3) {
		cb_task_data = &cb_data->ut.get_all;

		cb_data->result = g_variant_ref_sink(g_variant_builder_end(
							cb_task_data->vb));

		goto on_complete; /* No error here, just skip the property */
	}

	gupnp_service_proxy_begin_action(proxy, "GetServiceResetToken",
					 prv_service_reset_for_props_cb,
					 cb_data,
					 NULL);

	cb_data->proxy = proxy;

	cb_data->cancel_id = g_cancellable_connect(cancellable,
					G_CALLBACK(msu_async_task_cancelled),
					cb_data, NULL);
	cb_data->cancellable = cancellable;

on_complete:

	(void) g_idle_add(msu_async_complete_task, cb_data);

	MSU_LOG_DEBUG("Exit");
}

static gboolean prv_get_all_child_count_cb(msu_async_cb_data_t *cb_data,
				       gint count)
{
	msu_async_get_all_t *cb_task_data = &cb_data->ut.get_all;

	msu_props_add_child_count(cb_task_data->vb, count);
	if (cb_task_data->device_object)
		prv_get_system_update_id_for_props(cb_data->proxy,
						   cb_task_data->device,
						   cb_data->cancellable,
						   cb_data);
	else
		cb_data->result = g_variant_ref_sink(g_variant_builder_end(
						     cb_task_data->vb));

	return !cb_task_data->device_object;
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
	} else if (cb_data->task->type == MSU_TASK_GET_ALL_PROPS &&
						cb_task_data->device_object) {
		prv_get_system_update_id_for_props(proxy, cb_task_data->device,
						cb_data->cancellable, cb_data);

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

void msu_device_get_all_props(msu_device_t *device,  msu_client_t *client,
			      msu_task_t *task, msu_async_cb_data_t *cb_data,
			      gboolean root_object,
			      GCancellable *cancellable)
{
	msu_async_get_all_t *cb_task_data;
	msu_task_get_props_t *task_data = &task->ut.get_props;
	msu_device_context_t *context;

	MSU_LOG_DEBUG("Enter");

	context = msu_device_get_context(device, client);
	cb_task_data = &cb_data->ut.get_all;

	cb_task_data->vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	cb_task_data->device_object = root_object;
	cb_task_data->device = device;

	if (!strcmp(task_data->interface_name, MSU_INTERFACE_MEDIA_DEVICE)) {
		if (root_object) {
			msu_props_add_device(
				(GUPnPDeviceInfo *) context->device_proxy,
				device,
				cb_task_data->vb);

			prv_get_system_update_id_for_props(
							context->service_proxy,
							device,
							cancellable,
							cb_data);
		} else {
			cb_data->error =
				g_error_new(MSU_ERROR,
					    MSU_ERROR_UNKNOWN_INTERFACE,
					    "Interface is only valid on "
					    "root objects.");

			(void) g_idle_add(msu_async_complete_task, cb_data);
		}

	} else if (strcmp(task_data->interface_name, "")) {
		cb_task_data->device_object = FALSE;
		prv_get_all_ms2spec_props(context, cancellable, cb_data);
	} else {
		if (root_object)
			msu_props_add_device(
				(GUPnPDeviceInfo *) context->device_proxy,
				device,
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

	cb_data->result = msu_props_get_item_prop(task_data->prop_name,
						  cb_task_data->root_path,
						  object,
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

void msu_device_get_prop(msu_device_t *device, msu_client_t *client,
			 msu_task_t *task, msu_async_cb_data_t *cb_data,
			 msu_prop_map_t *prop_map, gboolean root_object,
			 GCancellable *cancellable)
{
	msu_task_get_prop_t *task_data = &task->ut.get_prop;
	msu_device_context_t *context;
	gboolean complete = FALSE;

	MSU_LOG_DEBUG("Enter");

	context = msu_device_get_context(device, client);

	if (!strcmp(task_data->interface_name, MSU_INTERFACE_MEDIA_DEVICE)) {
		if (root_object) {
			if (!strcmp(task_data->prop_name,
				MSU_INTERFACE_PROP_ESV_SYSTEM_UPDATE_ID)) {
				prv_get_system_update_id_for_prop(
							context->service_proxy,
							device,
							cancellable,
							cb_data);
			} else if (!strcmp(task_data->prop_name,
				  MSU_INTERFACE_PROP_ESV_SERVICE_RESET_TOKEN)) {
				prv_get_sr_token_for_prop(
							context->service_proxy,
							device,
							cancellable,
							cb_data);
			} else {
				cb_data->result =
					msu_props_get_device_prop(
						(GUPnPDeviceInfo *)
						context->device_proxy,
						device,
						task_data->prop_name);

				if (!cb_data->result)
					cb_data->error = g_error_new(
						MSU_ERROR,
						MSU_ERROR_UNKNOWN_PROPERTY,
						"Unknown property");

				(void) g_idle_add(msu_async_complete_task,
						  cb_data);
			}

		} else {
			cb_data->error =
				g_error_new(MSU_ERROR,
					    MSU_ERROR_UNKNOWN_INTERFACE,
					    "Interface is unknown.");

			(void) g_idle_add(msu_async_complete_task, cb_data);
		}

	} else if (strcmp(task_data->interface_name, "")) {
		prv_get_ms2spec_prop(context, prop_map, &task->ut.get_prop,
				     cancellable, cb_data);
	} else {
		if (root_object) {
			if (!strcmp(task_data->prop_name,
				MSU_INTERFACE_PROP_ESV_SYSTEM_UPDATE_ID)) {
				prv_get_system_update_id_for_prop(
							context->service_proxy,
							device,
							cancellable,
							cb_data);
				complete = TRUE;
			} else if (!strcmp(task_data->prop_name,
				  MSU_INTERFACE_PROP_ESV_SERVICE_RESET_TOKEN)) {
				prv_get_sr_token_for_prop(
							context->service_proxy,
							device,
							cancellable,
							cb_data);
				complete = TRUE;
			} else {
				cb_data->result = msu_props_get_device_prop(
					(GUPnPDeviceInfo *)
					context->device_proxy,
					device,
					task_data->prop_name);
				if (cb_data->result) {
					(void) g_idle_add(
							msu_async_complete_task,
							cb_data);
					complete = TRUE;
				}
			}
		}

		if (!complete)
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
		msu_props_add_item(builder->vb,
				   object,
				   cb_task_data->root_path,
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

void msu_device_search(msu_device_t *device, msu_client_t *client,
		       msu_task_t *task, msu_async_cb_data_t *cb_data,
		       const gchar *upnp_filter, const gchar *upnp_query,
		       const gchar *sort_by, GCancellable *cancellable)
{
	msu_device_context_t *context;

	MSU_LOG_DEBUG("Enter");

	context = msu_device_get_context(device, client);

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

void msu_device_get_resource(msu_device_t *device, msu_client_t *client,
			     msu_task_t *task, msu_async_cb_data_t *cb_data,
			     const gchar *upnp_filter,
			     GCancellable *cancellable)
{
	msu_async_get_all_t *cb_task_data;
	msu_device_context_t *context;

	context = msu_device_get_context(device, client);
	cb_task_data = &cb_data->ut.get_all;

	cb_task_data->vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	cb_task_data->prop_func = G_CALLBACK(prv_get_resource);
	cb_task_data->device_object = FALSE;

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

static gchar *prv_create_new_container_didl(const gchar *parent_id,
					    msu_task_t *task)
{
	GUPnPDIDLLiteWriter *writer;
	GUPnPDIDLLiteObject *item;
	GUPnPDIDLLiteContainer *container;
	gchar *retval;
	GVariantIter iter;
	GVariant *child_type;
	const gchar *actual_type;

	writer = gupnp_didl_lite_writer_new(NULL);
	item = GUPNP_DIDL_LITE_OBJECT(
				gupnp_didl_lite_writer_add_container(writer));
	container = GUPNP_DIDL_LITE_CONTAINER(item);

	gupnp_didl_lite_object_set_id(item, "");
	gupnp_didl_lite_object_set_title(item,
					task->ut.create_container.display_name);
	gupnp_didl_lite_object_set_parent_id(item, parent_id);
	actual_type = msu_props_media_spec_to_upnp_class(
						task->ut.create_container.type);
	gupnp_didl_lite_object_set_upnp_class(item, actual_type);
	gupnp_didl_lite_object_set_restricted(item, FALSE);
	gupnp_didl_lite_object_set_dlna_managed(item, GUPNP_OCM_FLAGS_UPLOAD);

	g_variant_iter_init(&iter, task->ut.create_container.child_types);
	while ((child_type = g_variant_iter_next_value(&iter))) {
		actual_type = msu_props_media_spec_to_upnp_class(
					g_variant_get_string(child_type, NULL));
		if (actual_type != NULL)
			gupnp_didl_lite_container_add_create_class(container,
								   actual_type);
		g_variant_unref(child_type);
	}

	retval = gupnp_didl_lite_writer_get_string(writer);

	g_object_unref(item);
	g_object_unref(writer);

	return retval;
}

static gchar *prv_create_upload_didl(const gchar *parent_id, msu_task_t *task,
				     const gchar *object_class,
				     const gchar *mime_type)
{
	GUPnPDIDLLiteWriter *writer;
	GUPnPDIDLLiteObject *item;
	gchar *retval;
	GUPnPProtocolInfo *protocol_info;
	GUPnPDIDLLiteResource *res;

	writer = gupnp_didl_lite_writer_new(NULL);
	item = GUPNP_DIDL_LITE_OBJECT(gupnp_didl_lite_writer_add_item(writer));

	gupnp_didl_lite_object_set_id(item, "");
	gupnp_didl_lite_object_set_title(item, task->ut.upload.display_name);
	gupnp_didl_lite_object_set_parent_id(item, parent_id);
	gupnp_didl_lite_object_set_upnp_class(item, object_class);
	gupnp_didl_lite_object_set_restricted(item, FALSE);

	protocol_info = gupnp_protocol_info_new();
	gupnp_protocol_info_set_mime_type(protocol_info, mime_type);
	gupnp_protocol_info_set_protocol(protocol_info, "*");
	gupnp_protocol_info_set_network(protocol_info, "*");

	res = gupnp_didl_lite_object_add_resource(item);
	gupnp_didl_lite_resource_set_protocol_info(res, protocol_info);

	/* TODO: Need to compute DLNA Profile */

	retval = gupnp_didl_lite_writer_get_string(writer);

	g_object_unref(res);
	g_object_unref(protocol_info);
	g_object_unref(item);
	g_object_unref(writer);

	return retval;
}

static void prv_extract_import_uri(GUPnPDIDLLiteParser *parser,
				   GUPnPDIDLLiteObject *object,
				   gpointer user_data)
{
	gchar **import_uri = user_data;
	GList *resources;
	GList *ptr;
	GUPnPDIDLLiteResource *res;
	const gchar *uri;

	if (!*import_uri) {
		resources = gupnp_didl_lite_object_get_resources(object);
		ptr = resources;
		while (ptr) {
			res = ptr->data;
			if (!*import_uri) {
				uri = gupnp_didl_lite_resource_get_import_uri(
					res);
				if (uri)
					*import_uri = g_strdup(uri);
			}
			g_object_unref(res);
			ptr = ptr->next;
		}

		g_list_free(resources);
	}
}

static void prv_upload_delete_cb(GUPnPServiceProxy *proxy,
				 GUPnPServiceProxyAction *action,
				 gpointer user_data)
{
	msu_async_cb_data_t *cb_data = user_data;

	MSU_LOG_DEBUG("Enter");

	(void) gupnp_service_proxy_end_action(cb_data->proxy, cb_data->action,
					      NULL, NULL);
	(void) g_idle_add(msu_async_complete_task, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

	MSU_LOG_DEBUG("Exit");
}

static void prv_msu_upload_job_delete(gpointer up_job)
{
	msu_device_upload_job_t *upload_job = up_job;

	if (up_job) {
		if (upload_job->remove_idle)
			(void) g_source_remove(upload_job->remove_idle);

		g_free(upload_job);
	}
}

static gboolean prv_remove_update_job(gpointer user_data)
{
	msu_device_upload_job_t *upload_job = user_data;
	msu_device_upload_t *upload;

	upload = g_hash_table_lookup(upload_job->device->uploads,
				     &upload_job->upload_id);
	if (upload) {
		g_hash_table_remove(upload_job->device->uploads,
				    &upload_job->upload_id);

		MSU_LOG_DEBUG("Removing Upload Object: %d",
			upload_job->upload_id);
	}

	upload_job->remove_idle = 0;
	g_hash_table_remove(upload_job->device->upload_jobs,
			    &upload_job->upload_id);

	return FALSE;
}

static void prv_post_finished(SoupSession *session, SoupMessage *msg,
			      gpointer user_data)
{
	msu_device_upload_job_t *upload_job = user_data;
	msu_device_upload_t *upload;
	gint *upload_id;

	MSU_LOG_DEBUG("Enter");

	MSU_LOG_DEBUG("Upload %u finished.  Code %u Message %s",
		      upload_job->upload_id, msg->status_code,
		      msg->reason_phrase);

	/* This is clumsy but we need to distinguish between two cases:
	   1. We cancel because the process is exitting.
	   2. We cancel because a client has called CancelUpload.

	   We could use custom SOUP error messages to distinguish the cases
	   but device->shutting_down seemed less hacky.

	   We need this check as if we are shutting down it is
	   dangerous to manipulate uploads as we are being called from its
	   destructor.
	*/

	if (upload_job->device->shutting_down) {
		MSU_LOG_DEBUG("Device shutting down. Cancelling Upload");
		goto on_error;
	}

	upload = g_hash_table_lookup(upload_job->device->uploads,
				     &upload_job->upload_id);
	if (upload) {
		upload_job->remove_idle =
			g_timeout_add(30000, prv_remove_update_job, user_data);

		if (SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
			upload->status = MSU_UPLOAD_STATUS_COMPLETED;
			upload->bytes_uploaded = upload->bytes_to_upload;
		} else if (msg->status_code == SOUP_STATUS_CANCELLED) {
			upload->status = MSU_UPLOAD_STATUS_CANCELLED;
		} else {
			upload->status = MSU_UPLOAD_STATUS_ERROR;
		}

		MSU_LOG_DEBUG("Upload Status: %s", upload->status);

		g_object_unref(upload->msg);
		upload->msg = NULL;

		g_object_unref(upload->soup_session);
		upload->soup_session = NULL;

		g_mapped_file_unref(upload->mapped_file);
		upload->mapped_file = NULL;

		upload_id = g_new(gint, 1);
		*upload_id = upload_job->upload_id;

		g_hash_table_insert(upload_job->device->upload_jobs, upload_id,
				    upload_job);

		upload_job = NULL;
	}

on_error:

	prv_msu_upload_job_delete(upload_job);

	MSU_LOG_DEBUG("Exit");
}

static void prv_msu_device_upload_delete(gpointer up)
{
	msu_device_upload_t *upload = up;

	MSU_LOG_DEBUG("Enter");

	if (upload) {
		if (upload->msg) {
			soup_session_cancel_message(upload->soup_session,
						    upload->msg,
						    SOUP_STATUS_CANCELLED);
			g_object_unref(upload->msg);
		}

		if (upload->soup_session)
			g_object_unref(upload->soup_session);

		if (upload->mapped_file)
			g_mapped_file_unref(upload->mapped_file);

		g_free(upload);
	}

	MSU_LOG_DEBUG("Exit");
}

static void prv_post_bytes_written(SoupMessage *msg, SoupBuffer *chunk,
				   gpointer user_data)
{
	msu_device_upload_t *upload = user_data;

	upload->bytes_uploaded += chunk->length;
	if (upload->bytes_uploaded > upload->bytes_to_upload)
		upload->bytes_uploaded = upload->bytes_to_upload;
}

static msu_device_upload_t *prv_msu_device_upload_new(const gchar *file_path,
						      const gchar *import_uri,
						      const gchar *mime_type,
						      GError **error)
{
	const char *body;
	gsize body_length;
	msu_device_upload_t *upload;

	MSU_LOG_DEBUG("Enter");

	upload = g_new0(msu_device_upload_t, 1);

	upload->mapped_file = g_mapped_file_new(file_path, FALSE, NULL);
	if (!upload->mapped_file) {
		MSU_LOG_WARNING("Unable to map %s into memory", file_path);

		*error = g_error_new(MSU_ERROR, MSU_ERROR_IO,
				     "Unable to map %s into memory",
				     file_path);
		goto on_error;
	}

	body = g_mapped_file_get_contents(upload->mapped_file);
	body_length = g_mapped_file_get_length(upload->mapped_file);

	upload->soup_session = soup_session_async_new();
	upload->msg = soup_message_new("POST", import_uri);

	if (!upload->msg) {
		MSU_LOG_WARNING("Invalid URI %s", import_uri);

		*error = g_error_new(MSU_ERROR, MSU_ERROR_BAD_RESULT,
				     "Invalid URI %s", import_uri);
		goto on_error;
	}
	upload->status = MSU_UPLOAD_STATUS_IN_PROGRESS;
	upload->bytes_to_upload = body_length;

	soup_message_headers_set_expectations(upload->msg->request_headers,
					      SOUP_EXPECTATION_CONTINUE);

	soup_message_set_request(upload->msg, mime_type, SOUP_MEMORY_STATIC,
				 body, body_length);
	g_signal_connect(upload->msg, "wrote-body-data",
			 G_CALLBACK(prv_post_bytes_written), upload);

	MSU_LOG_DEBUG("Exit with Success");

	return upload;

on_error:

	prv_msu_device_upload_delete(upload);

	MSU_LOG_WARNING("Exit with Fail");

	return NULL;
}

static void prv_create_container_cb(GUPnPServiceProxy *proxy,
		 GUPnPServiceProxyAction *action,
		 gpointer user_data)
{
	msu_async_cb_data_t *cb_data = user_data;
	msu_async_create_container_t *cb_task_data;
	GError *upnp_error = NULL;
	gchar *result = NULL;
	gchar *object_id = NULL;
	gchar *object_path;

	MSU_LOG_DEBUG("Enter");

	cb_task_data = &cb_data->ut.create_container;

	if (!gupnp_service_proxy_end_action(cb_data->proxy, cb_data->action,
					    &upnp_error,
					    "ObjectID", G_TYPE_STRING,
					    &object_id,
					    "Result", G_TYPE_STRING,
					    &result,
					    NULL)) {
		MSU_LOG_ERROR("Create Object operation failed: %s",
							upnp_error->message);

		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_OPERATION_FAILED,
					     "Create Object operation "
					     " failed: %s",
					     upnp_error->message);
		goto on_error;
	}

	object_path = msu_path_from_id(cb_task_data->root_path, object_id);
	cb_data->result = g_variant_ref_sink(g_variant_new_object_path(
								object_path));
	g_free(object_path);

on_error:

	(void) g_idle_add(msu_async_complete_task, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

	if (object_id)
		g_free(object_id);

	if (result)
		g_free(result);

	if (upnp_error)
		g_error_free(upnp_error);

	MSU_LOG_DEBUG("Exit");
}

static void prv_create_object_cb(GUPnPServiceProxy *proxy,
				 GUPnPServiceProxyAction *action,
				 gpointer user_data)
{
	gchar *result = NULL;
	GUPnPDIDLLiteParser *parser = NULL;
	GError *upnp_error = NULL;
	msu_async_cb_data_t *cb_data = user_data;
	msu_async_upload_t *cb_task_data = &cb_data->ut.upload;
	gchar *import_uri = NULL;
	gchar *object_id = NULL;
	gboolean delete_needed = FALSE;
	GVariant *out_params[2];
	gchar *object_path;
	msu_device_upload_t *upload;
	gint *upload_id;
	msu_device_upload_job_t *upload_job;

	MSU_LOG_DEBUG("Enter");

	if (!gupnp_service_proxy_end_action(cb_data->proxy, cb_data->action,
					    &upnp_error,
					    "ObjectID", G_TYPE_STRING,
					    &object_id,
					    "Result", G_TYPE_STRING,
					    &result,
					    NULL)) {
		MSU_LOG_WARNING("Create Object operation failed: %s",
				upnp_error->message);

		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_OPERATION_FAILED,
					     "Create Object operation "
					     " failed: %s",
					     upnp_error->message);
		goto on_error;
	}

	delete_needed = TRUE;
	parser = gupnp_didl_lite_parser_new();

	g_signal_connect(parser, "object-available" ,
			 G_CALLBACK(prv_extract_import_uri), &import_uri);

	MSU_LOG_DEBUG("Create Object Result: %s", result);

	if (!gupnp_didl_lite_parser_parse_didl(parser, result, &upnp_error)
		&& upnp_error->code != GUPNP_XML_ERROR_EMPTY_NODE) {

		MSU_LOG_WARNING("Unable to parse results of CreateObject: %s",
				upnp_error->message);

		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_OPERATION_FAILED,
					     "Unable to parse results of "
					     "CreateObject: %s",
					     upnp_error->message);
		goto on_error;
	}

	if (!import_uri) {
		MSU_LOG_WARNING("Missing Import URI");

		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_OPERATION_FAILED,
					     "Missing Import URI");
		goto on_error;
	}

	MSU_LOG_DEBUG("Import URI %s", import_uri);

	upload = prv_msu_device_upload_new(cb_data->task->ut.upload.file_path,
					   import_uri, cb_task_data->mime_type,
					   &cb_data->error);
	if (!upload)
		goto on_error;

	upload_job = g_new0(msu_device_upload_job_t, 1);
	upload_job->device = cb_task_data->device;
	upload_job->upload_id = (gint) cb_task_data->device->upload_id;

	soup_session_queue_message(upload->soup_session, upload->msg,
				   prv_post_finished, upload_job);
	g_object_ref(upload->msg);

	upload_id = g_new(gint, 1);
	*upload_id = upload_job->upload_id;
	g_hash_table_insert(cb_task_data->device->uploads, upload_id, upload);

	object_path = msu_path_from_id(cb_task_data->root_path, object_id);

	MSU_LOG_DEBUG("Upload ID %u", *upload_id);
	MSU_LOG_DEBUG("Object ID %s", object_id);
	MSU_LOG_DEBUG("Object Path %s", object_path);

	out_params[0] = g_variant_new_uint32(*upload_id);
	out_params[1] = g_variant_new_object_path(object_path);
	cb_data->result = g_variant_ref_sink(g_variant_new_tuple(out_params,
								 2));

	++cb_task_data->device->upload_id;
	if (cb_task_data->device->upload_id > G_MAXINT)
		cb_task_data->device->upload_id = 0;

	g_free(object_path);

on_error:

	if (cb_data->error && delete_needed) {
		MSU_LOG_WARNING("Upload failed deleting created object "
				"with id %s", object_id);

		cb_data->action = gupnp_service_proxy_begin_action(
			cb_data->proxy, "DestroyObject", prv_upload_delete_cb,
			cb_data, "ObjectID", G_TYPE_STRING,
			object_id, NULL);
	} else {
		(void) g_idle_add(msu_async_complete_task, cb_data);
		g_cancellable_disconnect(cb_data->cancellable,
					 cb_data->cancel_id);
	}

	g_free(object_id);
	g_free(import_uri);

	if (parser)
		g_object_unref(parser);

	g_free(result);

	if (upnp_error)
		g_error_free(upnp_error);

	MSU_LOG_DEBUG("Exit");
}

void msu_device_upload(msu_device_t *device, msu_client_t *client,
		       msu_task_t *task, const gchar *parent_id,
		       msu_async_cb_data_t *cb_data, GCancellable *cancellable)
{
	msu_device_context_t *context;
	gchar *didl;
	msu_async_upload_t *cb_task_data;

	MSU_LOG_DEBUG("Enter");
	MSU_LOG_DEBUG("Uploading file to %s", parent_id);

	context = msu_device_get_context(device, client);
	cb_task_data = &cb_data->ut.upload;

	didl = prv_create_upload_didl(parent_id, task,
				      cb_task_data->object_class,
				      cb_task_data->mime_type);

	MSU_LOG_DEBUG("DIDL: %s", didl);

	cb_data->action = gupnp_service_proxy_begin_action(
		context->service_proxy, "CreateObject",
		prv_create_object_cb, cb_data,
		"ContainerID", G_TYPE_STRING, parent_id,
		"Elements", G_TYPE_STRING, didl,
		NULL);

	cb_data->proxy = context->service_proxy;
	cb_task_data->device = device;

	cb_data->cancel_id =
		g_cancellable_connect(cancellable,
				      G_CALLBACK(msu_async_task_cancelled),
				      cb_data, NULL);
	cb_data->cancellable = cancellable;

	g_free(didl);

	MSU_LOG_DEBUG("Exit");
}

gboolean msu_device_get_upload_status(msu_device_t *device,
				      msu_task_t *task, GError **error)
{
	msu_device_upload_t *upload;
	gboolean retval = FALSE;
	GVariant *out_params[3];
	guint upload_id;

	MSU_LOG_DEBUG("Enter");

	upload_id = task->ut.upload_action.upload_id;

	upload = g_hash_table_lookup(device->uploads, &upload_id);
	if (!upload) {
		*error = g_error_new(MSU_ERROR, MSU_ERROR_OBJECT_NOT_FOUND,
				     "Unknown Upload ID %u ", upload_id);
		goto on_error;
	}

	out_params[0] = g_variant_new_string(upload->status);
	out_params[1] = g_variant_new_uint64(upload->bytes_uploaded);
	out_params[2] = g_variant_new_uint64(upload->bytes_to_upload);

	MSU_LOG_DEBUG("Upload ( %s %"G_GUINT64_FORMAT" %"G_GUINT64_FORMAT" )",
		      upload->status, upload->bytes_uploaded,
		      upload->bytes_to_upload);

	task->result = g_variant_ref_sink(g_variant_new_tuple(out_params, 3));

	retval = TRUE;

on_error:

	MSU_LOG_DEBUG("Exit");

	return retval;
}

void msu_device_get_upload_ids(msu_device_t *device, msu_task_t *task)
{
	GVariantBuilder vb;
	GHashTableIter iter;
	gpointer key;

	MSU_LOG_DEBUG("Enter");

	g_variant_builder_init(&vb, G_VARIANT_TYPE("au"));

	g_hash_table_iter_init(&iter, device->uploads);
	while (g_hash_table_iter_next(&iter, &key, NULL))
		g_variant_builder_add(&vb, "u", (guint32) (*((gint *) key)));

	task->result = g_variant_ref_sink(g_variant_builder_end(&vb));

	MSU_LOG_DEBUG("Exit");
}

gboolean msu_device_cancel_upload(msu_device_t *device, msu_task_t *task,
				  GError **error)
{
	msu_device_upload_t *upload;
	gboolean retval = FALSE;
	guint upload_id;

	MSU_LOG_DEBUG("Enter");

	upload_id = task->ut.upload_action.upload_id;

	upload = g_hash_table_lookup(device->uploads, &upload_id);
	if (!upload) {
		*error = g_error_new(MSU_ERROR, MSU_ERROR_OBJECT_NOT_FOUND,
				     "Unknown Upload ID %u ", upload_id);
		goto on_error;
	}

	if (upload->msg) {
		soup_session_cancel_message(upload->soup_session, upload->msg,
					    SOUP_STATUS_CANCELLED);
		MSU_LOG_DEBUG("Cancelling Upload %u ", upload_id);
	}

	retval = TRUE;

on_error:

	MSU_LOG_DEBUG("Exit");

	return retval;
}

static void prv_destroy_object_cb(GUPnPServiceProxy *proxy,
				  GUPnPServiceProxyAction *action,
				  gpointer user_data)
{
	GError *upnp_error = NULL;
	msu_async_cb_data_t *cb_data = user_data;

	MSU_LOG_DEBUG("Enter");

	if (!gupnp_service_proxy_end_action(cb_data->proxy, cb_data->action,
					    &upnp_error,
					    NULL)) {
		MSU_LOG_WARNING("Destroy Object operation failed: %s",
				upnp_error->message);

		cb_data->error = g_error_new(MSU_ERROR,
					     MSU_ERROR_OPERATION_FAILED,
					     "Destroy Object operation "
					     " failed: %s",
					     upnp_error->message);
	}

	(void) g_idle_add(msu_async_complete_task, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

	if (upnp_error)
		g_error_free(upnp_error);

	MSU_LOG_DEBUG("Exit");
}

void msu_device_delete_object(msu_device_t *device, msu_client_t *client,
			      msu_task_t *task,
			      msu_async_cb_data_t *cb_data,
			      GCancellable *cancellable)
{
	msu_device_context_t *context;

	MSU_LOG_DEBUG("Enter");

	context = msu_device_get_context(device, client);

	cb_data->action = gupnp_service_proxy_begin_action(
				context->service_proxy, "DestroyObject",
				prv_destroy_object_cb, cb_data,
				"ObjectID", G_TYPE_STRING, cb_data->id,
				NULL);

	cb_data->proxy = context->service_proxy;

	cb_data->cancel_id = g_cancellable_connect(cancellable,
					G_CALLBACK(msu_async_task_cancelled),
					cb_data, NULL);
	cb_data->cancellable = cancellable;

	MSU_LOG_DEBUG("Exit");
}

void msu_device_create_container(msu_device_t *device, msu_client_t *client,
				 msu_task_t *task,
				 const gchar *parent_id,
				 msu_async_cb_data_t *cb_data,
				 GCancellable *cancellable)
{
	msu_device_context_t *context;
	gchar *didl;

	MSU_LOG_DEBUG("Enter");

	context = msu_device_get_context(device, client);

	didl = prv_create_new_container_didl(parent_id, task);

	MSU_LOG_DEBUG("DIDL: %s", didl);

	cb_data->action = gupnp_service_proxy_begin_action(
				context->service_proxy, "CreateObject",
				prv_create_container_cb, cb_data,
				"ContainerID", G_TYPE_STRING, parent_id,
				"Elements", G_TYPE_STRING, didl,
				NULL);

	cb_data->proxy = context->service_proxy;

	cb_data->cancel_id =
		g_cancellable_connect(cancellable,
				      G_CALLBACK(msu_async_task_cancelled),
				      cb_data, NULL);
	cb_data->cancellable = cancellable;

	g_free(didl);

	MSU_LOG_DEBUG("Exit");
}
