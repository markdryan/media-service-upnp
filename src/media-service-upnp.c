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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <syslog.h>
#include <stdio.h>

#include "upnp.h"
#include "props.h"
#include "task.h"

#define MSU_INTERFACE_GET_VERSION "GetVersion"
#define MSU_INTERFACE_GET_SERVERS "GetServers"
#define MSU_INTERFACE_RELEASE "Release"
#define MSU_INTERFACE_SET_PROTOCOL_INFO "SetProtocolInfo"

#define MSU_INTERFACE_FOUND_SERVER "FoundServer"
#define MSU_INTERFACE_LOST_SERVER "LostServer"

#define MSU_INTERFACE_LIST_CHILDREN "ListChildren"
#define MSU_INTERFACE_LIST_CHILDREN_EX "ListChildrenEx"
#define MSU_INTERFACE_LIST_ITEMS "ListItems"
#define MSU_INTERFACE_LIST_ITEMS_EX "ListItemsEx"
#define MSU_INTERFACE_LIST_CONTAINERS "ListContainers"
#define MSU_INTERFACE_LIST_CONTAINERS_EX "ListContainersEx"
#define MSU_INTERFACE_SEARCH_OBJECTS "SearchObjects"
#define MSU_INTERFACE_SEARCH_OBJECTS_EX "SearchObjectsEx"

#define MSU_INTERFACE_GET_COMPATIBLE_RESOURCE "GetCompatibleResource"

#define MSU_INTERFACE_GET "Get"
#define MSU_INTERFACE_GET_ALL "GetAll"
#define MSU_INTERFACE_INTERFACE_NAME "InterfaceName"
#define MSU_INTERFACE_PROPERTY_NAME "PropertyName"
#define MSU_INTERFACE_PROPERTIES_VALUE "Properties"
#define MSU_INTERFACE_VALUE "value"

#define MSU_INTERFACE_VERSION "Version"
#define MSU_INTERFACE_SERVERS "Servers"


#define MSU_INTERFACE_CRITERIA "Criteria"
#define MSU_INTERFACE_DICT "Dictionary"
#define MSU_INTERFACE_PATH "Path"
#define MSU_INTERFACE_QUERY "Query"
#define MSU_INTERFACE_PROTOCOL_INFO "ProtocolInfo"


#define MSU_INTERFACE_OFFSET "Offset"
#define MSU_INTERFACE_MAX "Max"
#define MSU_INTERFACE_FILTER "Filter"
#define MSU_INTERFACE_CHILDREN "Children"
#define MSU_INTERFACE_SORT_BY "SortBy"
#define MSU_INTERFACE_TOTAL_ITEMS "TotalItems"

typedef struct msu_client_t_ msu_client_t;
struct msu_client_t_ {
	guint id;
	gchar *protocol_info;
};

typedef struct msu_context_t_ msu_context_t;
struct msu_context_t_ {
	bool error;
	guint msu_id;
	guint sig_id;
	guint idle_id;
	guint owner_id;
	GDBusNodeInfo *root_node_info;
	GDBusNodeInfo *server_node_info;
	GMainLoop *main_loop;
	GDBusConnection *connection;
	gboolean quitting;
	GPtrArray *tasks;
	GHashTable *watchers;
	GCancellable *cancellable;
	msu_upnp_t *upnp;
};

static const gchar g_msu_root_introspection[] =
	"<node>"
	"  <interface name='"MSU_INTERFACE_MANAGER"'>"
	"    <method name='"MSU_INTERFACE_GET_VERSION"'>"
	"      <arg type='s' name='"MSU_INTERFACE_VERSION"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_RELEASE"'>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_GET_SERVERS"'>"
	"      <arg type='ao' name='"MSU_INTERFACE_SERVERS"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_SET_PROTOCOL_INFO"'>"
	"      <arg type='s' name='"MSU_INTERFACE_PROTOCOL_INFO"'"
	"           direction='in'/>"
	"    </method>"
	"    <signal name='"MSU_INTERFACE_FOUND_SERVER"'>"
	"      <arg type='o' name='"MSU_INTERFACE_PATH"'/>"
	"    </signal>"
	"    <signal name='"MSU_INTERFACE_LOST_SERVER"'>"
	"      <arg type='o' name='"MSU_INTERFACE_PATH"'/>"
	"    </signal>"
	"  </interface>"
	"</node>";

