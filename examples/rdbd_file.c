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
#include "rtdevice.h"
#include <stdio.h>
#include <string.h>
//#include <dirent.h>
#include <dstr.h>
#include <rdbd_service.h>
#include <rdbd_service_manager.h>
#include <fcntl.h>
#include <sys/time.h>
#include <libc.h>
#include <dfs_select.h>
#include <dfs_posix.h>
//select dfs
//select libc
//select dstr v0.2.0
#define RDBD_FILE_HASH_FNV_SEED           0x811C9DC5

#undef DBG_ENABLE
#define DBG_SECTION_NAME  "RDBD FILE"
#define DBG_LEVEL         DBG_LOG
#define DBG_COLOR
#include <rtdbg.h>

#define RDBD_FILE_CTRL_NEW_FILE         0x40
#define RDBD_FILE_CTRL_WRITE_FILE       0x41
#define RDBD_FILE_CTRL_WRITE_DONE       0x42
#define RDBD_FILE_CTRL_LIST_DIR         0x43
#define RDBD_FILE_CTRL_READ_FILE        0x44
#define RDBD_FILE_CTRL_RM_FILE          0x45
#define RDBD_FTLE_CTRL_GET_HASH         0x46
#define RDBD_FTLE_CTRL_DIR_SYNC_START   0x47
#define RDBD_FTLE_CTRL_DIR_SYNC_STOP    0x48
#define RDBD_FTLE_CTRL_CHECK_PATH       0x49

#define RDBD_FTLE_CTRL_PATH_IS_DIR       0x01
#define RDBD_FTLE_CTRL_PATH_IS_FILE      0x02
#define RDBD_FTLE_CTRL_PATH_NONE         0x00

#define RDBD_FILE_READ_BLOCK_SIZE   4096

#define RDBD_FILE_HEADER_SIZE       (sizeof(struct rdbd_file_control_msg_header))

#define RDBD_FILE_MSG_SIZE(x)       (RDBD_FILE_MSG_MSG(x)->header.pathlength + RDBD_FILE_MSG_MSG(x)->header.length + RDBD_FILE_HEADER_SIZE)
#define RDBD_FILE_MSG_RAW(x)        ((rt_uint8_t *)x)
#define RDBD_FILE_MSG_MSG(x)        ((rdbd_file_ctrl_msg_t)x)

struct rdbd_file_control_msg_header
{
    rt_uint16_t command;
    rt_uint16_t pathlength;
    rt_uint32_t offset;
    rt_uint32_t length;
};

struct rdbd_file_session
{
    rt_list_t list;
    char * path;
    int flag;
    int fd;
};

static struct rdbd_file_session * create_session(rt_list_t * session_list, char * path, int flag)
{
    struct rdbd_file_session * session;
    if(session_list )
    session = rt_calloc(sizeof(struct rdbd_file_session), 1);
    if(RT_NULL == session_list || RT_NULL == path)
    {
        LOG_E("Invalid args %s,%d", __FUNCTION__, __LINE__);
        return RT_NULL;
    }
    session->fd = -1;
    session->flag = flag;
    session->path = rt_strdup(path);
    if(RT_NULL == session->path)
    {
        LOG_E("No memory create session");
        goto _error;
    }
    session->fd = open(session->path, session->flag);
    if(session->fd < 0)
    {
        LOG_E("Open file %s error", session->path);
        goto _error;
    }
    rt_list_insert_before(session_list, &session->list);
    return session;
    
_error:
    if(session)
    {
        if(session->path)
        {
            rt_free(session->path);
        }
        if(session->fd >= 0)
        {
            close(session->fd);
        }
        free(session);
    }
    return RT_NULL;
}

static struct rdbd_file_session * find_session(rt_list_t * session_list, char * path)
{
    rt_list_t * node;
    if(RT_NULL == session_list || RT_NULL == path)
    {
        LOG_E("Invalid args %s,%d", __FUNCTION__, __LINE__);
        return RT_NULL;
    }
    
