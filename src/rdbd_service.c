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

#include <rdbd_service.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#undef DBG_ENABLE
#ifdef PKGS_USING_RDBD_SHELL
#undef DBG_ENABLE
#endif
#define DBG_SECTION_NAME  "RDBD Service"
#define DBG_LEVEL         DBG_LOG
#define DBG_COLOR
#include <rtdbg.h>

struct rdbd_service * rdbd_create_service(rt_uint8_t service_id,
                                                const char * name,
                                                struct rdbd_service_control_ops * control_ops, 
                                                void * user_data, 
                                                const char * in_pipe_name,
                                                rt_uint32_t in_buf_size,
                                                const char * out_pipe_name,
                                                rt_uint32_t out_buf_size,
                                                rt_uint8_t flag)
{
    struct rdbd_service * service = RT_NULL;
    rt_pipe_t * inpipe = RT_NULL;
    rt_pipe_t * outpipe = RT_NULL;
    
    if(RT_NULL == name)
    {
        LOG_E("Service create failed,name is null");
        return RT_NULL;
    }
    if(RT_NULL == control_ops)
    {
        LOG_E("Service %s create failed,control_ops is null", name);
        return RT_NULL;
    }
    if((flag & RDBD_SERVICE_FLAG_RD) && ((RT_NULL == in_pipe_name) || (0 == in_buf_size)))
    {
        LOG_E("Service %s create failed,service is readable but in_pipe_name is null or in_buf_size is 0", name);
        return RT_NULL;
    }

    if((flag & RDBD_SERVICE_FLAG_WR) && ((RT_NULL == out_pipe_name) || (0 == out_buf_size)))
    {
        LOG_E("Service %s create failed,service is writeable but out_buf_size is null", name);
        return RT_NULL;
    }

    service = calloc(sizeof(struct rdbd_service), 1);
    if(RT_NULL == service)
    {
        LOG_E("Service %s create failed,no memory", name);
        return RT_NULL;
    }

    service->name = rt_strdup(name);
    if(RT_NULL == service->name)
    {
        LOG_E("Service %s create failed,no memory", name);
        goto _error;
    }

    if(flag & RDBD_SERVICE_FLAG_RD)
    {
        inpipe = rt_pipe_create(in_pipe_name, in_buf_size);
        if(RT_NULL == inpipe)
        {
            LOG_E("Service %s create failed,no memory", name);
            goto _error;
        }
        LOG_I("Create pipe %s size %d", in_pipe_name, in_buf_size);
        service->in_pipe_path = calloc(strlen("/dev/") + strlen(in_pipe_name) + 1, 1);
        if(RT_NULL == service->in_pipe_path)
        {
            LOG_E("Service %s create failed,no memory", name);
            goto _error;
        }
        sprintf(service->in_pipe_path, "/dev/%s", in_pipe_name);
    }

    if(flag & RDBD_SERVICE_FLAG_WR)
    {
        outpipe = rt_pipe_create(out_pipe_name, out_buf_size);
        if(RT_NULL == outpipe)
        {
            LOG_E("Service %s create failed,no memory", name);
            goto _error;
        }
        LOG_I("Create pipe %s size %d", out_pipe_name, out_buf_size);
        service->out_pipe_path = calloc(strlen("/dev/") + strlen(out_pipe_name) + 1, 1);
        if(RT_NULL == service->out_pipe_path)
        {
            LOG_E("Service %s create failed,no memory", name);
            goto _error;
        }
        sprintf(service->out_pipe_path, "/dev/%s", out_pipe_name);
    }

    service->control_ops = control_ops;
    service->user_data = user_data;
    service->service_id = service_id;
    service->status = RDBD_SERVICE_STATUS_STOP;
    service->flag = flag;
    rt_list_init(&service->request_write_list);
    return service;

_error:
    if(RT_NULL != service)
    {
        if(RT_NULL != service->name)
        {
            free(service->name);
            service->name = RT_NULL;
        }
        
        if(RT_NULL != service->in_pipe_path)
        {
            free(service->in_pipe_path);
            service->in_pipe_path = RT_NULL;
        }

        if(RT_NULL != service->out_pipe_path)
        {
            free(service->out_pipe_path);
            service->out_pipe_path = RT_NULL;
        }

        free(service);
        service = RT_NULL;
    }
    return RT_NULL;
}

int rdbd_service_request_write(struct rdbd_service * service, struct rdbd_msg * msg)
{
    struct rdbd_request_write * request;
    if(RT_NULL == service || RT_NULL == msg)
    {
        LOG_E("Write request arg error");
        return -1;
    }
    request = rt_malloc(sizeof(struct rdbd_request_write));
    if(RT_NULL == request)
    {
        LOG_E("No memory request write to %s", service->name);
        return -1;
    }
    request->msg = msg;
    request->msg_pos = 0;
    rt_list_insert_before(&service->request_write_list, &request->list);
    return 0;
}

int rdbd_service_request_delete(struct rdbd_request_write * request)
{
    if(RT_NULL == request)
    {
        LOG_E("Delete request arg error");
        return -1;
    }
    if(RT_NULL != request->msg)
    {
        rt_free(request->msg);
    }
    rt_list_remove(&request->list);
    rt_free(request);
    return 0;
}
