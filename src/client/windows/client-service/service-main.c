/*
 * (C) 2010-2022 Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 */

/* 
 * Client Service - service control functions 
 * -- Install or remove OrangeFS Client service
 * -- Run client as a service or console app
 */

#include "dokan.h"

#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <openssl/err.h>

#include "gen-locks.h"
#include "gossip.h"

#include "client-service.h"
#include "config.h"
#include "fs.h"
#include "cert.h"
#include "io-cache.h"
#include "user-cache.h"
#include "ldap-support.h"
#include "messages.h"

#define WIN32ServiceName           "orangefs-client"
#define WIN32ServiceDisplayName    "OrangeFS Client"
#define WIN32ServiceDescription    "Mounts an OrangeFS file system as a removable drive."

#define report_startup_error(msg, err)    _report_error(msg, err, TRUE)

/* globals */
SERVICE_STATUS_HANDLE hstatus;
SERVICE_STATUS service_status;

BOOL debug = FALSE;

int is_running = 0;
int run_service = 0;  

HANDLE hthread, hcache_thread;

int main_init();

DWORD main_thread_start(PORANGEFS_OPTIONS options);
DWORD main_thread_stop();

DWORD cache_thread_start();
DWORD cache_thread_stop();

DWORD WINAPI main_loop(LPVOID poptions);

FILE *debug_log = NULL;

extern struct qhash_table *user_cache;
extern gen_mutex_t user_cache_mutex;

extern struct qhash_table *io_cache;
extern gen_mutex_t io_cache_mutex;

PORANGEFS_OPTIONS goptions;

/* externs */
extern wchar_t* convert_mbstring(const char* mbstr);
extern int __cdecl dokan_loop(PORANGEFS_OPTIONS options);

void init_service_log()
{
    char exe_path[MAX_PATH], *p;
    int ret;

    if (!run_service)
        return;

    /* create log file in exe directory */
    ret = GetModuleFileName(NULL, exe_path, MAX_PATH);
    if (ret != 0)
    {
        /* get directory */
        p = strrchr(exe_path, '\\');
        if (p)
            *p = '\0';

        strcat(exe_path, "\\service.log");

        debug_log = fopen(exe_path, "w");
    }
}

void service_debug(char *format, ...)
{
    char buffer[512], prefix[128], *pp;
    struct timeval tv;
    time_t tp;
    va_list argp;

    if (!run_service)
        return;

    va_start(argp, format);
    vsprintf_s(buffer, sizeof(buffer), format, argp);
    va_end(argp);

    strcpy(prefix, "[S ");
    pp = prefix + strlen(prefix);
    gettimeofday(&tv, NULL);
    tp = tv.tv_sec;
    strftime(pp, sizeof(prefix) - strlen(prefix), "%H:%M:%S", localtime(&tp));
    pp = prefix + strlen(prefix);
    sprintf(pp, ".%03ld (%4ld)] ", (long)tv.tv_usec / 1000,
        GetThreadId(GetCurrentThread()));
    
    fprintf(debug_log, "%s%s", prefix, buffer);
    fflush(debug_log);

}

void close_service_log()
{
    if (!run_service)
        return;

    if (debug_log)
    {
        fprintf(debug_log, "\n");
        fclose(debug_log);
    }
}

/* Open our Event Log Provider */
ULONG init_event_log()
{
    /* hevent_log = RegisterEventSource(NULL, "OrangeFS Client"); */
    return EventRegisterOrangeFS_Client_Provider();
}

/* Close our Event Log Provider */
void close_event_log()
{
    EventUnregisterOrangeFS_Client_Provider();
}

/* get OpenSSL error message */
void get_security_error(const char *msg,
                        char *errstr,
                        size_t errlen)
{
    unsigned long ssl_err;
    char errbuf[256];

    errstr[0] = '\0';

    if (msg)
    {
        _snprintf(errstr, errlen, "%s\n", msg);
        errstr[errlen-1] = '\0';
    }

    while ((ssl_err = ERR_get_error()) != 0)
    {
        ERR_error_string_n(ssl_err, errbuf, sizeof(errbuf));
        errbuf[sizeof(errbuf)-1] = '\0';
        if (strlen(errstr) + strlen(errbuf) + 2 < errlen)
        {
            strcat(errstr, errbuf);
            strcat(errstr, "\n");
        }
    }

}

