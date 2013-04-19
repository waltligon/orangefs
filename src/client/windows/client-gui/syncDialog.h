
#ifndef __SYNCDIALOG_H
#define __SYNCDIALOG_H


class SyncDialog : public wxDialog
{
public:
	SyncDialog(wxWindow *, wxWindowID, const wxString &, const wxPoint &, const wxSize &, long, wxArrayString &);
	~SyncDialog();

	void onBrowseButtonClicked(wxCommandEvent &event);
	void onSyncAllButtonClicked(wxCommandEvent &event);
	void onSelectSyncButtonClicked(wxCommandEvent &event);

	void setWinFileAttrs(OrangeFS_attr &attrs, HANDLE file);

private:
	wxTextCtrl *filePathBox;
	wxComboBox *fsSelection;
	wxBoxSizer *mainLayout;
	wxStaticBoxSizer *fsSelectSizer;
	wxStaticBoxSizer *syncButtons;
	wxStaticBoxSizer *localPathSizer;
	FileListHandler *listHandler;
};

#endif