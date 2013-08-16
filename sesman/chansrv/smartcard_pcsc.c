/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Jay Sorg 2013 jay.sorg@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/*
 * smartcard redirection support, PCSC daemon standin
 * this will act like pcsc daemon
 */

#define PCSC_STANDIN 1

#include "os_calls.h"
#include "smartcard.h"
#include "log.h"
#include "irp.h"
#include "devredir.h"
#include "trans.h"

#if PCSC_STANDIN

/* module based logging */
#define LOG_ERROR   0
#define LOG_INFO    1
#define LOG_DEBUG   2
#define LOG_LEVEL   LOG_ERROR

#define log_error(_params...)                           \
{                                                       \
    g_write("[%10.10u]: PCSC       %s: %d : ERROR: ",   \
            g_time3(), __func__, __LINE__);             \
    g_writeln (_params);                                \
}

#define log_always(_params...)                          \
{                                                       \
    g_write("[%10.10u]: PCSC       %s: %d : ALWAYS: ",  \
            g_time3(), __func__, __LINE__);             \
    g_writeln (_params);                                \
}

#define log_info(_params...)                            \
{                                                       \
    if (LOG_INFO <= LOG_LEVEL)                          \
    {                                                   \
        g_write("[%10.10u]: PCSC       %s: %d : ",      \
                g_time3(), __func__, __LINE__);         \
        g_writeln (_params);                            \
    }                                                   \
}

#define log_debug(_params...)                           \
{                                                       \
    if (LOG_DEBUG <= LOG_LEVEL)                         \
    {                                                   \
        g_write("[%10.10u]: PCSC       %s: %d : ",      \
                g_time3(), __func__, __LINE__);         \
        g_writeln (_params);                            \
    }                                                   \
}

#define PCSCLITE_MSG_KEY_LEN 16
#define PCSCLITE_MAX_MESSAGE_SIZE 2048

struct version_struct
{
    tsi32 major; /**< IPC major \ref PROTOCOL_VERSION_MAJOR */
    tsi32 minor; /**< IPC minor \ref PROTOCOL_VERSION_MINOR */
    tui32 rv;
};
typedef struct version_struct version_struct;

struct rxSharedSegment
{
    tui32 mtype; /** one of the \c pcsc_adm_commands */
    tui32 user_id;
    tui32 group_id;
    tui32 command; /** one of the \c pcsc_msg_commands */
    tui64 date;
    tui8 key[PCSCLITE_MSG_KEY_LEN]; /* 16 bytes */
    union _u
    {
        tui8 data[PCSCLITE_MAX_MESSAGE_SIZE];
        struct version_struct veStr;
    } u;
};
typedef struct rxSharedSegment sharedSegmentMsg, *psharedSegmentMsg;

#define RXSHAREDSEGMENT_BYTES 2088

extern int g_display_num; /* in chansrv.c */

/*****************************************************************************/
struct pcsc_client
{
    struct trans *con;
};

static struct pcsc_client *g_head = 0;
static struct pcsc_client *g_tail = 0;

static struct trans *g_lis = 0;
static struct trans *g_con = 0; /* todo, remove this */

static char g_pcsc_directory[256] = "";

/*****************************************************************************/
int APP_CC
scard_pcsc_get_wait_objs(tbus *objs, int *count, int *timeout)
{
    log_debug("scard_pcsc_get_wait_objs");

    if (g_lis != 0)
    {
        trans_get_wait_objs(g_lis, objs, count);
    }
    if (g_con != 0)
    {
        trans_get_wait_objs(g_con, objs, count);
    }
    return 0;
}

/*****************************************************************************/
int APP_CC
scard_pcsc_check_wait_objs(void)
{
    log_debug("scard_pcsc_check_wait_objs");

    if (g_lis != 0)
    {
        trans_check_wait_objs(g_lis);
    }
    if (g_con != 0)
    {
        trans_check_wait_objs(g_con);
    }
    return 0;
}

/*****************************************************************************/
/* returns error */
int APP_CC
scard_process_version(psharedSegmentMsg msg)
{
    return 0;
}

