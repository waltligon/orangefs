
#include "main-app.h"

#include <vld.h>

#ifndef WIN32
	#define __declspec(x)	/* to null out the DLL export compiler operatives on non-windows machines */
#endif

#ifdef ORANGEFS_DEBUG
	const char *debugLogFilename = "C:\\Users\\Trent\\My Documents\\Visual Studio 2012\\Projects\\test-orangefs-app\\debug\\debugLog.txt";
#endif

const int MAX_FILES		= 256;
const int MAX_MNTENTS	= 256;
const int DEFAULT_WIN_WIDTH = 1024;
const int DEFAULT_WIN_HEIGHT = 800;
const OrangeFS_ds_position ORANGEFS_READDIR_START = INT32_MAX - 1;
const char *TABFILE = "\\orangefstab";
const wxString APP_LOGO_NAME = "OrangeFS_LOGO.png";

/* global handle to the main app */
MainApp *MAIN_APP;

/* global handle to the window so FileListHandler can use it externally */
MainFrame *MAIN_FRAME;

/* give life to the singleton FileListHandler object */
FileListHandler *FileListHandler::instance = NULL;

BEGIN_EVENT_TABLE(MainFrame, wxFrame)
	EVT_MENU(ID_FILE_QUIT, MainFrame::onQuit)
	EVT_MENU(ID_FILE_ABOUT, MainFrame::onAbout)
	EVT_MENU(ID_VIEW_PERMISSIONS, MainFrame::showPermissions)
	EVT_MENU(ID_VIEW_SIZE, MainFrame::showFileSize)
	EVT_MENU(ID_VIEW_MODIFIED, MainFrame::showLastModified)
	EVT_MENU(ID_SETTINGS_CONFIG, MainFrame::showConfigDialog)
	EVT_LIST_ITEM_SELECTED(LIST_CTRL_REMOTE, MainFrame::onRemoteFileSelected)
END_EVENT_TABLE()

/* sets up the required "main" */
IMPLEMENT_APP(MainApp)

void MainApp::cleanupApp()
{
	orangefs_cleanup_credentials(this->rootCred);
	free(this->rootCred);

	for (int i=0; i < MAX_MNTENTS; i++)
	{
		free(this->mntents[i]);
	}
	free(this->mntents);

	free(this->rootSysAttr);
}

void MainApp::initFileSystem()
{
	char error_msg[MAX_PATH];
	int ret = -1;

	if ((ret = orangefs_initialize(0, this->rootCred, this->mntents[0], error_msg, MAX_PATH, TABFILE, OrangeFS_DEBUG_FILE | OrangeFS_DEBUG_MVS, debugLogFilename)) == -1)
	{																												
		orangefs_debug_print("Failed to initialize file system\nMake sure the orangefs tabfile is in the proper directory");
	}

	orangefs_load_tabfile(TABFILE, this->mntents, error_msg, MAX_PATH);	/* currently unimplemented */
}

/* maybe instead of allocating main memory for large numbers of files ,
 * allocate memory, set the variables, save the values to a local database,
 * then free the memory, rinse-repeat */
void MainApp::allocateMembers()
{
	/* need to allocate the credential before calling credential functions */
	this->rootCred = (OrangeFS_credential *)malloc(sizeof(OrangeFS_credential));

	/* set up the root user credentials */
	orangefs_credential_init(this->rootCred);
	orangefs_credential_set_user(this->rootCred, 0);
	orangefs_credential_add_group(this->rootCred, 0);	/* add root user to root group */
	orangefs_credential_set_timeout(this->rootCred, ORANGEFS_DEFAULT_CREDENTIAL_TIMEOUT);	/* 1 hour */

	/* this method of allocation might have to be changed with large numbers of files 
	 * considering we can't put all this in memory at once on all systems */
	this->rootSysAttr = (OrangeFS_attr *)malloc(MAX_FILES * sizeof(OrangeFS_attr));
	memset(this->rootSysAttr, 0, MAX_FILES * sizeof(OrangeFS_attr));

	this->mntents = (OrangeFS_mntent **)malloc(MAX_MNTENTS * sizeof(OrangeFS_mntent *));
	for (int i=0; i < MAX_MNTENTS; i++)
	{
		this->mntents[i] = (OrangeFS_mntent *)malloc(sizeof(OrangeFS_mntent));
		memset(this->mntents[i], 0, sizeof(OrangeFS_mntent));
	}
}

