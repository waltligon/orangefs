/*************************************
 * File		: debug.h
 * Version	: $Id: debug.h,v 1.2 2008-11-20 01:16:52 slang Exp $
 ************************************/

/* Author: Aroon Nataraj */

#ifndef _DEBUG_H_
#define _DEBUG_H_

#define PFX TAU_NAME

#ifdef TAU_DEBUG
#define dbg(format, arg...)	printf(PFX ": " format "\n" , ## arg)
#define info(format, arg...) 	printf(PFX ": " format "\n" , ## arg)
#else /*TAU_DEBUG*/
#define dbg(format, arg...)  	do {} while (0)
#define info(format, arg...)  	do {} while (0)
#endif /*TAU_DEBUG*/

#define err(format, arg...)  	printf(PFX ":Error: " format "\n" , ## arg)
#define warn(format, arg...) 	printf(PFX ":Warn: " format "\n" , ## arg)

#endif /*_DEBUG_H_*/
