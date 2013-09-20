
#include "main-app.h"

#include <wx/dir.h>


#ifndef WIN32
	#define __declspec(x)	/* to null out the DLL export compiler operatives on non-windows machines */
#endif

#ifdef ORANGEFS_DEBUG
	const char *debugLogFilename = "C:\\Users\\Trent\\My Documents\\Visual Studio 2012\\Projects\\test-orangefs-app\\debug\\debugLog.txt";
#endif

const int MAX_MNTENTS	= 256;
const int DEFAULT_WIN_WIDTH = 1024;
const int DEFAULT_WIN_HEIGHT = 800;
const char *TABFILE = "\\orangefstab";
const wxString APP_LOGO_NAME = "OrangeFS_LOGO.png";

MainApp *MAIN_APP;
MainFrame *MAIN_FRAME;
static char **mainFileListing;
vector<int> deleteIndexes(0);
DirHandler *changeHandler;
DirWatcher *dirWatcher;

/* to determine whether the sync is for a new or existing file/dir */
static SYNC_TYPE SyncType;

unsigned int __stdcall updateAll(void *parms);		/* thread update routine */
static void CALLBACK InterpretDirChanges(DWORD errorCode, DWORD bytes, LPOVERLAPPED over);		/* callback for asynchronous directory changes */

/* give life to the singleton objects */
FileListHandler *FileListHandler::instance = NULL;
MetadataHandler *MetadataHandler::instance = NULL;
Utils *Utils::instance = NULL;

BEGIN_EVENT_TABLE(MainFrame, wxFrame)
	EVT_MENU(ID_FILE_QUIT, MainFrame::onQuit)
	EVT_MENU(ID_FILE_ABOUT, MainFrame::onAbout)
	EVT_MENU(ID_VIEW_PERMISSIONS, MainFrame::showPermissions)
	EVT_MENU(ID_VIEW_SIZE, MainFrame::showFileSize)
	EVT_MENU(ID_VIEW_MODIFIED, MainFrame::showLastModified)
	EVT_MENU(ID_SETTINGS_CONFIG, MainFrame::showConfigDialog)
	EVT_MENU(ID_NEW_SYNC, MainFrame::showNewSyncDialog)
END_EVENT_TABLE()

/* sets up the required "main" */
IMPLEMENT_APP(MainApp)

void MainApp::printLastWinError() 
{
	DWORD errCode = GetLastError();
	char *winError;
	void *msgBuffer;

	FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | 
				   FORMAT_MESSAGE_FROM_SYSTEM |
				   FORMAT_MESSAGE_IGNORE_INSERTS,
				   NULL,
				   errCode,
				   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				   (char *) &msgBuffer,
				   0, NULL);

	winError = (char *) malloc(25 + strlen((char *)msgBuffer));
	strcpy(winError, "---- WINDOWS ERROR : ");
	strcat(winError, (char *) msgBuffer);

	OutputDebugString(winError);

	free(winError);
}

void MainApp::cleanupApp()
{
	orangefs_cleanup_credentials(rootCred);
	free(rootCred);

	for (int i=0; i < MAX_MNTENTS; i++)
	{
		free(mntents[i]);

		if (mainFileListing)
		{
			if (mainFileListing[i])
			{
				free(mainFileListing[i]);
			}
		}
	}
	free(mntents);

	if (mainFileListing)
		free(mainFileListing);

	free(rootSysAttr);

	deleteIndexes.clear();


	delete changeHandler;
	delete dirWatcher;
	delete localConfig;
	delete MetadataHandler::getInstance();
}

void MainApp::initFileSystem()
{
	char error_msg[MAX_PATH];
	int ret = -1;

	if ((ret = orangefs_initialize(0, rootCred, mntents[0], error_msg, MAX_PATH, TABFILE, OrangeFS_DEBUG_FILE | OrangeFS_DEBUG_MVS, debugLogFilename)) == -1)
	{																												
		orangefs_debug_print("Failed to initialize file system\nMake sure the orangefs tabfile is in the proper directory");
	}

	orangefs_debug_print("---- Initialized connection to OrangeFS ----\n");

	orangefs_load_tabfile(TABFILE, mntents, error_msg, MAX_PATH);	/* currently unimplemented */
}

