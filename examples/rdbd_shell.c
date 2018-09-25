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

#include "rtthread.h"
#include "rdbd.h"
#include "rdbd_service.h"
#include "rdbd_service_manager.h"
#include "rthw.h"
#include <fcntl.h>
#include <sys/time.h>
#include <dfs_select.h>
#include <libc.h>
#include <dfs_posix.h>
#define PKGS_USING_USB_RDBD
#undef DBG_ENABLE
#define DBG_SECTION_NAME  "RDBD Shell"
#define DBG_LEVEL         DBG_WARNING
#define DBG_COLOR
#include <rtdbg.h>


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

extern rt_uint32_t finsh_get_echo(void);
extern void finsh_set_echo(rt_uint32_t echo);
char drop_buffer[128];
static rt_err_t rt_shell_service_device_init(struct rt_device *dev)
{
    ((void)dev);
    return RT_EOK;
}

static rt_err_t rt_shell_service_device_open(struct rt_device *dev, rt_uint16_t oflag)
{
    dev->open_flag = oflag & 0xff;
    return RT_EOK;
}

static rt_err_t rt_shell_service_device_close(struct rt_device *dev)
{
    ((void)dev);
    return RT_EOK;
}

static rt_size_t rt_shell_service_device_read(rt_device_t dev, rt_off_t pos, void* buffer, rt_size_t size)
{
    struct rdbd_service * shell_service = (struct rdbd_service *)dev->user_data;
    fd_set readset;
    int res = 0;
    int max_fd = shell_service->in_pipe_read_fd + 1;
    //if(RDBD_SERVICE_STATUS_RUNNING == shell_service->status)
    {
        FD_ZERO(&readset);
        FD_SET(shell_service->in_pipe_read_fd, &readset);
        if(finsh_get_echo())
        {
            finsh_set_echo(0);
        }
        res = select(max_fd, &readset, RT_NULL, RT_NULL, RT_NULL);
        if(res > 0)
        {
            if(FD_ISSET(shell_service->in_pipe_read_fd, &readset))
            {
                return read(shell_service->in_pipe_read_fd, buffer, size);
            }
        }
    }
    return 0;
}


static rt_size_t rt_shell_service_device_write(rt_device_t dev, rt_off_t pos, const void* buffer, rt_size_t size)
{
    struct rdbd_service * shell_service = (struct rdbd_service *)dev->user_data;
    fd_set writeset;
    int res = 0;
    int max_fd = shell_service->out_pipe_write_fd + 1;
    char * returnBuffer = RT_NULL;
    int written_size = 0;
    int result = size;
    int n_size = 0;
    int i;
    for(i = 0; i < size; i++)
    {
        if(((char *)buffer)[i] == '\n')
        {
            n_size ++;
        }
    }
    returnBuffer = rt_malloc(size + n_size);
    for(i = 0; i < size; i++)
    {
        if(((char *)buffer)[i] == '\n')
        {
            returnBuffer[written_size] = '\r';
            written_size++;
        }
        returnBuffer[written_size] = ((char *)buffer)[i];
        written_size++;
    }
    size = written_size;
    written_size = 0;
    while(size)
    {
        if(RDBD_SERVICE_STATUS_RUNNING == shell_service->status)
        {
            FD_ZERO(&writeset);
            FD_SET(shell_service->out_pipe_write_fd, &writeset);
            res = select(max_fd, RT_NULL, &writeset, RT_NULL, RT_NULL);
            if(res > 0)
            {
                if(FD_ISSET(shell_service->out_pipe_write_fd, &writeset))
                {
                    written_size = write(shell_service->out_pipe_write_fd, returnBuffer + written_size, size);
                    if(written_size > 0)
                    {
                        size -= written_size;
                    }
                }
            }
        }
        else
        {
            break;
        }
    }
    free(returnBuffer);
    return result;
}

#ifdef RT_USING_DEVICE_OPS
const static struct rt_device_ops shell_ops = 
{
    rt_shell_service_device_init,
    rt_shell_service_device_open,
    rt_shell_service_device_close,
    rt_shell_service_device_read,
    rt_shell_service_device_write,
    RT_NULL
};
#endif

