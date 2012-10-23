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

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/signalfd.h>

#include "client.h"
#include "interface.h"
#include "log.h"
#include "settings.h"
#include "task.h"
#include "upnp.h"

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
	msu_task_t *current_task;
	msu_upnp_t *upnp;
	msu_settings_context_t *settings;
};

static msu_context_t g_context;

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
	"    <method name='"MSU_INTERFACE_PREFER_LOCAL_ADDRESSES"'>"
	"      <arg type='b' name='"MSU_INTERFACE_PREFER"'"
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
	"    <signal name='"MSU_INTERFACE_PROPERTIES_CHANGED"'>"
	"      <arg type='s' name='"MSU_INTERFACE_INTERFACE_NAME"'/>"
	"      <arg type='a{sv}' name='"MSU_INTERFACE_CHANGED_PROPERTIES"'/>"
	"      <arg type='as' name='"MSU_INTERFACE_INVALIDATED_PROPERTIES"'/>"
	"    </signal>"
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
	"    <property type='s' name='"MSU_INTERFACE_PROP_CREATOR"'"
	"       access='read'/>"
	"    <property type='b' name='"MSU_INTERFACE_PROP_RESTRICTED"'"
	"       access='read'/>"
	"    <property type='a{sb}' name='"MSU_INTERFACE_PROP_DLNA_MANAGED"'"
	"       access='read'/>"
	"    <method name='"MSU_INTERFACE_DELETE"'>"
	"    </method>"
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
	"    <method name='"MSU_INTERFACE_UPLOAD"'>"
	"      <arg type='s' name='"MSU_INTERFACE_PROP_DISPLAY_NAME"'"
	"           direction='in'/>"
	"      <arg type='s' name='"MSU_INTERFACE_FILE_PATH"'"
	"           direction='in'/>"
	"      <arg type='u' name='"MSU_INTERFACE_UPLOAD_ID"'"
	"           direction='out'/>"
	"      <arg type='o' name='"MSU_INTERFACE_PATH"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_CREATE_CONTAINER"'>"
	"      <arg type='s' name='"MSU_INTERFACE_PROP_DISPLAY_NAME"'"
	"           direction='in'/>"
	"      <arg type='s' name='"MSU_INTERFACE_PROP_TYPE"'"
	"           direction='in'/>"
	"      <arg type='as' name='"MSU_INTERFACE_CHILD_TYPES"'"
	"           direction='in'/>"
	"      <arg type='o' name='"MSU_INTERFACE_PATH"'"
	"           direction='out'/>"
	"    </method>"
	"    <property type='u' name='"MSU_INTERFACE_PROP_CHILD_COUNT"'"
	"       access='read'/>"
	"    <property type='b' name='"MSU_INTERFACE_PROP_SEARCHABLE"'"
	"       access='read'/>"
	"    <property type='a(sb)' name='"MSU_INTERFACE_PROP_CREATE_CLASSES"'"
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
	"    <property type='as' name='"MSU_INTERFACE_PROP_ARTISTS"'"
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
	"    <property type='o' name='"MSU_INTERFACE_PROP_REFPATH"'"
	"       access='read'/>"
	"    <property type='aa{sv}' name='"MSU_INTERFACE_PROP_RESOURCES"'"
	"       access='read'/>"
	"  </interface>"
	"  <interface name='"MSU_INTERFACE_MEDIA_DEVICE"'>"
	"    <method name='"MSU_INTERFACE_UPLOAD_TO_ANY"'>"
	"      <arg type='s' name='"MSU_INTERFACE_PROP_DISPLAY_NAME"'"
	"           direction='in'/>"
	"      <arg type='s' name='"MSU_INTERFACE_FILE_PATH"'"
	"           direction='in'/>"
	"      <arg type='u' name='"MSU_INTERFACE_UPLOAD_ID"'"
	"           direction='out'/>"
	"      <arg type='o' name='"MSU_INTERFACE_PATH"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_GET_UPLOAD_STATUS"'>"
	"      <arg type='u' name='"MSU_INTERFACE_UPLOAD_ID"'"
	"           direction='in'/>"
	"      <arg type='s' name='"MSU_INTERFACE_UPLOAD_STATUS"'"
	"           direction='out'/>"
	"      <arg type='t' name='"MSU_INTERFACE_LENGTH"'"
	"           direction='out'/>"
	"      <arg type='t' name='"MSU_INTERFACE_TOTAL"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_GET_UPLOAD_IDS"'>"
	"      <arg type='au' name='"MSU_INTERFACE_TOTAL"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_CANCEL_UPLOAD"'>"
	"      <arg type='u' name='"MSU_INTERFACE_UPLOAD_ID"'"
	"           direction='in'/>"
	"    </method>"
	"    <method name='"MSU_INTERFACE_CREATE_CONTAINER_IN_ANY"'>"
	"      <arg type='s' name='"MSU_INTERFACE_PROP_DISPLAY_NAME"'"
	"           direction='in'/>"
	"      <arg type='s' name='"MSU_INTERFACE_PROP_TYPE"'"
	"           direction='in'/>"
	"      <arg type='as' name='"MSU_INTERFACE_CHILD_TYPES"'"
	"           direction='in'/>"
	"      <arg type='o' name='"MSU_INTERFACE_PATH"'"
	"           direction='out'/>"
	"    </method>"
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
	"    <property type='a{sv}'name='"
	MSU_INTERFACE_PROP_SV_DLNA_CAPABILITIES"'"
	"       access='read'/>"
	"    <property type='as' name='"
	MSU_INTERFACE_PROP_SV_SEARCH_CAPABILITIES"'"
	"       access='read'/>"
	"    <property type='as' name='"
	MSU_INTERFACE_PROP_SV_SORT_CAPABILITIES"'"
	"       access='read'/>"
	"    <property type='as' name='"
	MSU_INTERFACE_PROP_SV_SORT_EXT_CAPABILITIES"'"
	"       access='read'/>"
	"    <property type='u' name='"
	MSU_INTERFACE_PROP_ESV_SYSTEM_UPDATE_ID"'"
	"       access='read'/>"
	"    <property type='s' name='"
	MSU_INTERFACE_PROP_ESV_SERVICE_RESET_TOKEN"'"
	"       access='read'/>"
	"    <signal name='"MSU_INTERFACE_CONTAINER_UPDATE"'>"
	"      <arg type='ao' name='"MSU_INTERFACE_CONTAINER_PATHS"'/>"
	"    </signal>"
	"  </interface>"
	"</node>";