void MainApp::allocateMembers()
{
	/* need to allocate the credential before calling credential functions */
	rootCred = (OrangeFS_credential *) malloc(sizeof(OrangeFS_credential));

	/* set up the root user credentials */
	orangefs_credential_init(rootCred);
	orangefs_credential_set_user(rootCred, 0);
	orangefs_credential_add_group(rootCred, 0);	/* add root user to root group */
	orangefs_credential_set_timeout(rootCred, ORANGEFS_DEFAULT_CREDENTIAL_TIMEOUT);	/* 1 hour */

	/* this method of allocation might have to be changed with large numbers of files 
	 * considering we can't put all this in memory at once on all systems */
	rootSysAttr = (OrangeFS_attr *)malloc(MAX_FILES * sizeof(OrangeFS_attr));
	memset(rootSysAttr, 0, MAX_FILES * sizeof(OrangeFS_attr));

	mntents = (OrangeFS_mntent **)malloc(MAX_MNTENTS * sizeof(OrangeFS_mntent *));
	for (int i=0; i < MAX_MNTENTS; i++)
	{
		mntents[i] = (OrangeFS_mntent *)malloc(sizeof(OrangeFS_mntent));
		memset(mntents[i], 0, sizeof(OrangeFS_mntent));
	}
}

bool MainApp::OnInit() 
{
	synced = false;
	num_files = 0;
	screen_height = wxGetDisplaySize().GetHeight();
	screen_width = wxGetDisplaySize().GetWidth();
	endUpdating = false;
	SyncType = SYNC_NEW;

	/* give global main app handle a pointer to the app */
	MAIN_APP = this;

	orangefs_enable_debug( OrangeFS_DEBUG_FILE | OrangeFS_DEBUG_MVS, debugLogFilename, OrangeFS_CLIENT_DEBUG | OrangeFS_GETATTR_DEBUG | OrangeFS_SETATTR_DEBUG);

	allocateMembers();

	initFileSystem();

	/* get all the file system names */
	for (int i=0; i < mntents[0]->num_orangefs_config_servers; i++)
	{
		fsNames.Add(mntents[i]->orangefs_fs_name);
	}

	fsNames.Add("Other file system");

	MAIN_FRAME = new MainFrame("OrangeFS File Browser", 
							   wxPoint((screen_width/2) - (DEFAULT_WIN_WIDTH/2), /* middle of screen */
							   (screen_height/2) - (DEFAULT_WIN_HEIGHT/2)), 
 	 	 	 				   wxSize(DEFAULT_WIN_WIDTH, DEFAULT_WIN_HEIGHT));

	/* load up the local configurations */
	localConfig = new LocalConfig;

	/* parses the local metadata file and loads all runtime cache maps */
	metadataHandler = MetadataHandler::getInstance();	

	orangefs_debug_print("---- Parsed metadata file and loaded cache ----\n");

	/* if there is an existing configuration */
	if( localConfig->load() ) 
	{
		orangefs_debug_print("Using existing sync configuration from file : %s\n", localConfig->getConfigPath() );
	}
	else 
	{
		orangefs_debug_print("No existing sync config file found.\n");
	}

		/* now, just check for updates */
#ifdef WIN32
	HANDLE threadHandle;

		/* spawn a new thread to handle the syncing; if the user closes the app while it's still syncing, the app window will become invisible, but the sync thread will still be running */
		threadHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) updateAll, 0, 0, NULL);

		/* give the main app the handle so it can determine what it's finished */
		MAIN_APP->setThreadHandle(threadHandle);
#endif

	/* now that we've loaded (or not loaded) locally synced files, display the file browser */
	MAIN_FRAME->Show(true);
	SetTopWindow(MAIN_FRAME);

	return true;
}


