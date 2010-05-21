/*************************************
 * File		: debug.h
 * Version	: $Id: debug.h,v 1.2.30.3 2010-05-21 17:34:01 nlmills Exp $
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
