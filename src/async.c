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

#include "async.h"
#include "error.h"
#include "log.h"

msu_async_cb_data_t *msu_async_cb_data_new(msu_task_t *task,
					   msu_upnp_task_complete_t cb,
					   void *user_data)
{
	msu_async_cb_data_t *cb_data = g_new0(msu_async_cb_data_t, 1);

	cb_data->type = task->type;
	cb_data->task = task;
	cb_data->cb = cb;
	cb_data->user_data = user_data;

	return cb_data;
}

void msu_async_cb_data_delete(msu_async_cb_data_t *cb_data)
{
	if (cb_data) {
		switch (cb_data->type) {
		case MSU_TASK_GET_CHILDREN:
		case MSU_TASK_SEARCH:
			g_free(cb_data->ut.bas.root_path);
			if (cb_data->ut.bas.vbs)
				g_ptr_array_unref(cb_data->ut.bas.vbs);
			break;
		case MSU_TASK_GET_PROP:
			g_free(cb_data->ut.get_prop.root_path);
			break;
		case MSU_TASK_GET_ALL_PROPS:
		case MSU_TASK_GET_RESOURCE:
			g_free(cb_data->ut.get_all.root_path);
			if (cb_data->ut.get_all.vb)
				g_variant_builder_unref(cb_data->ut.get_all.vb);
			break;
		case MSU_TASK_UPLOAD_TO_ANY:
		case MSU_TASK_UPLOAD:
			g_free(cb_data->ut.upload.root_path);
			g_free(cb_data->ut.upload.mime_type);
			break;
		default:
			break;
		}

		g_free(cb_data->id);
		g_free(cb_data);
	}
}

gboolean msu_async_complete_task(gpointer user_data)
{
	msu_async_cb_data_t *cb_data = user_data;

	MSU_LOG_DEBUG("Enter. Error %p", (void *) cb_data->error);
	MSU_LOG_DEBUG_NL();

	cb_data->cb(cb_data->task, cb_data->result, cb_data->error,
		    cb_data->user_data);
	msu_async_cb_data_delete(cb_data);

	return FALSE;
}

void msu_async_task_cancelled(GCancellable *cancellable, gpointer user_data)
{
	msu_async_cb_data_t *cb_data = user_data;

	gupnp_service_proxy_cancel_action(cb_data->proxy, cb_data->action);

	if (!cb_data->error)
		cb_data->error = g_error_new(MSU_ERROR, MSU_ERROR_CANCELLED,
					     "Operation cancelled.");
	(void) g_idle_add(msu_async_complete_task, cb_data);
}