/* caller allocates input string */
void MainApp::getAbsoluteRunningPath(char *fileName)	/* local util to get absolute path to logo png file */
{
/* TODO 
 * add the same funtionality for linux */

#ifdef WIN32
	char exe_path[MAX_PATH];
	char *logoFile, *ptr;
	int ret, malloc_flag;
	
	ret = GetModuleFileName(NULL, exe_path, MAX_PATH);
	if (ret)
	{
		logoFile = (char *) malloc(MAX_PATH);
		malloc_flag = TRUE;

		/* cut off the exe file name, just get up to last directory */
		ptr = strrchr(exe_path, '\\');
		if (ptr)
			*ptr = '\0';

		strcpy(logoFile, exe_path);
		strcat(logoFile, "\\");
		strcat(logoFile, fileName);

		memset(fileName, 0, strlen(fileName));

		strcpy(fileName, logoFile);
	}

	if (malloc_flag)
		free(logoFile);
#endif
}

void MainApp::syncDir(const wxString &dir)
{
	wxString absolutePath = localConfig->getSyncPath();
	absolutePath += dir;
	absolutePath += '\\';

	/* first, create the directory if it doesn't exist */
#ifndef WIN32
	if ( !wxDirExists(absolutePath) )
	{
		mkdir(absolutePath.Trim(), 0700);
	}
#else
	if ( !wxDirExists(absolutePath.Trim()) )
	{
		if ( !CreateDirectory(absolutePath.c_str(), NULL) ) 
		{
			orangefs_debug_print("Failed to create directory : %s\n", absolutePath.c_str());
			MAIN_APP->printLastWinError();
		}
	}
#endif

}

void MainApp::syncFile(const wxString &file, const OrangeFS_size &size)
{
	long bytesCopied = 0;
	char buffer[262144];				/* 256 KB buffer */
	OrangeFS_size buffSize = 262144;
	FILE *fileToSync;
	bool copy = true;
	wxString absolutePath = localConfig->getSyncPath();

	if ( !file ) {
		orangefs_debug_print("ERROR - no file string passed in to syncFile()\n");
	}

	absolutePath += file;

#ifndef WIN32
	if ( access(absolutePath.c_str(), F_OK) == -1 )
#else
	if ( _access(absolutePath.c_str(), 0) == -1 )
#endif
	{
		orangefs_debug_print("FILE : %s doesn't already exist. Creating it..\n", absolutePath.c_str());
		/* it doesn't exist, so create it and return for next time */
		fileToSync = fopen(absolutePath.c_str(), "w");
		fclose(fileToSync);
		return;
	}
	orangefs_debug_print("OPENING FILE : %s\n", absolutePath.c_str());
	fileToSync = fopen(absolutePath.c_str(), "a");

	if (SyncType == SYNC_NEW)
	{
		while (copy)
		{
			/* remaining file data to read is less than buffer size, adjust read_size accordingly */
			if ((size - bytesCopied) < buffSize)
			{
				buffSize = (size - bytesCopied);
				orangefs_read(&mntents[0]->fs_id, rootCred, const_cast<char *>(file.c_str()), buffer, 262144, bytesCopied, &buffSize);
				bytesCopied += buffSize;

				/* stop copying after this iteration */
				copy = !copy;
			}
			else
			{
				orangefs_read(&mntents[0]->fs_id, rootCred, const_cast<char *>(file.c_str()), buffer, 262144, bytesCopied, &buffSize);
				bytesCopied += buffSize;
			}
			orangefs_debug_print("BUFFER : %s\n", buffer);
			/* now copy the buffer to the local file */
			fprintf(fileToSync, "%s", buffer);

			/* reset buffer */
			memset(buffer, 0, 262144);
		}
	}
	
	if (SyncType == SYNC_EXISTING) 
	{
		orangefs_debug_print("---- UPDATING EXISTING FILE : %s ----", file.c_str());

	}

	fclose(fileToSync);
}

