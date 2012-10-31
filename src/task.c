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

#include "error.h"
#include "task.h"

static msu_task_t *prv_upload_new_generic(msu_task_type_t type,
					  GDBusMethodInvocation *invocation,
					  const gchar *path,
					  GVariant *parameters);

msu_task_t *msu_task_get_version_new(GDBusMethodInvocation *invocation)
{
	msu_task_t *task = g_new0(msu_task_t, 1);

	task->type = MSU_TASK_GET_VERSION;
	task->invocation = invocation;
	task->result_format = "(@s)";
	task->result = g_variant_ref_sink(g_variant_new_string(VERSION));
	task->synchronous = TRUE;

	return task;
}

msu_task_t *msu_task_get_servers_new(GDBusMethodInvocation *invocation)
{
	msu_task_t *task = g_new0(msu_task_t, 1);

	task->type = MSU_TASK_GET_SERVERS;
	task->invocation = invocation;
	task->result_format = "(@ao)";
	task->synchronous = TRUE;

	return task;
}

static msu_task_t *prv_m2spec_task_new(msu_task_type_t type,
				       GDBusMethodInvocation *invocation,
				       const gchar *path,
				       const gchar *result_format)
{
	msu_task_t *task = g_new0(msu_task_t, 1);

	task->type = type;
	task->invocation = invocation;
	task->result_format = result_format;

	task->path = g_strdup(path);
	g_strstrip(task->path);

	return task;
}

msu_task_t *msu_task_get_children_new(GDBusMethodInvocation *invocation,
				      const gchar *path, GVariant *parameters,
				      gboolean items, gboolean containers)
{
	msu_task_t *task;

	task = prv_m2spec_task_new(MSU_TASK_GET_CHILDREN, invocation, path,
				   "(@aa{sv})");

	task->ut.get_children.containers = containers;
	task->ut.get_children.items = items;

	g_variant_get(parameters, "(uu@as)",
		      &task->ut.get_children.start,
		      &task->ut.get_children.count,
		      &task->ut.get_children.filter);

	task->ut.get_children.sort_by = g_strdup("");

	return task;
}

msu_task_t *msu_task_get_children_ex_new(GDBusMethodInvocation *invocation,
					 const gchar *path,
					 GVariant *parameters, gboolean items,
					 gboolean containers)
{
	msu_task_t *task;

	task = prv_m2spec_task_new(MSU_TASK_GET_CHILDREN, invocation, path,
				   "(@aa{sv})");

	task->ut.get_children.containers = containers;
	task->ut.get_children.items = items;

	g_variant_get(parameters, "(uu@ass)",
		      &task->ut.get_children.start,
		      &task->ut.get_children.count,
		      &task->ut.get_children.filter,
		      &task->ut.get_children.sort_by);

	return task;
}

msu_task_t *msu_task_get_prop_new(GDBusMethodInvocation *invocation,
				  const gchar *path, GVariant *parameters)
{
	msu_task_t *task;

	task = prv_m2spec_task_new(MSU_TASK_GET_PROP, invocation, path, "(v)");

	g_variant_get(parameters, "(ss)", &task->ut.get_prop.interface_name,
		      &task->ut.get_prop.prop_name);

	g_strstrip(task->ut.get_prop.interface_name);
	g_strstrip(task->ut.get_prop.prop_name);

	return task;
}

msu_task_t *msu_task_get_props_new(GDBusMethodInvocation *invocation,
				   const gchar *path, GVariant *parameters)
{
	msu_task_t *task;

	task = prv_m2spec_task_new(MSU_TASK_GET_ALL_PROPS, invocation, path,
				   "(@a{sv})");

	g_variant_get(parameters, "(s)", &task->ut.get_props.interface_name);
	g_strstrip(task->ut.get_props.interface_name);

	return task;
}

msu_task_t *msu_task_search_new(GDBusMethodInvocation *invocation,
				const gchar *path, GVariant *parameters)
{
	msu_task_t *task;

	task = prv_m2spec_task_new(MSU_TASK_SEARCH, invocation, path,
				   "(@aa{sv})");

	g_variant_get(parameters, "(suu@as)", &task->ut.search.query,
		      &task->ut.search.start, &task->ut.search.count,
		      &task->ut.search.filter);

	task->ut.search.sort_by = g_strdup("");

	return task;
}