static const gchar g_msu_server_introspection[] =
	"<node>"
	"  <interface name='"MSU_INTERFACE_PROPERTIES"'>"
	"    <method name='"MSU_INTERFACE_GET"'>"
	"      <arg type='s' name='"MSU_INTERFACE_INTERFACE_NAME"'"
	"           direction='in'/>"
	"      <arg type='s' name='"MSU_INTERFACE_PROPERTY_NAME"'"
	"           direction='in'/>"
	"      <arg type='v' name='"MSU_INTERFACE_VALUE"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_GET_ALL"'>"
	"      <arg type='s' name='"MSU_INTERFACE_INTERFACE_NAME"'"
	"           direction='in'/>"
	"      <arg type='a{sv}' name='"MSU_INTERFACE_PROPERTIES_VALUE"'"
	"           direction='out'/>"
	"    </method>"
	"  </interface>"
	"  <interface name='"MSU_INTERFACE_MEDIA_OBJECT"'>"
	"    <property type='o' name='"MSU_INTERFACE_PROP_PARENT"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_TYPE"'"
	"       access='read'/>"
	"    <property type='o' name='"MSU_INTERFACE_PROP_PATH"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_DISPLAY_NAME"'"
	"       access='read'/>"
	"  </interface>"
	"  <interface name='"MSU_INTERFACE_MEDIA_CONTAINER"'>"
	"    <method name='"MSU_INTERFACE_LIST_CHILDREN"'>"
	"      <arg type='u' name='"MSU_INTERFACE_OFFSET"'"
	"           direction='in'/>"
	"      <arg type='u' name='"MSU_INTERFACE_MAX"'"
	"           direction='in'/>"
	"      <arg type='as' name='"MSU_INTERFACE_FILTER"'"
	"           direction='in'/>"
	"      <arg type='aa{sv}' name='"MSU_INTERFACE_CHILDREN"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_LIST_CHILDREN_EX"'>"
	"      <arg type='u' name='"MSU_INTERFACE_OFFSET"'"
	"           direction='in'/>"
	"      <arg type='u' name='"MSU_INTERFACE_MAX"'"
	"           direction='in'/>"
	"      <arg type='as' name='"MSU_INTERFACE_FILTER"'"
	"           direction='in'/>"
	"      <arg type='s' name='"MSU_INTERFACE_SORT_BY"'"
	"           direction='in'/>"
	"      <arg type='aa{sv}' name='"MSU_INTERFACE_CHILDREN"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_LIST_CONTAINERS"'>"
	"      <arg type='u' name='"MSU_INTERFACE_OFFSET"'"
	"           direction='in'/>"
	"      <arg type='u' name='"MSU_INTERFACE_MAX"'"
	"           direction='in'/>"
	"      <arg type='as' name='"MSU_INTERFACE_FILTER"'"
	"           direction='in'/>"
	"      <arg type='aa{sv}' name='"MSU_INTERFACE_CHILDREN"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_LIST_CONTAINERS_EX"'>"
	"      <arg type='u' name='"MSU_INTERFACE_OFFSET"'"
	"           direction='in'/>"
	"      <arg type='u' name='"MSU_INTERFACE_MAX"'"
	"           direction='in'/>"
	"      <arg type='as' name='"MSU_INTERFACE_FILTER"'"
	"           direction='in'/>"
	"      <arg type='s' name='"MSU_INTERFACE_SORT_BY"'"
	"           direction='in'/>"
	"      <arg type='aa{sv}' name='"MSU_INTERFACE_CHILDREN"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_LIST_ITEMS"'>"
	"      <arg type='u' name='"MSU_INTERFACE_OFFSET"'"
	"           direction='in'/>"
	"      <arg type='u' name='"MSU_INTERFACE_MAX"'"
	"           direction='in'/>"
	"      <arg type='as' name='"MSU_INTERFACE_FILTER"'"
	"           direction='in'/>"
	"      <arg type='aa{sv}' name='"MSU_INTERFACE_CHILDREN"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_LIST_ITEMS_EX"'>"
	"      <arg type='u' name='"MSU_INTERFACE_OFFSET"'"
	"           direction='in'/>"
	"      <arg type='u' name='"MSU_INTERFACE_MAX"'"
	"           direction='in'/>"
	"      <arg type='as' name='"MSU_INTERFACE_FILTER"'"
	"           direction='in'/>"
	"      <arg type='s' name='"MSU_INTERFACE_SORT_BY"'"
	"           direction='in'/>"
	"      <arg type='aa{sv}' name='"MSU_INTERFACE_CHILDREN"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_SEARCH_OBJECTS"'>"
	"      <arg type='s' name='"MSU_INTERFACE_QUERY"'"
	"           direction='in'/>"
	"      <arg type='u' name='"MSU_INTERFACE_OFFSET"'"
	"           direction='in'/>"
	"      <arg type='u' name='"MSU_INTERFACE_MAX"'"
	"           direction='in'/>"
	"      <arg type='as' name='"MSU_INTERFACE_FILTER"'"
	"           direction='in'/>"
	"      <arg type='aa{sv}' name='"MSU_INTERFACE_CHILDREN"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_SEARCH_OBJECTS_EX"'>"
	"      <arg type='s' name='"MSU_INTERFACE_QUERY"'"
	"           direction='in'/>"
	"      <arg type='u' name='"MSU_INTERFACE_OFFSET"'"
	"           direction='in'/>"
	"      <arg type='u' name='"MSU_INTERFACE_MAX"'"
	"           direction='in'/>"
	"      <arg type='as' name='"MSU_INTERFACE_FILTER"'"
	"           direction='in'/>"
	"      <arg type='s' name='"MSU_INTERFACE_SORT_BY"'"
	"           direction='in'/>"
	"      <arg type='aa{sv}' name='"MSU_INTERFACE_CHILDREN"'"
	"           direction='out'/>"
	"      <arg type='u' name='"MSU_INTERFACE_TOTAL_ITEMS"'"
	"           direction='out'/>"
	"    </method>"
	"    <property type='u' name='"MSU_INTERFACE_PROP_CHILD_COUNT"'"
	"       access='read'/>"
	"    <property type='b' name='"MSU_INTERFACE_PROP_SEARCHABLE"'"
	"       access='read'/>"
	"  </interface>"
	"  <interface name='"MSU_INTERFACE_MEDIA_ITEM"'>"
	"    <method name='"MSU_INTERFACE_GET_COMPATIBLE_RESOURCE"'>"
	"      <arg type='s' name='"MSU_INTERFACE_PROTOCOL_INFO"'"
	"           direction='in'/>"
	"      <arg type='as' name='"MSU_INTERFACE_FILTER"'"
	"           direction='in'/>"
	"      <arg type='a{sv}' name='"MSU_INTERFACE_PROPERTIES_VALUE"'"
	"           direction='out'/>"
	"    </method>"
	"    <property type='as' name='"MSU_INTERFACE_PROP_URLS"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_MIME_TYPE"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_ARTIST"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_ALBUM"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_DATE"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_GENRE"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_DLNA_PROFILE"'"
	"       access='read'/>"
	"    <property type='i' name='"MSU_INTERFACE_PROP_TRACK_NUMBER"'"
	"       access='read'/>"
	"    <property type='x' name='"MSU_INTERFACE_PROP_SIZE"'"
	"       access='read'/>"
	"    <property type='i' name='"MSU_INTERFACE_PROP_DURATION"'"
	"       access='read'/>"
	"    <property type='i' name='"MSU_INTERFACE_PROP_BITRATE"'"
	"       access='read'/>"
	"    <property type='i' name='"MSU_INTERFACE_PROP_SAMPLE_RATE"'"
	"       access='read'/>"
	"    <property type='i' name='"MSU_INTERFACE_PROP_BITS_PER_SAMPLE"'"
	"       access='read'/>"
	"    <property type='i' name='"MSU_INTERFACE_PROP_WIDTH"'"
	"       access='read'/>"
	"    <property type='i' name='"MSU_INTERFACE_PROP_HEIGHT"'"
	"       access='read'/>"
	"    <property type='i' name='"MSU_INTERFACE_PROP_COLOR_DEPTH"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_ALBUM_ART_URL"'"
	"       access='read'/>"
	"    <property type='aa{sv}' name='"MSU_INTERFACE_PROP_RESOURCES"'"
	"       access='read'/>"
	"  </interface>"
	"  <interface name='"MSU_INTERFACE_MEDIA_DEVICE"'>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_LOCATION"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_UDN"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_DEVICE_TYPE"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_FRIENDLY_NAME"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_MANUFACTURER"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_MANUFACTURER_URL"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_MODEL_DESCRIPTION"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_MODEL_NAME"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_MODEL_NUMBER"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_MODEL_URL"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_SERIAL_NUMBER"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_PRESENTATION_URL"'"
	"       access='read'/>"
	"    <property type='s' name='"MSU_INTERFACE_PROP_ICON_URL"'"
	"       access='read'/>"
	"  </interface>"
	"</node>";