    for(node = session_list->next; 
        node != session_list; 
        node = node->next)
    {
        if(strcmp(path, rt_list_entry(node, struct rdbd_file_session, list)->path) == 0)
        {
            return rt_list_entry(node, struct rdbd_file_session, list);
        }
    }
    return RT_NULL;
}

static int delete_session(struct rdbd_file_session * session)
{
    if(RT_NULL == session)
    {
        LOG_E("Invalid args %s,%d", __FUNCTION__, __LINE__);
        return -1;
    }
    fsync(session->fd);
    close(session->fd);
    rt_free(session->path);
    rt_list_remove(&session->list);
    rt_free(session);
    return 0;
}

static int read_session(struct rdbd_file_session * session, off_t offset, rt_uint8_t * buffer, rt_size_t len)
{
    if(RT_NULL == session || RT_NULL == buffer)
    {
        LOG_E("Invalid args %s,%d", __FUNCTION__, __LINE__);
        return -1;
    }
    if((session->flag & 0xF) == O_WRONLY)
    {
        LOG_E("Invalid read %s,%d", __FUNCTION__, __LINE__);
        return -1;
    }
    LOG_I("seek to %d", offset);
    
    lseek(session->fd, offset, SEEK_SET);
    
    LOG_I("Read fd %d, 0x%08X %d", session->fd, buffer, len);
    return read(session->fd, buffer, len);
}

static int write_session(struct rdbd_file_session * session, off_t offset, const rt_uint8_t * buffer, rt_size_t len)
{
    if(RT_NULL == session || RT_NULL == buffer)
    {
        LOG_E("Invalid args %s,%d", __FUNCTION__, __LINE__);
        return -1;
    }
    if((session->flag & 0xF) == O_RDONLY)
    {
        LOG_E("Invalid write %s,%d", __FUNCTION__, __LINE__);
        return -1;
    }
    lseek(session->fd, offset, SEEK_SET);
    return write(session->fd, buffer, len);
}




struct rdbd_file_control_msg
{
    struct rdbd_file_control_msg_header header;
    rt_uint8_t msg[RT_UINT16_MAX];
};
typedef struct rdbd_file_control_msg * rdbd_file_ctrl_msg_t;

struct file_request_send
{
    rt_list_t list;
    char * msg;
    int msg_pos;
    int request_file_send;
};