bool MainApp::OnInit()
{
	int ret = 0;
	int numRetrieved = 0;
	int i;
	OrangeFS_ds_position ds_token;
	char **tempFileListing;

	this->num_files = 0;
	this->screen_height = wxGetDisplaySize().GetHeight();
	this->screen_width = wxGetDisplaySize().GetWidth();

	/* give global main app handle a pointer to the app */
	MAIN_APP = this;

	/* initialize and allocate class entities */
	tempFileListing = (char **)malloc(MAX_FILES * sizeof(char *));
	for (int i=0; i < MAX_FILES; i++)
	{	
		tempFileListing[i] = (char *)malloc(MAX_PATH);
		memset(tempFileListing[i], 0, MAX_PATH);
	}

	allocateMembers();

	initFileSystem();

	orangefs_enable_debug( OrangeFS_DEBUG_FILE | OrangeFS_DEBUG_MVS, debugLogFilename, OrangeFS_CLIENT_DEBUG | OrangeFS_GETATTR_DEBUG | OrangeFS_SETATTR_DEBUG);

	ds_token = ORANGEFS_READDIR_START;

	/* get a listing of all the files currently on the orangefs server */
	orangefs_find_files(&this->mntents[0]->fs_id, this->rootCred, "/", &ds_token, MAX_FILES, &numRetrieved, tempFileListing, this->rootSysAttr); 

	this->num_files = numRetrieved;

	if (numRetrieved != 0)
	{
		/* add the C-strings to the string array */
		for (int i=0; i < numRetrieved; i++)
		{
			/* check for directories: if it's a directory, append <dir> to the file listing */
			if (rootSysAttr[i].objtype == OrangeFS_TYPE_DIRECTORY)
			{
				wxString temp = tempFileListing[i];
				temp += "   <dir>";
				this->fileListing.Add(temp);
				continue;
			}

			/* add the file name string to the array of strings that will populate the list control */
			this->fileListing.Add(tempFileListing[i], 1);
		}
	}
	else
	{
		orangefs_debug_print("Failed to retrieve a file listing from the orangefs server.\nCheck orangefstab file and ensure settings are correct.\n");
	}

	MainFrame *frame = new MainFrame("OrangeFS File Browser", 
									 wxPoint((this->screen_width/2) - (DEFAULT_WIN_WIDTH/2), /* middle of screen */
									 (this->screen_height/2) - (DEFAULT_WIN_HEIGHT/2)), 
									 wxSize(DEFAULT_WIN_WIDTH, DEFAULT_WIN_HEIGHT));

	frame->Show(true);
	SetTopWindow(frame);

	for (i=0; i < MAX_FILES; i++)
	{
		free(tempFileListing[i]);
	}
	free(tempFileListing);

	return true;
}

wxString getLogoPath(const wxString &logoName)	/* local util to get absolute path to logo png file */
{
	wxString retString;

/* TODO 
 * add the same funtionality for linux based machines */

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
		strcat(logoFile, logoName.c_str());
		retString = logoFile;
	}

	if (malloc_flag)
		free(logoFile);
#endif

	return retString;
}