static gboolean prv_process_task(gpointer user_data);

static void prv_free_msu_task_cb(gpointer data, gpointer user_data)
{
	msu_task_delete(data);
}

static void prv_process_sync_task(msu_context_t *context, msu_task_t *task)
{
	const gchar *client_name;
	msu_client_t *client;

	switch (task->type) {
	case MSU_TASK_GET_VERSION:
		msu_task_complete_and_delete(task);
		break;
	case MSU_TASK_GET_SERVERS:
		task->result = msu_upnp_get_server_ids(context->upnp);
		msu_task_complete_and_delete(task);
		break;
	case MSU_TASK_SET_PROTOCOL_INFO:
		client_name =
			g_dbus_method_invocation_get_sender(task->invocation);
		client = g_hash_table_lookup(context->watchers, client_name);
		if (client) {
			g_free(client->protocol_info);
			if (task->protocol_info.protocol_info[0]) {
				client->protocol_info =
					task->protocol_info.protocol_info;
				task->protocol_info.protocol_info = NULL;
			} else {
				client->protocol_info = NULL;
			}
		}
		msu_task_complete_and_delete(task);
		break;

	default:
		break;
	}
}

static void prv_async_task_complete(msu_task_t *task, GVariant *result,
				    GError *error, void *user_data)
{
	msu_context_t *context = user_data;

	g_object_unref(context->cancellable);
	context->cancellable = NULL;

	if (error) {
		msu_task_fail_and_delete(task, error);
		g_error_free(error);
	} else {
		task->result = result;
		msu_task_complete_and_delete(task);
	}

	if (context->quitting)
		g_main_loop_quit(context->main_loop);
	else if (context->tasks->len > 0)
		context->idle_id = g_idle_add(prv_process_task, context);
}