static int start(void * args)
{
    struct rdbd_service * shell_service = (struct rdbd_service *)args;

    if(RT_NULL == shell_service)
    {
         LOG_E("Start up service error, args is null");
         return -1;
    }
    
    if(shell_service->flag & RDBD_SERVICE_FLAG_RD)
    {
        shell_service->in_pipe_read_fd = open(shell_service->in_pipe_path, O_RDONLY | O_NONBLOCK, 0);
        if(shell_service->in_pipe_read_fd < 0)
        {
            LOG_E("Start up service %s error open in pipe failed",shell_service->name);
            return -1;
        }
    }

    if(shell_service->flag & RDBD_SERVICE_FLAG_WR)
    {
        shell_service->out_pipe_write_fd = open(shell_service->out_pipe_path, O_WRONLY | O_NONBLOCK, 0);
        if(shell_service->out_pipe_write_fd < 0)
        {
            LOG_E("Start up service %s error open out pipe failed",shell_service->name);
            goto _error;
        }
    }
    
    
    if(shell_service->flag & RDBD_SERVICE_FLAG_RD)
    {
        shell_service->in_pipe_write_fd = open(shell_service->in_pipe_path, O_WRONLY | O_NONBLOCK, 0);
        if(shell_service->in_pipe_write_fd < 0)
        {
            LOG_E("Start up transfer error open in pipe failed",shell_service->name);
            return -1;
        }
    }

    if(shell_service->flag & RDBD_SERVICE_FLAG_WR)
    {
        shell_service->out_pipe_read_fd = open(shell_service->out_pipe_path, O_RDONLY | O_NONBLOCK, 0);
        if(shell_service->out_pipe_read_fd < 0)
        {
            LOG_E("Start up transfer error open out pipe failed",shell_service->name);
            goto _error;
        }
    }
    return 0;
    
_error:
    if(shell_service->in_pipe_read_fd >= 0)
    {
        close(shell_service->in_pipe_read_fd);
        shell_service->in_pipe_read_fd = -1;
    }
    if(shell_service->in_pipe_write_fd >= 0)
    {
        close(shell_service->in_pipe_write_fd);
        shell_service->in_pipe_write_fd = -1;
    }
    
    if(shell_service->out_pipe_read_fd >= 0)
    {
        close(shell_service->out_pipe_read_fd);
        shell_service->out_pipe_read_fd = -1;
    }
    if(shell_service->out_pipe_write_fd >= 0)
    {
        close(shell_service->out_pipe_write_fd);
        shell_service->out_pipe_write_fd = -1;
    }
    return -1;
}

static int stop(void * args)
{
    struct rdbd_service * shell_service = (struct rdbd_service *)args;
    
    if(RT_NULL == shell_service)
    {
         LOG_E("Stop service error, args is null");
         return -1;
    }
    
    if(shell_service->in_pipe_read_fd >= 0)
    {
        if(close(shell_service->in_pipe_read_fd) < 0)
        {
            LOG_E("Close fd %d failed", shell_service->in_pipe_read_fd);
            return -1;
        }
        shell_service->in_pipe_read_fd = -1;
    }
    
    if(shell_service->in_pipe_write_fd >= 0)
    {
        if(close(shell_service->in_pipe_write_fd) < 0)
        {
            LOG_E("Close fd %d failed", shell_service->in_pipe_write_fd);
            return -1;
        }
        shell_service->in_pipe_write_fd = -1;
    }
    
    if(shell_service->in_pipe_write_fd >= 0)
    {
        if(close(shell_service->in_pipe_write_fd) < 0)
        {
            LOG_E("Close fd %d failed", shell_service->in_pipe_write_fd);
            return -1;
        }
        shell_service->in_pipe_write_fd = -1;
    }
    
    if(shell_service->out_pipe_write_fd >= 0)
    {
        if(close(shell_service->out_pipe_write_fd) < 0)
        {
            LOG_E("Close fd %d failed", shell_service->out_pipe_write_fd);
            return -1;
        }
        shell_service->out_pipe_write_fd = -1;
    }
    
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


int rdbd_shell_service_init(void)
{
    struct rdbd_service * shell_service;
    rt_device_t shell_device = rt_calloc(sizeof(struct rt_thread), 1);
    rdbd_t usbrdbd = rdbd_find("usb");
    if(RT_NULL == usbrdbd)
    {
        LOG_E("rdbd usb find error");
        return -1;
    }

    shell_service = rdbd_create_service(RDBD_SERVICE_ID_SHELL, "shell", &control_ops, RT_NULL, "shellin", 128,"shellout", 128, RDBD_SERVICE_FLAG_WR|RDBD_SERVICE_FLAG_RD);
    if(RT_NULL == shell_service)
    {
        LOG_E("shell_service create error");
        goto _error;
    }
    LOG_I("Service %s created :", shell_service->name);
    LOG_I("in_pipe_path %s", shell_service->in_pipe_path);
    LOG_I("out_pipe_path %s", shell_service->out_pipe_path);
    LOG_I("service_id %d", shell_service->service_id);
    LOG_I("status %d", shell_service->status);
    shell_device->type       = RT_Device_Class_Char;
#ifdef RT_USING_DEVICE_OPS
    device->ops         = &shell_ops;
#else
    shell_device->init       = rt_shell_service_device_init;
    shell_device->open       = rt_shell_service_device_open;
    shell_device->close      = rt_shell_service_device_close;
    shell_device->read       = rt_shell_service_device_read;
    shell_device->write      = rt_shell_service_device_write;
    shell_device->control    = RT_NULL;
#endif
    shell_device->user_data  = shell_service;
    shell_service->user_data = shell_device;
    rt_device_register(shell_device, "rdbdsh", RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    rdbd_service_install(usbrdbd, shell_service);
    
    rt_console_set_device("rdbdsh");

    // /* add non-block flag */
    // ioctl(libc_stdio_get_console(), F_SETFL, (void *) (dev_old_flag | O_NONBLOCK));
    /* set tcp shell device for console */
    libc_stdio_set_console("rdbdsh", O_RDWR);
    rdbd_service_control(shell_service, RDBD_SERVICE_START, shell_service);
    
    return 0;

_error:
    if(RT_NULL != usbrdbd)
    {
        //TO DO
    }
    return -1;
}
INIT_APP_EXPORT(rdbd_shell_service_init);