MainFrame::MainFrame(const wxString &title, const wxPoint &pos, const wxSize &size)
	: wxFrame( NULL, -1, title, pos, size )
{
	/* set the global window handle */
	MAIN_FRAME = this;

	this->windowSize = size;

	/* set up the singleton object */
	this->fileHandler = FileListHandler::getInstance();

	wxImage::AddHandler( new wxPNGHandler );

	wxString logoPath = getLogoPath(APP_LOGO_NAME);

	wxIcon mainIcon(logoPath, wxBITMAP_TYPE_PNG, 32, 32);

	this->SetIcon(mainIcon);

	this->mainMenuBar = new wxMenuBar();	/* create the main menu bar */

	this->file = new wxMenu;	/* create the "File" dropdown menu tab */
	this->file->Append( ID_FILE_ABOUT, "&About..");	/* add "About to the "File" dropdown menu */
	this->file->AppendSeparator();					/* adds a line separator to the menu beneath "File" */
	this->file->Append( ID_FILE_QUIT, "E&xit");

	this->view = new wxMenu;	/* create the "View" dropdown menu tab */
	this->view->AppendCheckItem( ID_VIEW_PERMISSIONS, "Permissions" );	/* adds a "checkable" menu item to "View" called "Permissions" */
	this->view->AppendSeparator();
	this->view->AppendCheckItem( ID_VIEW_SIZE, "File Size" );
	this->view->AppendSeparator();
	this->view->AppendCheckItem( ID_VIEW_MODIFIED, "Last Modified" );

	this->settings = new wxMenu;
	this->settings->Append( ID_SETTINGS_CONFIG, "Configuration" );
	
	this->mainMenuBar->Append( this->file, "&File");	/* add the "File" dropdown tab to the main menu bar */
	this->mainMenuBar->Append( this->view, "&View");	
	this->mainMenuBar->Append( this->settings, "&Settings");

	SetMenuBar( this->mainMenuBar );	/* set the main manu bar as the visible menu bar */

	this->CreateStatusBar();
	this->SetStatusText("Select a file...");

	this->fileHandler->addColumn("File Name");
}

MainFrame::~MainFrame()
{
	MAIN_APP->cleanupApp();
	delete fileHandler;
}

void MainFrame::onQuit(wxCommandEvent &WXUNUSED(event))
{
	Close(true);
}

void MainFrame::onAbout(wxCommandEvent & WXUNUSED(event))
{
	wxMessageBox( "Developed by: Trent Vigar\nClemson University and Omnibond LLC",
				  "About This Application",
				  wxOK | wxICON_INFORMATION, this);
}

void MainFrame::showConfigDialog(wxCommandEvent & WXUNUSED(event))
{
	/* creates a directory dialog browser with an "Add new folder" button */
	wxDirDialog fileBrowser(this, "Choose a location", "/", wxDD_NEW_DIR_BUTTON);
	int ret = -1;

	/* show the dialog */
	ret = fileBrowser.ShowModal();

	if (ret == wxID_OK)
	{
		this->localSyncPath = fileBrowser.GetPath();

		orangefs_debug_print("Local file path selected : %s\n", this->localSyncPath);

		fileBrowser.Close(true);
		fileBrowser.Destroy();
		return;
	}
	if (ret == wxID_CANCEL)
	{
		fileBrowser.Close(true);
		fileBrowser.Destroy();
		return;
	}
}

void MainFrame::showPermissions(wxCommandEvent & WXUNUSED(event))
{
	if ( this->view->IsChecked( ID_VIEW_PERMISSIONS ) )	/* if the "Permissions" view is now checked, add the column */ 
	{
		this->fileHandler->addColumn("Permissions");	/* currently just adds to both, later add submenu inside the "View" menu to choose to check columns for each list */
	}
	else	/* otherwise, remove the column and resize the other column(s) */
	{
		this->fileHandler->removeColumn("Permissions");
	}
}

void MainFrame::showFileSize(wxCommandEvent & WXUNUSED(event))
{
	if ( this->view->IsChecked( ID_VIEW_SIZE ) )	/* if the "File Size" view is now checked, add the column */ 
	{
		this->fileHandler->addColumn("File Size");
	}
	else	/* otherwise, remove the column and resize the other column(s) */
	{
		this->fileHandler->removeColumn("File Size");
	}
}

void MainFrame::showLastModified(wxCommandEvent & WXUNUSED(event))
{
	if ( this->view->IsChecked( ID_VIEW_MODIFIED ) )	/* if the "Last Modified" view is now checked, add the column */ 
	{
		this->fileHandler->addColumn("Last Modified");
	}
	else	/* otherwise, remove the column and resize the other column(s) */
	{
		this->fileHandler->removeColumn("Last Modified");
	}
}

void MainFrame::onRemoteFileSelected(wxListEvent &event)
{
	/* determine whether the file selected is synced based on the index in the list control */
	bool synced = this->fileHandler->getSyncStatus(event.GetIndex());

	/* update status bar to say whether the file is synced or not */
	(synced) ? this->SetStatusText("File is Synced") : this->SetStatusText("File is NOT Synced");
}