/* get OrangeFS error message */
void get_orangefs_error(const char *msg,
                        int err,
                        char *errstr,
                        size_t errlen)
{
    char errbuf[256];

    errstr[0] = '\0';
    if (msg)
    {
        _snprintf(errstr, errlen, "%s: ", msg);
        errstr[errlen-1] = '\0';
    }


    PVFS_strerror_r(err, errbuf, sizeof(errbuf));
    errbuf[sizeof(errbuf)-1] = '\0';

    strncat(errstr, errbuf, errlen-strlen(errstr)-1);

}

/* get Windows error message */
void get_windows_error(const char *msg, 
                       DWORD err,
                       char *errstr,
                       size_t errlen)
{
    LPVOID msg_buf = NULL;
    LPTSTR win_msg;

    errstr[0] = '\0';

    /* Get Windows error message */
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | 
                  FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL,
                  err,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (LPTSTR) &msg_buf,
                  0, NULL);

    if (msg_buf == NULL)
    {
        return;
    }

    win_msg = (LPTSTR) msg_buf;
    /* remove trailing \r\n from win_msg */
    if (win_msg[strlen(win_msg)-1] == '\n')
        win_msg[strlen(win_msg)-1] = '\0';
    if (win_msg[strlen(win_msg)-1] == '\r')
        win_msg[strlen(win_msg)-1] = '\0';
    
    _snprintf(errstr, errlen, "%s: %s (%u)", msg, win_msg, err);

    LocalFree(win_msg);
}

/* Report an error to the Event Log, service log (file), and debug
   log. The entire text of the message is displayed without modification. */
BOOL report_error_event(char *message, BOOL startup)
{
    // char *strings[1];

    /* startup errors also go to service log or stderr */
    if (startup)
    {
        if (run_service)
        {
            service_debug("%s\n", message);
        }
        else
        {
            fprintf(stderr, "%s\n", message);
        }
    }

    client_debug("Error reported:\n");
    client_debug("%s\n", message);
    
    /* write to the Windows Application Event Log, viewable in Event Viewer */
    EventWriteERROR_EVENT(message);

    return FALSE;
}

/* report error through logging mechanism */
void _report_error(const char *msg,
                   int err,
                   BOOL startup)
{
    char errstr[1024];
    
    if (err == -PVFS_ESECURITY)
    {
        get_security_error(msg, errstr, sizeof(errstr));
    }
    else if ((-err) & PVFS_ERROR_BIT)
    {
        get_orangefs_error(msg, err, errstr, sizeof(errstr));
    }
    else if (err != 0)
    {
        get_windows_error(msg, (DWORD) err, errstr, sizeof(errstr));
    }
    else
    {
        strncpy(errstr, msg, sizeof(errstr));
        errstr[sizeof(errstr)-1] = '\0';
    }

    report_error_event(errstr, startup);

}

BOOL check_mount_point(const char *mount_point)
{
    const char *slash;
    char drive;
    DWORD mask;

    /* first check if a directory rather than drive is mapped */
    slash = strchr(mount_point, '\\');
    if (slash && slash[1] != '\0')
        /* Dokan will exit if directory is invalid */
        return TRUE;

    drive = toupper(mount_point[0]);
    drive -= 'A';
    if (drive < 0 || drive > 25)
        return FALSE;

    mask = GetLogicalDrives();
    if (mask == 0)
    {
        report_error("GetLogicalDrives failed: ", GetLastError());
        return FALSE;
    }

    return !(mask & (1 << drive));
}