MainFrame::MainFrame(const wxString &title, const wxPoint &pos, const wxSize &size)
	: wxFrame( NULL, -1, title, pos, size )
{
	/* set the global window handle */
	MAIN_FRAME = this;

	windowSize = size;

	/* set up the singleton object */
	fileHandler = FileListHandler::getInstance();

	wxImage::AddHandler( new wxPNGHandler );

	char *logoPath = (char *) malloc( MAX_PATH );
	strcpy(logoPath, APP_LOGO_NAME);
	MAIN_APP->getAbsoluteRunningPath( logoPath );

	wxIcon mainIcon(logoPath, wxBITMAP_TYPE_PNG, 32, 32);

	this->SetIcon(mainIcon);

	free(logoPath);

	mainMenuBar = new wxMenuBar();	/* create the main menu bar */

	file = new wxMenu;	/* create the "File" dropdown menu tab */
	file->Append( ID_FILE_ABOUT, "&About..");	/* add "About to the "File" dropdown menu */
	file->AppendSeparator();					/* adds a line separator to the menu beneath "File" */
	file->Append( ID_FILE_QUIT, "E&xit");

	view = new wxMenu;	/* create the "View" dropdown menu tab */
	view->AppendCheckItem( ID_VIEW_PERMISSIONS, "Permissions" );	/* adds a "checkable" menu item to "View" called "Permissions" */
	view->AppendSeparator();
	view->AppendCheckItem( ID_VIEW_SIZE, "File Size" );
	view->AppendSeparator();
	view->AppendCheckItem( ID_VIEW_MODIFIED, "Last Modified" );

	settings = new wxMenu;
	settings->Append( ID_SETTINGS_CONFIG, "Configuration" );

	newSync = new wxMenu;
	newSync->Append( ID_NEW_SYNC, "Add Sync Folder" );
	
	mainMenuBar->Append( file, "&File");	/* add the "File" dropdown tab to the main menu bar */
	mainMenuBar->Append( view, "&View");	
	mainMenuBar->Append( settings, "&Settings");
	mainMenuBar->Append( newSync, "&New");

	SetMenuBar( mainMenuBar );	/* set the main manu bar as the visible menu bar */

	this->CreateStatusBar();
	this->SetStatusText("File Syncing Complete");

	fileHandler->addColumn("File Name");
}

MainFrame::~MainFrame()
{
	if (this->IsVisible())
	{
		Close(true);
		MAIN_APP->stopUpdating();
	}

	MAIN_APP->cleanupApp();
	delete FileListHandler::getInstance();
}

void MainFrame::onQuit(wxCommandEvent &WXUNUSED(event))
{
	Close(true);
}

void MainFrame::onAbout(wxCommandEvent & WXUNUSED(event))
{
	wxMessageBox( "Developed by: Trent Vigar\nClemson University and Omnibond LLC",
				  "OrangeFS File Browser v1.0",
				  wxOK | wxICON_INFORMATION, this);
}

void MainFrame::showConfigDialog(wxCommandEvent & WXUNUSED(event))
{
	/******************************************************************************/
	/* This configuration dialog will show all the current FS sync configurations */
	/* and will allow the user to either edit them or delete them				  */
	/******************************************************************************/
}

void MainFrame::showNewSyncDialog(wxCommandEvent & WXUNUSED(event))
{
	int ret;

	newSyncDialog = new SyncDialog(NULL,									/* parent window will be the application's top level window */
									   ID_SYNC_DIALOG,							/* dialog unique identifier */
									   "Setup New File System Sync Folder",		/* window title */
									   wxDefaultPosition,						/* position */
									   wxSize(500, 270),						/* dialog size */
									   wxDEFAULT_DIALOG_STYLE
#ifdef __APPLE__
									   | wxDIALOG_EX_METAL						/* on Mac OS X, shows a metallic look */
#endif
									   , MAIN_APP->getFileSysNames()			/* array of file system names to be displayed in combo box */
									   );

	newSyncDialog->Show(TRUE);
	this->Centre(wxBOTH);	/* centers the dialog both horizontally and vertically on the parent window */

	ret = newSyncDialog->ShowModal();

	if (ret == wxID_CANCEL)		/* the dialog was exited */
	{
		/* save the new sync folder data to the local config object */
		MAIN_APP->saveConfig();

		newSyncDialog->Close(TRUE);
		delete newSyncDialog;
	}
	else         /* unexpected return from file browser event handler */
	{
		orangefs_debug_print("ERROR : unexpected return from file browser dialog\n");	
	}
}