msu_task_t *msu_task_search_ex_new(GDBusMethodInvocation *invocation,
				   const gchar *path, GVariant *parameters)
{
	msu_task_t *task;

	task = prv_m2spec_task_new(MSU_TASK_SEARCH, invocation, path,
				   "(@aa{sv}u)");

	g_variant_get(parameters, "(suu@ass)", &task->ut.search.query,
		      &task->ut.search.start, &task->ut.search.count,
		      &task->ut.search.filter, &task->ut.search.sort_by);

	task->multiple_retvals = TRUE;

	return task;
}

msu_task_t *msu_task_get_resource_new(GDBusMethodInvocation *invocation,
				      const gchar *path, GVariant *parameters)
{
	msu_task_t *task;

	task = prv_m2spec_task_new(MSU_TASK_GET_RESOURCE, invocation, path,
				   "(@a{sv})");

	g_variant_get(parameters, "(s@as)",
		      &task->ut.resource.protocol_info,
		      &task->ut.resource.filter);

	return task;
}

msu_task_t *msu_task_set_protocol_info_new(GDBusMethodInvocation *invocation,
					   GVariant *parameters)
{
	msu_task_t *task = g_new0(msu_task_t, 1);

	task->type = MSU_TASK_SET_PROTOCOL_INFO;
	task->invocation = invocation;
	task->synchronous = TRUE;
	g_variant_get(parameters, "(s)", &task->ut.protocol_info.protocol_info);

	return task;
}

static msu_task_t *prv_upload_new_generic(msu_task_type_t type,
					  GDBusMethodInvocation *invocation,
					  const gchar *path,
					  GVariant *parameters)
{
	msu_task_t *task;

	task = prv_m2spec_task_new(type, invocation, path, "(uo)");

	g_variant_get(parameters, "(ss)", &task->ut.upload.display_name,
		      &task->ut.upload.file_path);
	g_strstrip(task->ut.upload.file_path);
	task->multiple_retvals = TRUE;

	return task;
}

msu_task_t *msu_task_prefer_local_addresses_new(
					GDBusMethodInvocation *invocation,
					GVariant *parameters)
{
	msu_task_t *task = g_new0(msu_task_t, 1);

	task->type = MSU_TASK_SET_PREFER_LOCAL_ADDRESSES;
	task->invocation = invocation;
	task->synchronous = TRUE;
	g_variant_get(parameters, "(b)",
		      &task->ut.prefer_local_addresses.prefer);

	return task;
}

msu_task_t *msu_task_upload_to_any_new(GDBusMethodInvocation *invocation,
				       const gchar *path, GVariant *parameters)
{
	return prv_upload_new_generic(MSU_TASK_UPLOAD_TO_ANY, invocation,
				      path, parameters);
}

msu_task_t *msu_task_upload_new(GDBusMethodInvocation *invocation,
				const gchar *path, GVariant *parameters)
{
	return prv_upload_new_generic(MSU_TASK_UPLOAD, invocation,
				      path, parameters);
}

msu_task_t *msu_task_get_upload_status_new(GDBusMethodInvocation *invocation,
					   const gchar *path,
					   GVariant *parameters)
{
	msu_task_t *task;

	task = prv_m2spec_task_new(MSU_TASK_GET_UPLOAD_STATUS, invocation, path,
				   "(stt)");

	g_variant_get(parameters, "(u)",
		      &task->ut.upload_action.upload_id);
	task->synchronous = TRUE;
	task->multiple_retvals = TRUE;

	return task;
}

msu_task_t *msu_task_get_upload_ids_new(GDBusMethodInvocation *invocation,
					const gchar *path)
{
	msu_task_t *task;

	task = prv_m2spec_task_new(MSU_TASK_GET_UPLOAD_IDS, invocation, path,
				   "(@au)");

	task->synchronous = TRUE;

	return task;
}

msu_task_t *msu_task_cancel_upload_new(GDBusMethodInvocation *invocation,
				       const gchar *path,
				       GVariant *parameters)
{
	msu_task_t *task;

	task = prv_m2spec_task_new(MSU_TASK_CANCEL_UPLOAD, invocation, path,
				   NULL);

	g_variant_get(parameters, "(u)",
		      &task->ut.upload_action.upload_id);
	task->synchronous = TRUE;

	return task;
}

msu_task_t *msu_task_delete_new(GDBusMethodInvocation *invocation,
				const gchar *path)
{
	msu_task_t *task;

	task = prv_m2spec_task_new(MSU_TASK_DELETE_OBJECT, invocation,
				   path, NULL);
	return task;
}

