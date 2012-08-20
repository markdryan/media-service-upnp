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

	g_variant_get(parameters, "(uu@as)", &task->ut.get_children.start,
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

	g_variant_get(parameters, "(uu@ass)", &task->ut.get_children.start,
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

	g_variant_get(parameters, "(s@as)", &task->ut.resource.protocol_info,
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
	default:
		break;
	}

	g_free(task->path);
	if (task->result)
		g_variant_unref(task->result);

	g_free(task);
}

void msu_task_complete_and_delete(msu_task_t *task)
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
	prv_msu_task_delete(task);

finished:

	return;
}

void msu_task_fail_and_delete(msu_task_t *task, GError *error)
{
	if (!task)
		goto finished;

	if (task->invocation)
		g_dbus_method_invocation_return_gerror(task->invocation, error);

	prv_msu_task_delete(task);

finished:

	return;
}

void msu_task_cancel_and_delete(msu_task_t *task)
{
	GError *error;

	if (!task)
		goto finished;

	if (task->invocation) {
		error = g_error_new(MSU_ERROR, MSU_ERROR_CANCELLED,
				    "Operation cancelled.");
		g_dbus_method_invocation_return_gerror(task->invocation, error);
		g_error_free(error);
	}

	prv_msu_task_delete(task);

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