void MainFrame::showPermissions(wxCommandEvent & WXUNUSED(event))
{
	if ( view->IsChecked( ID_VIEW_PERMISSIONS ) )	/* if the "Permissions" view is now checked, add the column */ 
	{
		fileHandler->addColumn("Permissions");	/* currently just adds to both, later add submenu inside the "View" menu to choose to check columns for each list */
	}
	else	/* otherwise, remove the column and resize the other column(s) */
	{
		fileHandler->removeColumn("Permissions");
	}
}

void MainFrame::showFileSize(wxCommandEvent & WXUNUSED(event))
{
	if ( view->IsChecked( ID_VIEW_SIZE ) )	/* if the "File Size" view is now checked, add the column */ 
	{
		fileHandler->addColumn("File Size");
	}
	else	/* otherwise, remove the column and resize the other column(s) */
	{
		fileHandler->removeColumn("File Size");
	}
}

void MainFrame::showLastModified(wxCommandEvent & WXUNUSED(event))
{
	if ( view->IsChecked( ID_VIEW_MODIFIED ) )	/* if the "Last Modified" view is now checked, add the column */ 
	{
		fileHandler->addColumn("Last Modified");
	}
	else	/* otherwise, remove the column and resize the other column(s) */
	{
		fileHandler->removeColumn("Last Modified");
	}
}

