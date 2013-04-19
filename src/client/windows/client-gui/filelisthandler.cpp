
#include "main-app.h"

/* retrieve our global reference to the main window */
extern MainFrame *MAIN_FRAME;
extern MainApp *MAIN_APP;

Utils *utils = Utils::getInstance();

FileListHandler::FileListHandler(void) 
{
	wxImage::AddHandler( new wxXPMHandler );
	wxValidator validator;
	char *folderIconPath;
	char *fileIconPath;
	short int syncPanelWidth	= (MAIN_FRAME->getWinSize().GetWidth());
	short int syncPanelHeight	= (MAIN_FRAME->getWinSize().GetHeight() - 80);
	short int syncPanelStartX	= 0;
	short int syncPanelStartY	= 0;

	folderIconPath = (char *) malloc(MAX_PATH);
	strcpy(folderIconPath, FOLDER_ICON_NAME);
	MAIN_APP->getAbsoluteRunningPath( folderIconPath );

	fileIconPath = (char *) malloc(MAX_PATH);
	strcpy(fileIconPath, FILE_ICON_NAME);
	MAIN_APP->getAbsoluteRunningPath( fileIconPath );

	this->fileIndexCounter = 0;

	this->syncPane = new wxPanel(MAIN_FRAME);
	this->syncPane->SetSize( wxSize(syncPanelWidth, syncPanelHeight) );
	this->syncPane->SetDoubleBuffered(false);	/* make sure the pane is NOT double buffered or the pane will display nothing when adding new columns */
	this->syncPane->SetLabel("Local Files");
	this->syncPane->SetPosition( wxPoint(syncPanelStartX, syncPanelStartY) );

	this->fileIcons = new wxImageList(16, 16, true);
	this->fileIcons->Add( wxIcon(folderIconPath, wxBITMAP_TYPE_XPM), wxBITMAP_TYPE_XPM);
	this->fileIcons->Add( wxIcon(fileIconPath, wxBITMAP_TYPE_XPM), wxBITMAP_TYPE_XPM);

	/* create a list box and populate it with the retrieved file listing */
	this->syncList = new wxListCtrl(this->syncPane,
									    LIST_CTRL_SYNC,											/* window id */
										wxPoint(syncPanelStartX, syncPanelStartY),				/* position */
										wxSize(syncPanelWidth - 10, syncPanelHeight),				/* size */
										wxLC_REPORT | wxLC_VRULES ,								/* window style */
										validator,												/* window validator */
										"Local Files");											/* list control name */

	/* now that we have our list of images to populate the file and folder icons, register it with the list control as small icons */
	this->syncList->SetImageList(this->fileIcons, wxIMAGE_LIST_SMALL);

	free( fileIconPath );
	free( folderIconPath );
}

FileListHandler::~FileListHandler()
{
	delete this->syncList;
	delete this->syncPane;
	delete this->fileIcons;
}

void FileListHandler::addColumn(const wxString &name)
{
	MetadataHandler *metadataHandler = MetadataHandler::getInstance();
	wxListItem col;	/* column to create */

	col.SetId(this->syncList->GetColumnCount());
	col.SetText(name);
	col.SetMask( wxLIST_MASK_STATE | wxLIST_MASK_TEXT );
	col.SetStateMask( wxLIST_STATE_SELECTED );
	col.SetWidth(this->syncPane->GetSize().x / (this->syncList->GetColumnCount()+1));
	this->syncList->InsertColumn(this->syncList->GetColumnCount(), col);	/* add it to the end of the current columns */

	/* now add the column name and associated column ID to the map */
	this->syncColumnIDs[name] = this->syncList->GetColumnCount()-1;

		if ( !name.compare("File Size") )
		{
			for (int i=0; i < metadataHandler->getNumFiles(); i++)
			{
				OrangeFS_size tempSize = metadataHandler->getFileSize(metadataHandler->getFileName(i));	
				wxString data = utils->getPrefixString( tempSize );
				this->syncList->SetItem( i, this->syncColumnIDs[name], data );
			}
		}
		if ( !name.compare("Permissions") )
		{
			for (int i=0; i < metadataHandler->getNumFiles(); i++)
			{
				OrangeFS_permissions tempPerms = metadataHandler->getFilePerms(metadataHandler->getFileName(i));
				wxString data = utils->getPermString(tempPerms, metadataHandler->isDir(metadataHandler->getFileName(i)));	
				this->syncList->SetItem( i, this->syncColumnIDs[name], data );
			}
		}
		if ( !name.compare("Last Modified") )
		{
			for (int i=0; i < metadataHandler->getNumFiles(); i++)
			{
				OrangeFS_time tempTime = metadataHandler->getFileModTime(metadataHandler->getFileName(i));
				wxString data = utils->getTimeString(tempTime);
				this->syncList->SetItem( i, this->syncColumnIDs[name], data );
			}
		}

	/* new go through the other existing column(s) and resize accordingly */
	for (int i=0; i < this->syncList->GetColumnCount(); i++)
	{
		this->syncList->SetColumnWidth(i, this->syncPane->GetSize().x / this->syncList->GetColumnCount());	/* set to panel size / number of columns */
	}
}

