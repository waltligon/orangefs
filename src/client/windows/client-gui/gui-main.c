#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <tchar.h>

#include "fs.h"
#include "gen-locks.h"
#include "client-service.h"
#include "config.h"
#include "user-cache.h"
#include "cred.h"

#define MSG_GENERIC_ERROR	0xC0000001L
#define MAX_FILE_REFRESH	256
#define REFRESH_BUTTON		100
#define FILE_MENU			101
#define EXIT_BUTTON			102


/* globals and prototypes */
static char windowClass[] = "orangeFSexplorer";
static char windowTitle[] = "OrangeFS File Explorer";
static char **fileListing;

ORANGEFS_OPTIONS *goptions;

PVFS_credential *rootCred;

PVFS_sys_attr *rootSysAttr;

HINSTANCE mainWindow;

BOOL debug = TRUE;
BOOL firstRun = TRUE;

HANDLE hevent_log = NULL;
HANDLE hcache_thread;

int is_running = 0;
int currFileCount = 0;
int prevFileCount = 0;

FILE *debug_log = NULL;

extern struct qhash_table *user_cache;

extern gen_mutex_t user_cache_mutex;

LRESULT CALLBACK windowEventHandler(HWND, unsigned int, WPARAM, LPARAM);
BOOL report_error_event(char *, BOOL);
BOOL report_windows_error(char *, DWORD);
DWORD WINAPI init_file_system(void *);
LPTSTR get_windows_message(DWORD);
DWORD cache_thread_start();
DWORD cache_thread_stop();
DWORD init_event_log();
void close_event_log();
void DbgPrint(char *, ...);
/* end globals and prototypes */



/* Return the Windows error message for the specified code.
   The returned string must be freed with LocalFree. */
LPTSTR get_windows_message(DWORD err)
{
    LPVOID msg_buf;

    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | 
                  FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL,
                  err,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (LPTSTR) &msg_buf,
                  0, NULL);

    return (LPTSTR) msg_buf;
}


/* Report a Windows error to the Event Log 
   Format is {prefix}{windows msg} ({err}) */
BOOL report_windows_error(char *prefix, DWORD err)
{
    LPTSTR win_msg, message;
    size_t msg_len;
    BOOL ret;

    win_msg = get_windows_message(err);
    if (win_msg == NULL)
        return FALSE;

    /* remove trailing \r\n from win_msg */
    if (win_msg[strlen(win_msg)-1] == '\n')
        win_msg[strlen(win_msg)-1] = '\0';
    if (win_msg[strlen(win_msg)-1] == '\r')
        win_msg[strlen(win_msg)-1] = '\0';

    msg_len = strlen(prefix)+strlen(win_msg)+16;
    message = (LPTSTR) LocalAlloc(0, msg_len);
    _snprintf(message, msg_len, "%s%s (%u)", prefix, win_msg, err);

    ret = report_error_event(message, TRUE);

    LocalFree(message);
    LocalFree(win_msg);

    return ret;
}


/* Close our Event Log source */
void close_event_log()
{
    if (hevent_log != NULL)
        DeregisterEventSource(hevent_log);
}


/* Open our Event Log entry (from registry) */
DWORD init_event_log()
{
    hevent_log = RegisterEventSource(NULL, "OrangeFS Client");
    return GetLastError();
}


