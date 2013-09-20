#ifndef __METADATA_H
#define __METADATA_H


const wxString DATA_FILE_NAME = "METADATA.dat";

class MetadataHandler
{
public:
	static MetadataHandler* getInstance() { if (!instance) { instance = new MetadataHandler; } return instance; }

	~MetadataHandler() { free( DATA_FILE_PATH ); }

	const int getNumFiles() const { return num_files; }
	const int getFileIndex(const wxString& name) { return fileIndexes[name]; }
	const wxString& getFileName(const int index) { return fileNames[index]; }
	const OrangeFS_size& getFileSize(const wxString &name) { return fileSizes[name]; } 
	const OrangeFS_permissions& getFilePerms(const wxString &name) { return filePerms[name]; }
	const OrangeFS_time& getFileModTime(const wxString &name) { return fileModTimes[name]; }
	const OrangeFS_time& getFileCreateTime(const wxString &name) { return fileCreateTimes[name]; }
	const OrangeFS_time& getFileAccessTime(const wxString &name) { return fileAccessTimes[name]; }
	const OrangeFS_ds_type& getFileType(const wxString &name) { return fileTypes[name]; }
	const map<int, wxString> getFileNames() const { return fileNames; }
	const map<wxString, OrangeFS_size> getFileSizes() const { return fileSizes; }
	const wxString& getDataFilePath() const { return DATA_FILE_PATH; } 

	void addRemoteToRuntime(wxString &name, int index);
	int& getRemoteFileIndex(wxString &name) { return remoteFilesByName[name]; }
	wxString& getRemoteFileName(int &index) { return remoteFilesByIndex[index]; }

	bool isLocalOnServer(const wxString &name);
	bool isServerOnLocal(const wxString &name);

	bool isDir(const wxString &fileName) { return ( (fileTypes[fileName] == OrangeFS_TYPE_DIRECTORY) ? 1 : 0 ); }

	void addFileMetadata(const wxString &filename, const OrangeFS_attr *fileattr);
	void updateFileMetadata(const wxString &name, const OrangeFS_attr *attr);
	void parseDataFile(const wxString &path);
	void removeFile(const wxString &file);

private:
	MetadataHandler();

	void removeFileMetadata(const wxString &name);

	static MetadataHandler *instance;	/* singleton instance */

	char *DATA_FILE_PATH;
	int num_files;
	int file_index;
	map<wxString, int> fileIndexes;
	map<int, wxString> fileNames;
	map<int, wxString> remoteFilesByIndex;
	map<wxString, int> remoteFilesByName;
	map<wxString, OrangeFS_ds_type> fileTypes;
	map<wxString, OrangeFS_size> fileSizes;
	map<wxString, OrangeFS_permissions> filePerms;
	map<wxString, OrangeFS_time> fileModTimes;
	map<wxString, OrangeFS_time> fileCreateTimes;
	map<wxString, OrangeFS_time> fileAccessTimes;
	FILE *fileHandle;
};

#endif