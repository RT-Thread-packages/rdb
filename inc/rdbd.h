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

#ifndef __RDBD_H__
#define __RDBD_H__
#include <rtthread.h>
#include <rtdevice.h>
#ifdef __cplusplus
extern "C" {
#endif

#define RDBD_STATUS_CONNECTED        (1)
#define RDBD_STATUS_DISCONNECTED     (0)


typedef struct rdbd_msg * rdbd_msg_t;
typedef struct rdbd * rdbd_t;
struct rdbd_header
{
    rt_uint8_t source;
    rt_uint32_t msg_len : 24;
};

struct rdbd_msg
{
    struct rdbd_header header;
    rt_uint8_t msg[RT_UINT16_MAX];
};

struct rdbd_transfer_ops
{
    int (* read)(void * buffer, size_t size, void (* callback)(void * context, int size), void * context);
    int (* write)(const void * buffer, size_t size,void (* callback)(void * context, int size), void * context);
};

struct rdbd
{
    rt_list_t list;
    int status;
    char * name;
    struct rdbd_transfer_ops * private_transfer_ops;
    rt_list_t service_list;
};

extern rdbd_t rdbd_find(const char * rdbd_name);
extern int rdbd_register_transfer_ops(rdbd_t rdbd, struct rdbd_transfer_ops * ops);

extern rdbd_t rdbd_create(const char * rdbd_name);
extern int rdbd_delete(rdbd_t rdbd);
extern int rdbd_get_status(rdbd_t rdbd);

#define RDBD_MSG(x)      ((rdbd_msg_t)x)
#define RDBD_RAW_MSG(x)  ((char *)x)
#define RDBD_MSG_LEN(x)  (RDBD_MSG(x)->header.msg_len + sizeof(struct rdbd_header))

#ifdef __cplusplus
}
#endif
#endif
