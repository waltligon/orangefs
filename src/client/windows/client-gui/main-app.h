
/* This program is currently using version 2.8.12 of the wxWidgets API */
/* This version was released in March 2011 */
/* The "trunk" version has documentation updated in July 2012 */
/* The version before trunk was 2.9.5 */

#ifndef __MAIN_APP_H
#define __MAIN_APP_H

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/listbase.h>
#include <cstring>
#include <orangefs-client.h>
#include "filelisthandler.h"
 
enum ID
{
	ID_FILE_QUIT = 1,
	ID_FILE_ABOUT = 2, 
	ID_VIEW_PERMISSIONS = 3,
	ID_VIEW_SIZE = 4,
	ID_VIEW_MODIFIED = 5,
	ID_SETTINGS_CONFIG = 6,
	LIST_CTRL_REMOTE = 7
};

class MainApp: public wxApp
{
public:
	virtual bool OnInit();
	void initFileSystem();
	void allocateMembers();
	void cleanupApp();
	short int getNumFiles() const { return this->num_files; }
	wxString getFileName(int index) const { return this->fileListing[index]; }
	OrangeFS_attr getFileAttrs(int index) const { return this->rootSysAttr[index]; }
	short int getScreenHeight() const { return this->screen_height; }
	short int getScreenWidth() const { return this->screen_width; }
private:
	wxArrayString fileListing;
	OrangeFS_credential *rootCred;
	OrangeFS_attr *rootSysAttr;
	OrangeFS_mntent **mntents;			/* TODO: create hash map for accessing file attributes given file name */
	short int num_files;
	short int screen_height;
	short int screen_width;
};

class MainFrame: public wxFrame
{
public:
	MainFrame(const wxString &title, const wxPoint &pos, const wxSize &size);
	MainFrame(const MainFrame& mf);
	MainFrame& operator=(const MainFrame &mf);
	~MainFrame();
	
	wxMenuBar *mainMenuBar;
	wxMenu *file;
	wxMenu *view;
	wxMenu *settings;
	wxSize windowSize;
	wxString localSyncPath;
	FileListHandler *fileHandler;

	void onQuit(wxCommandEvent &event);
	void onAbout(wxCommandEvent &event);
	void showConfigDialog(wxCommandEvent &event);
	void showPermissions(wxCommandEvent &event);
	void showFileSize(wxCommandEvent &event);
	void showLastModified(wxCommandEvent &event);
	wxSize getWinSize() const { return this->windowSize; }
	void setStatusbarText(wxString &string) { this->SetStatusText(string); }
	void onRemoteFileSelected(wxListEvent &event);

	DECLARE_EVENT_TABLE()
};

#endif