LRESULT CALLBACK windowEventHandler(HWND winHandle, unsigned int message, WPARAM wParam, LPARAM lParam)
{
  PAINTSTRUCT paintData;
  PVFS_ds_position rootToken;
  HDC hdc;
  HMENU systemMenu;
  HWND refreshButton;
  MENUITEMINFO *exit;
  char rootPath = '/';
  int numFilesRefreshed = 0;
  int i;

  exit = (MENUITEMINFO *)malloc(sizeof(MENUITEMINFO));
  memset(exit, 0, sizeof(MENUITEMINFO));

  exit->cbSize			= sizeof(MENUITEMINFO);
  exit->fMask			= MIIM_STATE | MIIM_ID | MIIM_TYPE;
  exit->fType			= NULL;
  exit->fState			= MFS_ENABLED;
  exit->wID				= EXIT_BUTTON;
  exit->hSubMenu		= NULL;
  exit->hbmpChecked		= NULL;
  exit->hbmpUnchecked	= NULL;
  exit->dwItemData		= NULL;
  exit->dwTypeData		= "Exit";
  exit->cch				= strlen("Exit");
  exit->hbmpItem		= NULL;

  switch(message)
  {
  /* put stuff here to be done on window creation */
  case WM_CREATE:
	  /* create the system "File" menu */
	  systemMenu = CreateMenu();
	  /* systemMenu = GetSystemMenu(winHandle, FALSE); */

	  /* add the "Exit" item to the Menu */
	  InsertMenuItem(systemMenu, EXIT_BUTTON, FALSE, exit);

	  /* assign the newly created menu to the current window */
	  SetMenu(winHandle, systemMenu);

	  /* create the refresh button */
	  refreshButton = CreateWindowEx(NULL, 
									 "BUTTON", 
									 "REFRESH", 
									 WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 
									 400, 
									 10, 
									 100, 
									 75, 
									 winHandle, 
									 (HMENU)REFRESH_BUTTON, 
									 GetModuleHandle(NULL), 
									 NULL);

	  rootToken = PVFS_READDIR_START;

	  /* retrieve all files from PVFS2 server */
	  fs_find_files(&rootPath, rootCred, &rootToken, MAX_FILE_REFRESH, &numFilesRefreshed, fileListing, rootSysAttr);

	  if (firstRun)
	  {
		  prevFileCount = numFilesRefreshed;
		  firstRun = FALSE;
	  }

	  /* update current file count to compare with the refreshed file coutn */
	  currFileCount = numFilesRefreshed;
	  break;

  /* every time a button is clicked or text is typed into a message box, this is called */
  case WM_COMMAND:
	  switch(LOWORD(wParam))
	  {
	  case REFRESH_BUTTON:
		  if (currFileCount != prevFileCount)
		  {
			  MessageBox(NULL, "New files have arrived!", "Status", MB_ICONINFORMATION);
		  }
		  else
		  {
			  MessageBox(NULL, "No new files...", "Status", MB_ICONINFORMATION);
		  }

		  prevFileCount = currFileCount;
		  break;

	  case EXIT_BUTTON:
		  PostQuitMessage(WM_QUIT);
		  free(exit);
		  break;
	  }
	  break;

  /* fairly obvious, when the window is being displayed */
  case WM_SHOWWINDOW:

	  break;

  /* when there are multiple windows stacked, the window is still displayed but might not be active, this checks if it's actually active and visible */
  case WM_ACTIVATE:

	  break;

  case WM_PAINT:
	  hdc = BeginPaint(winHandle, &paintData);

	  /* Here is where the application logic lies */
	  for (i=0; i < numFilesRefreshed; i++)
	  {
		/* TODO: check for files or directories from rootSysAttr->objtype which will be either PVFS_TYPE_NONE, PVFS_TYPE_DATAFILE, PVFS_TYPE_DIRECTORY, PVFS_TYPE_SYMLINK, PVFS_TYPE_DIRDATA, etc */
		TextOut(hdc, 10, 20*(i+1), fileListing[i], strlen(fileListing[i]));
	  }
	  /* Application specific layout logic ends here */

	  EndPaint(winHandle, &paintData);
	  break;

  case WM_DESTROY:
	  PostQuitMessage(0);
	  free(exit);
	  break;

  default:
	  return DefWindowProc(winHandle, message, wParam, lParam);
	  break;
  }

  return 0;
}


/* Report an error to the Event Log, service log (file), and stderr. The 
   entire text of the message is displayed without modification. */
BOOL report_error_event(char *message, BOOL startup)
{
    char *strings[1];

    /* startup errors also go to service log or stderr */
    if (startup)
    {
        fprintf(stderr, "%s\n", message);
    }

    if (hevent_log != NULL)
    {
        strings[0] = message;

        return ReportEvent(hevent_log, EVENTLOG_ERROR_TYPE, 0, 
            MSG_GENERIC_ERROR, NULL, 1, 0, strings, NULL);
    }

    return FALSE;
}


#define DEBUG_BUF_SIZE    8192
void DbgPrint(char *format, ...)
{
  char buffer[DEBUG_BUF_SIZE];        
  va_list argp;

        va_start(argp, format);
        vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, argp);
        buffer[DEBUG_BUF_SIZE-1] = '\0';
        va_end(argp);

        /* output to debugger window */
        OutputDebugString(buffer); 
        
        /* use gossip to debug to file or stderr (set in config file) */
        /* gossip_debug(GOSSIP_WIN_CLIENT_DEBUG, "%s", buffer); */
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
        report_windows_error("CreateThread (user cache) failed: ", err);
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
            report_windows_error("TerminateThread (user cache) failed: ", err);
        }

    return err;
}


