/* Copyright (C) 2010 Omnibond, Inc. */

/* Dokan Client Service - service control functions */

#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define WIN32ServiceName           "orangefs-client"
#define WIN32ServiceDisplayName    "OrangeFS Client"


DWORD service_install()
{
    SC_HANDLE sch_service;
    SC_HANDLE sch_manager;
    char *exe_path, *command;
    size_t size;
    int rc, err;

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
    command = (char *) malloc(strlen(exe_path) + 8);
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

}

void main_loop()
{

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
      main_loop();
  }

  return 0;
}