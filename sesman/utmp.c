/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Emmanuel Blindauer 2017
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
 */

/**
 *
 * @file utmp.c
 * @brief utmp/wtmp handling code
 *
 */

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <pwd.h>
#include <unistd.h>
#include <stdio.h>

#include "log.h"
#include "os_calls.h"

#include <utmp.h>
#include <utmpx.h>

/*
 * Prepare the utmpx struct and write it.
 * this can handle login and logout at once with the 'state' parameter
 */

int
add_xtmp_entry(int pid, const char *line, const char *user, const char *rhostname, short state)
{
    struct utmpx ut;
    struct timeval tv;

    memset (&ut, 0, sizeof (ut));

    ut.ut_type=state;
    ut.ut_pid = pid;
    gettimeofday(&tv, NULL);
    ut.ut_tv.tv_sec = tv.tv_sec;
    ut.ut_tv.tv_usec = tv.tv_usec;
    strncpy(ut.ut_line, line , sizeof (ut.ut_line));
    strncpy(ut.ut_user, user , sizeof (ut.ut_user));
    strncpy(ut.ut_host, rhostname, sizeof (ut.ut_host));

    /* utmp */
    setutxent();
    pututxline(&ut);
    endutxent ();

    /* wtmp XXX hardcoded! */
    updwtmpx("/var/log/wtmp", &ut);

    return 0;
}

int
utmp_login(int pid, int display, const char *user, const char *rhostname)
{
    char str_display[16];

    log_message(LOG_LEVEL_DEBUG,
            "adding login info for utmp/wtmp: %d - %d - %s - %s",
            pid, display, user, rhostname);
    g_snprintf(str_display, 15, XRDP_LINE_FORMAT, display);
    return add_xtmp_entry(pid, str_display, user, rhostname, USER_PROCESS);
}

int
utmp_logout(int pid, int display, const char *user, const char *rhostname)
{
    char str_display[16];

    log_message(LOG_LEVEL_DEBUG,
            "adding logout info for utmp/wtmp: %d - %d - %s - %s",
            pid, display, user, rhostname);
    g_snprintf(str_display, 15, XRDP_LINE_FORMAT, display);
    return add_xtmp_entry(pid, str_display, user, rhostname, DEAD_PROCESS);
}
