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
 * Ludovic Ferrandis <ludovic.ferrandis@intel.com>
 *
 */

#ifndef MSU_SETTINGS_H__
#define MSU_SETTINGS_H__

#include <glib.h>
#include <gio/gio.h>

#include "log.h"

struct msu_settings_context_t_ {
	GKeyFile *keyfile;
	GFileMonitor *monitor;
	gulong handler_id;
	guint ev_id;

	/* Global section */
	gboolean never_quit;

	/* Log section */
	msu_log_type_t log_type;
	int log_level;
};
typedef struct msu_settings_context_t_ msu_settings_context_t;

void msu_settings_init(msu_settings_context_t *settings);
void msu_settings_finalize(msu_settings_context_t *settings);

#endif /* MSU_SETTINGS_H__ */
