/* Copyright (C) 1991-1995,1997-2006,2007,2009 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Written by Per Bothner <bothner@cygnus.com>.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.

   As a special exception, if you link the code in this file with
   files compiled with a GNU compiler to produce an executable,
   that does not cause the resulting executable to be covered by
   the GNU Lesser General Public License.  This exception does not
   however invalidate any other reasons why the executable file
   might be covered by the GNU Lesser General Public License.
   This exception applies to code released by its copyright holders
   in files containing the exception.  */

#ifndef _IO_OLDIO_H
#define _IO_OLDIO_H

#if 0
#endif

#define _IOS_INPUT        1
#define _IOS_OUTPUT       2
#define _IOS_ATEND        4
#define _IOS_APPEND       8
#define _IOS_TRUNC        16
#define _IOS_NOCREATE     32
#define _IOS_NOREPLACE    64
#define _IOS_BIN         128

/* Magic numbers and bits for the _flags field.
   The magic numbers use the high-order bits of _flags;
   the remaining bits are available for variable flags.
   Note: The magic numbers must all be negative if stdio
   emulation is desired. */

#define _IO_MAGIC        0xFBAD0000 /* Magic number */
#define _OLD_STDIO_MAGIC 0xFABC0000 /* Emulate old stdio. */
#define _IO_MAGIC_MASK   0xFFFF0000
#define _IO_USER_BUF     1 /* User owns buffer; don't delete it on close. */
#define _IO_UNBUFFERED   2
#define _IO_NO_READS     4 /* Reading not allowed */
#define _IO_NO_WRITES    8 /* Writing not allowd */
#ifndef _IO_EOF_SEEN
#define _IO_EOF_SEEN     0x10
#endif
#ifndef _IO_ERR_SEEN
#define _IO_ERR_SEEN     0x20
#endif
#define _IO_DELETE_DONT_CLOSE 0x40 /* Don't call close(_fileno) on cleanup. */
#define _IO_LINKED       0x80 /* Set if linked (using _chain) to streambuf::_list_all.*/
#define _IO_IN_BACKUP    0x100
#define _IO_LINE_BUF     0x200
#define _IO_TIED_PUT_GET 0x400 /* Set if put and get pointer logicly tied. */
#define _IO_CURRENTLY_PUTTING 0x800
#define _IO_IS_APPENDING 0x1000
#define _IO_IS_FILEBUF   0x2000
#define _IO_BAD_SEEN     0x4000
#define _IO_USER_LOCK    0x8000

#define _IO_FLAGS2_MMAP 1
#define _IO_FLAGS2_NOTCANCEL 2
#ifdef _LIBC
# define _IO_FLAGS2_FORTIFY 4
#endif
#define _IO_FLAGS2_USER_WBUF 8
#ifdef _LIBC
# define _IO_FLAGS2_SCANF_STD 16
#endif

/* These are "formatting flags" matching the iostream fmtflags enum values. */
#define _IO_SKIPWS 01
#define _IO_LEFT 02
#define _IO_RIGHT 04
#define _IO_INTERNAL 010
#define _IO_DEC 020
#define _IO_OCT 040
#define _IO_HEX 0100
#define _IO_SHOWBASE 0200
#define _IO_SHOWPOINT 0400
#define _IO_UPPERCASE 01000
#define _IO_SHOWPOS 02000
#define _IO_SCIENTIFIC 04000
#define _IO_FIXED 010000
#define _IO_UNITBUF 020000
#define _IO_STDIO 040000
#define _IO_DONT_CLOSE 0100000
#define _IO_BOOLALPHA 0200000

#if 0
#endif

/* A streammarker remembers a position in a buffer. */

struct _IO_marker {
  struct _IO_marker *_next;
  struct _IO_FILE *_sbuf;
  /* If _pos >= 0
 it points to _buf->Gbase()+_pos. FIXME comment */
  /* if _pos < 0, it points to _buf->eBptr()+_pos. FIXME comment */
  int _pos;
};

#if 0
#endif /* OFS */

#endif /* _IO_OLDIO_H */

