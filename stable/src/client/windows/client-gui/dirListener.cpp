#include "main-app.h"

extern MainApp *MAIN_APP;
extern MainFrame *MAIN_FRAME;

DWORD WINAPI monitorDirChanges(void *thisPointer);		/* the worker thread that monitors dirs */

DirHandler::DirHandler() : dirWatcher( NULL ) { }

DirHandler::~DirHandler() 
{
	unwatchDir();
	delete dirWatcher;
}

bool DirHandler::unwatchDir()
{
	// if ( dirWatcher )
		// return dirWatcher->unwatchDir( this );
	return true;
}

void DirHandler::onFileAdded(const char *fileName)
{
	OrangeFS_handle newFile = NULL;
	OrangeFS_size bytesWritten = 0;
	char *serverPath = (char *) malloc( MAX_PATH );
	char *localPath = (char *) malloc( MAX_PATH );

	memset(serverPath, 0, MAX_PATH);
	memset(localPath, 0, MAX_PATH);

	strcpy(localPath, MAIN_APP->getLocalSyncPath().c_str());
	strcat(localPath, "\\");
	strcat(localPath, fileName);
	localPath[ strlen(localPath) ] = '\0';
	/* opening file so we can get handle, then file size */
	FILE *tempHandle = fopen(localPath, "r");

	fseek(tempHandle, 0, SEEK_END);
	long fileSize = ftell(tempHandle);
	fseek(tempHandle, 0, SEEK_SET);

	wxString nameCol = "File Name";
	wxString sizeCol = "File Size";
	wxString nameVal = fileName;
	OrangeFS_attr attrVal;
	OrangeFS_time currTime = (OrangeFS_time) time( NULL );
	attrVal.atime = currTime;
	attrVal.ctime = currTime;
	attrVal.mtime = currTime;
	attrVal.objtype = OrangeFS_TYPE_NONE;
	attrVal.owner = 0;
	attrVal.perms = (OrangeFS_permissions) 0775;
	attrVal.size = (OrangeFS_size) fileSize;

	/* we add the new file to the metadataHandler, then add it to the list handler */
	MAIN_APP->addMetadata(nameVal, &attrVal);
	MAIN_FRAME->getListHandler()->addListData(nameCol, nameVal);


	orangefs_debug_print("FILE SIZE TO COPY TO SERVER : %ld bytes\n", fileSize);
	orangefs_debug_print("LOCAL FILE PATH : %s\n", localPath);

	/* put the new file at the root dir (for now, later we'll set the server sync path in the local config) */
	serverPath[0] = '/';
	strcat(serverPath, fileName);
	serverPath[ strlen(serverPath) ] = '\0';

	int ret = orangefs_create( &MAIN_APP->getMntentAt(0)->fs_id, MAIN_APP->getCred(), serverPath,  0x644, &newFile );

	/* we've created the file, now copy over the local contents into it */
	long bytesCopied = 0;
	char *buffer;				/* 256 KB buffer */
	OrangeFS_size buffSize = 262144;
	bool copy = true;

	buffer = (char *) malloc(262144);
	memset(buffer, 0, buffSize);

		while (copy)
		{
			/* remaining file data to read is less than buffer size, adjust read_size accordingly */
			if ((fileSize - bytesCopied) < buffSize)
			{
				buffSize = (fileSize - bytesCopied);
				fread(buffer, sizeof(char), buffSize, tempHandle);
				buffer[ strlen(buffer) ] = '\0';

				/* can't print out the buffer here because the internal gossip_debug_print() has a maximum internal buffer size of 1024 */
				// orangefs_debug_print("SMALL BUFFER of size : %d\nCONTENTS : %s\n", buffSize, buffer);
				orangefs_debug_print("SMALL BUFFER.\n");

				/* stop copying after this iteration */
				copy = !copy;
			}
			else
			{
				fread(buffer, sizeof(char), buffSize, tempHandle);
				orangefs_debug_print("BIG BUFFER\n.");
			}

			/* now copy the buffer to the server file */
			orangefs_write( &MAIN_APP->getMntentAt(0)->fs_id, MAIN_APP->getCred(), serverPath, buffer, buffSize, bytesCopied, &bytesWritten );
			bytesCopied += buffSize;

			orangefs_debug_print("WROTE : %d bytes for file : %s\n", bytesWritten, fileName);

			/* reset buffer */
			memset(buffer, 0, buffSize);
		}

	if ( tempHandle )
		fclose(tempHandle);
	if ( serverPath )
		free(serverPath);
	if ( localPath )
		free(localPath);
	if ( buffer )
		free(buffer);
}

