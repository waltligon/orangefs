/*
 * (C) 2005 Frederik Grll <frederik.gruell@web.de>
 *
 * See COPYING in top-level directory.
 */


#include "dist-varstrip-parser.h"
#include "pvfs2-dist-varstrip.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int _strips_ParsElem(char* inp, const PVFS_offset *prevOffset,
			    const PVFS_size *prevSize, unsigned *servernr,
			    PVFS_offset *offset, PVFS_size *size);

static Strips* _strips_AllocMem(const char* inp);

void strips_FreeMem(Strips **strips)     // free allocated memory
{
  if (*strips)
  {
    free(*strips);
    *strips = 0;
  }
  return;
}


static int _strips_ParsElem(char* inp, const PVFS_offset *prevOffset,
			    const PVFS_size *prevSize, unsigned *servernr,
			    PVFS_offset *offset, PVFS_size *size)
{
  char *sServer, *sSize;
  unsigned iServer;
  PVFS_size iSize;
  
  // get servernr and offset
  if (prevOffset != NULL && prevSize != NULL) 
  {
    sServer = strtok(NULL, ":");
  }
  else
  {
    sServer = strtok(inp, ":");    // first call
  }
  
  if (sServer != NULL)
  {
    iServer = atoi(sServer);
    *servernr = iServer;
  }
  else                                  // no servernr found
    return 1;

  if (prevOffset != NULL && prevSize != NULL) 
    *offset = (*prevOffset) + (*prevSize);
  else
     *offset = 0; // first call
              // get size
  sSize = strtok(NULL, ";");            // set size
  if (sSize != NULL)                    // if possible
  {
    iSize = atoll(sSize);
    if (iSize > 0)
    {
      if (strlen(sSize) > 1)
      {
        switch (sSize[strlen(sSize) - 1]) {
          case 'k':
          case 'K':
            iSize *= 1024;
            break;
          case 'm':
          case 'M':
            iSize *= (1024 * 1024);
            break;
          case 'g':
          case 'G':
            iSize *= (1024 * 1024 * 1024);
            break;
        }
      }
      *size = iSize;
    }
    else                                // size <= 0, abort
      return -1;
  }
  else                                  // no size info found, abort
    return -1;

  return 0;
}


static Strips* _strips_AllocMem(const char* inp)
{
  int i, count = 0;
  for (i = 0; i < strlen(inp); i++)   // count ":" to allocate enough memory
  {
  	if (inp[i] == ':')
    {
  	  count++;
    }
  }  
                          
  if (!count)                        // no ";" found, abort
    return (Strips*) NULL;
                                     // allocate array of struct slicing
  return (Strips*) (malloc(sizeof(Strips) * count));
}


// parse hint to array of struct slicing
// input sytax: {<datafile number>:<strip size>[K|M|G];}+
int strips_Parse(const char *input, Strips **strips, unsigned *count)  
{
  char inp[PVFS_DIST_VARSTRIP_MAX_STRIPS_STRING_LENGTH];
  Strips *stripsElem;
  PVFS_size *prevSize   = NULL;
  PVFS_offset *prevOffset = NULL;
  int i;
      
  *count = 0;
  *strips = 0;

  if (strlen(input) < PVFS_DIST_VARSTRIP_MAX_STRIPS_STRING_LENGTH - 1)
    strcpy(inp, input);
  else                                // input string too long, abort
    return -1;
  
  *strips = _strips_AllocMem(inp);

  if (!(*strips))                      // allocation failed, abort
    return -1;
  
  for (i = 0;; i++)
  {
    stripsElem = (*strips) + i;
switch (_strips_ParsElem(inp, prevOffset, prevSize, &(stripsElem->servernr),
                              &(stripsElem->offset), &(stripsElem->size)))
    {
      case 0:     // do next element
        prevOffset = &(stripsElem->offset);
        prevSize   = &(stripsElem->size);
        break;
      case -1:     // an error occured
        strips_FreeMem(strips);
       *count = 0;
        return -1;
        break;
      case 1:      // finished
       *count = i;
        if (*count == 0)    
        {         // // 0 elements, abort
          strips_FreeMem(strips);
          return -1;
        }
        else
          return 0;
        break;
    }
  }
}
