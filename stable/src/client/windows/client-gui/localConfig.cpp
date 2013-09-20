#include "main-app.h"

extern MainApp *MAIN_APP;


/* will return false if there isn't already a sync configuration, true if one already exists */
bool LocalConfig::load() 
{
	char buff[256];

	syncConfigPath = (char *) malloc(MAX_PATH);
	strcpy(syncConfigPath, CONFIG_FILE_NAME );
	MAIN_APP->getAbsoluteRunningPath( syncConfigPath );

	/* if it doesn't already exist, create it and return false because there's nothing to load */
#ifndef WIN32
	if ( access(syncConfigPath, F_OK) == -1 )
#else
	if ( _access(syncConfigPath, 0) == -1 )
#endif
	{
		this->configFile = fopen(syncConfigPath, "w");
		fclose(this->configFile);
		return false;
	}
	/* it exists, open it up */
	this->configFile = fopen(syncConfigPath, "r");

	if (fgets(buff, 256, this->configFile))
	{
		/* didn't find the first config element */
		if (strncmp(buff, "FS = ", 1) != 0)
			/* doesn't have any config data, do nothing and wait until a config is saved */
			return false;
		else
		{
			this->fsSelection = buff;
			this->fsSelection = this->fsSelection.substr(5);	/* start at the value of the fs selection */
		}
	}

	memset(buff, 0, 256);

	if (fgets(buff, 256, this->configFile))		/* will read up to a newline character */
	{
		this->localSyncPath = buff;		/* wxString assignment operator is overloaded to copy over instead of assign pointers */
		this->localSyncPath = this->localSyncPath.substr(13);	/* start at the value of the local sync path */
	}

	fclose(this->configFile);
	return true;
}

bool LocalConfig::syncExists() {
	if ( localSyncPath )
			return true;
		else
			return false;
		return false;
}

void LocalConfig::save() 
{
	this->configFile = fopen(syncConfigPath, "w");

	fprintf(this->configFile, "FS = %s\n", this->fsSelection);
	fprintf(this->configFile, "LOCAL_PATH = %s\n", this->localSyncPath);

	fclose(this->configFile);
}

LocalConfig::~LocalConfig(void)
{
	free(syncConfigPath);
}
