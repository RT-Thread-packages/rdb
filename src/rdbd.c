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
#include <rtthread.h>
#include <string.h>
#include <rdbd_service_manager.h>
#undef DBG_ENABLE
#define DBG_SECTION_NAME  "RDBD"
#define DBG_LEVEL         DBG_LOG
#define DBG_COLOR
#include <rtdbg.h>
static rt_list_t rdbd_list;

int rdbd_init(void)
{
    rt_memset(&rdbd_list, 0, sizeof(rdbd_list));
    rt_list_init(&rdbd_list);
    return 0;
}
INIT_PREV_EXPORT(rdbd_init);

rdbd_t rdbd_create(const char * rdbd_name)
{
    rdbd_t rdbd = rt_calloc(sizeof(struct rdbd),1);
    if(RT_NULL == rdbd)
    {
        LOG_E("rdbd %s create failed,no memory", rdbd_name);
        return RT_NULL;
    } 
    rdbd->name = rt_strdup(rdbd_name);
    if(RT_NULL == rdbd->name)
    {
        LOG_E("rdbd %s create failed,no memory", rdbd_name);
        goto _error;
    }
    rdbd->status = RDBD_STATUS_DISCONNECTED;
    rt_list_init(&rdbd->service_list);
    rt_list_insert_before(&rdbd_list, &rdbd->list);
    LOG_I("Create rdbd %s", rdbd->name);
    return rdbd;

_error:
    if(RT_NULL != rdbd)
    {
        if(RT_NULL != rdbd->name)
        {
            rt_free(rdbd->name);
            rdbd->name = RT_NULL;
        }
        rt_free(rdbd);
        rdbd = RT_NULL;
    }
    return RT_NULL;
}

int rdbd_delete(rdbd_t rdbd)
{
    int result = 0;
    //STOP all and uninstall
    while(!rt_list_isempty(&rdbd->service_list))
    {
        result += rdbd_service_control(rt_list_entry(rdbd->service_list.next,struct rdbd_service, list), RDBD_SERVICE_STOP, rt_list_entry(rdbd->service_list.next,struct rdbd_service, list));
        result += rdbd_service_uninstall(rt_list_entry(rdbd->service_list.next,struct rdbd_service, list));
    }
    LOG_I("Delete rdb %s", rdbd->name);
    rt_free(rdbd->name);
    rdbd->name = RT_NULL;
    rt_free(rdbd);
    rdbd = RT_NULL;
    return result;
}

int rdbd_get_status(rdbd_t rdbd)
{
    return rdbd->status;
}

rdbd_t rdbd_find(const char * rdbd_name)
{
    rt_list_t * l;
    rdbd_t rdbd;
    for(l = rdbd_list.next;
        l != &rdbd_list; 
        l = l->next)
    {
        rdbd = rt_list_entry(l, struct rdbd, list);
        if(0 == strcmp(rdbd->name, rdbd_name))
        {
            return rdbd;
        }
    }
    return RT_NULL;
}

int rdbd_register_transfer_ops(rdbd_t rdbd, struct rdbd_transfer_ops * ops)
{
    if(RT_NULL == ops)
    {
        LOG_E("Register transfer ops error");
        return -1;
    }
    rdbd->private_transfer_ops = ops;
    return 0;
}

