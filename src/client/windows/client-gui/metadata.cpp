#include "main-app.h"

#define BUFFERSIZE		512
#define COPYBUFFSIZE	1048576		/* 1 MB */

typedef struct
{
	char *fName;
	OrangeFS_attr *attr;
}MetadataRecord;

extern MainApp *MAIN_APP;

void *memcat(void *parm1, size_t size1, void *parm2, size_t size2);

MetadataHandler::MetadataHandler() : num_files(0), file_index(0)
{
	DATA_FILE_PATH = (char *) malloc(MAX_PATH);
	strcpy(DATA_FILE_PATH, DATA_FILE_NAME);
	MAIN_APP->getAbsoluteRunningPath( DATA_FILE_PATH );

	parseDataFile(DATA_FILE_PATH);
}

void MetadataHandler::parseDataFile(const wxString &path)
{
#ifndef WIN32
	if ( access(DATA_FILE_PATH.c_str(), F_OK) == -1 )
#else
	if ( _access(DATA_FILE_PATH, 0) == -1 )
#endif
	{
		/* it doesn't exist, so create it and return for next time */
		fileHandle = fopen(DATA_FILE_PATH, "wb");
		fclose(fileHandle);

		orangefs_debug_print("No existing metadata file. Creating one.\n");

		/* if a sync configuration exists, we can populate the data file here, if not, we'll wait until one is created (this is done in the main update loop) */
		if ( MAIN_APP->syncExists() )
		{
			HANDLE hDir = INVALID_HANDLE_VALUE;
			WIN32_FIND_DATA fileData;

			hDir = FindFirstFile( MAIN_APP->getLocalSyncPath().c_str(), &fileData );
			if ( hDir == INVALID_HANDLE_VALUE )
			{
				orangefs_debug_print("ERROR listing contents of directory : %s\n", MAIN_APP->getLocalSyncPath().c_str());
				MAIN_APP->printLastWinError();
				return;
			}

			do
			{
				/* directory */
				if ( fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
				{

				}
				else
				{
					wxString fileName = fileData.cFileName;
					OrangeFS_attr attr;
					LARGE_INTEGER fileSize;
					Utils *utils = Utils::getInstance();

					fileSize.LowPart = fileData.nFileSizeLow;
					fileSize.HighPart = fileData.nFileSizeHigh;

					attr.atime = utils->winFiletimeToOrangeFS(fileData.ftLastAccessTime);
					attr.mtime = utils->winFiletimeToOrangeFS(fileData.ftLastWriteTime);
					attr.ctime = utils->winFiletimeToOrangeFS(fileData.ftCreationTime);
					attr.objtype = OrangeFS_TYPE_NONE;
					attr.size = fileSize.QuadPart;
					attr.perms = (OrangeFS_permissions) 0775;

					MAIN_APP->addMetadata(fileName, &attr);
				}
			} while( FindNextFile(hDir, &fileData) != 0 );
		}

		return;
	}

	char name_buffer[BUFFERSIZE];
	OrangeFS_attr *tempAttr = (OrangeFS_attr *) malloc(sizeof(OrangeFS_attr));

	fileHandle = fopen(DATA_FILE_PATH, "rb");

	orangefs_debug_print("Found existing metadata file.\n");

	while (1)
	{
		if ( !fgets(name_buffer, BUFFERSIZE, fileHandle) )
			break;

		wxString tempName = name_buffer;
		fread(tempAttr, sizeof(OrangeFS_attr), 1, fileHandle);

		fileNames.insert(std::make_pair(num_files, tempName));
		fileIndexes.insert(std::make_pair(tempName, num_files));

		num_files++;

		fileTypes.insert(std::make_pair(name_buffer, tempAttr->objtype));
		fileSizes.insert(std::make_pair(name_buffer, tempAttr->size));
		filePerms.insert(std::make_pair(name_buffer, tempAttr->perms));
		fileModTimes.insert(std::make_pair(name_buffer, tempAttr->mtime));
		fileCreateTimes.insert(std::make_pair(name_buffer, tempAttr->ctime));
		fileAccessTimes.insert(std::make_pair(name_buffer, tempAttr->atime));

		memset(tempAttr, 0, sizeof(OrangeFS_attr));
	}
	free(tempAttr);
	fclose(fileHandle);
}

void MetadataHandler::addFileMetadata(const wxString &name, const OrangeFS_attr *attr)
{
	fileHandle = fopen(DATA_FILE_PATH, "ab");

	fileIndexes.insert(std::make_pair(name, file_index));
	fileNames.insert(std::make_pair(file_index++, name));
	fileTypes.insert(std::make_pair(name, attr->objtype));
	fileSizes.insert(std::make_pair(name, attr->size));
	filePerms.insert(std::make_pair(name, attr->perms));
	fileModTimes.insert(std::make_pair(name, attr->mtime));
	fileCreateTimes.insert(std::make_pair(name, attr->ctime));
	fileAccessTimes.insert(std::make_pair(name, attr->atime));

	wxString newlineName = name;
	newlineName += '\n';
	fwrite(newlineName.c_str(), 1, (size_t) newlineName.length(), fileHandle);
	fwrite(attr, sizeof(OrangeFS_attr), 1, fileHandle);

	fclose(fileHandle);
}

void MetadataHandler::addRemoteToRuntime(wxString &name, int index)
{
	if ( !index )	/* we're adding a new set of remote files for a new update iteration, clear the maps */
	{
		remoteFilesByIndex.clear();
		remoteFilesByName.clear();
		remoteFilesByIndex[index] = name;
		remoteFilesByName[name] = index;
	}
	else
	{
		remoteFilesByIndex[index] = name;
		remoteFilesByName[name] = index;
	}
}

/* returns true if the remote server file was found in the local sync path (useful in checking for files created on the server) */
bool MetadataHandler::isServerOnLocal(const wxString &name)
{
	map<wxString, OrangeFS_size>::const_iterator it = fileSizes.find(name);

	/* the remote file was not found locally, the remote file will need to be created/synced locally */
	if (it == fileSizes.end())
	{
		return false;
	}
	else
	{
		return true;
	}
	return false;
}

/* returns true if the file synced locally is on the server (useful in checking for files deleted on the server) */
bool MetadataHandler::isLocalOnServer(const wxString &name)
{
	map<wxString, int>::const_iterator it = remoteFilesByName.find(name);
	
	/* the local file was not found on the server, the local file will need to be deleted */
	if (it == remoteFilesByName.end())
	{
		orangefs_debug_print("Local file not found on server.\nDeleting : %s\n", name.c_str());
		return false;
	}
	else
	{
		orangefs_debug_print("Local file found on server.\nMoving on...\n");
		return true;
	}
	return false;
}

/* the idea of this routine is to go through the metadata file until we find the filename 
 * pointed to by the "name" parameter, then copy over the "attr" parameter's data to the
 * data immediately following the filename; we will also update the local maps */
void MetadataHandler::updateFileMetadata(const wxString &name, const OrangeFS_attr *attr)
{
	void *buffer;
	size_t bytesReturned = 0;
	int count = 0;

	buffer = malloc(BUFFERSIZE);

	fileSizes[name] = attr->size;
	filePerms[name] = attr->perms;
	fileModTimes[name] = attr->mtime;
	fileAccessTimes[name] = attr->atime;

	fileHandle = fopen(DATA_FILE_PATH, "r+b");

	while ( (bytesReturned = fread(buffer, 1, strlen(fileNames[count++]), fileHandle)) )
	{
		/* we have found the file name, now copy over the attr data */
		if ( !strncmp((char *) buffer, name, strlen(name)) )
		{
			fwrite(attr, sizeof(OrangeFS_attr), 1, fileHandle);
			goto cleanup;
		}
		else
		{
			/* skip through the file attributes (plus the newline char) to the next file name */
			fseek(fileHandle, sizeof(OrangeFS_attr)+1, SEEK_CUR);
		}
	}

cleanup:
	fclose(fileHandle);
	free(buffer);
}

void MetadataHandler::removeFile(const wxString &file)
{
	removeFileMetadata(file);
	if ( fileSizes[file] )
		fileSizes.erase(file);
	if ( fileNames[fileIndexes[file]] )
		// fileNames.erase(fileIndexes[file]);		/* remove from here before fileIndexes while we can still access the index */
	if ( fileIndexes[file] )
		fileIndexes.erase(file);
	if ( fileTypes[file] )
		fileTypes.erase(file);
	if ( filePerms[file] )
		filePerms.erase(file);
	if ( fileModTimes[file] )
		fileModTimes.erase(file);
	if ( fileCreateTimes[file] )
		fileCreateTimes.erase(file);
	if ( fileAccessTimes[file] )
		fileAccessTimes.erase(file);
}

void MetadataHandler::removeFileMetadata(const wxString &file)
{
	void *copyBuffer, *buffer;
	FILE *newFile;
	fpos_t deletePos;
	size_t bytesReturned = 1;
	int count, numRecords = 0;
	wxString newFileName = DATA_FILE_PATH;
	MetadataRecord *tempRecord;
	newFileName += ".save";

	tempRecord = (MetadataRecord *) malloc(sizeof(MetadataRecord));
	tempRecord->fName = (char *) malloc(BUFFERSIZE);
	tempRecord->attr = (OrangeFS_attr *) malloc(sizeof(OrangeFS_attr));
	copyBuffer = malloc(COPYBUFFSIZE);
	buffer = malloc(BUFFERSIZE);

	fileHandle = fopen(DATA_FILE_PATH, "r+b");
	newFile = fopen(newFileName.c_str(), "wb");

	while ( bytesReturned )
	{
		memset(buffer, 0, BUFFERSIZE);
		bytesReturned = fread(buffer, 1, strlen(fileNames[count]), fileHandle);

		/* we have found the file to remove */
		size_t checkSize = (strlen(fileNames[count]) > strlen(file)) ? strlen(fileNames[count]) : strlen(file);		/* will scan through the larger filename to ensure uniqueness */
		if ( !strncmp((char *) buffer, file, checkSize) )
		{
				/* now skip over copying it into the buffer */
				fseek(fileHandle, sizeof(OrangeFS_attr)+1, SEEK_CUR);
		}

		/* keep building up the copy buffer and dump once at capacity (or eof) */
		if ( strlen((char *) copyBuffer) < COPYBUFFSIZE )
		{
			memset(tempRecord, 0, sizeof(MetadataRecord));
			strcpy(tempRecord->fName, (char *) buffer);

			memset(buffer, 0, BUFFERSIZE);
			fread(buffer, sizeof(OrangeFS_attr), 1, fileHandle);
			memcpy(tempRecord->attr, buffer, sizeof(OrangeFS_attr));

			/* then add the metadata record to the copyBuffer */
			memcat(copyBuffer, sizeof(MetadataRecord) * numRecords++, tempRecord, sizeof(MetadataRecord));
		}
		else
		{
			/* dump the buffer and zero it out */
			fwrite(copyBuffer, 1, COPYBUFFSIZE, newFile);
			memset(copyBuffer, 0, COPYBUFFSIZE);
			numRecords = 0;
		}

		++count;
	}

	/* now, delete the old data file and rename the new one */
#ifdef WIN32
				DeleteFile(DATA_FILE_PATH);
				MoveFile(newFileName.c_str(), DATA_FILE_PATH);
#elif
				remove(DATA_FILE_PATH.c_str());
				rename(newFileName.c_str(), DATA_FILE_PATH.c_str());
#endif

cleanup:
	free(copyBuffer);
	free(buffer);
	free(tempRecord->fName);
	free(tempRecord->attr);
	free(tempRecord);
	fclose(fileHandle);
	fclose(newFile);
}

/* util routine for buffer concatenation */
void *memcat(void *parm1, size_t size1, void *parm2, size_t size2) {
	memcpy( (char*)parm1 + size1, parm2, size2);
	return parm1;
}