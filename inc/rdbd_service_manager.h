/*
 * Copyright (c) 2018, Real-Thread Information Technology Ltd
 * All rights reserved
 *
 * This software is dual-licensed: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. For the terms of this
 * license, see <http://www.gnu.org/licenses/>.
 *
 * You are free to use this software under the terms of the GNU General
 * Public License, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * Alternatively, you can license this software under a commercial
 * license, please send mail to business@rt-thread.com for contact. 
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-09-25     ZYH          the first version
 */

#ifndef __RDBD_SERVICE_MANAGER_H__
#define __RDBD_SERVICE_MANAGER_H__

#include <rtthread.h>
#include <rdbd_service.h>

#ifdef __cplusplus
extern "C" {
#endif

enum rdbd_service_control_cmd
{
    RDBD_SERVICE_START = 1,
    RDBD_SERVICE_STOP = 2,
    RDBD_SERVICE_SUSPEND = 3,
    RDBD_SERVICE_RESUME = 4,
    RDBD_SERVICE_GET_STATUS = 5,
};

extern int rdbd_service_control(struct rdbd_service * service, int cmd, void * args);
extern int rdbd_service_install(rdbd_t rdbd, struct rdbd_service * service);
extern int rdbd_service_uninstall(struct rdbd_service * service);
extern struct rdbd_service * rdbd_service_find(rdbd_t rdbd, const char * name);
extern struct rdbd_service * rdbd_service_get(rdbd_t rdbd, rt_uint8_t service_id);
#ifdef __cplusplus
}
#endif

#endif
