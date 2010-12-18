/* Copyright (C) 2010 Omnibond, Inc. */

/* Client Service - service control functions */

#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "fs.h"

#define WIN32ServiceName           "orangefs-client"
#define WIN32ServiceDisplayName    "OrangeFS Client"

/* globals */
SERVICE_STATUS_HANDLE hstatus;
SERVICE_STATUS service_status;

int is_running = 0;

/* externs */
extern int __cdecl dokan_loop();

void main_loop();

DWORD service_install()
{
    SC_HANDLE sch_service;
    SC_HANDLE sch_manager;
    char *exe_path, *command;
    size_t size;
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

                if (DeleteService(sch_service))                
                    printf("%s removed\n", WIN32ServiceDisplayName);
                else
                    fprintf(stderr, "Error: DeleteService (%u)\n", 
                      GetLastError());

                CloseServiceHandle(sch_service);
            }
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
        service_status.dwCurrentState = SERVICE_STOP_PENDING;
        Sleep(1000);
        SetServiceStatus(hstatus, &service_status);
        is_running = 0;
    }
}

void WINAPI service_main(DWORD argc, char *argv[])
{
    /* register our control handler routine */
    if ((hstatus = RegisterServiceCtrlHandler(WIN32ServiceName, service_ctrl))
           != 0)
    {
        /* run the service */
        service_status.dwCurrentState = SERVICE_RUNNING;
        service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
        service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        service_status.dwWin32ExitCode = NO_ERROR;
        service_status.dwServiceSpecificExitCode = 0;

        /* execute service main loop */
        if (SetServiceStatus(hstatus, &service_status))
        {
            is_running = 1;
            main_loop();
        }
        
        /* shut down service */
        service_status.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(hstatus, &service_status);
    }
    /* TODO: error reporting */
}

void main_loop()
{
    int ret;

    /* init file systems */
    ret = fs_initialize();

    /* run dokan operations */
    if (ret == 0)
    {
        dokan_loop();
    }

    /* close file systems */
    fs_finalize();

}

int main(int argc, char **argv, char **envp)
{
  int i = 0;
  int run_service = 0;  

  SERVICE_TABLE_ENTRY dispatch_table[2] = 
  {
      {WIN32ServiceName, (LPSERVICE_MAIN_FUNCTION) service_main},
      {NULL, NULL}
  };

  for (i = 0; i < argc; i++) 
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
          break;
      }
  }

  if (run_service) 
  {
      /* dispatch the main service thread */
      StartServiceCtrlDispatcher(dispatch_table);
  } 
  else 
  {    
      is_running = 1;
      main_loop();
  }

  return 0;
}