DWORD service_install()
{
    SC_HANDLE sch_service;
    SC_HANDLE sch_manager;
    SERVICE_DESCRIPTION service_desc;
    char *exe_path, *command;
    DWORD size;
    int err;

    /* Get location of executable */
    size = 512;
    do {
        exe_path = (char *) malloc(size);
        if (!exe_path) 
        {
            fprintf(stderr, "Insufficient memory\n");
            return -1;
        }

        GetModuleFileName(NULL, exe_path, size);
        err = GetLastError();

        if (err == ERROR_INSUFFICIENT_BUFFER)
        {
            free(exe_path);
            size *= 2;
        }
        else if (err != ERROR_SUCCESS)
        {
            free(exe_path);
            fprintf(stderr, "Error: GetModuleFileName (%u)\n", err);
            return -1;
        }

    } while (err == ERROR_INSUFFICIENT_BUFFER);

    /* append -service option to command */
    command = (char *) malloc(strlen(exe_path) + 16);
    if (!command)
    {
        fprintf(stderr, "Insufficient memory\n");
        return -1;
    }
    strcpy(command, exe_path);
    strcat(command, " -service");

    /* open the service manager */
    sch_manager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (sch_manager != NULL)
    {
        /* create the service */
        sch_service = CreateService(
            sch_manager,
            WIN32ServiceName,
            WIN32ServiceDisplayName,
            SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START,
            SERVICE_ERROR_NORMAL,
            command,
            NULL,
            NULL,
            TEXT("\0\0"),
            NULL,
            NULL);

        if (sch_service != NULL)
        {
            printf("%s installed\n", WIN32ServiceDisplayName);
            /* Set service description */
            service_desc.lpDescription = (LPSTR)WIN32ServiceDescription;
            if (!ChangeServiceConfig2(sch_service, SERVICE_CONFIG_DESCRIPTION, &service_desc))
            {
                printf("Warning: could not set service description\n");
            }

            CloseServiceHandle(sch_service);
        }
        else
        {
            fprintf(stderr, "Error: CreateService (%u)\n", GetLastError());
            return -1;
        }

        CloseServiceHandle(sch_manager);
    }
    else
    {
        fprintf(stderr, "Error: OpenSCManager (%u)\n", GetLastError());
        return -1;
    }

    free(exe_path);
    free(command);

    return 0;
}

DWORD service_remove()
{
    SC_HANDLE sch_service;
    SC_HANDLE sch_manager;

    /* open service manager */
    sch_manager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

    if (sch_manager != NULL)
    {
        /* open service */
        sch_service = OpenService(sch_manager, WIN32ServiceName, 
                        SERVICE_ALL_ACCESS | DELETE);
        if (sch_service != NULL)
        {
            /* attempt to stop service */
            if (ControlService(sch_service, SERVICE_CONTROL_STOP,
                  &service_status)) 
            {
                Sleep(1000);

                while (QueryServiceStatus(sch_service, &service_status))
                {
                    if (service_status.dwCurrentState == SERVICE_STOP_PENDING)                    
                        Sleep(1000);
                    else
                        break;
                }

            }
            
            if (DeleteService(sch_service))                
                printf("%s removed\n", WIN32ServiceDisplayName);
            else
                fprintf(stderr, "Error: DeleteService (%u)\n", GetLastError());

            CloseServiceHandle(sch_service);

        }
        else
        {
            fprintf(stderr, "Error: OpenService (%u)\n", GetLastError());
            return -1;
        }

        CloseServiceHandle(sch_manager);
    }
    else
    {
        fprintf(stderr, "Error: OpenSCManager (%u)\n", GetLastError());
        return -1;
    }

    return 0;

}

/* service control handler */
void WINAPI service_ctrl(DWORD ctrl_code)
{
    switch (ctrl_code)
    {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        service_debug("service_ctrl: shutdown received\n");
        
        service_status.dwCurrentState = SERVICE_STOP_PENDING;
        Sleep(1000);
        SetServiceStatus(hstatus, &service_status);
        
        is_running = 0;
        main_thread_stop();
    }
}