static void prv_process_async_task(msu_context_t *context, msu_task_t *task)
{
	const gchar *client_name;
	msu_client_t *client;
	const gchar *protocol_info = NULL;

	context->cancellable = g_cancellable_new();
	client_name =
		g_dbus_method_invocation_get_sender(task->invocation);
	client = g_hash_table_lookup(context->watchers, client_name);
	if (client)
		protocol_info = client->protocol_info;

	switch (task->type) {
	case MSU_TASK_GET_CHILDREN:
		msu_upnp_get_children(context->upnp, task, protocol_info,
				      context->cancellable,
				      prv_async_task_complete, context);
		break;
	case MSU_TASK_GET_PROP:
		msu_upnp_get_prop(context->upnp, task, protocol_info,
				  context->cancellable,
				  prv_async_task_complete, context);
		break;
	case MSU_TASK_GET_ALL_PROPS:
		msu_upnp_get_all_props(context->upnp, task, protocol_info,
				       context->cancellable,
				       prv_async_task_complete, context);
		break;
	case MSU_TASK_SEARCH:
		msu_upnp_search(context->upnp, task, protocol_info,
				context->cancellable,
				prv_async_task_complete, context);
		break;
	case MSU_TASK_GET_RESOURCE:
		msu_upnp_get_resource(context->upnp, task,
				      context->cancellable,
				      prv_async_task_complete, context);
		break;
	default:
		break;
	}
}

static gboolean prv_process_task(gpointer user_data)
{
	msu_context_t *context = user_data;
	msu_task_t *task;
	gboolean retval = FALSE;

	if (context->tasks->len > 0) {
		task = g_ptr_array_index(context->tasks, 0);
		if (task->synchronous) {
			prv_process_sync_task(context, task);
			retval = TRUE;
		} else {
			prv_process_async_task(context, task);
			context->idle_id = 0;
		}
		g_ptr_array_remove_index(context->tasks, 0);
	} else {
		context->idle_id = 0;
	}

	return retval;
}

