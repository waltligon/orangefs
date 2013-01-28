
#include "main-app.h"

/* retrieve our global reference to the main window */
extern MainFrame *MAIN_FRAME;
extern MainApp *MAIN_APP;

FileListHandler::FileListHandler(void)
{
	wxValidator validator;
	short int syncPanelWidth	= (MAIN_FRAME->getWinSize().GetWidth());
	short int syncPanelHeight	= (MAIN_FRAME->getWinSize().GetHeight() - 80);
	short int syncPanelStartX	= 0;
	short int syncPanelStartY	= 0;

	this->syncPane = new wxPanel(MAIN_FRAME);
	this->syncPane->SetSize( wxSize(syncPanelWidth, syncPanelHeight) );
	this->syncPane->SetDoubleBuffered(false);	/* make sure the pane is NOT double buffered or the pane will display nothing when adding new columns */
	this->syncPane->SetLabel("Local Files");
	this->syncPane->SetPosition( wxPoint(syncPanelStartX, syncPanelStartY) );

	/* create a list box and populate it with the retrieved file listing */
	this->syncList = new wxListCtrl(this->syncPane,
									    LIST_CTRL_SYNC,											/* window id */
										wxPoint(syncPanelStartX, syncPanelStartY),				/* position */
										wxSize(syncPanelWidth - 10, syncPanelHeight),				/* size */
										wxLC_REPORT | wxLC_VRULES,								/* window style */
										validator,												/* window validator */
										"Local Files");											/* list control name */
}

FileListHandler::~FileListHandler()
{
	delete this->syncList;
	delete this->syncPane;
}

/* function to take the file size in bytes and return
 * a string containing the new file size > 1024 with 
 * the associated storage size */
wxString getPrefixString(OrangeFS_size &size)
{
	const wxString prefixes[4] = { " KB" , " MB", " GB" , " TB" };
	wxString size_string = " bytes";
	wxString retString;
	short int count = 0;
	stringstream sizeStream;
	float newSize = (float) size;

	while (1)
	{
		if ((newSize / 1024.0) > 1.0)
		{
			size_string = prefixes[count];
			count++;
			newSize /= 1024;
		}
		else
		{
			break;
		}
	}

	sizeStream << newSize;
	retString += sizeStream.str();
	retString += size_string;

	return retString;
}

/* function to construct the Linux-style file permissions
 * string given an OrangeFS_permissions value */
wxString getPermString(OrangeFS_attr &attr)	
{																
	OrangeFS_permissions tempPerm = attr.perms;
	wxString retString;

	/* first, set the first character with the file type */
	if (attr.objtype == OrangeFS_TYPE_DIRECTORY)
	{
		retString += 'd';
	}
	else if (attr.objtype == OrangeFS_TYPE_SYMLINK)
	{
		retString += 'l';
	}
	else
	{
		retString += '-';
	}

	/* zero out the bits we're not interested in before each set */

	/* owner perms */
	tempPerm &= 0x000001ff;
	retString += (((tempPerm >> 6) & 4) == 4) ? 'r' : '-';
	retString += (((tempPerm >> 6) & 2) == 2) ? 'w' : '-';
	retString += (((tempPerm >> 6) & 1) == 1) ? 'x' : '-';

	/* group perms */
	tempPerm &= 0x0000003f;
	retString += (((tempPerm >> 3) & 4) == 4) ? 'r' : '-';
	retString += (((tempPerm >> 3) & 2) == 2) ? 'w' : '-';
	retString += (((tempPerm >> 3) & 1) == 1) ? 'x' : '-';

	/* other perms */
	tempPerm &= 0x00000007;
	retString += ((tempPerm & 4) == 4) ? 'r' : '-';
	retString += ((tempPerm & 2) == 2) ? 'w' : '-';
	retString += ((tempPerm & 1) == 1) ? 'x' : '-';

	return retString;
}

wxString getTimeString(OrangeFS_attr &attr)
{
	wxString retString;
	time_t binTime = attr.mtime;

	retString = ctime(&binTime);
	return retString;
}

void FileListHandler::addColumn(const wxString &name)
{
	wxListItem col;	/* column to create */

	col.SetId(this->syncList->GetColumnCount());
	col.SetText(name);
	col.SetMask( wxLIST_MASK_STATE | wxLIST_MASK_TEXT );
	col.SetStateMask( wxLIST_STATE_SELECTED );
	col.SetTextColour(*wxRED);
	col.SetWidth(this->syncPane->GetSize().x / (this->syncList->GetColumnCount()+1));
	this->syncList->InsertColumn(this->syncList->GetColumnCount(), col);	/* add it to the end of the current columns */

	/* now add the column name and associated column ID to the map */
	this->syncColumnIDs[name] = this->syncList->GetColumnCount()-1;

	/* fill the columns with the appropriate data */
	if (!name.compare("File Name"))	/* will be the first column populated: done on app startup */
	{
		for (int i = MAIN_APP->getNumFiles()-1; i>=0; i--)	/* start at the last file first because the list will be populated by pushing files down */
		{
			this->syncList->InsertItem( this->syncColumnIDs["File Name"], MAIN_APP->getFileName(i) );	/* put the file name in the first column */
		}
	}
	else if (!name.compare("File Size"))
	{
		/* populate the file size column with the "size" file attributes */
		for (int i=0; i < MAIN_APP->getNumFiles(); i++)	
		{
			OrangeFS_size tempSize = MAIN_APP->getFileAttrs(i).size;	/* get size of file in bytes */

			wxString prefixString = getPrefixString( tempSize );	/* get the prefix size (i.e. KB, MB, GB, etc.), and set tempSize to value less then 1000 */

			this->syncList->SetItem( i, this->syncColumnIDs["File Size"], prefixString);
		}
	}
	else if (!name.compare("Permissions"))
	{
		/* populate the permissions column with the "OrangeFS_permissions attribute */
		for (int i=0; i < MAIN_APP->getNumFiles(); i++)
		{
			OrangeFS_permissions tempPerms = MAIN_APP->getFileAttrs(i).perms;

			/* construct the Linux-style file permissions string from the 32-bit int value */
			wxString permString = getPermString(MAIN_APP->getFileAttrs(i));		
			this->syncList->SetItem( i, this->syncColumnIDs["Permissions"], permString);
		}
	}
	else if (!name.compare("Last Modified"))
	{
		/* populate the permissions column wtih the "mtime" attribute */
		for (int i=0; i < MAIN_APP->getNumFiles(); i++)
		{
			OrangeFS_time tempTime = MAIN_APP->getFileAttrs(i).mtime;

			wxString timeString = getTimeString(MAIN_APP->getFileAttrs(i));
			this->syncList->SetItem( i, this->syncColumnIDs["Last Modified"], timeString);
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

bool FileListHandler::getSyncStatus(int index) const
{
	/* TODO */
	return false;
}
