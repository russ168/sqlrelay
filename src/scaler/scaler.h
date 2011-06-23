// Copyright (c) 1999-2001  David Muse
// See the file COPYING for more information

#ifndef MONITOR_H
#define MONITOR_H

#include <config.h>
#include <defaults.h>

#include <sqlrconfigfile.h>
#include <cmdline.h>
#include <tempdir.h>

#include <rudiments/daemonprocess.h>
#include <rudiments/semaphoreset.h>
#include <rudiments/sharedmemory.h>
#include <rudiments/randomnumber.h>

// for pid_t
#include <sys/types.h>

#ifdef RUDIMENTS_NAMESPACE
using namespace rudiments;
#endif

class scaler : public daemonprocess {

	public:
			scaler();
			~scaler();
		bool	initScaler(int argc, const char **argv);
		void	loop();

	private:
		void	cleanUp();

		pid_t	openOneConnection();
		bool	connectionStarted();
		void	killConnection(pid_t connpid);
		bool	openMoreConnections();
		bool	reapChildren(pid_t connpid);
		void	getRandomConnectionId();
		bool	availableDatabase();
		int32_t	countSessions();
		int32_t	countConnections();
		void	incConnections();
		void	decConnections();

		char		*pidfile;

		const char	*id;
		char		*config;
		const char	*dbase;

		sqlrconfigfile	*cfgfile;

		int32_t		maxconnections;
		int32_t		currentconnections;
		int32_t		maxqueuelength;
		int32_t		growby;
		int32_t		ttl;

		char		*idfilename;

		semaphoreset	*semset;

		sharedmemory	*idmemory;

		linkedlist< connectstringcontainer * >	*connectstringlist;
		const char		*connectionid;
		int32_t			metrictotal;

		int		currentseed;

		bool		init;

		bool		debug;

		tempdir		*tmpdir;
		cmdline		*cmdl;
};

#endif
