// Copyright (c) 1999-2014  David Muse
// See the file COPYING for more information

#ifndef SQLRDEBUGPRINT_H
#define SQLRDEBUGPRINT_H

//#define DEBUG_MESSAGES 1
//#define DEBUG_TO_FILE 1

#ifdef DEBUG_TO_FILE
	#include <rudiments/file.h>
	#include <rudiments/permissions.h>
	#ifdef _MSC_VER
		//static const char	debugfile[]="C:\\sqlrdebug.txt";
		static const char	debugfile[]="C:\\Users\\dmuse\\Desktop\\sqlrdebug.txt";
	#else
		static const char	debugfile[]="/tmp/sqlrdebug.txt";
	#endif
	static	file	f;
#else
	#include <rudiments/stdio.h>
#endif

#if (defined(_MSC_VER) && (_MSC_VER <= 1300)) || \
	(defined(__USLC__) && !defined(__GNUC__))

	// degenerate debug macros for really incapable compilers
	// (old microsoft compilers, unixware native compiler)
	extern "C" static void debugFunction() {}
	extern "C" static void debugPrintf(const char *format, ...) {}
	extern "C" static void debugSafePrint(const char *str,
						int32_t length) {}
	extern "C" static void debugPrintBits(uint16_t a) {}

#else

	// working debug macros for more modern compilers
	#ifdef DEBUG_MESSAGES
		#ifdef DEBUG_TO_FILE
			#define debugFunction() { if (f.getFileDescriptor()==-1) { f.dontGetCurrentPropertiesOnOpen(); f.open(debugfile,O_RDWR|O_APPEND|O_CREAT,permissions::evalPermString("rw-r--r--")); } f.printf("%s:%s():%d:\n",__FILE__,__FUNCTION__,__LINE__); }
			#ifdef _MSC_VER
				#define debugPrintf(args,...) { if (f.getFileDescriptor()==-1) { f.dontGetCurrentPropertiesOnOpen(); f.open(debugfile,O_RDWR|O_APPEND|O_CREAT,permissions::evalPermString("rw-r--r--")); } f.printf(args,__VA_ARGS__); }
			#else
				#define debugPrintf(args...) { if (f.getFileDescriptor()==-1) { f.dontGetCurrentPropertiesOnOpen(); f.open(debugfile,O_RDWR|O_APPEND|O_CREAT,permissions::evalPermString("rw-r--r--")); } f.printf(args); }
			#endif
			#define debugSafePrint(a,b) { if (f.getFileDescriptor()==-1) { f.dontGetCurrentPropertiesOnOpen(); f.open(debugfile,O_RDWR|O_APPEND|O_CREAT,permissions::evalPermString("rw-r--r--")); } f.safePrint(a,b); }
			#define debugPrintBits(a) { if (f.getFileDescriptor()==-1) { f.dontGetCurrentPropertiesOnOpen(); f.open(debugfile,O_RDWR|O_APPEND|O_CREAT,permissions::evalPermString("rw-r--r--")); } f.printBits(a); }
		#else
			#define debugFunction() stdoutput.printf("%s:%s():%d:\n",__FILE__,__FUNCTION__,__LINE__); stdoutput.flush();
			#ifdef _MSC_VER
				#define debugPrintf(args,...) stdoutput.printf(args,__VA_ARGS__); stdoutput.flush();
			#else
				#define debugPrintf(args...) stdoutput.printf(args); stdoutput.flush();
			#endif
			#define debugSafePrint(a,b) stdoutput.safePrint(a,b); stdoutput.flush();
			#define debugPrintBits(a) stdoutput.printBits(a); stdoutput.flush();
		#endif
	#else
		#define debugFunction() /* */
		#ifdef _MSC_VER
			#define debugPrintf(args,...) /* */
		#else
			#define debugPrintf(args...) /* */
		#endif
		#define debugSafePrint(a,b) /* */
		#define debugPrintBits(a) /* */
	#endif

#endif

#endif