void DirHandler::onFileRemoved(const char *fileName)
{
	char *serverPath = (char *) malloc( MAX_PATH );

	memset(serverPath, 0, MAX_PATH);

	serverPath[0] = '/';
	strcat(serverPath, fileName);
	serverPath[ strlen(fileName)+1 ] = '\0';

	orangefs_debug_print("DirHandler removing file : %s at server path : %s\n", fileName, serverPath);

	orangefs_remove( &MAIN_APP->getMntentAt(0)->fs_id, MAIN_APP->getCred(), serverPath );

	orangefs_debug_print("REMOVED FILE : %s\n", serverPath);

	// free(serverPath);
}

void DirHandler::onFileNameChanged(const char *fileName)
{
	orangefs_debug_print("---- File name was changed : %s\n", fileName);
}

void DirHandler::onFileModified(const char *fileName)
{
	/* TODO */
}

DirWatcher::DirWatcher() { }

DWORD DirWatcher::watchDirectory(const wxString &dirToWatch, DWORD changesToWatchFor, DirHandler *changeHandler, bool watchSubDirs)
{
#ifdef WIN32
	// assert( changesToWatchFor != 0 );

	if( dirToWatch.IsEmpty() || changesToWatchFor == 0 || changeHandler == NULL )
	{
		return ERROR_INVALID_PARAMETER;
	}

	dirInfo = (DirInfo *) malloc(sizeof(DirInfo));

	init_dirInfo(dirInfo);

	dirInfo->completionKey = (ULONG_PTR) &completionKey;
	dirInfo->handler = changeHandler;
	runThread = true;
	MAIN_APP->setThreadFlag(runThread);

	orangefs_debug_print("Registering watch on directory : %s\n", dirToWatch.c_str());

	/* open the directory and let the OS know we're listening for i/o changes asynchronously with the overlapped flag */
	dirInfo->dirPort = CreateFile(dirToWatch.c_str(), 
								  FILE_LIST_DIRECTORY,
								  FILE_SHARE_READ | FILE_SHARE_WRITE,
								  NULL,
								  OPEN_EXISTING,
								  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
								  NULL);

	if ( !dirInfo->dirPort )
	{
		orangefs_debug_print("ERROR : invalid handle returned from CreateFile.\n");
		return 0;
	}

	/* this creates the completion port, we then associate this handle with the completion port listening on the directory */
	dirInfo->dirIoPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
											   0,		/* no existing port */
											   0,		/* ignored */
											   1);

	if ( !dirInfo->dirIoPort )
	{
		orangefs_debug_print("ERROR : creating the completion port failed.\n");
		return 0;
	}

	HANDLE tempHandle = CreateIoCompletionPort(	dirInfo->dirPort,
												dirInfo->dirIoPort,		/* handle to the completion port we already created */
												(ULONG_PTR) dirInfo->completionKey,
												0);		/* this parameter is ignored since we already declared the number of threads in creating the port */

	if ( !tempHandle )
	{
		orangefs_debug_print("ERROR : invalid handle returned from CreateIoCompletionPort.\n");
		MAIN_APP->printLastWinError();
		return 0;
	}

	/* spawn a new thread to handle the syncing; if the user closes the app while it's still syncing, the app window will become invisible, but the sync thread will still be running */
	threadHandle = CreateThread(NULL, 0, monitorDirChanges, dirInfo, 0, NULL);
