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

#ifndef __RDBD_SERVICE_H__
#define __RDBD_SERVICE_H__
#include <rtthread.h>
#include <rdbd.h>
#ifdef __cplusplus
extern "C" {
#endif
///<summer>
///default Service id for rdbd service, you can used service id 20-255
///</summer>
#define RDBD_SERVICE_ID_CONTROL      (0U)
#define RDBD_SERVICE_ID_FILE         (1U)
#define RDBD_SERVICE_ID_SHELL        (2U)
#define RDBD_SERVICE_ID_RTI          (3U)
#define RDBD_SERVICE_ID_TCP_DUMP     (4U)
#define RDBD_SERVICE_ID_OTA          (5U)

#define RDBD_SERVICE_FLAG_NONE  0
#define RDBD_SERVICE_FLAG_RD    1
#define RDBD_SERVICE_FLAG_WR    2

enum rdbd_service_status
{
    RDBD_SERVICE_STATUS_RUNNING = 1,
    RDBD_SERVICE_STATUS_STOP = 2,
    RDBD_SERVICE_STATUS_SUSPENDED = 3
};

struct rdbd_service_control_ops
{
    int (*start)(void * args);
    int (*stop)(void * args);
    int (* resume)(void * args);
    int (* suspend)(void * args);
};

struct rdbd_request_write
{
    rt_list_t list;
    struct rdbd_msg * msg;
    int msg_pos;
};

struct rdbd_service
{
    rt_list_t list;
    char * name;
    rdbd_t rdbd;
    int status;
    char * in_pipe_path;
    char * out_pipe_path;

    int in_pipe_read_fd;
    int in_pipe_write_fd;
    int out_pipe_read_fd;
    int out_pipe_write_fd;
    rt_thread_t service_thread;
    struct rdbd_service_control_ops * control_ops;
    void * user_data;
    struct rdbd_msg * msg;
    int msg_pos;

    rt_list_t request_write_list;
    rt_uint8_t service_id;
    rt_uint8_t flag;
};
extern struct rdbd_service * rdbd_create_service(rt_uint8_t service_id,
                                                const char * name,
                                                struct rdbd_service_control_ops * control_ops, 
                                                void * user_data, 
                                                const char * in_pipe_name,
                                                rt_uint32_t in_buf_size,
                                                const char * out_pipe_name,
                                                rt_uint32_t out_buf_size,
                                                rt_uint8_t flag);

extern int rdbd_service_request_write(struct rdbd_service * service, struct rdbd_msg * msg);

extern int rdbd_service_request_delete(struct rdbd_request_write * request);

#ifdef __cplusplus
}
#endif

#endif