static gboolean prv_process_task(gpointer user_data);

static void prv_free_msu_task_cb(gpointer data, gpointer user_data)
{
	msu_task_delete(data);
}

static void prv_process_sync_task(msu_task_t *task)
{
	const gchar *client_name;
	msu_client_t *client;

	g_context.current_task = task;

	switch (task->type) {
	case MSU_TASK_GET_VERSION:
		msu_task_complete_and_delete(task);
		break;
	case MSU_TASK_GET_SERVERS:
		task->result = msu_upnp_get_server_ids(g_context.upnp);
		msu_task_complete_and_delete(task);
		break;
	case MSU_TASK_SET_PROTOCOL_INFO:
		client_name =
			g_dbus_method_invocation_get_sender(task->invocation);
		client = g_hash_table_lookup(g_context.watchers, client_name);
		if (client) {
			g_free(client->protocol_info);
			if (task->ut.protocol_info.protocol_info[0]) {
				client->protocol_info =
					task->ut.protocol_info.protocol_info;
				task->ut.protocol_info.protocol_info = NULL;
			} else {
				client->protocol_info = NULL;
			}
		}
		msu_task_complete_and_delete(task);
		break;
	case MSU_TASK_SET_PREFER_LOCAL_ADDRESSES:
		client_name =
			g_dbus_method_invocation_get_sender(task->invocation);
		client = g_hash_table_lookup(g_context.watchers, client_name);
		if (client) {
			client->prefer_local_addresses =
					task->ut.prefer_local_addresses.prefer;
		}
		msu_task_complete_and_delete(task);
		break;
	case MSU_TASK_GET_UPLOAD_STATUS:
		msu_upnp_get_upload_status(g_context.upnp, task);
		break;
	case MSU_TASK_GET_UPLOAD_IDS:
		msu_upnp_get_upload_ids(g_context.upnp, task);
		break;
	case MSU_TASK_CANCEL_UPLOAD:
		msu_upnp_cancel_upload(g_context.upnp, task);
		break;
	default:
		break;
	}

	g_context.current_task = NULL;
}

