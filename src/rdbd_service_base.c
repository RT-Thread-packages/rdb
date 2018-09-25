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

#include <rdbd.h>
#include <rdbd_service.h>
#include <rdbd_service_manager.h>
#include <fcntl.h>
#include <sys/time.h>
#include <dfs_select.h>
#include <unistd.h>
#define RDB_RINGBUFF_SIZE   2048

#undef DBG_ENABLE
#define DBG_SECTION_NAME  "RDBD Service"
#define DBG_LEVEL         DBG_LOG
#define DBG_COLOR
#include <rtdbg.h>
static void hex_dump(const char *name, const char *buf, rt_size_t size)
{
    #define __is_print(ch)       ((unsigned int)((ch) - ' ') < 127u - ' ')
    #define WIDTH_SIZE           16

    rt_size_t i, j;

    for (i = 0; i < size; i += WIDTH_SIZE)
    {
        LOG_I("%s: %04X-%04X: ", name, i, i + WIDTH_SIZE);
        for (j = 0; j < WIDTH_SIZE; j++)
        {
            if (i + j < size)
            {
                LOG_RAW("%02X ", buf[i + j]);
            }
            else
            {
                LOG_RAW("   ");
            }
            if ((j + 1) % 8 == 0)
            {
                LOG_RAW(" ");
            }
        }
        LOG_RAW("  ");
        for (j = 0; j < WIDTH_SIZE; j++)
        {
            if (i + j < size)
            {
                LOG_RAW("%c", __is_print(buf[i + j]) ? buf[i + j] : '.');
            }
        }
        LOG_RAW("\n");
    }
}
static int start(void * args);
static int stop(void * args);
static int resume(void * args);
static int suspend(void * args);
static struct rdbd_service_control_ops control_ops = 
{
    start,
    stop,
    resume,
    suspend,
};


extern struct rdbd_transfer_ops usb_rdb_transfer_ops;
int rdbd_base_service_init(void)
{
    struct rdbd_service * base_service;
    rdbd_t usbrdbd = rdbd_create("usb");
    if(RT_NULL == usbrdbd)
    {
        LOG_E("rdbd create error");
        return -1;
    }

    rdbd_register_transfer_ops(usbrdbd, &usb_rdb_transfer_ops);
    
    base_service = rdbd_create_service(RDBD_SERVICE_ID_CONTROL, "base", &control_ops, RT_NULL, "basein", RDB_RINGBUFF_SIZE,"baseout", RDB_RINGBUFF_SIZE, RDBD_SERVICE_FLAG_WR|RDBD_SERVICE_FLAG_RD);
    if(RT_NULL == base_service)
    {
        LOG_E("base_service create error");
        goto _error;
    }
    LOG_I("Service %s created :", base_service->name);
    LOG_I("in_pipe_path %s", base_service->in_pipe_path);
    LOG_I("out_pipe_path %s", base_service->out_pipe_path);
    LOG_I("service_id %d", base_service->service_id);
    LOG_I("status %d", base_service->status);

    rdbd_service_install(usbrdbd, base_service);
    
    rdbd_service_control(base_service, RDBD_SERVICE_START, base_service);
    
//    rdbd_delete(usbrdbd);

    return 0;

_error:
    if(RT_NULL != usbrdbd)
    {
        //TO DO
    }
    return -1;
}
INIT_COMPONENT_EXPORT(rdbd_base_service_init);
static int rdbd_service_base_response(struct rdbd_service * base_service, char * request, int request_size, char ** response, int * response_size);

