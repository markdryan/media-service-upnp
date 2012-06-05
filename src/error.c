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

#include "config.h"

#include <glib.h>
#include <gio/gio.h>

#include "error.h"


static const GDBusErrorEntry g_error_entries[] = {
	{ MSU_ERROR_BAD_PATH, MSU_SERVICE".BadPath" },
	{ MSU_ERROR_OBJECT_NOT_FOUND, MSU_SERVICE".ObjectNotFound" },
	{ MSU_ERROR_BAD_QUERY, MSU_SERVICE".BadQuery" },
	{ MSU_ERROR_OPERATION_FAILED, MSU_SERVICE".OperationFailed" },
	{ MSU_ERROR_BAD_RESULT, MSU_SERVICE".BadResult" },
	{ MSU_ERROR_UNKNOWN_INTERFACE, MSU_SERVICE".UnknownInterface" },
	{ MSU_ERROR_UNKNOWN_PROPERTY, MSU_SERVICE".UnknownProperty" },
	{ MSU_ERROR_DEVICE_NOT_FOUND, MSU_SERVICE".DeviceNotFound" },
	{ MSU_ERROR_DIED, MSU_SERVICE".Died" },
	{ MSU_ERROR_CANCELLED, MSU_SERVICE".Cancelled" },
};

GQuark msu_error_quark(void)
{
	static gsize quark = 0;
	g_dbus_error_register_error_domain("mc-error-quark", &quark,
					   g_error_entries,
					   sizeof(g_error_entries) /
					   sizeof(const GDBusErrorEntry));
	return (GQuark) quark;
}
