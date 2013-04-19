#include "main-app.h"

extern MainApp *MAIN_APP;
extern MainFrame *MAIN_FRAME;
extern unsigned int __stdcall updateAll(void *parms);		/* thread update routine */

/* sync thread callback routine */
unsigned int __stdcall syncAll(void *parms);

SyncDialog::SyncDialog(wxWindow *parent, wxWindowID ID, const wxString &title, const wxPoint &pos, const wxSize &size, long style, wxArrayString &fileSysNames)
	: wxDialog(parent, ID, title, pos, size, style)
{
	const short int NUM_SIZERS			= 3;
	const short int VERTICAL_PADDING	= 15;
	const short int HORIZONTAL_PADDING	= 20;
	const short int SIZER_WIDTH			= size.GetWidth() - (HORIZONTAL_PADDING * 2);								/* 20 px padding on left and right sides */
	const short int SIZER_HEIGHT		= (size.GetHeight() - ((NUM_SIZERS + 1) * VERTICAL_PADDING))  / NUM_SIZERS;	/* equal height with 15 px padding between each */ 

	this->listHandler = FileListHandler::getInstance();

	this->fsSelection = new wxComboBox(this,				/* parent window will be the dialog */
								  ID_FS_SELECTOR,					/* unique identifier */
								  "",								/* default selection */
								  wxPoint(220, 35),
								  wxSize(150, 50),
								  fileSysNames,		/* array of strings to populate the combo box wtih */
								  wxCB_READONLY					/* dropdown selector, but can't edit the fields */
								  );

	/* the main layout will contain all the static box sizers, putting one after the other from top to bottom */
	mainLayout = new wxBoxSizer(wxVERTICAL);
	
	/* set up the layout sizer for the combo box selector */
	/* the static box sizer is a box layout with a border around it */
	fsSelectSizer = new wxStaticBoxSizer(wxHORIZONTAL, this);
	fsSelectSizer->SetDimension(10,				/* x dimension */
							    10,				/* y dimension */
								SIZER_WIDTH,	/* width */
								SIZER_HEIGHT);	/* height : the "3" here is the number of static boxes in the dialog */

	/* add label next to the file system selection combo box */
	fsSelectSizer->Add(new wxStaticText(this, wxID_ANY, "Select a file system : ", wxPoint(80, 40), wxSize(100, 50), wxALIGN_LEFT), 
					   0, 
					   wxEXPAND,		/* the controls fill the space available */
					   0);


	/* add combo box for selecting file system */
	fsSelectSizer->Add(this->fsSelection,
						0,
						wxEXPAND,
						0);
		
	localPathSizer = new wxStaticBoxSizer(wxHORIZONTAL, this);
	localPathSizer->SetDimension(10, 
								 80, 
								 SIZER_WIDTH, 
								 SIZER_HEIGHT);

	this->filePathBox = new wxTextCtrl(this, wxID_ANY, "", wxPoint(150, 105), wxSize(200, 20), wxTE_READONLY);

	localPathSizer->Add(new wxStaticText(this, wxID_ANY, "Local Store Path : ", wxPoint(40, 105), wxSize(100, 50), wxALIGN_LEFT),
						0,
						wxEXPAND,
						0);

	localPathSizer->Add(this->filePathBox,
						0,
						wxEXPAND,
						0);

	localPathSizer->Add(new wxButton(this, ID_BROWSE_BUTTON, "Browse:", wxPoint(360, 105), wxSize(90, 20), wxBU_EXACTFIT),
						0, 
						wxEXPAND,
						0);
	
	/* connect the browse button to the onBrowseButtonClicked() function when the button is pressed */
	Connect(ID_BROWSE_BUTTON, wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(SyncDialog::onBrowseButtonClicked));

	syncButtons = new wxStaticBoxSizer(wxHORIZONTAL, this);
	syncButtons->SetDimension(10, 
							  150, 
							  SIZER_WIDTH, 
							  SIZER_HEIGHT);

	syncButtons->Add(new wxButton(this, ID_SYNC_ALL_BUTTON, "Sync All Files/Folders", wxPoint(70, 168), wxSize(150, 30), wxBU_EXACTFIT),
					 0,
					 wxEXPAND,
					 0);

	syncButtons->Add(new wxButton(this, ID_SELECT_SYNC_BUTTON, "Select Files/Folders to Sync", wxPoint(250, 168), wxSize(150, 30), wxBU_EXACTFIT),
					 0,
					 wxEXPAND,
					 0);

	Connect(ID_SYNC_ALL_BUTTON, wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(SyncDialog::onSyncAllButtonClicked));
	Connect(ID_SELECT_SYNC_BUTTON, wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(SyncDialog::onSelectSyncButtonClicked));

	mainLayout->Add(fsSelectSizer, 0, wxEXPAND | wxSHAPED, 5);
	mainLayout->Add(localPathSizer, 0, wxEXPAND | wxSHAPED, 5);
	mainLayout->Add(syncButtons, 0, wxEXPAND | wxSHAPED, 5);
}

SyncDialog::~SyncDialog()
{
	delete fsSelection;
	delete filePathBox;
	delete mainLayout;
}