static void base_service_thread_entry(void * arg)
{
    struct rdbd_service * base_service= (struct rdbd_service *)arg;
    struct rdbd_service * service;
    struct rdbd_request_write * request;
    char * response = RT_NULL;
    int response_size = 0;
    fd_set readset, writeset;
    int res = 0;
    struct timeval timeout;
    rt_list_t * node;
    int max_fd = base_service->in_pipe_read_fd + 1;
    int result = 0;
    while(1)
    {
        FD_ZERO(&readset);
        FD_ZERO(&writeset);
        
        FD_SET(base_service->in_pipe_read_fd, &readset);
        if(!rt_list_isempty(&base_service->request_write_list)) //need write data to pc
        {
            FD_SET(base_service->out_pipe_write_fd, &writeset);
            if(base_service->out_pipe_write_fd > (max_fd - 1))
            {
                max_fd = base_service->out_pipe_write_fd + 1;
            }
        }

        for(node = base_service->rdbd->service_list.next; 
            node != &base_service->rdbd->service_list; 
            node = node->next)
        {
            service = rt_list_entry(node, struct rdbd_service, list);
            if(service == base_service || service->status != RDBD_SERVICE_STATUS_RUNNING)
            {
                continue;
            }
            if(service->flag & RDBD_SERVICE_FLAG_WR)
            {
                FD_SET(service->out_pipe_read_fd, &readset);
                if(service->out_pipe_read_fd > (max_fd - 1))
                {
                    max_fd = service->out_pipe_read_fd + 1;
                }
            }
            if((service->flag & RDBD_SERVICE_FLAG_RD) && !rt_list_isempty(&service->request_write_list)) // need write data to service
            {
                FD_SET(service->in_pipe_write_fd, &writeset);
                if(service->in_pipe_write_fd > (max_fd - 1))
                {
                    max_fd = service->in_pipe_write_fd + 1;
                }
            }
        }

        timeout.tv_sec=0;
        timeout.tv_usec=500000; //timeout 500ms
        res = select(max_fd, &readset, &writeset, RT_NULL, &timeout);
        if(res == 0)//timeout
        {
            //LOG_I("res %d", res);
        }
        else if(res > 0)//select successful
        {
            if(FD_ISSET(base_service->in_pipe_read_fd, &readset))//pc data in
            {
                if(base_service->msg == RT_NULL)
                {
                    base_service->msg = rt_calloc(sizeof(struct rdbd_header), 1);
                    base_service->msg_pos = read(base_service->in_pipe_read_fd,
                                                    RDBD_RAW_MSG(base_service->msg), 
                                                    sizeof(struct rdbd_header));//read header
                    if(base_service->msg_pos < 0)
                    {
                        base_service->msg_pos = 0;
                    }
                    if(base_service->msg->header.msg_len == 0)
                    {
                        //drop
                        free(base_service->msg);
                        base_service->msg = RT_NULL;
                    }
                }
                else if(base_service->msg_pos < sizeof(struct rdbd_header))//continue read header
                {
                    result = read(base_service->in_pipe_read_fd,
                                    RDBD_RAW_MSG(base_service->msg) + base_service->msg_pos, 
                                    sizeof(struct rdbd_header) - base_service->msg_pos);//read header
                    if(result > 0)
                    {
                        base_service->msg_pos += result;
                    }
                }
                else if(base_service->msg_pos < (base_service->msg->header.msg_len + sizeof(struct rdbd_header)))//read body
                {
                    if(base_service->msg_pos == sizeof(struct rdbd_header))
                    {
                        base_service->msg = rt_realloc(base_service->msg, (base_service->msg->header.msg_len + sizeof(struct rdbd_header)));//realloc
                    }
                    result = read(base_service->in_pipe_read_fd,
                                    RDBD_RAW_MSG(base_service->msg) + base_service->msg_pos, 
                                    (base_service->msg->header.msg_len + sizeof(struct rdbd_header)) - base_service->msg_pos);//read body
                    if(result > 0)
                    {
                        base_service->msg_pos += result;
                    }
                    if(base_service->msg_pos == (base_service->msg->header.msg_len + sizeof(struct rdbd_header)))//read done
                    {
                        service = rdbd_service_get(base_service->rdbd, base_service->msg->header.source);
                        if(RT_NULL == service || service->status != RDBD_SERVICE_STATUS_RUNNING)
                        {
                            //drop
                            LOG_W("Not found service %d or service not running", base_service->msg->header.source);
                        }
                        else if(service == base_service)
                        {
                            rdbd_service_base_response(base_service, (char *)base_service->msg->msg, base_service->msg->header.msg_len, &response, &response_size);
                            free(base_service->msg);
                            base_service->msg = RT_NULL;
                            if(response != RT_NULL)
                            {
                                base_service->msg = rt_malloc(response_size + sizeof(struct rdbd_header));
                                base_service->msg->header.source = RDBD_SERVICE_ID_CONTROL;
                                base_service->msg->header.msg_len = response_size;
                                rt_memcpy(base_service->msg->msg, response, response_size);
                                free(response);
                                response = RT_NULL;
                                response_size = 0;
                                rdbd_service_request_write(base_service, base_service->msg);
                                base_service->msg = RT_NULL;
                            }
                            
                        }
                        else
                        {
                            rdbd_service_request_write(service, base_service->msg);
                            base_service->msg = RT_NULL;//restart receive
                        }
                    }
                }
            }

            if(!rt_list_isempty(&base_service->request_write_list))//write data to pc
            {
                if(FD_ISSET(base_service->out_pipe_write_fd, &writeset))
                {
                    request = rt_list_entry(base_service->request_write_list.next, struct rdbd_request_write, list);
                    result = write(base_service->out_pipe_write_fd,
                                                RDBD_RAW_MSG(request->msg) + request->msg_pos, RDBD_MSG_LEN(request->msg) - request->msg_pos);
                    if(result > 0)
                    {
                        request->msg_pos += result;
                    }
                    if(request->msg_pos == RDBD_MSG_LEN(request->msg))
                    {
                        rdbd_service_request_delete(request);
                    }
                }
            }

            for(node = base_service->rdbd->service_list.next; 
                node != &base_service->rdbd->service_list; 
                node = node->next)// poll all service
            {
                service = rt_list_entry(node, struct rdbd_service, list);
                if(service == base_service || service->status != RDBD_SERVICE_STATUS_RUNNING)
                {
                    continue;
                }
                if(service->flag & RDBD_SERVICE_FLAG_WR)
                {
                    if(FD_ISSET(service->out_pipe_read_fd, &readset))
                    {
                        service->msg_pos = 0;
                        service->msg = rt_malloc(RDB_RINGBUFF_SIZE + sizeof(struct rdbd_header));
                        service->msg->header.source = service->service_id;
                        service->msg->header.msg_len = read(service->out_pipe_read_fd, service->msg->msg, RDB_RINGBUFF_SIZE);
                        if(service->msg->header.msg_len <= 0)
                        {
                            free(service->msg);
                        }
                        else
                        {
                            service->msg = rt_realloc(service->msg, RDBD_MSG_LEN(service->msg));
                            rdbd_service_request_write(base_service, service->msg);
                        }
                        service->msg = RT_NULL;
                    }
                }
                if((service->flag & RDBD_SERVICE_FLAG_RD) && !rt_list_isempty(&service->request_write_list))
                {
                    if(FD_ISSET(service->in_pipe_write_fd, &writeset))
                    {
                        request = rt_list_entry(service->request_write_list.next, struct rdbd_request_write, list);
                        if(request->msg_pos == 0)
                        {
                            request->msg_pos = sizeof(struct rdbd_header);
                        }
                        result = write(service->in_pipe_write_fd,
                                                    RDBD_RAW_MSG(request->msg) + request->msg_pos, RDBD_MSG_LEN(request->msg) - request->msg_pos);
                        if(result > 0)
                        {
                            request->msg_pos += result;
                        }
                        if(request->msg_pos == RDBD_MSG_LEN(request->msg))
                        {
                            rdbd_service_request_delete(request);//write done
                        }
                    }
                }
            }
        }
        else
        {
            RT_ASSERT(0);
        }
    }
}

