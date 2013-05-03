
#ifndef __FILELISTHANDLER_H
#define __FILELISTHANDLER_H

#include <map>
using std::map;

const wxWindowID LIST_CTRL_SYNC = 0x1;
const wxString FOLDER_ICON_NAME = "folder_icon.xpm";
const wxString FILE_ICON_NAME	= "file_icon.xpm";

/*
 * This class contains all the necessary implementation for modifying and instantiating the list view which displays the file information for the applicaion
 */
class FileListHandler: public wxListCtrl
{
public:
	static FileListHandler* getInstance() { if (!instance) { instance = new FileListHandler; } return instance; }

	~FileListHandler();

	void addColumn(const wxString &name);
	void removeColumn(const wxString &name);

	void displayFileStatus(wxCommandEvent &event);
	void processListCtrlDoubleClick(wxCommandEvent &event);

	void updateList(wxString &name);
	void addListData(wxString &colName, wxString &fileName);
	void removeListData(const wxString &filename);

private:
	FileListHandler();
	FileListHandler(const FileListHandler &) { }	/* copy constructor */
	FileListHandler& operator=(const FileListHandler &) { }	/* overloaded assignment operator */

	static FileListHandler *instance;	/* singleton instance */
	wxListCtrl *syncList;
	wxPanel *syncPane;
	wxImageList *fileIcons;
	map<wxString, int> syncColumnIDs;
	map<wxString, long> syncListIndexes;
	int fileIndexCounter;
};

#endif