static void prv_async_task_complete(msu_task_t *task, GVariant *result,
				    GError *error)
{
	MSU_LOG_DEBUG("Enter");

	g_object_unref(g_context.cancellable);
	g_context.cancellable = NULL;
	g_context.current_task = NULL;

	if (error) {
		msu_task_fail_and_delete(task, error);
		g_error_free(error);
	} else {
		task->result = result;
		msu_task_complete_and_delete(task);
	}

	if (g_context.quitting)
		g_main_loop_quit(g_context.main_loop);
	else if (g_context.tasks->len > 0)
		g_context.idle_id = g_idle_add(prv_process_task, NULL);

	MSU_LOG_DEBUG("Exit");
}

static void prv_process_async_task(msu_task_t *task)
{
	const gchar *client_name;
	msu_client_t *client;

	MSU_LOG_DEBUG("Enter");

	g_context.cancellable = g_cancellable_new();
	g_context.current_task = task;
	client_name =
		g_dbus_method_invocation_get_sender(task->invocation);
	client = g_hash_table_lookup(g_context.watchers, client_name);

	switch (task->type) {
	case MSU_TASK_GET_CHILDREN:
		msu_upnp_get_children(g_context.upnp, client, task,
				      g_context.cancellable,
				      prv_async_task_complete);
		break;
	case MSU_TASK_GET_PROP:
		msu_upnp_get_prop(g_context.upnp, client, task,
				  g_context.cancellable,
				  prv_async_task_complete);
		break;
	case MSU_TASK_GET_ALL_PROPS:
		msu_upnp_get_all_props(g_context.upnp, client, task,
				       g_context.cancellable,
				       prv_async_task_complete);
		break;
	case MSU_TASK_SEARCH:
		msu_upnp_search(g_context.upnp, client, task,
				g_context.cancellable,
				prv_async_task_complete);
		break;
	case MSU_TASK_GET_RESOURCE:
		msu_upnp_get_resource(g_context.upnp, client, task,
				      g_context.cancellable,
				      prv_async_task_complete);
		break;
	case MSU_TASK_UPLOAD_TO_ANY:
		msu_upnp_upload_to_any(g_context.upnp, client, task,
				       g_context.cancellable,
				       prv_async_task_complete);
		break;
	case MSU_TASK_UPLOAD:
		msu_upnp_upload(g_context.upnp, client, task,
				g_context.cancellable,
				prv_async_task_complete);
		break;
	case MSU_TASK_DELETE_OBJECT:
		msu_upnp_delete_object(g_context.upnp, client, task,
				       g_context.cancellable,
				       prv_async_task_complete);
		break;
	case MSU_TASK_CREATE_CONTAINER:
		msu_upnp_create_container(g_context.upnp, client, task,
					  g_context.cancellable,
					  prv_async_task_complete);
		break;
	case MSU_TASK_CREATE_CONTAINER_IN_ANY:
		msu_upnp_create_container_in_any(g_context.upnp, client, task,
						 g_context.cancellable,
						 prv_async_task_complete);
		break;

	default:
		break;
	}

	MSU_LOG_DEBUG("Exit");
}