void WINAPI service_main(DWORD argc, char *argv[])
{
    PORANGEFS_OPTIONS options;
    int err = 0;
    char error_msg[512];
    char env_debug_file[MAX_PATH+16], env_debug_mask[256+16];

    init_service_log();

    if ((err = main_init()) != 0) {
        goto service_main_exit;
    }

    /* allocate options */
    options = (PORANGEFS_OPTIONS) calloc(1, sizeof(ORANGEFS_OPTIONS));

    /* init user cache */
    user_cache = qhash_init(user_compare, quickhash_string_hash, 257);
    
    gen_mutex_init(&user_cache_mutex);

    /* init IO cache */
    io_cache = qhash_init(io_cache_compare, quickhash_64bit_hash, 257);

    gen_mutex_init(&io_cache_mutex);

    /* read from config file */
    if ((err = get_config(options, error_msg, 512)) != 0) {
        goto service_main_exit;
    }

    /* point global options */
    goptions = options;

    /* add users (in list mode) */
    if ((err = add_users(options, error_msg, 512)) != 0) {
        goto service_main_exit;
    }
   
    debug = options->debug;

    /* now under service_main_exit label 
    if (ret != 0 || ret2 != 0)
    {
        report_startup_error(error_msg, (ret != 0) ? ret : ret2);
        close_service_log();
        close_event_log();
        return;
    } */

    /* see if mount point is already in use */
    if (!check_mount_point(options->mount_point)) {
        err = 1;
        goto service_main_exit;
    }

    /* turn on gossip debugging */
    if (debug)
    {
          /* enable win_client debugging by default */
          if (strlen(options->debug_mask) == 0)
          {
              strcpy(options->debug_mask, "win_client");
          }
          _snprintf(env_debug_mask, sizeof(env_debug_mask), "PVFS2_DEBUGMASK=%s",
              options->debug_mask);
          _putenv(env_debug_mask);
          /* debug file */
          _snprintf(env_debug_file, sizeof(env_debug_file), "PVFS2_DEBUGFILE=%s",
              options->debug_file);
          _putenv(env_debug_file);
          gossip_set_logstamp(GOSSIP_LOGSTAMP_THREAD);
    }

    /* register our control handler routine */
    if ((hstatus = RegisterServiceCtrlHandler(WIN32ServiceName, service_ctrl))
           != NULL)
    {
        service_debug("Service registered\n");

        /* run the user cache thread */
        if ((err = cache_thread_start()) != 0)
        {
            _snprintf(error_msg, sizeof(error_msg), "Fatal init error: could "
                "not start cache thread: %u", err);
            goto service_main_exit;
        }

        /* run the service */
        service_status.dwCurrentState = SERVICE_RUNNING;
        service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
        service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        service_status.dwWin32ExitCode = NO_ERROR;
        service_status.dwServiceSpecificExitCode = 0;

        /*** execute service main thread ***/
        if (SetServiceStatus(hstatus, &service_status))
        {
            is_running = 1;
            service_debug("Starting thread\n");
            main_thread_start(options);
        }
        else
        {
            report_error("SetServiceStatus to SERVICE_RUNNING failed: ", GetLastError());
        }
    }
    else
    {
        report_error("RegisterServiceCtrlHandler failed: ", GetLastError());
    }

service_main_exit:

    if (err)
    {
        report_startup_error(error_msg, GetLastError());
    }

    qhash_destroy_and_finalize(user_cache, struct user_entry, hash_link, free);

    qhash_destroy_and_finalize(io_cache, struct io_cache_entry, hash_link, free);

    cache_thread_stop();

    PVFS_ldap_cleanup();

    openssl_cleanup();
    
    close_service_log();

    close_event_log();

    free(options);
    
    if (hstatus != NULL) {
        service_status.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(hstatus, &service_status);
    }
}

DWORD main_thread_start(PORANGEFS_OPTIONS options)
{
    DWORD err = 0;
    int rc = 0;

    service_debug("main_thread_start enter\n");

    /* create and run the new thread */
    hthread = CreateThread(NULL, 
                           0, 
                           main_loop,
                           options,
                           0,
                           NULL);
    if (hthread)
    {  
        service_debug("main_thread_start: created thread %ld\n", GetThreadId(hthread));
        
        /* wait until main_loop() is exited */
        WaitForSingleObject(hthread, INFINITE);
    }
    else
    {
        err = GetLastError();
        report_error("CreateThread (main) failed: ", err);
    }

    service_debug("main_thread_start exit\n");

    return err;                           
}

DWORD main_thread_stop()
{
    DWORD err = 0;
    LPWSTR w_mount_point;
    BOOL unmounted = FALSE;

    service_debug("main_thread_stop enter\n");

    /* Unmount drive */
    service_debug("main_thread_stop: unmounting %s...\n", goptions->mount_point);
    if ((w_mount_point = convert_mbstring(goptions->mount_point)) != NULL) {
        unmounted = DokanRemoveMountPoint(w_mount_point);
        free(w_mount_point);
    }

    if (unmounted) {
        service_debug("main_thread_stop: unmount OK\n");
    }
    else {
        service_debug("main_thread_stop: unmount failed\n");
    }

    service_debug("main_thread_stop exit\n");

    return err;
}

DWORD cache_thread_start()
{
    DWORD err = 0;

    /* create and run the user cache thread */
    hcache_thread = CreateThread(NULL,
                                 0,
                                 (LPTHREAD_START_ROUTINE) user_cache_thread,
                                 NULL,
                                 0,
                                 NULL);
    
    if (hcache_thread == NULL)
    {
        err = GetLastError();
        report_error("CreateThread (user cache) failed: ", err);
    }

    return err;
}

