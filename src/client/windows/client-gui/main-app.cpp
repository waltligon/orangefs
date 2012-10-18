#include "main-app.h"

#ifndef WIN32
	#define __declspec(x)	/* to null out the DLL export compiler operatives on non-windows machines */
#endif

#ifdef ORANGEFS_DEBUG
	const char *debugLogFilename = "C:\\Users\\Trent\\My\ Documents\\Visual\ Studio\ 2010\\Projects\\test-orangefs-app\\debug\\debugLog.txt";
#endif


/* global handle to the main app */
static MainApp *MAIN_APP;


IMPLEMENT_APP(MainApp)

void MainApp::cleanupApp()
{
	orangefs_cleanup_credentials(this->rootCred);
	free(rootCred);

	for (int i=0; i < MAX_MNTENTS; i++)
	{
		free(this->mntents[i]);
	}
	free(this->mntents);

	free(this->rootSysAttr);
	free(this->fileSize);
}

void MainApp::initFileSystem()
{
	char error_msg[MAX_PATH];
	int ret = -1;

	if ((ret = orangefs_initialize(0, rootCred, mntents[0], error_msg, MAX_PATH, TABFILE, OrangeFS_DEBUG_FILE | OrangeFS_DEBUG_MVS, debugLogFilename)) == -1)		
	{																												
		orangefs_debug_print("Failed to initialize file system\nMake sure the orangefs tabfile is in the proper directory");
	}

	orangefs_load_tabfile(TABFILE, mntents, error_msg, MAX_PATH);	/* currently unimplemented */

/*	orangefs_debug_print("First config server: %s\nMount dir: %s\nFile System name: %s\nNumber of config servers: %d\n", mntents[0]->first_orangefs_config_server, 
																														 mntents[0]->mnt_dir, 
																														 mntents[0]->orangefs_fs_name, 
																														 mntents[0]->num_orangefs_config_servers);
*/
}

void MainApp::allocateMembers()
{
	/* need to allocate the credential before calling credential functions */
	this->rootCred = (OrangeFS_credential *)malloc(sizeof(OrangeFS_credential));

	/* set up the root user credentials */
	orangefs_credential_init(this->rootCred);
	orangefs_credential_set_user(this->rootCred, 0);
	orangefs_credential_add_group(this->rootCred, 0);	/* add root user to root group */
	orangefs_credential_set_timeout(this->rootCred, ORANGEFS_DEFAULT_CREDENTIAL_TIMEOUT);	/* 1 hour */

	this->rootSysAttr = (OrangeFS_attr *)malloc(MAX_FILES * sizeof(OrangeFS_attr));
	memset(this->rootSysAttr, 0, MAX_FILES * sizeof(OrangeFS_attr));

	this->fileSize = (OrangeFS_size *)malloc(MAX_FILES * sizeof(OrangeFS_size));
	memset(this->fileSize, 0, MAX_FILES * sizeof(OrangeFS_size));

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

	/* give global main app handle a pointer to the app */
	MAIN_APP = this;

	orangefs_enable_debug( OrangeFS_DEBUG_FILE | OrangeFS_DEBUG_MVS, debugLogFilename);

	/* initialize and allocate class entities */
	tempFileListing = (char **)malloc(MAX_FILES * sizeof(char *));
	for (int i=0; i < MAX_FILES; i++)
	{	
		tempFileListing[i] = (char *)malloc(MAX_PATH);
		memset(tempFileListing[i], 0, MAX_PATH);
	}

	allocateMembers();

	initFileSystem();

	ds_token = ORANGEFS_READDIR_START;

	/* get a listing of all the files currently on the orangefs server */
	orangefs_find_files(&mntents[0]->fs_id, this->rootCred, "/", &ds_token, MAX_FILES, &numRetrieved, tempFileListing, this->rootSysAttr); 

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
			this->fileListing.Add(tempFileListing[i]);

			this->fileSize[i] = rootSysAttr[i].size;
		}
	}
	else
	{
		orangefs_debug_print("Failed to retrieve a file listing from the orangefs server.\nCheck orangefstab file and ensure settings are correct.\n");
	}

	MainFrame *frame = new MainFrame("OrangeFS File Browser", wxPoint(300, 200), wxSize(600, 480));

	frame->Show(true);
	SetTopWindow(frame);

	for (i=0; i < MAX_FILES; i++)
	{
		free(tempFileListing[i]);
	}
	free(tempFileListing);

	return true;
}

MainFrame::MainFrame(const wxString &title, const wxPoint &pos, const wxSize &size)
	: wxFrame( NULL, -1, title, pos, size )
{
	wxValidator validator;

	this->mainPane = new wxPanel(this);
	this->mainPane->SetSize(size);

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

	
	this->mainMenuBar->Append( this->file, "&File");	/* add the "File" dropdown tab to the main menu bar */
	this->mainMenuBar->Append( this->view, "&View");	

	SetMenuBar( this->mainMenuBar );	/* set the main manu bar as the visible menu bar */

	CreateStatusBar();
	SetStatusText("Loading files...");

	/* create a list box and populate it with the retrieved file listing */
	this->fileListCtrl = new wxListCtrl(this->mainPane,			/* window parent */
									    LIST_CTRL,					/* window id */
										wxDefaultPosition,			/* position */
										wxDefaultSize,				/* size */
										wxLC_REPORT | wxLC_VRULES,	/* window style */
										validator,					/* window validator */
										"");						/* list control name */
	this->fileListCtrl->SetSize(size);
	this->addColumn("File Name");
}