#endif

	return 0;
}

DWORD WINAPI monitorDirChanges(void *parms)
{
	DWORD error = 0;
	DWORD numRetBytesQ = 0;
	DWORD numRetBytesR = 0;
	BOOL retStatusR = FALSE;
	BOOL retStatusQ = FALSE;
	FILE_NOTIFY_INFORMATION *fileInfo = NULL;
	DirInfo *dirInfo = (DirInfo *) parms;

	/* ensure the handles created before this thread was created are still valid */
	if ( !dirInfo->dirIoPort || !dirInfo->dirPort )
	{
		orangefs_debug_print("ERROR : invalid handle at start of monitorDirChanges thread.\n");
		return 0;
	}

	while ( MAIN_APP->getThreadFlag() ) 
	{
		assert( MAIN_APP->getThreadFlag() == true );
		retStatusR = ReadDirectoryChangesW( dirInfo->dirPort,
											(void *) dirInfo->buffer,
											BUFFSIZE,
											FALSE,
											FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
											FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
											FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_LAST_ACCESS | 
											FILE_NOTIFY_CHANGE_CREATION   | FILE_NOTIFY_CHANGE_SECURITY,
											&numRetBytesR,
											&dirInfo->over->overlapped,
											NULL);

		assert( MAIN_APP->getThreadFlag() == true );
		retStatusQ = GetQueuedCompletionStatus(	dirInfo->dirIoPort,
												&numRetBytesQ,
												&(dirInfo->completionKey),
												(LPOVERLAPPED *) (dirInfo->over), 
												INFINITE);	/* wait time milliseconds */

		/* ReadDirectoryChangesW seems to return TRUE always, however, 
		 * GetQueuedCompletionStatus only is TRUE when there's data in 
		 * the buffer from ReadDirectoryChangesW */
		if ( retStatusR && retStatusQ )
		{
			char fileName[MAX_PATH];

			/* convert the buffer */
			fileInfo = (FILE_NOTIFY_INFORMATION *) dirInfo->buffer;

			/* convert the file name from wide char to regular char */
			int count = WideCharToMultiByte(CP_ACP, 
											0,
											fileInfo->FileName,
											fileInfo->FileNameLength / sizeof(wchar_t),
											fileName,
											MAX_PATH - 1,
											NULL,
											NULL);
			/* MSDN says the filename is NOT null terminated, so add it here */
			fileName[count] = '\0';

			orangefs_debug_print("---- FILENAME : %s ----\n", fileName);

						/* finally, handle the change notification */
			switch (fileInfo->Action)
			{
				case FILE_ACTION_ADDED:
					dirInfo->handler->onFileAdded(fileName);
					break;
				case FILE_ACTION_REMOVED:
					dirInfo->handler->onFileRemoved(fileName);
					break;
				case FILE_ACTION_MODIFIED:
					dirInfo->handler->onFileModified(fileName);
					break;
				case FILE_ACTION_RENAMED_NEW_NAME:
					dirInfo->handler->onFileNameChanged(fileName);
					break;
				case FILE_ACTION_RENAMED_OLD_NAME:
					dirInfo->handler->onFileNameChanged(fileName);
					break;
				default:
					orangefs_debug_print("---- ERROR : unhandled dir change notification.\nFile name : %s\nAction : %u\n", fileName, fileInfo->Action);
					break;
			}

			memset(fileName, 0, MAX_PATH);
			memset(dirInfo->buffer, 0, BUFFSIZE);
		}
		else
		{
			error = GetLastError();

			if ( error == WAIT_TIMEOUT )
			{
				orangefs_debug_print("---- 1 second timeout exceeded waiting ----\n");
			}
			else
			{
				// orangefs_debug_print("---- ERROR : GetQueuedCompletionStatus failed\nERROR CODE : %d\n", error);
			}
		}
	}
}