DWORD cache_thread_stop()
{
    DWORD err = 0;

    if (hcache_thread != NULL)
        if (!TerminateThread(hcache_thread, 0))
        {
            err = GetLastError();
            report_error("TerminateThread (user cache) failed: ", err);
        }

    return err;
}

#define RETRY_TIMEOUT    15000
DWORD WINAPI main_loop(LPVOID poptions)
{
    PORANGEFS_OPTIONS options = (PORANGEFS_OPTIONS) poptions;
    char *tabfile, exe_path[MAX_PATH], *p, error_msg[256],
         event_msg[512];
    FILE *f;
    int ret, malloc_flag = 0;
    DWORD err = 0;

    /* locate tabfile -- env. variable overrides */
    if (!(tabfile = getenv("PVFS2TAB_FILE")))
    {
        ret = GetModuleFileName(NULL, exe_path, MAX_PATH);
        if (ret)
        {
            /* get directory */
            p = strrchr(exe_path, '\\');
            if (p)
                *p = '\0';

            tabfile = (char*)malloc(MAX_PATH);
            if (tabfile != NULL) {
                malloc_flag = TRUE;

                strcpy(tabfile, exe_path);
                strcat(tabfile, "\\orangefstab");

                /* attempt to open file */
                f = fopen(tabfile, "r");
                if (f)
                    fclose(f);
                else
                {
                    /* switch to pvfs2tab -- fs_initialize will fail if not valid */
                    strcpy(tabfile, exe_path);
                    strcat(tabfile, "\\pvfs2tab");
                }
            }
            else {
                return 1;
            }
        }
    }

    /* init file systems */
    if (tabfile)
    {
        service_debug("Using tabfile: %s\n", tabfile);
        do {
            ret = fs_initialize(tabfile, error_msg, 256);

            if (ret != 0)
            {
                /* delay until retrying connecting to the filesystem */
                service_debug("Retrying fs initialization...\n");
                report_startup_error(error_msg, 0);
                Sleep(RETRY_TIMEOUT);
            }
        } while (is_running && ret != 0);
    }
    else
    {
        report_error("GetModuleFileName failed: ", GetLastError());
    }

    /*** main loop - run dokan client ***/
    if (is_running && ret == 0)
    {
        /* note: dokan_loop does not return normally */
        ret = dokan_loop(options);
        if (ret == -1) {
          _snprintf(event_msg, sizeof(event_msg), "Fatal init error - no memory");
          report_startup_error(event_msg, ret);
        }

        /* close file systems */
        fs_finalize();
    }

    if (malloc_flag)
        free(tabfile);

    return (DWORD) ret;
}

int main_init()
{
    DWORD err = 0;
    ULONG evterr = 0;
    WORD version;
    WSADATA wsaData;

    /* init event log */
    if ((evterr = init_event_log()) != ERROR_SUCCESS)
    {
        /* since we can't log to event log, log to stderr */
        fprintf(stderr, "Could not register event log: %lu\n", evterr);
    }

    /* init Windows Sockets -- this needs to be done in order
       to use gethostname() if loading credentials in advance. */
    version = MAKEWORD(2, 2);
    err = WSAStartup(version, &wsaData);
    if (err != 0)
    {
        report_startup_error("WSAStartup (Windows Sockets) error:", err);
        return 1;
    }

    /* initialize OpenSSL */
    openssl_init();

    /* initialize LDAP */
    if (PVFS_ldap_init() != 0)
    {
        report_startup_error("Fatal error: LDAP could not be initialized", 0);
        return 1;
    }

    return 0;
}

