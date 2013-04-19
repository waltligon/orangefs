
#ifndef __LOCALCONFIG_H
#define __LOCALCONFIG_H

#ifdef WIN32
const wxString CONFIG_FILE_NAME = "appConfig.txt";
#else
const wxString CONFIG_FILE_NAME = ".appConfig";	
#endif

/* this class is designed to be an array of LocalConfig's */
/* you will have a local config for each file system the client is subscribed to */
/* TODO : move this config to the registry on windows */
class LocalConfig
{
public:
	LocalConfig() { 
		fileSystemName = "";
		syncConfigPath = NULL;
		fsSelection = "";
		localSyncPath = "";
		configFile = NULL;
	}
	~LocalConfig(void);
	bool load();
	void save();

	void setSyncPath(const wxString path) { this->localSyncPath = path; }
	void setFsSelection(const wxString selection) { this->fsSelection = selection; }

	wxString getSyncPath() const { return this->localSyncPath; }
	wxString getConfigPath() const { return syncConfigPath; }
	bool syncExists() 
	{
		if ( localSyncPath.size() )
			return true;
		else
			return false;
		return false;
	}

private:
	wxString fileSystemName;
	char *syncConfigPath;
	wxString fsSelection;
	wxString localSyncPath;
	FILE *configFile;
};

#endif