static void prv_msu_method_call(GDBusConnection *conn,
				const gchar *sender,
				const gchar *object,
				const gchar *interface,
				const gchar *method,
				GVariant *parameters,
				GDBusMethodInvocation *invocation,
				gpointer user_data);

static void prv_item_method_call(GDBusConnection *conn,
				 const gchar *sender,
				 const gchar *object,
				 const gchar *interface,
				 const gchar *method,
				 GVariant *parameters,
				 GDBusMethodInvocation *invocation,
				 gpointer user_data);

static void prv_con_method_call(GDBusConnection *conn,
				const gchar *sender,
				const gchar *object,
				const gchar *interface,
				const gchar *method,
				GVariant *parameters,
				GDBusMethodInvocation *invocation,
				gpointer user_data);

static void prv_props_method_call(GDBusConnection *conn,
				  const gchar *sender,
				  const gchar *object,
				  const gchar *interface,
				  const gchar *method,
				  GVariant *parameters,
				  GDBusMethodInvocation *invocation,
				  gpointer user_data);

static const GDBusInterfaceVTable g_msu_vtable = {
	prv_msu_method_call,
	NULL,
	NULL
};

static const GDBusInterfaceVTable g_item_vtable = {
	prv_item_method_call,
	NULL,
	NULL
};

static const GDBusInterfaceVTable g_ms_vtable = {
	prv_con_method_call,
	NULL,
	NULL
};

static const GDBusInterfaceVTable g_props_vtable = {
	prv_props_method_call,
	NULL,
	NULL
};

static const GDBusInterfaceVTable *g_server_vtables[MSU_INTERFACE_INFO_MAX] = {
	&g_props_vtable,
	NULL,
	&g_ms_vtable,
	&g_item_vtable
};

static void prv_msu_context_init(msu_context_t *context)
{
	memset(context, 0, sizeof(*context));
}

static void prv_msu_context_free(msu_context_t *context)
{
	msu_upnp_delete(context->upnp);

	if (context->watchers)
		g_hash_table_unref(context->watchers);

	if (context->tasks) {
		g_ptr_array_foreach(context->tasks, prv_free_msu_task_cb, NULL);
		g_ptr_array_unref(context->tasks);
	}

	if (context->idle_id)
		(void) g_source_remove(context->idle_id);

	if (context->sig_id)
		(void) g_source_remove(context->sig_id);

	if (context->connection) {
		if (context->msu_id)
			g_dbus_connection_unregister_object(
				context->connection,
				context->msu_id);
	}

	if (context->owner_id)
		g_bus_unown_name(context->owner_id);

	if (context->connection)
		g_object_unref(context->connection);

	if (context->main_loop)
		g_main_loop_unref(context->main_loop);

	if (context->server_node_info)
		g_dbus_node_info_unref(context->server_node_info);

	if (context->root_node_info)
		g_dbus_node_info_unref(context->root_node_info);
}

static void prv_quit(msu_context_t *context)
{
	if (context->cancellable) {
		g_cancellable_cancel(context->cancellable);
		context->quitting = TRUE;
	} else {
		g_main_loop_quit(context->main_loop);
	}
}

static void prv_remove_client(msu_context_t *context, const gchar *name)
{
	(void) g_hash_table_remove(context->watchers, name);
	if (g_hash_table_size(context->watchers) == 0)
		prv_quit(context);
}

static void prv_lost_client(GDBusConnection *connection, const gchar *name,
			    gpointer user_data)
{
	prv_remove_client(user_data, name);
}

static void prv_add_task(msu_context_t *context, msu_task_t *task)
{
	const gchar *client_name;
	msu_client_t *client;

	client_name = g_dbus_method_invocation_get_sender(task->invocation);

	if (!g_hash_table_lookup(context->watchers, client_name)) {
		client = g_new0(msu_client_t, 1);
		client->id = g_bus_watch_name(G_BUS_TYPE_SESSION, client_name,
					      G_BUS_NAME_WATCHER_FLAGS_NONE,
					      NULL, prv_lost_client, context,
					      NULL);
		g_hash_table_insert(context->watchers, g_strdup(client_name),
				    client);
	}

	if (!context->cancellable && !context->idle_id)
		context->idle_id = g_idle_add(prv_process_task, context);

	g_ptr_array_add(context->tasks, task);
}

