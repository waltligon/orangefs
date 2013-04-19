
/* This program is currently using version 2.8.12 (Stable) of the wxWidgets API */
/* This version was released in March 2011 */
/* The "trunk" version has documentation updated in July 2012 */
/* The version before trunk was 2.9.5 */

#ifndef __MAIN_APP_H
#define __MAIN_APP_H

/*****************************/
/* for memory leak detection */
/*****************************/
#include "memLeaks.h"
#include <vld.h>
/*****************************/

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/listbase.h>
#include <cstring>
#include <sstream>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <map>
#include <orangefs-client.h>
#include "filelisthandler.h"
#include "localConfig.h"
#include "syncDialog.h"
#include "metadata.h"
#include "dirListener.h"
#include "utils.h"

#ifdef WIN32
	#include <Windows.h>
	#include <atltime.h>
#endif

#ifndef WIN32
	#include <unistd.h>
	#define MAX_PATH 255
#endif

using std::cout;
using std::endl;
using std::cerr;
using std::map;
using std::stringstream;
using std::vector;

const OrangeFS_ds_position ORANGEFS_READDIR_START = INT32_MAX - 1;
const int MAX_FILES		= 256;

typedef struct
{
		OVERLAPPED overlapped;
		wchar_t *buffer;
}AsynchronousData;

enum SYNC_TYPE
{
	SYNC_NEW = 1,
	SYNC_EXISTING = 2
};

enum ID
{
	ID_FILE_QUIT = 1,
	ID_FILE_ABOUT = 2, 
	ID_VIEW_PERMISSIONS = 3,
	ID_VIEW_SIZE = 4,
	ID_VIEW_MODIFIED = 5,
	ID_SETTINGS_CONFIG = 6,
	LIST_CTRL_REMOTE = 7,
	ID_NEW_SYNC = 8,
	ID_SYNC_DIALOG = 9,
	ID_FS_SELECTOR = 10,
	ID_BROWSE_BUTTON = 11,
	ID_SYNC_ALL_BUTTON = 12,
	ID_SELECT_SYNC_BUTTON = 13
};

class MainApp: public wxApp
{
public:
	virtual bool OnInit();
	void initFileSystem();
	void allocateMembers();
	void cleanupApp();

	void printLastWinError();

	short int getNumFiles() const { return num_files; }
	wxString getFileName(int index) const { return fileListing[index]; }
	wxArrayString getFileSysNames() const { return fsNames; }
	OrangeFS_attr getFileAttrs(int index) const { return rootSysAttr[index]; }
	short int getScreenHeight() const { return screen_height; }
	short int getScreenWidth() const { return screen_width; }
	wxString getLocalSyncPath() const { return localConfig->getSyncPath(); }
	HANDLE getThreadHandle() const { return syncThread; }
	OrangeFS_mntent* getMntentAt(int index) { return mntents[0]; }
	OrangeFS_credential* getCred() const { return rootCred; }
	OrangeFS_attr* getAttr() const { return rootSysAttr; }
	void finishedSyncing(bool done) { synced = done; }
	bool getSyncStatus() const { return synced; }
	int getFileIndex(const wxString &name) { return fileIndexes[name]; }
	void getAbsoluteRunningPath(char *logoName);
	bool stillUpdating() const { return endUpdating; }

	void setLocalSyncPath(const wxString path) { localConfig->setSyncPath(path); }
	void setNumFiles(int num) { num_files = num; }
	void setFsSelection(const wxString selection) { localConfig->setFsSelection(selection); }
	void setThreadHandle(HANDLE thread) { syncThread = thread; }
	void setThreadFlag(bool flag) { threadRun = flag; } 

	bool getThreadFlag() const { return threadRun; }

	bool syncExists() 
	{
		if ( localConfig->syncExists() )
			return true;
		else
			return false;
		return false;
	}

	void saveConfig() { localConfig->save(); }
	void syncDir(const wxString &dir);
	void syncFile(const wxString &file, const OrangeFS_size &size);

	void stopUpdating() { endUpdating = true; }

	void addFileToListing(char *file) { fileListing.Add(file, 1); }
	void addFileIndex(wxString &name, int index) { fileIndexes.insert(std::make_pair(name, index)); }
	void addMetadata(const wxString &filename, const OrangeFS_attr *fileAttr) { metadataHandler->addFileMetadata(filename, fileAttr); }

private:
	wxArrayString fileListing;
	wxArrayString fsNames;

	bool threadRun;

	OrangeFS_credential *rootCred;
	LocalConfig *localConfig;
	OrangeFS_attr *rootSysAttr;
	OrangeFS_mntent **mntents;			
	short int num_files;
	short int screen_height;
	short int screen_width;
	bool synced;
	bool endUpdating;
	map<wxString, int> fileIndexes;
	MetadataHandler *metadataHandler;

#ifdef WIN32
	HANDLE syncThread;
	HANDLE updateThread;
#endif
};

class MainFrame: public wxFrame
{
public:
	MainFrame(const wxString &title, const wxPoint &pos, const wxSize &size);
	MainFrame(const MainFrame& mf);
	MainFrame& operator=(const MainFrame &mf);
	~MainFrame();
	
	/* top menu bar */
	wxMenuBar *mainMenuBar;
	wxMenu *file;
	wxMenu *view;
	wxMenu *settings;
	wxMenu *newSync;

	wxSize windowSize;
	FileListHandler *fileHandler;
	SyncDialog *newSyncDialog;

	void onQuit(wxCommandEvent &event);
	void onAbout(wxCommandEvent &event);
	void showConfigDialog(wxCommandEvent &event);
	void showNewSyncDialog(wxCommandEvent &event);
	void showPermissions(wxCommandEvent &event);
	void showFileSize(wxCommandEvent &event);
	void showLastModified(wxCommandEvent &event);
	wxSize getWinSize() const { return windowSize; }
	void setStatusbarText(wxString &string) { this->SetStatusText(string); }
	void onBrowseButtonClicked(wxCommandEvent &event);
	void addLocalFileToSyncList(wxString &colName, wxString &name) { fileHandler->addListData(colName, name); }
	void setWinFileAttrs(OrangeFS_attr attr, HANDLE handle) { newSyncDialog->setWinFileAttrs(attr, handle); }

	FileListHandler* getListHandler() { return fileHandler->getInstance(); }

	DECLARE_EVENT_TABLE()
};

#endif