void MainFrame::onQuit(wxCommandEvent &WXUNUSED(event))
{
	MAIN_APP->cleanupApp();
	Close(TRUE);
}

void MainFrame::onAbout(wxCommandEvent & WXUNUSED(event))
{
	wxMessageBox( _("This is a wxWidgets Testing sample"),
				  _("About This Application"),
				  wxOK | wxICON_INFORMATION, this);
}

/* function to take the file size in bytes and do 2 things :
 *		1) bit shift the file size down to a size < 1024
 *		2) return the proper byte prefix string			*/
wxString MainFrame::getPrefixString(OrangeFS_size &size)
{
	const wxString prefixes[4] = { " KB" , " MB", " GB" , " TB" };
	wxString size_string = " bytes";
	short int count = 0;

	while (1)
	{
		if ((size >> 10) > 0)
		{
			size_string = prefixes[count];
			count++;
			size >>= 10;
		}
		else
		{
			break;
		}
	}

	return size_string;
}

void MainFrame::addColumn(const wxString &name)
{
	/* create the column to appear */
	wxListItem size;
	size.SetId(0);
	size.SetText(name);
	size.SetWidth(this->mainPane->GetSize().x / (this->fileListCtrl->GetColumnCount()+1));
	this->fileListCtrl->InsertColumn(this->fileListCtrl->GetColumnCount(), size);	/* add it to the end of the current columns, then increment the number of columns */

	/* now add the column name and associated column ID to the map */
	this->columnIDs[name] = this->fileListCtrl->GetColumnCount()-1;

	/* fill the columns with the appropriate data */
	if (!name.compare("File Name"))
	{
		/* populate the file name column */
		for (int i = MAIN_APP->getNumFiles()-1; i>=0; i--)	
		{
			this->fileListCtrl->InsertItem( 0, MAIN_APP->getFileName(i) );
		}
	}
	else if (!name.compare("File Size"))
	{
		/* populate the file size column with the "size" file attributes */
		for (int i=0; i < MAIN_APP->getNumFiles(); i++)	
		{
			stringstream size;
			wxString setString;
			OrangeFS_size tempSize = MAIN_APP->getFileSize(i);	/* get size of file in bytes */

			/* orangefs_debug_print("Old File size: %ld\n", tempSize); */

			wxString prefixString = this->getPrefixString( tempSize );	/* get the prefix size (i.e. KB, MB, GB, etc.), and set tempSize to value less then 1000 */

			/* orangefs_debug_print("New File size: %ld\nFile Size Prefix: %s\n", tempSize, prefixString); */

			size << tempSize;
			setString = size.str();
			setString += prefixString;	/* should be <fileSize> <prefixString> now */

			this->fileListCtrl->SetItem( i, this->columnIDs["File Size"], setString );
		}
	}
	else if (!name.compare("Permissions"))
	{
		/* TODO */
	}
	else if (!name.compare("Last Modified"))
	{
		/* TODO */
	}

	/* new go through the other existing column(s) and resize accordingly */
	for (int i=0; i < this->fileListCtrl->GetColumnCount(); i++)
	{
		this->fileListCtrl->SetColumnWidth(i, this->mainPane->GetSize().x / this->fileListCtrl->GetColumnCount());	/* set to panel size / number of columns */
	}

	/* orangefs_debug_print("ADDED COLUMN: %s\nCOLUMN ID: %d\nMAP SIZE: %d\n", name, this->columnIDs[name], this->columnIDs.size()); */
}

void MainFrame::removeColumn(const wxString &name)
{
	if (this->columnIDs.find(name) == this->columnIDs.end())	/* wasn't found in the map (sanity checking here) */
	{
		orangefs_debug_print("ERROR: unable to remove column: %s\n", name);
		return;
	}

	this->fileListCtrl->DeleteColumn( this->columnIDs[name] );	/* columnIDs[name] will be the column identifier */
	this->columnIDs.erase(name);	/* also remove the column name and ID from the map */

	/* new go through the other existing column(s) and resize accordingly */
	for (int i=0; i < this->fileListCtrl->GetColumnCount(); i++)
	{
		this->fileListCtrl->SetColumnWidth(i, this->mainPane->GetSize().x / this->fileListCtrl->GetColumnCount());	/* set to panel size / number of columns */
	}
}

void MainFrame::showPermissions(wxCommandEvent & WXUNUSED(event))
{
	if ( this->view->IsChecked( ID_VIEW_PERMISSIONS ) )	/* if the "Permissions" view is now checked, add the column */ 
	{
		this->addColumn("Permissions");
	}
	else	/* otherwise, remove the column and resize the other column(s) */
	{
		this->removeColumn("Permissions");
	}
}

void MainFrame::showFileSize(wxCommandEvent & WXUNUSED(event))
{
	if ( this->view->IsChecked( ID_VIEW_SIZE ) )	/* if the "File Size" view is now checked, add the column */ 
	{
		this->addColumn("File Size");
	}
	else	/* otherwise, remove the column and resize the other column(s) */
	{
		this->removeColumn("File Size");
	}
}

void MainFrame::showLastModified(wxCommandEvent & WXUNUSED(event))
{
	if ( this->view->IsChecked( ID_VIEW_MODIFIED ) )	/* if the "Last Modified" view is now checked, add the column */ 
	{
		this->addColumn("Last Modified");
	}
	else	/* otherwise, remove the column and resize the other column(s) */
	{
		this->removeColumn("Last Modified");
	}
}

void MainFrame::processListCtrlDoubleClick(wxCommandEvent & WXUNUSED(event))	/* user double clicked an entry in the file listing */
{
	/* TODO */
}



