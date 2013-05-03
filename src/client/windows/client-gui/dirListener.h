#ifndef __DIRLISTENER
#define __DIRLISTENER

#define BUFFSIZE (32 * 1024)	/* 32KB buffer */

class DirHandler;

/* this is the asynchronous structure windows uses for thread data */
typedef struct
{
	OVERLAPPED overlapped;
	wchar_t *buffer;
}Overlapped;

typedef struct
{
	HANDLE dirPort;
	HANDLE dirIoPort;
	Overlapped *over;
	int buffLen;
	wchar_t *buffer;
	unsigned char *dirName;
	ULONG_PTR completionKey;
	DirHandler *handler;
}DirInfo;

/*
 * I got a majority of the functionality and structure/implementation for this directory watcher 
 * from online. It has been tested and every case (delete, create, update, rename, etc) reports back asynchronously just fine.
 */
class DirWatcher
{
public:
	DirWatcher();

	virtual ~DirWatcher()
	{
		free_dirInfo(dirInfo);
	}

	void init_dirInfo(DirInfo *dirInfo)
	{
		completionKey = 1;

		dirInfo->buffer = (wchar_t *) malloc(BUFFSIZE * sizeof(wchar_t)); /* 32KB buffer */ 
		dirInfo->buffLen = BUFFSIZE;
		dirInfo->over = (Overlapped *) calloc(1, sizeof(Overlapped));
		dirInfo->over->buffer = (wchar_t *) calloc(BUFFSIZE, sizeof(wchar_t));
		dirInfo->dirName = NULL;
		dirInfo->handler = NULL;

		memset(dirInfo->over->buffer, 0, BUFFSIZE);
		memset(dirInfo->over, 0, sizeof(OVERLAPPED));
	}

	void free_dirInfo(DirInfo *dirInfo)
	{
		/* close handles first */
		if ( dirInfo->dirIoPort )
			CloseHandle( dirInfo->dirIoPort );
		if ( dirInfo->dirPort )
			CloseHandle( dirInfo->dirPort );

		if ( dirInfo->buffer )
			free(dirInfo->buffer);
		if ( dirInfo->over->buffer ) 
			free(dirInfo->over->buffer);
		if ( dirInfo->over )
			free(dirInfo->over);
		if ( dirInfo ) 
			free(dirInfo);
	}

	DWORD watchDirectory(const wxString &dirToWatch, DWORD changesToWatchFor, DirHandler *changeHandler, bool watchSubDirs = false);
	bool unwatchDirectory(const wxString &dirName) const;
	bool unwatchAllDirectories();

	HANDLE getThreadHandle() const { return threadHandle; }
private:
	friend class DirHandler;
	DirInfo *dirInfo;
	bool runThread;
	int completionKey;

	HANDLE threadHandle;
	bool unwatchDir(DirHandler *changeHandler);		/* called in DirHandler destructor */
};

class DirHandler
{
public:
	DirHandler();
	virtual ~DirHandler();

	bool unwatchDir();
	const wxString& getChangedDirName() const { return changedDirName; }

	void onFileAdded(const char *fileName);
	void onFileRemoved(const char *fileName);
	void onFileNameChanged(const char *fileName);
	void onFileModified(const char *fileName);

	void setChangedDirName(const wxString& dirName) { changedDirName = dirName; }

private:
	wxString changedDirName;
	DirWatcher *dirWatcher;
};

#endif