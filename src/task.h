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

#ifndef MSU_TASK_H__
#define MSU_TASK_H__

#include <gio/gio.h>
#include <glib.h>

enum msu_task_type_t_ {
	MSU_TASK_GET_VERSION,
	MSU_TASK_GET_SERVERS,
	MSU_TASK_GET_CHILDREN,
	MSU_TASK_GET_ALL_PROPS,
	MSU_TASK_GET_PROP,
	MSU_TASK_SEARCH,
	MSU_TASK_GET_RESOURCE,
	MSU_TASK_SET_PROTOCOL_INFO,
	MSU_TASK_UPLOAD_TO_ANY,
	MSU_TASK_UPLOAD
};
typedef enum msu_task_type_t_ msu_task_type_t;

typedef void (*msu_cancel_task_t)(void *handle);

typedef struct msu_task_get_children_t_ msu_task_get_children_t;
struct msu_task_get_children_t_ {
	gboolean containers;
	gboolean items;
	guint start;
	guint count;
	GVariant *filter;
	gchar *sort_by;
};

typedef struct msu_task_get_props_t_ msu_task_get_props_t;
struct msu_task_get_props_t_ {
	gchar *interface_name;
};

typedef struct msu_task_get_prop_t_ msu_task_get_prop_t;
struct msu_task_get_prop_t_ {
	gchar *prop_name;
	gchar *interface_name;
};

typedef struct msu_task_search_t_ msu_task_search_t;
struct msu_task_search_t_ {
	gchar *query;
	guint start;
	guint count;
	gchar *sort_by;
	GVariant *filter;
};

typedef struct msu_task_get_resource_t_ msu_task_get_resource_t;
struct msu_task_get_resource_t_ {
	gchar *protocol_info;
	GVariant *filter;
};

typedef struct msu_task_set_protocol_info_t_ msu_task_set_protocol_info_t;
struct msu_task_set_protocol_info_t_ {
	gchar *protocol_info;
};

typedef struct msu_task_upload_t_ msu_task_upload_t;
struct msu_task_upload_t_ {
	gchar *display_name;
	gchar *file_path;
};

typedef struct msu_task_t_ msu_task_t;
struct msu_task_t_ {
	msu_task_type_t type;
	gchar *path;
	const gchar *result_format;
	GVariant *result;
	GDBusMethodInvocation *invocation;
	gboolean synchronous;
	gboolean multiple_retvals;
	union {
		msu_task_get_children_t get_children;
		msu_task_get_props_t get_props;
		msu_task_get_prop_t get_prop;
		msu_task_search_t search;
		msu_task_get_resource_t resource;
		msu_task_set_protocol_info_t protocol_info;
		msu_task_upload_t upload;
	} ut;
};

msu_task_t *msu_task_get_version_new(GDBusMethodInvocation *invocation);
msu_task_t *msu_task_get_servers_new(GDBusMethodInvocation *invocation);
msu_task_t *msu_task_get_children_new(GDBusMethodInvocation *invocation,
				      const gchar *path, GVariant *parameters,
				      gboolean items, gboolean containers);
msu_task_t *msu_task_get_children_ex_new(GDBusMethodInvocation *invocation,
					 const gchar *path,
					 GVariant *parameters, gboolean items,
					 gboolean containers);
msu_task_t *msu_task_get_prop_new(GDBusMethodInvocation *invocation,
				  const gchar *path, GVariant *parameters);
msu_task_t *msu_task_get_props_new(GDBusMethodInvocation *invocation,
				   const gchar *path, GVariant *parameters);
msu_task_t *msu_task_search_new(GDBusMethodInvocation *invocation,
				const gchar *path, GVariant *parameters);
msu_task_t *msu_task_search_ex_new(GDBusMethodInvocation *invocation,
				   const gchar *path, GVariant *parameters);
msu_task_t *msu_task_get_resource_new(GDBusMethodInvocation *invocation,
				      const gchar *path, GVariant *parameters);
msu_task_t *msu_task_set_protocol_info_new(GDBusMethodInvocation *invocation,
					   GVariant *parameters);
msu_task_t *msu_task_upload_to_any_new(GDBusMethodInvocation *invocation,
				       const gchar *path, GVariant *parameters);
msu_task_t *msu_task_upload_new(GDBusMethodInvocation *invocation,
				const gchar *path, GVariant *parameters);
void msu_task_complete_and_delete(msu_task_t *task);
void msu_task_fail_and_delete(msu_task_t *task, GError *error);
void msu_task_cancel_and_delete(msu_task_t *task);
void msu_task_delete(msu_task_t *task);

#endif