msu_task_t *msu_task_create_container_new_generic(
					GDBusMethodInvocation *invocation,
					msu_task_type_t type,
					const gchar *path,
					GVariant *parameters)
{
	msu_task_t *task;

	task = prv_m2spec_task_new(type, invocation, path, "(@o)");

	g_variant_get(parameters, "(ss@as)",
		      &task->ut.create_container.display_name,
		      &task->ut.create_container.type,
		      &task->ut.create_container.child_types);

	return task;
}

msu_task_t *msu_task_update_new(GDBusMethodInvocation *invocation,
				const gchar *path, GVariant *parameters)
{
	msu_task_t *task;

	task = prv_m2spec_task_new(MSU_TASK_UPDATE_OBJECT, invocation, path,
				   NULL);

	g_variant_get(parameters, "(@a{sv}@as)",
		      &task->ut.update.to_add_update,
		      &task->ut.update.to_delete);

	return task;
}

static void prv_msu_task_delete(msu_task_t *task)
{
	switch (task->type) {
	case MSU_TASK_GET_CHILDREN:
		if (task->ut.get_children.filter)
			g_variant_unref(task->ut.get_children.filter);
		g_free(task->ut.get_children.sort_by);
		break;
	case MSU_TASK_GET_ALL_PROPS:
		g_free(task->ut.get_props.interface_name);
		break;
	case MSU_TASK_GET_PROP:
		g_free(task->ut.get_prop.interface_name);
		g_free(task->ut.get_prop.prop_name);
		break;
	case MSU_TASK_SEARCH:
		g_free(task->ut.search.query);
		if (task->ut.search.filter)
			g_variant_unref(task->ut.search.filter);
		g_free(task->ut.search.sort_by);
		break;
	case MSU_TASK_GET_RESOURCE:
		if (task->ut.resource.filter)
			g_variant_unref(task->ut.resource.filter);
		g_free(task->ut.resource.protocol_info);
		break;
	case MSU_TASK_SET_PROTOCOL_INFO:
		if (task->ut.protocol_info.protocol_info)
			g_free(task->ut.protocol_info.protocol_info);
		break;
	case MSU_TASK_UPLOAD_TO_ANY:
	case MSU_TASK_UPLOAD:
		g_free(task->ut.upload.display_name);
		g_free(task->ut.upload.file_path);
		break;
	case MSU_TASK_CREATE_CONTAINER:
	case MSU_TASK_CREATE_CONTAINER_IN_ANY:
		g_free(task->ut.create_container.display_name);
		g_free(task->ut.create_container.type);
		g_variant_unref(task->ut.create_container.child_types);
		break;
	case MSU_TASK_UPDATE_OBJECT:
		if (task->ut.update.to_add_update)
			g_variant_unref(task->ut.update.to_add_update);
		if (task->ut.update.to_delete)
			g_variant_unref(task->ut.update.to_delete);
		break;
	default:
		break;
	}

	g_free(task->path);
	if (task->result)
		g_variant_unref(task->result);

	g_free(task);
}

void msu_task_complete(msu_task_t *task)
{
	GVariant *variant = NULL;

	if (!task)
		goto finished;

	if (task->invocation) {
		if (task->result_format) {
			if (task->multiple_retvals)
				variant = task->result;
			else
				variant = g_variant_new(task->result_format,
							task->result);
		}
		g_dbus_method_invocation_return_value(task->invocation,
						      variant);
	}

finished:

	return;
}

void msu_task_fail(msu_task_t *task, GError *error)
{
	if (!task)
		goto finished;

	if (task->invocation) {
		g_dbus_method_invocation_return_gerror(task->invocation, error);
		task->invocation = NULL;
	}

finished:

	return;
}

void msu_task_cancel(msu_task_t *task)
{
	GError *error;

	if (!task)
		goto finished;

	if (task->invocation) {
		error = g_error_new(MSU_ERROR, MSU_ERROR_CANCELLED,
				    "Operation cancelled.");
		g_dbus_method_invocation_return_gerror(task->invocation, error);
		task->invocation = NULL;
		g_error_free(error);
	}

finished:

	return;
}

void msu_task_delete(msu_task_t *task)
{
	GError *error;

	if (!task)
		goto finished;

	if (task->invocation) {
		error = g_error_new(MSU_ERROR, MSU_ERROR_DIED,
				    "Unable to complete command.");
		g_dbus_method_invocation_return_gerror(task->invocation, error);
		g_error_free(error);
	}

	prv_msu_task_delete(task);

finished:

	return;
}