static rt_uint32_t rdbd_file_calc_hash(const char * filename);
static rt_dstr_t * get_file_list(const char *pathname);
static int file_service_request_send(rt_list_t * header, struct rdbd_file_control_msg * msg, int request_file_send);
static int file_service_delete_request(struct file_request_send * request);
static void file_service_thread_entry(void * arg)
{
    struct rdbd_service * file_service= (struct rdbd_service *)arg;
    fd_set readset, writeset;
    int result = 0;
    int res = 0;
    int max_fd = file_service->in_pipe_read_fd + 1;
    rt_list_t request_write_list;
    rt_list_t session_list;
    struct file_request_send * request;
    rdbd_file_ctrl_msg_t msg = RT_NULL;
    int msg_pos = 0;
    rt_dstr_t * file_list;
    char * path;
    struct rdbd_file_session * session = RT_NULL;
    rt_list_init(&request_write_list);
    rt_list_init(&session_list);
    
    while(1)
    {
        FD_ZERO(&readset);
        FD_ZERO(&writeset);
        FD_SET(file_service->in_pipe_read_fd, &readset);
        if(!rt_list_isempty(&request_write_list)) //need write data to pc
        {
            FD_SET(file_service->out_pipe_write_fd, &writeset);
            if(file_service->out_pipe_write_fd > (max_fd - 1))
            {
                max_fd = file_service->out_pipe_write_fd + 1;
            }
        }
        res = select(max_fd, &readset, &writeset, RT_NULL, RT_NULL);
        if(res > 0)
        {
            if(FD_ISSET(file_service->in_pipe_read_fd, &readset))
            {
                if(msg == RT_NULL)
                {
                    msg = rt_calloc(RDBD_FILE_HEADER_SIZE, 1);
                    msg_pos = read(file_service->in_pipe_read_fd, RDBD_FILE_MSG_RAW(msg), RDBD_FILE_HEADER_SIZE);
                    if(msg_pos < 0)
                    {
                        msg_pos = 0;
                    }
                }
                else if(msg_pos < RDBD_FILE_HEADER_SIZE)//continue read header
                {
                    result = read(file_service->in_pipe_read_fd,
                                                    RDBD_FILE_MSG_RAW(msg) + msg_pos, 
                                                    RDBD_FILE_HEADER_SIZE - msg_pos);//read header
                    if(result > 0)
                    {
                        msg_pos += result;
                    }
                }
                else if(msg_pos < (RDBD_FILE_MSG_SIZE(msg)))//read body
                {
                    if(msg_pos == RDBD_FILE_HEADER_SIZE)
                    {
                        msg = rt_realloc(msg, RDBD_FILE_MSG_SIZE(msg));//realloc
                    }
                    result = read(file_service->in_pipe_read_fd,
                                                    RDBD_FILE_MSG_RAW(msg) + msg_pos, 
                                                    RDBD_FILE_MSG_SIZE(msg) - msg_pos);//read body
                    if(result > 0)
                    {
                        msg_pos += result;
                    }
                    if(msg_pos == RDBD_FILE_MSG_SIZE(msg))//read done
                    {
                        path = rt_calloc(1,msg->header.pathlength + 1);
                        if(RT_NULL == path)
                        {
                            LOG_E("no memory");
                            RT_ASSERT(0);
                        }
                        rt_strncpy(path, (const char *)msg->msg, msg->header.pathlength);
                        LOG_D("path: %s", path);
                        path[msg->header.pathlength] = '\0';
                        if(RDBD_FILE_CTRL_NEW_FILE == msg->header.command || RDBD_FILE_CTRL_WRITE_FILE == msg->header.command)//Done
                        {
                            while (1) //replace '\\' to '/'
                            {
                                char *p;
                                p = strchr(path, '\\');
                                if (p)
                                {
                                    *p = '/';
                                }
                                else
                                {
                                    break;
                                }
                            }
                            {
                                char *begin, *end;
                                begin = path;
                                char *_path;
                                while (1)
                                {
                                    end = strchr(begin, '/'); //find '\\' since begin
                                    if (end == NULL) //if not find '\\' finsh
                                    {
                                        break;
                                    }
                                    if (begin == end) //if begin is '\\'
                                    {
                                        begin += 1;
                                        continue;
                                    }
                                    _path = rt_malloc(end - path + 1);
                                    if (_path == RT_NULL) //malloc fail
                                    {
                                        break;//???
                                    }
                                    strncpy(_path, path, end - path); //copy path
                                    _path[end - path] = '\0';
                                    {
                                        if (access(_path, 0) != 0) //dir not exist
                                        {
                                            if (mkdir(_path, 0777) != 0) //create fail
                                            {
                                                LOG_E("mkdir %s failed", _path);
                                                free(_path);
                                                _path = RT_NULL;
                                                break;
                                            }
                                        }
                                    }
                                    free(_path);
                                    _path = RT_NULL;
                                    begin = end + 1; //next
                                }
                            }
                            
                            switch(msg->header.command)
                            {
                            case RDBD_FILE_CTRL_NEW_FILE:
                                session = create_session(&session_list, path, O_CREAT|O_TRUNC|O_WRONLY);
                                if(session == RT_NULL)
                                {
                                    LOG_E("Create file error %s", path);
                                    RT_ASSERT(0);
                                }
                                break;
                            case RDBD_FILE_CTRL_WRITE_FILE:
                                session = find_session(&session_list, path);
                                if(session == RT_NULL)
                                {
                                    LOG_E("Please open the file %s first", path);
                                    RT_ASSERT(0);
                                }
                                break;
                            default:
                                RT_ASSERT(0);
                            }
                            LOG_D("Offset %d", msg->header.offset);
                            LOG_D("Write %d", msg->header.length);
                            if(msg->header.length)
                            {
                                if(write_session(session, msg->header.offset, &msg->msg[msg->header.pathlength], msg->header.length) != msg->header.length)
                                {
                                    LOG_W("Space not enought");
                                }
                            }
                            session = RT_NULL;
                            //end of function release mem
                        }
                        else if(RDBD_FILE_CTRL_WRITE_DONE == msg->header.command)
                        {
                            session = find_session(&session_list, path);
                            if(session == RT_NULL)
                            {
                                LOG_W("Not need release session %s", path);
                                RT_ASSERT(0);
                            }
                            delete_session(session);
                        }
                        else if(RDBD_FILE_CTRL_READ_FILE == msg->header.command)
                        {
                            msg = rt_realloc(msg, msg->header.pathlength + RDBD_FILE_READ_BLOCK_SIZE + RDBD_FILE_HEADER_SIZE);
                            if(msg == RT_NULL)
                            {
                                LOG_E("No memory");
                                RT_ASSERT(0);
                            }
                            session = create_session(&session_list, path, O_RDONLY);
                            if(session == RT_NULL)
                            {
                                LOG_E("Open file error %s", path);
                                RT_ASSERT(0);
                            }
                            
                            msg->header.offset = 0;
                            msg->header.length = read_session(session, msg->header.offset, &msg->msg[msg->header.pathlength], RDBD_FILE_READ_BLOCK_SIZE);
                            file_service_request_send(&request_write_list, msg, 1);
                            msg = RT_NULL;
                        }
                        else if(RDBD_FILE_CTRL_RM_FILE == msg->header.command)
                        {
                            remove(path);
                        }
                        else if(RDBD_FILE_CTRL_LIST_DIR == msg->header.command)
                        {
                            file_list = get_file_list(path);
                            msg->header.offset = 0;
                            msg->header.pathlength = 0;
                            msg->header.length = rt_dstr_strlen(file_list);
                            msg = rt_realloc(msg, RDBD_FILE_MSG_SIZE(msg));
                            memcpy(msg->msg, file_list->str, msg->header.length);
                            file_service_request_send(&request_write_list, msg, 0);
                            rt_dstr_del(file_list);
                            file_list = RT_NULL;
                            msg = RT_NULL;
                        }
                        else if(RDBD_FTLE_CTRL_GET_HASH == msg->header.command)
                        {
                            msg->header.length = 4;
                            msg->header.offset = 0;
                            msg = rt_realloc(msg, RDBD_FILE_MSG_SIZE(msg));
                            {
                                rt_uint32_t hash = rdbd_file_calc_hash(path);
                                memcpy(&msg->msg[msg->header.pathlength], &hash, 4);
                            }
                            file_service_request_send(&request_write_list, msg, 0);
                            msg = RT_NULL;
                        }
                        else if(RDBD_FTLE_CTRL_DIR_SYNC_START == msg->header.command)
                        {
                        //TO DO
                        }
                        else if(RDBD_FTLE_CTRL_DIR_SYNC_STOP == msg->header.command)
                        {
                        //TO DO
                        }
                        else if(RDBD_FTLE_CTRL_CHECK_PATH == msg->header.command)
                        {
                            msg->header.length = 1;
                            msg->header.offset = 0;
                            msg = rt_realloc(msg, RDBD_FILE_MSG_SIZE(msg));
                            {
                                struct stat filestat;
                                if (stat(path, &filestat) == -1)
                                {
                                    msg->msg[msg->header.pathlength] = RDBD_FTLE_CTRL_PATH_NONE;
                                }
                                else
                                {
                                    if((filestat.st_mode & S_IFMT) == S_IFDIR)
                                    {
                                        msg->msg[msg->header.pathlength] = RDBD_FTLE_CTRL_PATH_IS_DIR;
                                    }
                                    else
                                    {
                                        msg->msg[msg->header.pathlength] = RDBD_FTLE_CTRL_PATH_IS_FILE;
                                    }
                                }
                            }
                            file_service_request_send(&request_write_list, msg, 0);
                            msg = RT_NULL;
                        }

                        if(RT_NULL != path)
                        {
                            rt_free(path);
                            path = RT_NULL;
                        }
                        if(RT_NULL != msg)
                        {
                            rt_free(msg);
                            msg = RT_NULL;
                        }
                    }
                }
            }
            if(!rt_list_isempty(&request_write_list)) //need write data to pc
            {
                if(FD_ISSET(file_service->out_pipe_write_fd, &writeset))
                {
                    request = rt_list_entry(request_write_list.next, struct file_request_send, list);
                    result = write(file_service->out_pipe_write_fd,
                                                request->msg + request->msg_pos, RDBD_FILE_MSG_SIZE(request->msg) - request->msg_pos);
                    if(result > 0)
                    {
                        request->msg_pos += result;
                    }
                    if(request->msg_pos == RDBD_FILE_MSG_SIZE(request->msg))
                    {
                        request->msg_pos = 0;
                        if(request->request_file_send && 0 != RDBD_FILE_MSG_MSG(request->msg)->header.length)
                        {
                            RDBD_FILE_MSG_MSG(request->msg)->header.offset += RDBD_FILE_MSG_MSG(request->msg)->header.length;
                            path = rt_calloc(1,RDBD_FILE_MSG_MSG(request->msg)->header.pathlength + 1);
                            if(RT_NULL == path)
                            {
                                LOG_E("No memory");
                                RT_ASSERT(0);
                            }
                            rt_strncpy(path, (const char *)RDBD_FILE_MSG_MSG(request->msg)->msg, RDBD_FILE_MSG_MSG(request->msg)->header.pathlength);
                            path[RDBD_FILE_MSG_MSG(request->msg)->header.pathlength] = '\0';
                            
                            request->msg = rt_realloc(request->msg, RDBD_FILE_MSG_MSG(request->msg)->header.pathlength + RDBD_FILE_READ_BLOCK_SIZE + RDBD_FILE_HEADER_SIZE);
                            if(request->msg == RT_NULL)
                            {
                                LOG_E("No memory");
                                RT_ASSERT(0);
                            }
                            session = find_session(&session_list, path);
                            if(session == RT_NULL)
                            {
                                LOG_E("Please open the file %s first", path);
                                RT_ASSERT(0);
                            }
                            free(path);
                            path = RT_NULL;
                            RDBD_FILE_MSG_MSG(request->msg)->header.length = read_session(session, RDBD_FILE_MSG_MSG(request->msg)->header.offset, &RDBD_FILE_MSG_MSG(request->msg)->msg[RDBD_FILE_MSG_MSG(request->msg)->header.pathlength], RDBD_FILE_READ_BLOCK_SIZE);
                            if(RDBD_FILE_MSG_MSG(request->msg)->header.length == 0)
                            {
                                RDBD_FILE_MSG_MSG(request->msg)->header.command = RDBD_FILE_CTRL_WRITE_DONE;
                                request->request_file_send = 0;
                                delete_session(session);
                                session = RT_NULL;
                            }
                        }
                        else
                        {
                            file_service_delete_request(request);
                        }
                    }
                }
            }
        }
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

static int file_service_request_send(rt_list_t * header, struct rdbd_file_control_msg * msg, int request_file_send)
{
    struct file_request_send * request;
    if(RT_NULL == header || RT_NULL == msg)
    {
        LOG_E("Write request arg error");
        return -1;
    }
    request = rt_malloc(sizeof(struct file_request_send));
    if(RT_NULL == request)
    {
        LOG_E("No memory request send to file");
        return -1;
    }
    request->msg = (char *)msg;
    request->msg_pos = 0;
    request->request_file_send = request_file_send;
    rt_list_insert_before(header, &request->list);
    return 0;
}

static int file_service_delete_request(struct file_request_send * request)
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

int rdbd_file_service_init(void)
{
    struct rdbd_service * file_service;
    rdbd_t usbrdbd = rdbd_find("usb");
    if(RT_NULL == usbrdbd)
    {
        LOG_E("rdbd usb find error");
        return -1;
    }

    file_service = rdbd_create_service(RDBD_SERVICE_ID_FILE, "file", &control_ops, RT_NULL, "filein", 1024,"fileout", 1024, RDBD_SERVICE_FLAG_WR|RDBD_SERVICE_FLAG_RD);
    if(RT_NULL == file_service)
    {
        LOG_E("file_service create error");
        goto _error;
    }
    LOG_I("Service %s created :", file_service->name);
    LOG_I("in_pipe_path %s", file_service->in_pipe_path);
    LOG_I("out_pipe_path %s", file_service->out_pipe_path);
    LOG_I("service_id %d", file_service->service_id);
    LOG_I("status %d", file_service->status);

    rdbd_service_install(usbrdbd, file_service);
    
    rdbd_service_control(file_service, RDBD_SERVICE_START, file_service);
    
    return 0;

_error:
    if(RT_NULL != usbrdbd)
    {
        //TO DO
    }
    return -1;
}
INIT_APP_EXPORT(rdbd_file_service_init);

static int start(void * args)
{
    struct rdbd_service * file_service = (struct rdbd_service *)args;

    if(RT_NULL == file_service)
    {
         LOG_E("Start up service error, args is null");
         return -1;
    }
    
    if(file_service->flag & RDBD_SERVICE_FLAG_RD)
    {
        file_service->in_pipe_read_fd = open(file_service->in_pipe_path, O_RDONLY | O_NONBLOCK, 0);
        if(file_service->in_pipe_read_fd < 0)
        {
            LOG_E("Start up service %s error open in pipe failed",file_service->name);
            return -1;
        }
    }

    if(file_service->flag & RDBD_SERVICE_FLAG_WR)
    {
        file_service->out_pipe_write_fd = open(file_service->out_pipe_path, O_WRONLY | O_NONBLOCK, 0);
        if(file_service->out_pipe_write_fd < 0)
        {
            LOG_E("Start up service %s error open out pipe failed",file_service->name);
            goto _error;
        }
    }
    
    
    if(file_service->flag & RDBD_SERVICE_FLAG_RD)
    {
        file_service->in_pipe_write_fd = open(file_service->in_pipe_path, O_WRONLY | O_NONBLOCK, 0);
        if(file_service->in_pipe_write_fd < 0)
        {
            LOG_E("Start up transfer error open in pipe failed",file_service->name);
            return -1;
        }
    }

    if(file_service->flag & RDBD_SERVICE_FLAG_WR)
    {
        file_service->out_pipe_read_fd = open(file_service->out_pipe_path, O_RDONLY | O_NONBLOCK, 0);
        if(file_service->out_pipe_read_fd < 0)
        {
            LOG_E("Start up transfer error open out pipe failed",file_service->name);
            goto _error;
        }
    }

    file_service->service_thread = rt_thread_create(file_service->name,
                                                    file_service_thread_entry,
                                                    file_service,
                                                    2048,
                                                    21,
                                                    20);
    if(RT_NULL != file_service->service_thread)
    {
        if(RT_EOK != rt_thread_startup(file_service->service_thread))
        {
            rt_thread_delete(file_service->service_thread);
            file_service->service_thread = RT_NULL;
            goto _error;
        }
    }
    else
    {
        goto _error;
    }
    return 0;
    
_error:
    stop(args);
    return -1;
}

static int stop(void * args)
{
    struct rdbd_service * file_service = (struct rdbd_service *)args;

    if(RT_NULL == file_service)
    {
         LOG_E("Stop service error, args is null");
         return -1;
    }

    if(RT_NULL != file_service->service_thread)
    {
        if(RT_EOK != rt_thread_delete(file_service->service_thread))
        {
            LOG_E("Delete thread %s failed", file_service->service_thread->name);
            return -1;
        }
        file_service->service_thread = RT_NULL;
    }
    
    if(file_service->in_pipe_read_fd >= 0)
    {
        if(close(file_service->in_pipe_read_fd) < 0)
        {
            LOG_E("Close fd %d failed", file_service->in_pipe_read_fd);
            return -1;
        }
        file_service->in_pipe_read_fd = -1;
    }
    
    if(file_service->in_pipe_write_fd >= 0)
    {
        if(close(file_service->in_pipe_write_fd) < 0)
        {
            LOG_E("Close fd %d failed", file_service->in_pipe_write_fd);
            return -1;
        }
        file_service->in_pipe_write_fd = -1;
    }
    
    if(file_service->in_pipe_write_fd >= 0)
    {
        if(close(file_service->in_pipe_write_fd) < 0)
        {
            LOG_E("Close fd %d failed", file_service->in_pipe_write_fd);
            return -1;
        }
        file_service->in_pipe_write_fd = -1;
    }
    
    if(file_service->out_pipe_write_fd >= 0)
    {
        if(close(file_service->out_pipe_write_fd) < 0)
        {
            LOG_E("Close fd %d failed", file_service->out_pipe_write_fd);
            return -1;
        }
        file_service->out_pipe_write_fd = -1;
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

static rt_dstr_t * get_file_list(const char *pathname)
{
    rt_dstr_t * list = RT_NULL, *fullpath = RT_NULL, *temp_list = RT_NULL;
    struct dirent * dir = NULL;
    DIR * root_dir = NULL;
    struct stat filestat;

    list = rt_dstr_new("");
    root_dir = opendir(pathname);
    if(root_dir == NULL)
    {
        return list;
    }
    while(1)
    {
        dir = readdir(root_dir);
        if(dir == NULL)
        {
            break;
        }
        if (strncmp(dir->d_name, ".", 1) == 0)
			continue; /* skip hide file*/
        if(strcmp("/", pathname) == 0)
        {
            fullpath = rt_dstr_sprintf(fullpath, "/%s", dir->d_name);
        }
        else
        {
            fullpath = rt_dstr_sprintf(fullpath, "%s/%s", pathname, dir->d_name);
        }
        if (stat(fullpath->str, &filestat) == -1)
        {
            LOG_E("cannot access the file %s", fullpath->str);
            return list;
        }
        if ((filestat.st_mode & S_IFMT) == S_IFDIR)
        {
            temp_list = get_file_list(fullpath->str);
            list = rt_dstr_append_printf(list, "%s", temp_list->str);
            rt_dstr_del(temp_list);
            temp_list = RT_NULL;
        }
        else
        {
            list = rt_dstr_append_printf(list, "%s\n", fullpath->str);
        }
        rt_dstr_del(fullpath);
        fullpath = RT_NULL;
    }
    closedir(root_dir);
    return list;
}

int list_dir(void)
{
    int i = 0;
    rt_dstr_t * list = get_file_list("/");
    while(list->str[i] != '\0')
    {
        rt_kprintf("%c",list->str[i++]);
    }
    
    rt_dstr_del(list);
    return 0;
}
MSH_CMD_EXPORT(list_dir, list dir);

/* hash a single byte */
static rt_uint32_t fnv1a_r(unsigned char oneByte, rt_uint32_t hash)
{
    return (oneByte ^ hash) * 0x01000193; // 0x01000193 = 16777619
}

static rt_uint32_t rdbd_file_calc_hash(const char * filename)
{
    FILE * fp = NULL;
    int ch;
    uint32_t hash = RDBD_FILE_HASH_FNV_SEED;
    fp = fopen(filename, "r");
    if(fp == NULL)
    {
        LOG_W("%s not found!", filename);
        return 0;
    }
    while(1)
    {
        ch = fgetc(fp);
        if(ch == EOF)
        {
            break;
        }
        hash = fnv1a_r(ch, hash);
    }
    fclose(fp);
    return hash;
}