static void prv_msu_method_call(GDBusConnection *conn,
				const gchar *sender, const gchar *object,
				const gchar *interface,
				const gchar *method, GVariant *parameters,
				GDBusMethodInvocation *invocation,
				gpointer user_data)
{
	msu_context_t *context = user_data;
	const gchar *client_name;
	msu_task_t *task;

	if (!strcmp(method, MSU_INTERFACE_RELEASE)) {
		client_name = g_dbus_method_invocation_get_sender(invocation);
		prv_remove_client(context, client_name);
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (!strcmp(method, MSU_INTERFACE_GET_VERSION)) {
		task = msu_task_get_version_new(invocation);
		prv_add_task(context, task);
	} else if (!strcmp(method, MSU_INTERFACE_GET_SERVERS)) {
		task = msu_task_get_servers_new(invocation);
		prv_add_task(context, task);
	} else if (!strcmp(method, MSU_INTERFACE_SET_PROTOCOL_INFO)) {
		task = msu_task_set_protocol_info_new(invocation, parameters);
		prv_add_task(context, task);
	}
}

static void prv_item_method_call(GDBusConnection *conn,
				 const gchar *sender, const gchar *object,
				 const gchar *interface,
				 const gchar *method, GVariant *parameters,
				 GDBusMethodInvocation *invocation,
				 gpointer user_data)
{
	msu_context_t *context = user_data;
	msu_task_t *task;

	if (!strcmp(method, MSU_INTERFACE_GET_COMPATIBLE_RESOURCE)) {
		task = msu_task_get_resource_new(invocation, object,
						 parameters);
		prv_add_task(context, task);
	}
}


static void prv_con_method_call(GDBusConnection *conn,
				const gchar *sender,
				const gchar *object,
				const gchar *interface,
				const gchar *method,
				GVariant *parameters,
				GDBusMethodInvocation *invocation,
				gpointer user_data)
{
	msu_context_t *context = user_data;
	msu_task_t *task;

	if (!strcmp(method, MSU_INTERFACE_LIST_CHILDREN))
		task = msu_task_get_children_new(invocation, object,
						 parameters, TRUE, TRUE);
	else if (!strcmp(method, MSU_INTERFACE_LIST_CHILDREN_EX))
		task = msu_task_get_children_ex_new(invocation, object,
						    parameters, TRUE, TRUE);
	else if (!strcmp(method, MSU_INTERFACE_LIST_ITEMS))
		task = msu_task_get_children_new(invocation, object,
						 parameters, TRUE, FALSE);
	else if (!strcmp(method, MSU_INTERFACE_LIST_ITEMS_EX))
		task = msu_task_get_children_ex_new(invocation, object,
						    parameters, TRUE, FALSE);
	else if (!strcmp(method, MSU_INTERFACE_LIST_CONTAINERS))
		task = msu_task_get_children_new(invocation, object,
						 parameters, FALSE, TRUE);
	else if (!strcmp(method, MSU_INTERFACE_LIST_CONTAINERS_EX))
		task = msu_task_get_children_ex_new(invocation, object,
						    parameters, FALSE, TRUE);
	else if (!strcmp(method, MSU_INTERFACE_SEARCH_OBJECTS))
		task = msu_task_search_new(invocation, object,
					   parameters);
	else if (!strcmp(method, MSU_INTERFACE_SEARCH_OBJECTS_EX))
		task = msu_task_search_ex_new(invocation, object,
					      parameters);
	else
		goto finished;

	prv_add_task(context, task);

finished:

	return;
}

static void prv_props_method_call(GDBusConnection *conn,
				  const gchar *sender,
				  const gchar *object,
				  const gchar *interface,
				  const gchar *method,
				  GVariant *parameters,
				  GDBusMethodInvocation *invocation,
				  gpointer user_data)
{
	msu_context_t *context = user_data;
	msu_task_t *task;

	if (!strcmp(method, MSU_INTERFACE_GET_ALL))
		task = msu_task_get_props_new(invocation, object, parameters);
	else if (!strcmp(method, MSU_INTERFACE_GET))
		task = msu_task_get_prop_new(invocation, object, parameters);
	else
		goto finished;

	prv_add_task(context, task);

finished:

	return;
}

static void prv_found_media_server(const gchar *path, void *user_data)
{
	msu_context_t *context = user_data;

	(void) g_dbus_connection_emit_signal(context->connection,
					     NULL,
					     MSU_OBJECT,
					     MSU_INTERFACE_MANAGER,
					     MSU_INTERFACE_FOUND_SERVER,
					     g_variant_new("(o)", path),
					     NULL);
}

static void prv_lost_media_server(const gchar *path, void *user_data)
{
	msu_context_t *context = user_data;

	(void) g_dbus_connection_emit_signal(context->connection,
					     NULL,
					     MSU_OBJECT,
					     MSU_INTERFACE_MANAGER,
					     MSU_INTERFACE_LOST_SERVER,
					     g_variant_new("(o)", path),
					     NULL);
}

static void prv_bus_acquired(GDBusConnection *connection, const gchar *name,
			     gpointer user_data)
{
	msu_context_t *context = user_data;
	msu_interface_info_t *info;
	unsigned int i;

	context->connection = connection;

	context->msu_id =
		g_dbus_connection_register_object(connection, MSU_OBJECT,
						  context->root_node_info->
						  interfaces[0],
						  &g_msu_vtable,
						  user_data, NULL, NULL);

	if (!context->msu_id) {
		context->error = true;
		g_main_loop_quit(context->main_loop);
	} else {
		info = g_new(msu_interface_info_t, MSU_INTERFACE_INFO_MAX);
		for (i = 0; i < MSU_INTERFACE_INFO_MAX; ++i) {
			info[i].interface =
				context->server_node_info->interfaces[i];
			info[i].vtable = g_server_vtables[i];
		}
		context->upnp = msu_upnp_new(connection, info,
					    prv_found_media_server,
					    prv_lost_media_server,
					    user_data);
	}
}

static void prv_name_lost(GDBusConnection *connection, const gchar *name,
			  gpointer user_data)
{
	msu_context_t *context = user_data;

	context->connection = NULL;

	prv_quit(context);
}

static gboolean prv_quit_handler(GIOChannel *source, GIOCondition condition,
				 gpointer user_data)
{
	msu_context_t *context = user_data;

	prv_quit(context);
	context->sig_id = 0;

	return FALSE;
}

static bool prv_init_signal_handler(sigset_t mask, msu_context_t *context)
{
	bool retval = false;
	int fd = -1;
	GIOChannel *channel = NULL;

	fd = signalfd(-1, &mask, SFD_NONBLOCK);
	if (fd == -1)
		goto on_error;

	channel = g_io_channel_unix_new(fd);
	g_io_channel_set_close_on_unref(channel, TRUE);

	if (g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, NULL) !=
	    G_IO_STATUS_NORMAL)
		goto on_error;

	if (g_io_channel_set_encoding(channel, NULL, NULL) !=
	    G_IO_STATUS_NORMAL)
		goto on_error;

	context->sig_id = g_io_add_watch(channel, G_IO_IN | G_IO_PRI,
					 prv_quit_handler,
					 context);

	retval = true;

on_error:

	if (channel)
		g_io_channel_unref(channel);

	return retval;
}