unsigned int __stdcall updateAll(void *parms)
{
	unsigned int ret = 0;
	OrangeFS_size fileSize;
	FileListHandler *syncList = FileListHandler::getInstance();
	MetadataHandler *metadataHandler = MetadataHandler::getInstance();
	wxString statusBarString = "Updated ";
	int numRetrieved = 0;
	int numCreated = 0;
	int numDeleted = 0;
	static int timesUpdated = 1;
	OrangeFS_ds_position ds_token;
	bool filesCreated = false;
	bool filesDeleted = false;
	bool syncFlag = false;

	mainFileListing = (char **) malloc(MAX_FILES * sizeof(char *));
	for (int i=0; i < MAX_FILES; i++)
	{	
		mainFileListing[i] = (char *) malloc(MAX_PATH);
		memset(mainFileListing[i], 0, MAX_PATH);
	}

	/* if the sync configuration has not been set, we'll wait for it and stay in the thread */
	do
	{
		if ( MAIN_APP->syncExists() )
		{
			syncFlag = true;
		}
		else
		{
			orangefs_debug_print("---- Unable to find sync configuration... Waiting 5 seconds ----\n");
#ifdef WIN32
			Sleep(5000);
#else
			sleep(5000);
#endif
			continue;
		}
	} while ( !syncFlag );

	/* register our local directory listener for changes to files locally */
	/* for now, we'll just monitor the root dir */
	MAIN_APP->setLocalSyncPath( MAIN_APP->getLocalSyncPath().substr(0, MAIN_APP->getLocalSyncPath().length()-1) );		/* set sync path without trailing '/' */

	orangefs_debug_print("Local sync path : %s\n", MAIN_APP->getLocalSyncPath().c_str());

	/* subscribe to changes on our local sync directory */
	changeHandler = new DirHandler();
	dirWatcher = new DirWatcher();
	dirWatcher->watchDirectory(MAIN_APP->getLocalSyncPath(), 
								FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
								changeHandler,
								false);

	/*****************************************/
	/*									     */
	/*		   MAIN SYNC UPDATE LOOP		 */
	/*										 */
	/*****************************************/
	do
	{
		ds_token = ORANGEFS_READDIR_START;
		filesCreated, filesDeleted = false;

		/* get a listing of all the files currently on the orangefs server at the subscribed directory/directories (for now, it's the root directory until we add different sync paths to the local config) */
		orangefs_find_files(&MAIN_APP->getMntentAt(0)->fs_id, MAIN_APP->getCred(), "/", &ds_token, MAX_FILES, &numRetrieved, mainFileListing, MAIN_APP->getAttr()); 

		/* set up the remote files runtime maps accessible by index or filename */
		for (int i=0; i < numRetrieved; i++)
		{
			wxString temp = mainFileListing[i];

			metadataHandler->addRemoteToRuntime(temp, i);
		}

		/* if the number of files changed, determine if file(s) was deleted or created */
		if (MAIN_APP->getNumFiles() != numRetrieved)
		{
			/* file(s) was deleted */
			if (numRetrieved < MAIN_APP->getNumFiles())
			{
				/* set a flag so we know the list control needs to be updated */
				filesDeleted = true;
				numDeleted = MAIN_APP->getNumFiles() - numRetrieved;
				orangefs_debug_print("NUM LOCAL : %d - NUM RETRIEVED : %d = NUM DELETED : %d\n", MAIN_APP->getNumFiles(), numRetrieved, numDeleted);
			}

			/* file(s) was created */
			if (numRetrieved > MAIN_APP->getNumFiles())
			{
				filesCreated = true;
				numCreated = numRetrieved - MAIN_APP->getNumFiles();
				orangefs_debug_print("NUM RETRIEVED: %d - NUM LOCAL : %d = NUM CREATED : %d\n", numRetrieved, MAIN_APP->getNumFiles(), numCreated);
			}
		}

		/**************************************************************************************/
		/* Here, we will check a few things to determine if things need to be updated locally */
		/*		-  compare the last modified times of the subscribed files on the server from */
		/*		   the last update to the current update, if they're different, something was */
		/*		   obviously changed and we'll do a low level scan and sync for the modified  */
		/*		   binary data																  */
		/**************************************************************************************/
		for (int i=0; i < numRetrieved; i++)
		{
			/* go through all the files saved locally */
			for (int j=0; j < metadataHandler->getNumFiles(); j++)
			{
				if ( !metadataHandler->getFileName(j).compare( metadataHandler->getRemoteFileName(i) ) )	/* we have found a file that is both on the server and locally stored */
				{
					if ( metadataHandler->getFileModTime( metadataHandler->getFileName(j) ) == MAIN_APP->getAttr()[i].mtime )
					{
						/* the mod times match, keep going until we find non-matching times */
						continue;
					}
					else
					{
						/* modified times don't match, update the modified time locally, and do low-level data copy */
						/* updateFileMetadata() not only adds the metadata to the application's data file, but updates all the hash maps that store the metadata during runtime */
						metadataHandler->updateFileMetadata(metadataHandler->getFileName(j), &MAIN_APP->getAttr()[i]);

						/****************************************************************************/
						/* FILES TO SYNC :															*/
						/*		local copy that needs updating : metadataHandler->getFileName(j)	*/
						/*		remote copy to sync data from  : mainFileListing[i]					*/
						/****************************************************************************/
						SyncType = SYNC_EXISTING;

						MAIN_APP->syncFile(metadataHandler->getFileName(j), MAIN_APP->getAttr()[i].size);	/* passing in size of remote file to sync */
						syncList->updateList(MAIN_APP->getFileName(i));
					}
				}
			}
		}

		/* find which files need to be created (if any) */
		if (filesCreated)
		{
			for (int i=0; i < numRetrieved; i++)
			{
				/* server file was not found locally, needs to be created/synced */
				if ( !metadataHandler->isServerOnLocal( metadataHandler->getRemoteFileName(i) ) )
				{
					SyncType = SYNC_NEW;

					orangefs_debug_print("FILE NAME : %s\n", metadataHandler->getRemoteFileName(i).c_str());

					/* sync the file/folder over, then add the file metadata to the data file and runtime maps */
					if ( MAIN_APP->getAttr()[i].objtype == OrangeFS_TYPE_DIRECTORY )
					{
						MAIN_APP->syncDir(metadataHandler->getRemoteFileName(i));
					}
					else 
					{
						MAIN_APP->syncFile(metadataHandler->getRemoteFileName(i), MAIN_APP->getAttr()[i].size);			
					}
					metadataHandler->addFileMetadata(metadataHandler->getRemoteFileName(i), &MAIN_APP->getAttr()[i]);
					syncList->updateList(metadataHandler->getRemoteFileName(i));
				}
			}
		}

		deleteIndexes.reserve(MAIN_APP->getNumFiles());		/* if all the files on the server were deleted, getNumFiles() would appropriately hold index for all of them */
		if (filesDeleted)
		{
			for (int i=0; i < MAIN_APP->getNumFiles(); i++)
			{
				/* the local file wasn't found on the server, delete locally */
				if ( !metadataHandler->isLocalOnServer( metadataHandler->getFileName(i) ) )
				{
					/* THIS ISN"T WORKING, GETS IN HERE EVERY TIME */

					orangefs_debug_print("---- ADDDED index : %d\n", i);
					deleteIndexes.push_back(i);
				}
			}

			orangefs_debug_print("NUM FILES FOR DELETE : %d\n", deleteIndexes.size());
			/* now we have the indexes of files stored locally to be deleted */
			for (int i=0; i < deleteIndexes.size(); i++)
			{
				/***********************************************************************************************/
				/* we will need to delete the actual file, and all of it's traces at the following locations : */
				/*																							   */
				/* 1) the physical file on the local hard disk												   */
				/* 2) the metadata entry from the metadata file												   */
				/* 3) the list control entry for the file													   */
				/* 4) the runtime maps																		   */
				/***********************************************************************************************/
				/* (1) */
				wxString fullPath = MAIN_APP->getLocalSyncPath();
				fullPath += metadataHandler->getFileName(i);
#ifdef WIN32
				DeleteFile(fullPath.c_str());
#elif
				remove(fullPath.c_str());
#endif
				/* (2) and (4) */
				metadataHandler->removeFile(metadataHandler->getFileName(i));

				/* (3) */
				MAIN_FRAME->fileHandler->removeListData(metadataHandler->getFileName(i));
			}
		}

#ifdef WIN32
		Sleep(5000);	/* Wait 5 seconds before checking for updates again */
#elif
		sleep(5000);	/* maybe in the future have the user set this value in the app configuration */
#endif

		stringstream ss;
		ss << timesUpdated;

		/* clear and refresh the status text */
		statusBarString.clear();
		statusBarString = "Updated ";

		statusBarString += ss.str();
		statusBarString += " times";
		MAIN_FRAME->setStatusbarText(statusBarString);
		timesUpdated++;
		
	}while(!MAIN_APP->stillUpdating());	/* keep checking for new files or existing file modifications on the subscribed dirs/files */


done_syncing:

	/* let the app know we're finished syncing */
	MAIN_APP->finishedSyncing(true);

	orangefs_debug_print("---- Syncing Completed ----\n");

	for (int i=0; i < MAX_FILES; i++)
	{	
		free(mainFileListing[i]);
	}
	free(mainFileListing);

	deleteIndexes.clear();

	LPDWORD exitCode;
	GetExitCodeThread(dirWatcher->getThreadHandle(), exitCode);
	TerminateThread(dirWatcher->getThreadHandle(), *exitCode);

	return ret;
}

