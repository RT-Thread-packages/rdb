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

#include <rdbd_service_manager.h>
#include <string.h>

#undef DBG_ENABLE
#define DBG_SECTION_NAME  "RDBD Service Manager"
#define DBG_LEVEL         DBG_LOG
#define DBG_COLOR
#include <rtdbg.h>


int rdbd_service_control(struct rdbd_service * service, int cmd, void * args)
{
    int result = 0;
    if(RT_NULL == service)
    {
        LOG_E("Null can not be control");
        return -1;
    }
    if(RT_NULL == service->rdbd)
    {
        LOG_E("Service %s not be install", service->name);
        return -1;
    }
    switch(cmd)
    {
    case RDBD_SERVICE_START:
        LOG_I("Service %s start", service->name);
        if(RDBD_SERVICE_STATUS_STOP != service->status)
        {
            LOG_W("Service %s is allready start", service->name);
            result = -1;
            break;
        }
        result = service->control_ops->start(args);
        if(0 == result)
        {
            if(rdbd_get_status(service->rdbd) == RDBD_STATUS_CONNECTED)
            {
                result = service->control_ops->resume(RT_NULL);
                if(0 == result)
                {
                    service->status = RDBD_SERVICE_STATUS_RUNNING;
                    LOG_I("Service %s is running", service->name);
                }
                else
                {
                    service->control_ops->stop(RT_NULL);//?
                    LOG_E("Service %s start failed!", service->name);
                }
            }
            else
            {
                service->status = RDBD_SERVICE_STATUS_SUSPENDED;
                LOG_I("Service %s is suspended", service->name);
            }
        }
        break;
    case RDBD_SERVICE_STOP:
        if(RDBD_SERVICE_STATUS_STOP == service->status)
        {
            LOG_W("Service %s is not start", service->name);
            result = -1;
            break;
        }
        result = service->control_ops->stop(args);
        if(0 == result)
        {
            LOG_I("Service %s stop", service->name);
            service->status = RDBD_SERVICE_STATUS_STOP;
        }
        else
        {
            LOG_E("Service %s stop error!", service->name);
        }
        break;
    case RDBD_SERVICE_SUSPEND:
        if(RDBD_SERVICE_STATUS_STOP == service->status)
        {
            LOG_W("Service %s is not start", service->name);
            result = -1;
            break;
        }
        if(RDBD_SERVICE_STATUS_SUSPENDED == service->status)
        {
            LOG_W("Service %s is allready suspended", service->name);
            result = 0;
            break;
        }
        result = service->control_ops->suspend(args);
        if(0 == result)
        {
            LOG_I("Service %s suspended", service->name);
            service->status = RDBD_SERVICE_STATUS_SUSPENDED;
        }
        else
        {
            LOG_E("Service %s stop error!", service->name);
        }
        break;
    case RDBD_SERVICE_RESUME:
        if(RDBD_SERVICE_STATUS_STOP == service->status)
        {
            LOG_W("Service %s is not start", service->name);
            result = -1;
            break;
        }
        if(RDBD_SERVICE_STATUS_RUNNING == service->status)
        {
            LOG_W("Service %s is allready running", service->name);
            result = 0;
            break;
        }
        result = service->control_ops->resume(args);
        if(0 == result)
        {
            LOG_I("Service %s running", service->name);
            service->status = RDBD_SERVICE_STATUS_RUNNING;
        }
        else
        {
            LOG_E("Service %s resume error!", service->name);
        }
        break;
    case RDBD_SERVICE_GET_STATUS:
        if(RT_NULL == args)
        {
            LOG_E("Service %s get status args invalid", service->name);
            result = -1;
            break;
        }
        *((int *)args) = service->status;
        break;
    default:
        LOG_W("Service %s control cmd invalid", service->name);
        break;
    }
    return result;
}

int rdbd_service_install(rdbd_t rdbd, struct rdbd_service * service)
{
    if(RT_NULL == service)
    {
        LOG_E("Service install failed service is null");
        return -1;
    }
    if(RT_NULL == rdbd)
    {
        LOG_E("Service %s install failed rdbd is null", service->name);
        return -1;
    }
    if(RT_NULL != service->rdbd)
    {
        LOG_E("Service %s allready be installed to %s", service->name, service->rdbd->name);
        return -1;
    }
    rt_list_insert_before(&rdbd->service_list, &service->list);
    service->rdbd = rdbd;
    LOG_I("Service %s installed to %s", service->name, service->rdbd->name);
    return 0;
}

int rdbd_service_uninstall(struct rdbd_service * service)
{
    if(RT_NULL == service)
    {
        LOG_E("Uninstall failed service is null");
        return -1;
    }
    if(RT_NULL == service->rdbd)
    {
        LOG_E("Service %s is nor be installed", service->name);
        return -1;
    }
    rt_list_remove(&service->list);
    service->rdbd = RT_NULL;
    return 0;
}

struct rdbd_service * rdbd_service_find(rdbd_t rdbd, const char * name)
{
    rt_list_t * l;
    struct rdbd_service * service;
    for(l = rdbd->service_list.next;
        l != &rdbd->service_list; 
        l = l->next)
    {
        service = rt_list_entry(l, struct rdbd_service, list);
        if(0 == strcmp(service->name, name))
        {
            return service;
        }
    }
    return RT_NULL;
}

struct rdbd_service * rdbd_service_get(rdbd_t rdbd, rt_uint8_t service_id)
{
    rt_list_t * l;
    struct rdbd_service * service;
    for(l = rdbd->service_list.next;
        l != &rdbd->service_list; 
        l = l->next)
    {
        service = rt_list_entry(l, struct rdbd_service, list);
        if(service_id == service->service_id)
        {
            return service;
        }
    }
    return RT_NULL;
}