static char transfer_read_buff[RDB_RINGBUFF_SIZE];
static char transfer_write_buff[RDB_RINGBUFF_SIZE];
struct rt_completion transfer_write_completion;

static void transfer_read_callback(void * context, int size)
{
    struct rdbd_service * base_service= (struct rdbd_service *)context;
    fd_set writeset;
    int res = 0;
    int max_fd = base_service->in_pipe_write_fd + 1;
    int write_len = 0;
    char * pwrite = transfer_read_buff;
    rt_list_t * node;
    if(size < 0)
    {
        base_service->rdbd->status = RDBD_STATUS_DISCONNECTED;
        rdbd_service_control(base_service, RDBD_SERVICE_SUSPEND, base_service);
        return;
    }
    while(size)
    {
        FD_ZERO(&writeset);
        FD_SET(base_service->in_pipe_write_fd, &writeset);
        res = select(max_fd, RT_NULL, &writeset, RT_NULL, RT_NULL);
        if(res < 0)
        {
            RT_ASSERT(0);
        }
        else if(res > 0)
        {
            write_len = write(base_service->in_pipe_write_fd, pwrite, size);
            LOG_I("Read size:%d",write_len);
            if(write_len > 0)
            {
                size -= write_len;
                pwrite += write_len;
            }
        }
    }
    if(base_service->rdbd->private_transfer_ops->read(transfer_read_buff, sizeof(transfer_read_buff), transfer_read_callback, base_service) < 0)
    {
        base_service->rdbd->status = RDBD_STATUS_DISCONNECTED;
        
        for(node = base_service->rdbd->service_list.next; 
            node != &base_service->rdbd->service_list; 
            node = node->next)// poll all service
        {
            rdbd_service_control(rt_list_entry(node, struct rdbd_service, list), RDBD_SERVICE_SUSPEND, rt_list_entry(node, struct rdbd_service, list));
        }
    }
}

static void transfer_write_callback(void * context, int size)
{
    LOG_I("Write size:%d", size);
    rt_completion_done(&transfer_write_completion);
}