void SyncDialog::onBrowseButtonClicked(wxCommandEvent & WXUNUSED(event))
{
	wxString pathToSet;

	/* creates a directory dialog browser with an "Add new folder" button */
	wxDirDialog fileBrowser(this, "Choose a location", "/", wxDD_NEW_DIR_BUTTON);
	int ret = -1;

	/* show the dialog */
	ret = fileBrowser.ShowModal();

	if (ret == wxID_OK)
	{
		pathToSet = fileBrowser.GetPath();
#ifdef WIN32
		pathToSet += "\\OrangeFS\\";
#else
		pathToSet += "/OrangeFS/";
#endif

		if ( !wxDirExists(pathToSet.c_str()) )
		{
			orangefs_debug_print("Directory : %s does not exist, creating...\n", pathToSet.c_str());
			wxMkdir(pathToSet.c_str(), 0755);	/* access mask parameter only works on Unix-based systems, for Windows, we'll use native API */
		}

		/* set the local sync path with the local config object */
		MAIN_APP->setLocalSyncPath(pathToSet);

		/* and set the text ctrl box with the path */
		this->filePathBox->SetLabel(pathToSet);

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

void SyncDialog::onSyncAllButtonClicked(wxCommandEvent & WXUNUSED(event))
{
	MAIN_APP->setFsSelection(this->fsSelection->GetLabelText());

#ifdef WIN32
	HANDLE threadHandle;
#endif
	/* spawn a new thread to handle the syncing; if the user closes the app while it's still syncing, the app window will become invisible, but the sync thread will still be running */
	threadHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) updateAll, 0, 0, NULL);

	/* give the main app the handle so it can determine what it's finished */
	MAIN_APP->setThreadHandle(threadHandle);

	/* now close the dialog now that the sync thread has started */
	this->Close(true);
}

void SyncDialog::onSelectSyncButtonClicked(wxCommandEvent & WXUNUSED(event))
{
	orangefs_debug_print("==== SYNCING SELECTED FILES/FOLDERS ====\n");
}

//
//unsigned int __stdcall updateAll(void *parms)
//{
//	unsigned int ret = 0;
//	OrangeFS_size fileSize;
//	FileListHandler *syncList = FileListHandler::getInstance();
//	int numRetrieved = 0;
//	OrangeFS_ds_position ds_token;
//	char **tempFileListing;
//
//	tempFileListing = (char **) malloc(MAX_FILES * sizeof(char *));
//	for (int i=0; i < MAX_FILES; i++)
//	{	
//		tempFileListing[i] = (char *) malloc(MAX_PATH);
//		memset(tempFileListing[i], 0, MAX_PATH);
//	}
//
//		ds_token = ORANGEFS_READDIR_START;
//	
//		/* get a listing of all the files currently on the orangefs server at the root directory */
//		orangefs_find_files(&MAIN_APP->getMntentAt(0)->fs_id, MAIN_APP->getCred(), "/", &ds_token, MAX_FILES, &numRetrieved, tempFileListing, MAIN_APP->getAttr()); 
//
//		if (numRetrieved > 0)
//		{
//			MAIN_APP->setNumFiles(numRetrieved);
//
//			OrangeFS_attr tempAttr;	
//			HANDLE fileHandle;
//			wxString absoluteFilePath;
//
//			for (int i=0; i < numRetrieved; i++)
//			{
//				/* add the file name string to the array of strings that will populate the list control */
//				MAIN_APP->addFileToListing(tempFileListing[i]);
//				MAIN_APP->addFileIndex(MAIN_APP->getFileName(i), i);
//
//				absoluteFilePath = MAIN_APP->getLocalSyncPath();
//				absoluteFilePath += '/';
//				absoluteFilePath += tempFileListing[i];
//
//				tempAttr = MAIN_APP->getFileAttrs(i);
//
//				/* make sure it's not a directory */
//				if (tempAttr.objtype != OrangeFS_TYPE_DIRECTORY)
//				{
//					/* create the file here so we can set the file attributes next */
//					fileHandle = CreateFile(absoluteFilePath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
//
//					/* now set all the associated windows file attributes for the local copy */
//					MAIN_FRAME->setWinFileAttrs(tempAttr, fileHandle);
//					CloseHandle(fileHandle); 
//				}
//
//				/* reset the absolute file path string */
//				absoluteFilePath.clear();
//
//				/* store some of the file meta-data into their respective hash maps via the file's name for easier access later */
//				fileSize = tempAttr.size;
//
//				if (tempAttr.objtype == OrangeFS_TYPE_DIRECTORY)
//				{
//					MAIN_APP->syncDir(MAIN_APP->getFileName(i));
//
//					/* write the file metadata to the app XML file */
//					MAIN_APP->addMetadata(tempFileListing[i], &tempAttr);
//
//					/* now we need to recursively look inside this directory on the server to determine which files/dirs we need to create inside of it */
//					/** TODO **/
//					/***********/
//				}
//				else 
//				{
//					MAIN_APP->syncFile(MAIN_APP->getFileName(i), fileSize);
//
//					/* write the file metadata to the app XML file */
//					MAIN_APP->addMetadata(tempFileListing[i], &tempAttr);
//				}
//
//				/* manually tell the sync list to update after each added file/dir */
//				syncList->updateList(MAIN_APP->getFileName(i));
//			}
//		}
//		else
//		{
//				orangefs_debug_print("Failed to retrieve a file listing from the orangefs server.\nCheck orangefstab file and ensure settings are correct.\n");
//		}
//
//	/* let the app know we're finished syncing */
//	MAIN_APP->finishedSyncing(true);
//
//	for (int i=0; i < MAX_FILES; i++)
//	{
//		free(tempFileListing[i]);
//	}
//	free(tempFileListing);
//
//	orangefs_debug_print("---- Syncing Completed ----\n");
//
//	return ret;
//}
