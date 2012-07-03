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

#ifndef MSU_ERROR_H__
#define MSU_ERROR_H__

#include <glib.h>

enum msu_error_t_ {
	MSU_ERROR_BAD_PATH,
	MSU_ERROR_OBJECT_NOT_FOUND,
	MSU_ERROR_BAD_QUERY,
	MSU_ERROR_OPERATION_FAILED,
	MSU_ERROR_BAD_RESULT,
	MSU_ERROR_UNKNOWN_INTERFACE,
	MSU_ERROR_UNKNOWN_PROPERTY,
	MSU_ERROR_DEVICE_NOT_FOUND,
	MSU_ERROR_DIED,
	MSU_ERROR_CANCELLED
};
typedef enum msu_error_t_ msu_error_t;

#define MSU_ERROR (msu_error_quark())
GQuark msu_error_quark(void);

#endif