DWORD WINAPI init_file_system(void *options)
{
  char *tabfile, *p, error_msg[256], exe_path[MAX_PATH];
  int ret, malloc_flag = 0;
  FILE *f;

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

          tabfile = (char *) malloc(MAX_PATH);
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
  }

  /* init file systems */
  if (tabfile)
  {
     do {
          ret = fs_initialize(tabfile, error_msg, 256);

	      if (ret != 0)
          {
              report_error_event(error_msg, TRUE);
              Sleep(30000);
          }
     } while (ret != 0);
  }

  if (malloc_flag)
	  free(tabfile);

  return (DWORD) ret;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR argv, int cmdShow)
{
  PORANGEFS_OPTIONS options;
  WNDCLASSEX winClass;
  HWND winHandle;
  MSG message;
  DWORD err = 0;
  char error_msg[512], env_debug_file[MAX_PATH+16], env_debug_mask[256+16];
  int i;

  /* allocate file name listing */
  fileListing = (char **)malloc(MAX_FILE_REFRESH * sizeof(char *));
  for (i = 0; i < MAX_FILE_REFRESH; i++)
  {
      fileListing[i] = (char *)malloc(MAX_PATH);
  }

  /* allocate all file sys attributes */
  rootSysAttr = (PVFS_sys_attr *)malloc(MAX_FILE_REFRESH * sizeof(PVFS_sys_attr));

  winClass.cbSize			= sizeof(WNDCLASSEX);
  winClass.style			= CS_HREDRAW | CS_VREDRAW;
  winClass.lpfnWndProc		= windowEventHandler;
  winClass.cbClsExtra		= 0;
  winClass.cbWndExtra		= 0;
  winClass.hInstance		= hInstance;
  winClass.hIcon			= LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPLICATION));
  winClass.hCursor			= LoadCursor(NULL, IDC_ARROW);
  winClass.hbrBackground	= (HBRUSH)GetStockObject(WHITE_BRUSH);
  winClass.lpszMenuName		= MAKEINTRESOURCE(FILE_MENU);
  winClass.lpszClassName	= windowClass;
  winClass.hIconSm			= LoadIcon(winClass.hInstance, MAKEINTRESOURCE(IDI_APPLICATION));

  if (!RegisterClassEx(&winClass))
  {
	  MessageBox(NULL, "Call to RegisterClassEx failed!", "OrangeFS Explorer", 0);
	  return 1;
  }

  /* initialize and set up the root credential (for testing purposes */
  rootCred = (PVFS_credential*)malloc(sizeof(PVFS_credential));
  init_credential(rootCred);
  rootCred->userid = 0;					/* root user */
  credential_add_group(rootCred, 0);	/* add to root group */
  credential_set_timeout(rootCred, PVFS2_DEFAULT_CREDENTIAL_TIMEOUT);	/* 1 hour */

  /* get global handle to the window instance */
  mainWindow = hInstance;

  /* init event log */
  if ((err = init_event_log()) != 0)
      /* since we can't log to event log, log to stderr */
      fprintf(stderr, "Could not open event log: %u\n", err);

  options = (PORANGEFS_OPTIONS) calloc(1, sizeof(ORANGEFS_OPTIONS));

  /* init user list 
  user_cache = qhash_init(user_compare, quickhash_string_hash, 257);
      
  gen_mutex_init(&user_cache_mutex);
  */

  /* get options from config file */
  if (get_config(options, error_msg, 512) != 0)
  {          
      err = 1;
      goto main_exit;
  }

  /* point goptions */
  goptions = options;

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
  }
  

  /* start user cache thread  
  err = cache_thread_start();
  if (err != 0)
  {
      sprintf(error_msg, "Fatal error: user cache thread did not start");
      goto main_exit;
  }
  */

  is_running = 1;

  err = init_file_system(options);
  if (err != 0)
  {
      sprintf(error_msg, "Init file system exited with error code: %d", err);
  }

  /*
  cache_thread_stop();

  gen_mutex_destroy(&user_cache_mutex);
  */

  winHandle = CreateWindow(windowClass, 
						   windowTitle,		/* window name */
						   WS_OVERLAPPEDWINDOW, 
						   400,				/* x coord */
						   400,				/* y coord */
						   600,				/* width */
						   450,				/* height */
						   NULL, 
						   NULL, 
						   hInstance, 
						   NULL);

  if (!winHandle)
  {
	  MessageBox(NULL, "Call to CreateWindow failed!", "OrangeFS Explorer", NULL);
	  goto main_exit;
  }

  ShowWindow(winHandle, SW_SHOWNORMAL);
  
  UpdateWindow(winHandle);

  /* main message loop (will call the callback function to handle events */
  while (GetMessage(&message, NULL, 0, 0))
  {
	  TranslateMessage(&message);
	  DispatchMessage(&message);
  }

  main_exit:

      if (err != 0)
      {          
          report_error_event(error_msg, TRUE);
      }

      qhash_destroy_and_finalize(user_cache, struct user_entry, hash_link, free);

      close_event_log();

      free(options);

	  cleanup_credential(rootCred);

	  for (i=0; i < MAX_FILE_REFRESH; i++)
	  {
		  free(fileListing[i]);
	  }

	  free(fileListing);

	  free(rootSysAttr);

  return (int)message.wParam;
}