/*****************************************************************************/
/* returns error */
int APP_CC
scard_process_msg(struct stream *s)
{
    sharedSegmentMsg msg;
    int rv;

    g_memset(&msg, 0, sizeof(msg));
    in_uint32_le(s, msg.mtype);
    in_uint32_le(s, msg.user_id);
    in_uint32_le(s, msg.group_id);
    in_uint32_le(s, msg.command);
    in_uint64_le(s, msg.date);

    log_debug("scard_process_msg: mtype 0x%2.2x command 0x%2.2x",
           msg.mtype, msg.command);

    rv = 0;
    switch (msg.mtype)
    {
        case 0xF8: /* CMD_VERSION */
            rv = scard_process_version(&msg);
            break;
    }
    return rv;
}

/*****************************************************************************/
/* returns error */
int DEFAULT_CC
my_pcsc_trans_data_in(struct trans *trans)
{
    struct stream *s;
    int id;
    int size;
    int error;

    log_debug("my_pcsc_trans_data_in:");

    if (trans == 0)
    {
        return 0;
    }

    if (trans != g_con)
    {
        return 1;
    }
    s = trans_get_in_s(trans);
    g_hexdump(s->p, 64);
    error = scard_process_msg(s);
    return error;
}

/*****************************************************************************/
/* got a new connection from libpcsclite */
int DEFAULT_CC
my_pcsc_trans_conn_in(struct trans *trans, struct trans *new_trans)
{
    log_debug("my_pcsc_trans_conn_in:");

    if (trans == 0)
    {
        return 1;
    }

    if (trans != g_lis)
    {
        return 1;
    }

    if (new_trans == 0)
    {
        return 1;
    }

    g_con = new_trans;
    g_con->trans_data_in = my_pcsc_trans_data_in;
    g_con->header_size = RXSHAREDSEGMENT_BYTES;

    log_debug("my_pcsc_trans_conn_in: sizeof sharedSegmentMsg is %d",
           sizeof(sharedSegmentMsg));

    return 0;
}

/*****************************************************************************/
int APP_CC
scard_pcsc_init(void)
{
    char port[256];
    int error;

    log_debug("scard_pcsc_init:");

    if (g_lis == 0)
    {
        g_lis = trans_create(2, 8192, 8192);
        g_snprintf(g_pcsc_directory, 255, "/tmp/.xrdp/pcsc%d", g_display_num);
        if (g_directory_exist(g_pcsc_directory))
        {
            if (g_remove_dir(g_pcsc_directory) != 0)
            {
                log_error("scard_pcsc_init: g_remove_dir failed");
            }
        }
        if (g_create_dir(g_pcsc_directory) != 0)
        {
            log_error("scard_pcsc_init: g_create_dir failed");
        }
        g_chmod_hex(g_pcsc_directory, 0x1777);
        g_snprintf(port, 255, "%s/pcscd.comm", g_pcsc_directory);
        g_lis->trans_conn_in = my_pcsc_trans_conn_in;
        error = trans_listen(g_lis, port);
        if (error != 0)
        {
            log_error("scard_pcsc_init: trans_listen failed for port %s",
                   port);
            return 1;
        }
    }
    return 0;
}

/*****************************************************************************/
int APP_CC
scard_pcsc_deinit(void)
{
    log_debug("scard_pcsc_deinit:");

    if (g_lis != 0)
    {
        trans_delete(g_lis);
        g_lis = 0;
        if (g_remove_dir(g_pcsc_directory) != 0)
        {
            log_error("scard_pcsc_deinit: g_remove_dir failed");
        }
        g_pcsc_directory[0] = 0;
    }
    return 0;
}

#else

int APP_CC
scard_pcsc_get_wait_objs(tbus *objs, int *count, int *timeout)
{
    return 0;
}
int APP_CC
scard_pcsc_check_wait_objs(void)
{
    return 0;
}
int APP_CC
scard_pcsc_init(void)
{
    return 0;
}
int APP_CC
scard_pcsc_deinit(void)
{
    return 0;
}

#endif