int main(int argc, char **argv, char **envp)
{
  int i = 0, err = 0;
  PORANGEFS_OPTIONS options;
  BOOL cmd_debug = FALSE;
  char mount_point[256];
  char error_msg[512];
  char env_debug_file[MAX_PATH+16], env_debug_mask[256+16];

  SERVICE_TABLE_ENTRY dispatch_table[2] = 
  {
      {WIN32ServiceName, (LPSERVICE_MAIN_FUNCTION) service_main},
      {NULL, NULL}
  };

  mount_point[0] = '\0';

  /* command line arguments */
  for (i = 1; i < argc; i++) 
  {
      if (!stricmp(argv[i], "-installService") ||
          !stricmp(argv[i], "-w") || !stricmp(argv[i], "/w")) 
      {
          return service_install();
      }
      else if (!stricmp(argv[i], "-removeService") ||
               !stricmp(argv[i], "-u") || !stricmp(argv[i], "/u"))
      {
          return service_remove();
      }
      else if (!strcmp(argv[i], "-service"))
      {
          run_service = 1;
      }
      else if (!strcmp(argv[i], "-mount") || !strcmp(argv[i], "-m") ||
               !strcmp(argv[i], "/m"))
      {
          if (i < (argc - 1))
              strncpy(mount_point, argv[++i], MAX_PATH);
          else
              fprintf(stderr, "Invalid argument -mount. Using mount point Z:\n");
      }
      else if (!strcmp(argv[i], "-debug") || !strcmp(argv[i], "-d") ||
               !strcmp(argv[i], "/d"))
      {
          cmd_debug = TRUE;
      }
      else {
          fprintf(stderr, "Invalid argument %s - exiting\n", argv[i]);
          return 1;
      }
  }

  if (run_service) 
  {
      /* dispatch the main service thread */
      StartServiceCtrlDispatcher(dispatch_table);
  } 
  else 
  {          
#if defined(_DEBUG) && defined(_MEMLEAKS)
      /* Windows memory debugging 
         NOTE: run from command prompt to generate file */
      _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
      /* Output to memleaks.log */
      _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
      {
          HANDLE hfile = CreateFile("memleaks.log", GENERIC_WRITE,
              FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
              NULL);
          if (hfile != INVALID_HANDLE_VALUE)
              _CrtSetReportFile(_CRT_WARN, hfile);
      } 
#endif
      /* call initialization function */
      if ((err = main_init()) != 0) {
          goto main_exit;
      }

      options = (PORANGEFS_OPTIONS) calloc(1, sizeof(ORANGEFS_OPTIONS));

      /* init user list */
      user_cache = qhash_init(user_compare, quickhash_string_hash, 257);
      
      gen_mutex_init(&user_cache_mutex);

      /* init IO cache */
      io_cache = qhash_init(io_cache_compare, quickhash_64bit_hash, 257);
      
      gen_mutex_init(&io_cache_mutex);

      /* get options from config file */
      if (get_config(options, error_msg, 512) != 0)
      {          
          err = 1;
          goto main_exit;
      }

      /* point goptions */
      goptions = options;

      /* add users -- list mode */
      if (add_users(options, error_msg, 512) != 0)
      {
          err = 1;
          goto main_exit;
      }

      /* override with mount point from command line */
      if (strlen(mount_point) > 0)
          strcpy(options->mount_point, mount_point);

      /* turn debug on if specified on command line */
      if (cmd_debug)
          debug = TRUE;
      if (debug)
          options->debug = TRUE;

      if (options->debug)
      {          
          /* enable win_client debugging by default */
          if (strlen(options->debug_mask) == 0)
          {
              strcpy(options->debug_mask, "win_client");
          }
          _snprintf(env_debug_mask, sizeof(env_debug_mask), "PVFS2_DEBUGMASK=%s",
              options->debug_mask);
          _putenv(env_debug_mask);
          /* debug file */
          if (!options->debug_stderr)
          {
              _snprintf(env_debug_file, sizeof(env_debug_file), "PVFS2_DEBUGFILE=%s",
                  options->debug_file);
              _putenv(env_debug_file);
          }
          /* log thread id */
          gossip_set_logstamp(GOSSIP_LOGSTAMP_THREAD);
      }

      if (!check_mount_point(options->mount_point))
      {
          _snprintf(error_msg, sizeof(error_msg), "Fatal error: %s already "
              "in use", options->mount_point);
          err = 1;
          goto main_exit;
      }

      /* start user cache thread  */
      err = cache_thread_start();
      if (err != 0)
      {
          sprintf(error_msg, "Fatal error: user cache thread did not start");
          goto main_exit;
      }

      is_running = 1;

      /* process requests */
      err = main_loop(options);
      if (err != 0)
      {
          sprintf(error_msg, "Main loop exited with error code: %d", err);
      }

      printf("main_loop exited: %d\n", err);

      cache_thread_stop();

      gen_mutex_destroy(&user_cache_mutex);

      gen_mutex_destroy(&io_cache_mutex);

main_exit:

      if (err != 0)
      {          
          report_error_event(error_msg, TRUE);
      }

      qhash_destroy_and_finalize(user_cache, struct user_entry, hash_link, free);

      qhash_destroy_and_finalize(io_cache, struct io_cache_entry, hash_link, free);

      PVFS_ldap_cleanup();

      openssl_cleanup();

      close_event_log();

      free(options);
  }

  return err;
}