void FileListHandler::removeColumn(const wxString &name) 
{
	int delID;	/* column ID to be deleted */

	/* get the unique ID of the column to be removed */
	delID = this->syncColumnIDs[name];

	if (this->syncColumnIDs.find(name) == this->syncColumnIDs.end())	/* wasn't found in the map (sanity checking here) */
	{
		orangefs_debug_print("ERROR: unable to remove column: %s\n", name);
		return;
	}

	if (this->syncList->DeleteColumn( delID ))	/* sanity checking */
	{
		this->syncColumnIDs.erase(name);	/* also remove the column name and ID from the map */
	}
	else	/* wasn't removed successfully */
	{
		orangefs_debug_print("ERROR: couldn't remove column: %s\n", name);
		return;
	}

	/* new go through the other existing column(s) and resize */
	for (int i=0; i < this->syncList->GetColumnCount(); i++)
	{
		this->syncList->SetColumnWidth(i, this->syncPane->GetSize().x / this->syncList->GetColumnCount());	
	}

	/* update columnIDs explicitly after one was removed */
	if (this->syncColumnIDs.count("File Size") > 0)	/* will be > 0 if the key exists in the map */
	{
		if (delID < this->syncColumnIDs["File Size"])	/* the column being deleted was to the left of this column, decrement the columnID */
		{
			this->syncColumnIDs["File Size"] -= 1;
		}
	}
	if (this->syncColumnIDs.count("Permissions") > 0)
	{
		if (delID < this->syncColumnIDs["Permissions"])
		{
			this->syncColumnIDs["Permissions"] -= 1;
		}
	}
	if (this->syncColumnIDs.count("Last Modified") > 0)
	{
		if (delID < this->syncColumnIDs["Last Modified"])
		{
			this->syncColumnIDs["Last Modified"] -= 1;
		}
	}
}

void FileListHandler::updateList(wxString &name)
{
	/* iterate through each column and update the new information to be displayed */
	map<wxString,int>::iterator columnIterator;

	/* after we sync each file, we will update each currently visible column here with their appropriate data for that file */
	for ( columnIterator=this->syncColumnIDs.begin(); columnIterator != this->syncColumnIDs.end(); columnIterator++ )
	{
		this->addListData(const_cast<wxString &>((*columnIterator).first), name);
	}
}

void FileListHandler::addListData(wxString &colName, wxString &fileName)
{
	static long rowIndex;	/* will be the unique application provided index for setting column data */
	MetadataHandler *metadataHandler = MetadataHandler::getInstance();

	/* fill the columns with the appropriate data */
	if (!colName.compare("File Name"))	
	{	
		/* if it's a directory object, show the folder icon, otherwise, show the file icon */
		if (metadataHandler->isDir(fileName))
		{
			rowIndex = this->syncList->InsertItem( this->fileIndexCounter++, fileName, 0);	/* the 0 here specifies the directory icon */
			this->syncListIndexes[fileName] = rowIndex;		/* store the unique row index by file name in our map for setting column data later */
		}
		else
		{
			rowIndex = this->syncList->InsertItem( this->fileIndexCounter++, fileName, 1);	/* the 1 here specifies the file icon */
			this->syncListIndexes[fileName] = rowIndex;		
		}
	}
	else if (!colName.compare("File Size"))
	{
		/* populate the file size column with the "size" file attributes */
		OrangeFS_size tempSize = metadataHandler->getFileSize(fileName);	/* get size of file in bytes */
		wxString prefixString = utils->getPrefixString( tempSize );	/* get the prefix size (i.e. KB, MB, GB, etc.), and set tempSize to value less than 1000 */

		//this->syncList->SetItem( this->syncListIndexes[fileName], this->syncColumnIDs["File Size"], prefixString);
		this->syncList->InsertItem(this->fileIndexCounter++, fileName);
	}
	else if (!colName.compare("Permissions"))
	{
		/* populate the permissions column with the "OrangeFS_permissions attribute */
		wxString tempPerms = utils->getPermString(metadataHandler->getFilePerms(fileName), metadataHandler->isDir(fileName));
	
		this->syncList->SetItem( MAIN_APP->getFileIndex(fileName), this->syncColumnIDs["Permissions"], tempPerms);
	}
	else if (!colName.compare("Last Modified"))
	{
		/* populate the permissions column wtih the "mtime" attribute */
		wxString tempTime = utils->getTimeString(metadataHandler->getFileModTime(fileName));

		this->syncList->SetItem( MAIN_APP->getFileIndex(fileName), this->syncColumnIDs["Last Modified"], tempTime);
	}
}

void FileListHandler::removeListData(const wxString &filename)
{
	syncList->DeleteItem(syncListIndexes[filename]);
}