static void transfer_thread_entry(void * arg)
{
    struct rdbd_service * base_service= (struct rdbd_service *)arg;
    fd_set readset;
    int res = 0;
    int max_fd = base_service->out_pipe_read_fd + 1;
    int write_size = 0;
    struct timeval timeout;
    rt_list_t * node;
    while(1)
    {
        if(base_service->rdbd->status == RDBD_STATUS_DISCONNECTED)
        {
            if(base_service->rdbd->private_transfer_ops->read(transfer_read_buff, sizeof(transfer_read_buff), transfer_read_callback, base_service) >= 0)
            {
                base_service->rdbd->status = RDBD_STATUS_CONNECTED;
                for(node = base_service->rdbd->service_list.next; 
                    node != &base_service->rdbd->service_list; 
                    node = node->next)// poll all service
                {
                    rdbd_service_control(rt_list_entry(node, struct rdbd_service, list), RDBD_SERVICE_RESUME, rt_list_entry(node, struct rdbd_service, list));
                }
                continue;
            }
            rt_thread_delay(500);
        }
        else
        {
            FD_ZERO(&readset);
            FD_SET(base_service->out_pipe_read_fd, &readset);
            timeout.tv_sec=0;
            timeout.tv_usec=500000; //timeout 500ms
            res = select(max_fd, &readset, RT_NULL, RT_NULL, &timeout);
            if(res < 0)
            {
                RT_ASSERT(0);
            }
            else if(res > 0)
            {
                if(FD_ISSET(base_service->out_pipe_read_fd, &readset))
                {
                    write_size = read(base_service->out_pipe_read_fd, transfer_write_buff, sizeof(transfer_write_buff));
                    if(write_size > 0)
                    {
                        rt_completion_init(&transfer_write_completion);
                        if(base_service->rdbd->private_transfer_ops->write(transfer_write_buff, write_size, transfer_write_callback, RT_NULL) >= 0)
                        {
                            rt_completion_wait(&transfer_write_completion, RT_WAITING_FOREVER);
                        }
                        else
                        {
                            rt_completion_wait(&transfer_write_completion, 1);
                            base_service->rdbd->status = RDBD_STATUS_DISCONNECTED;
                            for(node = base_service->rdbd->service_list.next; 
                                node != &base_service->rdbd->service_list; 
                                node = node->next)// poll all service
                            {
                                rdbd_service_control(rt_list_entry(node, struct rdbd_service, list), RDBD_SERVICE_SUSPEND, rt_list_entry(node, struct rdbd_service, list));
                            }
                        }
                    }
                }
            }
        }
    }
}

static int start(void * args)
{
    struct rdbd_service * base_service = (struct rdbd_service *)args;
    rt_thread_t tid = RT_NULL;

    if(RT_NULL == base_service)
    {
         LOG_E("Start up service error, args is null");
         return -1;
    }
    
    if(base_service->flag & RDBD_SERVICE_FLAG_RD)
    {
        base_service->in_pipe_read_fd = open(base_service->in_pipe_path, O_RDONLY | O_NONBLOCK, 0);
        if(base_service->in_pipe_read_fd < 0)
        {
            LOG_E("Start up service %s error open in pipe failed",base_service->name);
            return -1;
        }
    }

    if(base_service->flag & RDBD_SERVICE_FLAG_WR)
    {
        base_service->out_pipe_write_fd = open(base_service->out_pipe_path, O_WRONLY | O_NONBLOCK, 0);
        if(base_service->out_pipe_write_fd < 0)
        {
            LOG_E("Start up service %s error open out pipe failed",base_service->name);
            goto _error;
        }
    }
    
    
    base_service->service_thread = rt_thread_create(base_service->name,
                                                    base_service_thread_entry,
                                                    base_service,
                                                    1024,
                                                    23,
                                                    20);
    if(RT_NULL != base_service->service_thread)
    {
        if(RT_EOK != rt_thread_startup(base_service->service_thread))
        {
            rt_thread_delete(base_service->service_thread);
            base_service->service_thread = RT_NULL;
            goto _error;
        }
    }
    else
    {
        goto _error;
    }
    
    if(base_service->flag & RDBD_SERVICE_FLAG_RD)
    {
        base_service->in_pipe_write_fd = open(base_service->in_pipe_path, O_WRONLY | O_NONBLOCK, 0);
        if(base_service->in_pipe_write_fd < 0)
        {
            LOG_E("Start up transfer error open in pipe failed",base_service->name);
            return -1;
        }
    }

    if(base_service->flag & RDBD_SERVICE_FLAG_WR)
    {
        base_service->out_pipe_read_fd = open(base_service->out_pipe_path, O_RDONLY | O_NONBLOCK, 0);
        if(base_service->out_pipe_read_fd < 0)
        {
            LOG_E("Start up transfer error open out pipe failed",base_service->name);
            goto _error;
        }
    }
    
    tid = rt_thread_create("rdbt",
                            transfer_thread_entry,
                            base_service,
                            1024,
                            22,
                            20);
    if(RT_NULL != tid)
    {
        if(RT_EOK != rt_thread_startup(tid))
        {
            rt_thread_delete(tid);
            tid = RT_NULL;
            goto _error;
        }
        base_service->user_data = tid;
        return 0;
    }
_error:
    if(base_service->in_pipe_read_fd >= 0)
    {
        close(base_service->in_pipe_read_fd);
        base_service->in_pipe_read_fd = -1;
    }
    if(base_service->in_pipe_write_fd >= 0)
    {
        close(base_service->in_pipe_write_fd);
        base_service->in_pipe_write_fd = -1;
    }
    
    if(base_service->out_pipe_read_fd >= 0)
    {
        close(base_service->out_pipe_read_fd);
        base_service->out_pipe_read_fd = -1;
    }
    if(base_service->out_pipe_write_fd >= 0)
    {
        close(base_service->out_pipe_write_fd);
        base_service->out_pipe_write_fd = -1;
    }
    base_service->user_data = RT_NULL;
    return -1;
}