static void CALLBACK InterpretDirChanges(DWORD error, DWORD bytes, LPOVERLAPPED over)
{
	AsynchronousData *data = (AsynchronousData *) over;
	FILE_NOTIFY_INFORMATION *changes = (FILE_NOTIFY_INFORMATION *) &data->buffer[0];
	char *localBuff = (char *) malloc(512);

	free(over);

	orangefs_debug_print("IN CALLBACK!\n");

	wcstombs(localBuff, changes->FileName, changes->FileNameLength);
	switch (changes->Action)
	{
		case FILE_ACTION_ADDED:
			orangefs_debug_print("FILE ADDED : %s\n", localBuff);
			break;
		case FILE_ACTION_REMOVED :
			orangefs_debug_print("FILE REMOVED : %s\n", localBuff);
			break;
		case FILE_ACTION_MODIFIED:
			orangefs_debug_print("FILE UPDATED : %s\n", localBuff);
			break;
		case FILE_ACTION_RENAMED_OLD_NAME:
			orangefs_debug_print("FILE RENAMED, OLD NAME : %s\n", localBuff);
			break;
		case FILE_ACTION_RENAMED_NEW_NAME:
			orangefs_debug_print("FILE RENAMED, NEW NAME : %S\n", localBuff);
			break;
		default:
			orangefs_debug_print("UNHANDLED DIRECTORY CHANGE : %s\n", localBuff);
			break;
	}

	free(localBuff);
}