static void prv_unregister_client(gpointer user_data)
{
	msu_client_t *client = user_data;

	if (client) {
		g_bus_unwatch_name(client->id);
		g_free(client->protocol_info);
		g_free(client);
	}
}

int main(int argc, char *argv[])
{
	msu_context_t context;
	sigset_t mask;
	int retval = 1;

	prv_msu_context_init(&context);

	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);

	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
		goto on_error;

	g_type_init();

	context.root_node_info =
		g_dbus_node_info_new_for_xml(g_msu_root_introspection, NULL);
	if (!context.root_node_info)
		goto on_error;

	context.server_node_info =
		g_dbus_node_info_new_for_xml(g_msu_server_introspection, NULL);
	if (!context.server_node_info)
		goto on_error;

	context.main_loop = g_main_loop_new(NULL, FALSE);

	context.owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
					  MSU_SERVER_NAME,
					  G_BUS_NAME_OWNER_FLAGS_NONE,
					  prv_bus_acquired, NULL,
					  prv_name_lost, &context, NULL);

	context.tasks = g_ptr_array_new();

	context.watchers = g_hash_table_new_full(g_str_hash, g_str_equal,
						 g_free, prv_unregister_client);

	if (!prv_init_signal_handler(mask, &context))
		goto on_error;

	g_main_loop_run(context.main_loop);
	if (context.error)
		goto on_error;

	retval = 0;

on_error:

	prv_msu_context_free(&context);

	return retval;
}