static int stop(void * args)
{
    struct rdbd_service * base_service = (struct rdbd_service *)args;
    rt_thread_t tid;
    if(RT_NULL == base_service)
    {
         LOG_E("Stop service error, args is null");
         return -1;
    }
    
    tid = (rt_thread_t)base_service->user_data;
    if(RT_EOK != rt_thread_delete(tid))
    {
        LOG_E("Delete thread %s failed", tid->name);
        return -1;
    }
    tid = RT_NULL;
    
    if(RT_EOK != rt_thread_delete(base_service->service_thread))
    {
        LOG_E("Delete thread %s failed", base_service->service_thread->name);
        return -1;
    }
    base_service->service_thread = RT_NULL;
    
    if(base_service->in_pipe_read_fd >= 0)
    {
        if(close(base_service->in_pipe_read_fd) < 0)
        {
            LOG_E("Close fd %d failed", base_service->in_pipe_read_fd);
            return -1;
        }
        base_service->in_pipe_read_fd = -1;
    }
    
    if(base_service->in_pipe_write_fd >= 0)
    {
        if(close(base_service->in_pipe_write_fd) < 0)
        {
            LOG_E("Close fd %d failed", base_service->in_pipe_write_fd);
            return -1;
        }
        base_service->in_pipe_write_fd = -1;
    }
    
    if(base_service->in_pipe_write_fd >= 0)
    {
        if(close(base_service->in_pipe_write_fd) < 0)
        {
            LOG_E("Close fd %d failed", base_service->in_pipe_write_fd);
            return -1;
        }
        base_service->in_pipe_write_fd = -1;
    }
    
    if(base_service->out_pipe_write_fd >= 0)
    {
        if(close(base_service->out_pipe_write_fd) < 0)
        {
            LOG_E("Close fd %d failed", base_service->out_pipe_write_fd);
            return -1;
        }
        base_service->out_pipe_write_fd = -1;
    }
    
    base_service->user_data = RT_NULL;
    return 0;
}

static int resume(void * args)
{
    //Not need
    return 0;
}
static int suspend(void * args)
{
    //Not need
    return 0;
}
#define RDBD_SERVICE_BASE_CMD_REQUEST_SERVICE_LIST 0x00

static int rdbd_service_base_response(struct rdbd_service * base_service, char * request, int request_size, char ** response, int * response_size)
{
    int service_list_len = 0;
    rt_list_t * node = RT_NULL;
    *response = RT_NULL;
    *response_size = 0;
    int i = 0;
    hex_dump("request", request, request_size);
    switch(request[0])
    {
    case RDBD_SERVICE_BASE_CMD_REQUEST_SERVICE_LIST:
        service_list_len = rt_list_len(&base_service->rdbd->service_list) - 1;//skip base service
        *response = rt_malloc(service_list_len + 2);
        *response_size = service_list_len + 2;
        (*response)[i++] = 0;
        (*response)[i++] = service_list_len;
        for(node = base_service->rdbd->service_list.next; 
                node != &base_service->rdbd->service_list; 
                node = node->next)// poll all service
        {
            if(rt_list_entry(node, struct rdbd_service, list) != base_service)//skip base service
            {
                (*response)[i++] = rt_list_entry(node, struct rdbd_service, list)->service_id;
            }
        }
        break;
    default:
        break;
    }
    return 0;
}