static gboolean prv_process_task(gpointer user_data)
{
	msu_task_t *task;
	gboolean retval = FALSE;

	if (g_context.tasks->len > 0) {
		task = g_ptr_array_index(g_context.tasks, 0);
		if (task->synchronous) {
			prv_process_sync_task(task);
			retval = TRUE;
		} else {
			prv_process_async_task(task);
			g_context.idle_id = 0;
		}
		g_ptr_array_remove_index(g_context.tasks, 0);
	} else {
		g_context.idle_id = 0;
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

static void prv_object_method_call(GDBusConnection *conn,
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

static void prv_device_method_call(GDBusConnection *conn,
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

static const GDBusInterfaceVTable g_object_vtable = {
	prv_object_method_call,
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

static const GDBusInterfaceVTable g_device_vtable = {
	prv_device_method_call,
	NULL,
	NULL
};

static const GDBusInterfaceVTable *g_server_vtables[MSU_INTERFACE_INFO_MAX] = {
	&g_props_vtable,
	&g_object_vtable,
	&g_ms_vtable,
	&g_item_vtable,
	&g_device_vtable
};

static void prv_msu_context_init(void)
{
	memset(&g_context, 0, sizeof(g_context));
}

static void prv_msu_context_free(void)
{
	msu_upnp_delete(g_context.upnp);

	if (g_context.watchers)
		g_hash_table_unref(g_context.watchers);

	if (g_context.tasks) {
		g_ptr_array_foreach(g_context.tasks, prv_free_msu_task_cb,
				    NULL);
		g_ptr_array_unref(g_context.tasks);
	}

	if (g_context.idle_id)
		(void) g_source_remove(g_context.idle_id);

	if (g_context.sig_id)
		(void) g_source_remove(g_context.sig_id);

	if (g_context.connection) {
		if (g_context.msu_id)
			g_dbus_connection_unregister_object(
							g_context.connection,
							g_context.msu_id);
	}

	if (g_context.owner_id)
		g_bus_unown_name(g_context.owner_id);

	if (g_context.connection)
		g_object_unref(g_context.connection);

	if (g_context.main_loop)
		g_main_loop_unref(g_context.main_loop);

	if (g_context.server_node_info)
		g_dbus_node_info_unref(g_context.server_node_info);

	if (g_context.root_node_info)
		g_dbus_node_info_unref(g_context.root_node_info);

	if (g_context.settings)
		msu_settings_delete(g_context.settings);
}

static void prv_quit(void)
{
	if (g_context.cancellable) {
		g_cancellable_cancel(g_context.cancellable);
		g_context.quitting = TRUE;
	} else {
		g_main_loop_quit(g_context.main_loop);
	}
}

static void prv_remove_client(const gchar *name)
{
	const gchar *client_name;
	msu_task_t *task;
	guint pos;

	if (g_context.cancellable) {
		client_name = g_dbus_method_invocation_get_sender(
					g_context.current_task->invocation);
		if (!strcmp(client_name, name)) {
			MSU_LOG_DEBUG("Cancelling current task, type is %d",
				      g_context.current_task->type);

			g_cancellable_cancel(g_context.cancellable);
		}
	}

	pos = 0;
	while (pos < g_context.tasks->len) {
		task = (msu_task_t *) g_ptr_array_index(g_context.tasks, pos);

		client_name = g_dbus_method_invocation_get_sender(
							task->invocation);

		if (strcmp(client_name, name)) {
			pos++;
			continue;
		}

		MSU_LOG_DEBUG("Removing task type %d from array", task->type);

		(void) g_ptr_array_remove_index(g_context.tasks, pos);
		msu_task_cancel_and_delete(task);
	}

	(void) g_hash_table_remove(g_context.watchers, name);

	if (g_hash_table_size(g_context.watchers) == 0)
		if (!msu_settings_is_never_quit(g_context.settings))
			prv_quit();
}

static void prv_lost_client(GDBusConnection *connection, const gchar *name,
			    gpointer user_data)
{
	MSU_LOG_DEBUG("Lost Client %s", name);

	prv_remove_client(name);
}

static void prv_add_task(msu_task_t *task)
{
	const gchar *client_name;
	msu_client_t *client;

	client_name = g_dbus_method_invocation_get_sender(task->invocation);

	if (!g_hash_table_lookup(g_context.watchers, client_name)) {
		client = g_new0(msu_client_t, 1);
		client->prefer_local_addresses = TRUE;
		client->id = g_bus_watch_name(G_BUS_TYPE_SESSION, client_name,
					      G_BUS_NAME_WATCHER_FLAGS_NONE,
					      NULL, prv_lost_client, NULL,
					      NULL);
		g_hash_table_insert(g_context.watchers, g_strdup(client_name),
				    client);
	}

	if (!g_context.cancellable && !g_context.idle_id)
		g_context.idle_id = g_idle_add(prv_process_task, NULL);

	g_ptr_array_add(g_context.tasks, task);
}

static void prv_msu_method_call(GDBusConnection *conn,
				const gchar *sender, const gchar *object,
				const gchar *interface,
				const gchar *method, GVariant *parameters,
				GDBusMethodInvocation *invocation,
				gpointer user_data)
{
	const gchar *client_name;
	msu_task_t *task;

	if (!strcmp(method, MSU_INTERFACE_RELEASE)) {
		client_name = g_dbus_method_invocation_get_sender(invocation);
		prv_remove_client(client_name);
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (!strcmp(method, MSU_INTERFACE_GET_VERSION)) {
		task = msu_task_get_version_new(invocation);
		prv_add_task(task);
	} else if (!strcmp(method, MSU_INTERFACE_GET_SERVERS)) {
		task = msu_task_get_servers_new(invocation);
		prv_add_task(task);
	} else if (!strcmp(method, MSU_INTERFACE_SET_PROTOCOL_INFO)) {
		task = msu_task_set_protocol_info_new(invocation, parameters);
		prv_add_task(task);
	} else if (!strcmp(method, MSU_INTERFACE_PREFER_LOCAL_ADDRESSES)) {
		task = msu_task_prefer_local_addresses_new(invocation,
							   parameters);
		prv_add_task(task);
	}
}

static void prv_object_method_call(GDBusConnection *conn,
				   const gchar *sender, const gchar *object,
				   const gchar *interface,
				   const gchar *method, GVariant *parameters,
				   GDBusMethodInvocation *invocation,
				   gpointer user_data)
{
	msu_task_t *task;

	if (!strcmp(method, MSU_INTERFACE_DELETE)) {
		task = msu_task_delete_new(invocation, object);
		prv_add_task(task);
	}
}

static void prv_item_method_call(GDBusConnection *conn,
				 const gchar *sender, const gchar *object,
				 const gchar *interface,
				 const gchar *method, GVariant *parameters,
				 GDBusMethodInvocation *invocation,
				 gpointer user_data)
{
	msu_task_t *task;

	if (!strcmp(method, MSU_INTERFACE_GET_COMPATIBLE_RESOURCE)) {
		task = msu_task_get_resource_new(invocation, object,
						 parameters);
		prv_add_task(task);
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
	else if (!strcmp(method, MSU_INTERFACE_UPLOAD))
		task = msu_task_upload_new(invocation, object, parameters);
	else if (!strcmp(method, MSU_INTERFACE_CREATE_CONTAINER))
		task = msu_task_create_container_new_generic(invocation,
						MSU_TASK_CREATE_CONTAINER,
						object, parameters);
	else
		goto finished;

	prv_add_task(task);

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
	msu_task_t *task;

	if (!strcmp(method, MSU_INTERFACE_GET_ALL))
		task = msu_task_get_props_new(invocation, object, parameters);
	else if (!strcmp(method, MSU_INTERFACE_GET))
		task = msu_task_get_prop_new(invocation, object, parameters);
	else
		goto finished;

	prv_add_task(task);

finished:

	return;
}

static void prv_device_method_call(GDBusConnection *conn,
				   const gchar *sender, const gchar *object,
				   const gchar *interface,
				   const gchar *method, GVariant *parameters,
				   GDBusMethodInvocation *invocation,
				   gpointer user_data)
{
	msu_task_t *task;

	if (!strcmp(method, MSU_INTERFACE_UPLOAD_TO_ANY)) {
		task = msu_task_upload_to_any_new(invocation, object,
						  parameters);
		prv_add_task(task);
	} else if (!strcmp(method, MSU_INTERFACE_CREATE_CONTAINER_IN_ANY)) {
		task = msu_task_create_container_new_generic(invocation,
					MSU_TASK_CREATE_CONTAINER_IN_ANY,
					object, parameters);
		prv_add_task(task);
	} else if (!strcmp(method, MSU_INTERFACE_GET_UPLOAD_STATUS)) {
		task = msu_task_get_upload_status_new(invocation, object,
						      parameters);
		prv_add_task(task);
	} else if (!strcmp(method, MSU_INTERFACE_GET_UPLOAD_IDS)) {
		task = msu_task_get_upload_ids_new(invocation, object);
		prv_add_task(task);
	} else if (!strcmp(method, MSU_INTERFACE_CANCEL_UPLOAD)) {
		task = msu_task_cancel_upload_new(invocation, object,
						  parameters);
		prv_add_task(task);
	}
}

static void prv_found_media_server(const gchar *path, void *user_data)
{
	(void) g_dbus_connection_emit_signal(g_context.connection,
					     NULL,
					     MSU_OBJECT,
					     MSU_INTERFACE_MANAGER,
					     MSU_INTERFACE_FOUND_SERVER,
					     g_variant_new("(o)", path),
					     NULL);
}

static void prv_lost_media_server(const gchar *path, void *user_data)
{
	(void) g_dbus_connection_emit_signal(g_context.connection,
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
	msu_interface_info_t *info;
	unsigned int i;

	g_context.connection = connection;

	g_context.msu_id =
		g_dbus_connection_register_object(connection, MSU_OBJECT,
						  g_context.root_node_info->
						  interfaces[0],
						  &g_msu_vtable,
						  user_data, NULL, NULL);

	if (!g_context.msu_id) {
		g_context.error = true;
		g_main_loop_quit(g_context.main_loop);
	} else {
		info = g_new(msu_interface_info_t, MSU_INTERFACE_INFO_MAX);
		for (i = 0; i < MSU_INTERFACE_INFO_MAX; ++i) {
			info[i].interface =
				g_context.server_node_info->interfaces[i];
			info[i].vtable = g_server_vtables[i];
		}
		g_context.upnp = msu_upnp_new(connection, info,
					    prv_found_media_server,
					    prv_lost_media_server,
					    user_data);
	}
}

static void prv_name_lost(GDBusConnection *connection, const gchar *name,
			  gpointer user_data)
{
	g_context.connection = NULL;

	prv_quit();
}

static gboolean prv_quit_handler(GIOChannel *source, GIOCondition condition,
				 gpointer user_data)
{
	prv_quit();
	g_context.sig_id = 0;

	return FALSE;
}

static bool prv_init_signal_handler(sigset_t mask)
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

	g_context.sig_id = g_io_add_watch(channel, G_IO_IN | G_IO_PRI,
					 prv_quit_handler,
					 NULL);

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
	sigset_t mask;
	int retval = 1;

	prv_msu_context_init();

	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);

	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
		goto on_error;

	g_type_init();

	msu_log_init(argv[0]);
	msu_settings_new(&g_context.settings);

	g_context.root_node_info =
		g_dbus_node_info_new_for_xml(g_msu_root_introspection, NULL);
	if (!g_context.root_node_info)
		goto on_error;

	g_context.server_node_info =
		g_dbus_node_info_new_for_xml(g_msu_server_introspection, NULL);
	if (!g_context.server_node_info)
		goto on_error;

	g_context.main_loop = g_main_loop_new(NULL, FALSE);

	g_context.owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
					  MSU_SERVER_NAME,
					  G_BUS_NAME_OWNER_FLAGS_NONE,
					  prv_bus_acquired, NULL,
					  prv_name_lost, NULL, NULL);

	g_context.tasks = g_ptr_array_new();

	g_context.watchers = g_hash_table_new_full(g_str_hash, g_str_equal,
						 g_free, prv_unregister_client);

	if (!prv_init_signal_handler(mask))
		goto on_error;

	g_main_loop_run(g_context.main_loop);
	if (g_context.error)
		goto on_error;

	retval = 0;

on_error:

	prv_msu_context_free();

	msu_log_finalize();

	return retval;
}
