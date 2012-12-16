// Copyright (c) 1999-2011  David Muse
// See the file COPYING for more information

#include <config.h>
#include <sqlrcontroller.h>
#include <rudiments/file.h>
#include <rudiments/rawbuffer.h>
#include <rudiments/passwdentry.h>
#include <rudiments/groupentry.h>
#include <rudiments/process.h>
#include <rudiments/permissions.h>
#include <rudiments/snooze.h>
#include <rudiments/error.h>
#include <rudiments/signalclasses.h>
#include <rudiments/datetime.h>
#include <rudiments/character.h>
#include <rudiments/charstring.h>

// for gettimeofday()
#include <sys/time.h>

#include <defines.h>
#include <datatypes.h>

// for sprintf
#include <stdio.h>

// for gettimeofday()
#include <sys/time.h>

// for umask
#include <sys/types.h>
#include <sys/stat.h>

#include <math.h>

sqlrcontroller_svr::sqlrcontroller_svr() : daemonprocess(), listener() {

	conn=NULL;

	cmdl=NULL;
	cfgfl=NULL;
	semset=NULL;
	idmemory=NULL;
	connstats=NULL;

	updown=NULL;

	dbselected=false;
	originaldb=NULL;

	tmpdir=NULL;

	unixsocket=NULL;
	unixsocketptr=NULL;
	serversockun=NULL;
	serversockin=NULL;
	serversockincount=0;

	inetport=0;
	authc=NULL;
	lastuserbuffer[0]='\0';
	lastpasswordbuffer[0]='\0';
	lastauthsuccess=false;

	commitorrollback=false;

	autocommitforthissession=false;

	translatebegins=true;
	faketransactionblocks=false;
	faketransactionblocksautocommiton=false;
	intransactionblock=false;

	fakeinputbinds=false;
	translatebinds=false;

	isolationlevel=NULL;

	ignoreselectdb=false;

	maxquerysize=0;
	maxbindcount=0;
	maxbindnamelength=0;
	maxstringbindvaluelength=0;
	maxlobbindvaluelength=0;
	maxerrorlength=0;
	idleclienttimeout=-1;

	connected=false;
	inclientsession=false;
	loggedin=false;

	// maybe someday these parameters will be configurable
	bindpool=new memorypool(512,128,100);
	bindmappingspool=new memorypool(512,128,100);
	inbindmappings=new namevaluepairs;
	outbindmappings=new namevaluepairs;

	sqlp=NULL;
	sqlt=NULL;
	sqlw=NULL;
	sqlrlg=NULL;
	sqlrq=NULL;
	sqlrpe=NULL;

	decrypteddbpassword=NULL;

	debugsqltranslation=false;
	debugtriggers=false;

	cur=NULL;

	pidfile=NULL;

	clientinfo=NULL;
	clientinfolen=0;

	decrementonclose=false;
	silent=false;

	loggedinsec=0;
	loggedinusec=0;
}

sqlrcontroller_svr::~sqlrcontroller_svr() {

	if (connstats) {
		rawbuffer::zero(connstats,sizeof(sqlrconnstatistics));
	}

	delete cmdl;
	delete cfgfl;

	delete[] updown;

	delete[] originaldb;

	delete tmpdir;

	dbgfile.debugPrint("connection",0,"deleting authc...");
	delete authc;
	dbgfile.debugPrint("connection",0,"done deleting authc");

	dbgfile.debugPrint("connection",0,"deleting idmemory...");
	delete idmemory;
	dbgfile.debugPrint("connection",0,"done deleting idmemory");

	dbgfile.debugPrint("connection",0,"deleting semset...");
	delete semset;
	dbgfile.debugPrint("connection",0,"done deleting semset");

	dbgfile.debugPrint("connection",0,"deleting unixsocket...");
	if (unixsocket) {
		file::remove(unixsocket);
		delete[] unixsocket;
	}
	dbgfile.debugPrint("connection",0,"done deleting unixsocket");

	dbgfile.debugPrint("connection",0,"deleting bindpool...");
	delete bindpool;
	dbgfile.debugPrint("connection",0,"done deleting bindpool");

	dbgfile.debugPrint("connection",0,"deleting bindmappings...");
	delete bindmappingspool;
	delete inbindmappings;
	delete outbindmappings;
	dbgfile.debugPrint("connection",0,"done deleting bindmappings");

	delete sqlp;
	delete sqlt;
	delete sqlw;
	delete sqltr;
	delete sqlrlg;
	delete sqlrq;
	delete sqlrpe;

	delete[] decrypteddbpassword;

	delete[] clientinfo;

	if (pidfile) {
		file::remove(pidfile);
		delete[] pidfile;
	}

	delete conn;
}

void sqlrcontroller_svr::flushWriteBuffer() {
	clientsock->flushWriteBuffer(-1,-1);
}

bool sqlrcontroller_svr::init(int argc, const char **argv) {

	// process command line
	cmdl=new cmdline(argc,argv);

	// default id warning
	if (!charstring::compare(cmdl->getId(),DEFAULT_ID)) {
		fprintf(stderr,"Warning: using default id.\n");
	}

	// get whether this connection was spawned by the scaler
	scalerspawned=cmdl->found("-scaler");

	// get the connection id from the command line
	connectionid=cmdl->getValue("-connectionid");
	if (!connectionid[0]) {
		connectionid=DEFAULT_CONNECTIONID;
		fprintf(stderr,"Warning: using default connectionid.\n");
	}

	// get the time to live from the command line
	const char	*ttlstr=cmdl->getValue("-ttl");
	ttl=(ttlstr)?charstring::toInteger(cmdl->getValue("-ttl")):-1;

	silent=cmdl->found("-silent");

	cfgfl=new sqlrconfigfile();
	if (!cfgfl->parse(cmdl->getConfig(),cmdl->getId())) {
		return false;
	}
	const char	*pwdencs=cfgfl->getPasswordEncryptions();
	if (charstring::length(pwdencs)) {
		sqlrpe=new sqlrpwdencs;
		sqlrpe->loadPasswordEncryptions(pwdencs);
	}	
	authc=new authenticator(cfgfl,sqlrpe);
	tmpdir=new tempdir(cmdl);

	setUserAndGroup();

	// load database plugin
	conn=initConnection(cfgfl->getDbase());
	if (!conn) {
		return false;
	}

	dbgfile.init("connection",cmdl->getLocalStateDir());
	if (cmdl->found("-debug")) {
		dbgfile.enable();
	}

	// handle the unix socket directory
	if (cfgfl->getListenOnUnix()) {
		setUnixSocketDirectory();
	}

	// handle the pid file
	if (!handlePidFile()) {
		return false;
	}

	constr=cfgfl->getConnectString(connectionid);
	if (!constr) {
		fprintf(stderr,"Error: invalid connectionid \"%s\".\n",
							connectionid);
		return false;
	}
	conn->handleConnectString();

	initDatabaseAvailableFileName();

	if (cfgfl->getListenOnUnix() &&
		!getUnixSocket(tmpdir->getString(),unixsocketptr)) {
		return false;
	}

	if (!createSharedMemoryAndSemaphores(tmpdir->getString(),
							cmdl->getId())) {
		return false;
	}

	bool	reloginatstart=cfgfl->getReLoginAtStart();
	if (!reloginatstart) {
		if (!attemptLogIn(!silent)) {
			return false;
		}
	}
	if (!cmdl->found("-nodetach")) {
		detach();
	}
	if (reloginatstart) {
		while (!attemptLogIn(false)) {
			snooze::macrosnooze(5);
		}
	}
	initConnStats();

	// Get the query translators.  Do it after logging in, as
	// getSqlTranslator might return a different class depending on what
	// version of the db it gets logged into
	const char	*translations=cfgfl->getTranslations();
	if (charstring::length(translations)) {
		sqlp=new sqlparser;
		sqlt=conn->getSqlTranslations();
		sqlt->loadTranslations(translations);
		sqlw=conn->getSqlWriter();
	}
	debugsqltranslation=cfgfl->getDebugTranslations();

	// get the triggers
	const char	*triggers=cfgfl->getTriggers();
	if (charstring::length(triggers)) {
		// for triggers, we'll need an sqlparser as well
		if (!sqlp) {
			sqlp=new sqlparser;
		}
		sqltr=new sqltriggers;
		sqltr->loadTriggers(triggers);
	}
	debugtriggers=cfgfl->getDebugTriggers();

	// update various configurable parameters
	maxclientinfolength=cfgfl->getMaxClientInfoLength();
	maxquerysize=cfgfl->getMaxQuerySize();
	maxbindcount=cfgfl->getMaxBindCount();
	maxbindnamelength=cfgfl->getMaxBindNameLength();
	maxstringbindvaluelength=cfgfl->getMaxStringBindValueLength();
	maxlobbindvaluelength=cfgfl->getMaxLobBindValueLength();
	maxerrorlength=cfgfl->getMaxErrorLength();
	idleclienttimeout=cfgfl->getIdleClientTimeout();

	// set autocommit behavior
	setAutoCommit(conn->autocommit);

	// get fake input bind variable behavior
	// (this may have already been set true by the connect string)
	fakeinputbinds=(fakeinputbinds || cfgfl->getFakeInputBindVariables());

	// get translate bind variable behavior
	translatebinds=cfgfl->getTranslateBindVariables();

	// initialize cursors
	mincursorcount=cfgfl->getCursors();
	maxcursorcount=cfgfl->getMaxCursors();
	if (!initCursors(mincursorcount)) {
		return false;
	}

	// create connection pid file
	pid_t	pid=process::getProcessId();
	size_t	pidfilelen=tmpdir->getLength()+22+
				charstring::length(cmdl->getId())+1+
				charstring::integerLength((uint64_t)pid)+1;
	pidfile=new char[pidfilelen];
	snprintf(pidfile,pidfilelen,"%s/pids/sqlr-connection-%s.%ld",
				tmpdir->getString(),cmdl->getId(),(long)pid);
	createPidFile(pidfile,permissions::ownerReadWrite());

	// create clientinfo buffer
	clientinfo=new char[maxclientinfolength+1];

	// create error buffer
	// FIXME: this should definitely be done inside the connection class
	conn->error=new char[maxerrorlength+1];

	// increment connection counter
	if (cfgfl->getDynamicScaling()) {
		incrementConnectionCount();
	}

	// set the transaction isolation level
	isolationlevel=cfgfl->getIsolationLevel();
	conn->setIsolationLevel(isolationlevel);

	// ignore selectDatabase() calls?
	ignoreselectdb=cfgfl->getIgnoreSelectDatabase();

	// get the database/schema we're using so
	// we can switch back to it at end of session
	originaldb=conn->getCurrentDatabase();

	markDatabaseAvailable();

	// if we're not passing descriptors around, listen on 
	// inet and unix sockets for client connections
	if (!cfgfl->getPassDescriptor() && !openSockets()) {
		return false;
	}

	// get the loggers
	const char	*loggers=cfgfl->getLoggers();
	if (charstring::length(loggers)) {
		sqlrlg=new sqlrloggers;
		sqlrlg->loadLoggers(loggers);
		sqlrlg->initLoggers(conn);
	}

	// get the custom query handlers
	const char	*queries=cfgfl->getQueries();
	if (charstring::length(queries)) {
		sqlrq=new sqlrqueries;
		sqlrq->loadQueries(queries);
	}
	
	return true;
}

sqlrconnection_svr *sqlrcontroller_svr::initConnection(const char *dbase) {

	// load the connection module
	stringbuffer	modulename;
	modulename.append(LIBEXECDIR);
	modulename.append("/sqlrconnection_");
	modulename.append(dbase)->append(".so");
	if (!dl.open(modulename.getString(),true,true)) {
		fprintf(stderr,"failed to load connection module: %s\n",
							modulename.getString());
		char	*error=dl.getError();
		fprintf(stderr,"%s\n",error);
		delete[] error;
		return NULL;
	}

	// load the connection itself
	stringbuffer	functionname;
	functionname.append("new_")->append(dbase)->append("connection");
	sqlrconnection_svr	*(*newConn)(sqlrcontroller_svr *)=
				(sqlrconnection_svr *(*)(sqlrcontroller_svr *))
					dl.getSymbol(functionname.getString());
	if (!newConn) {
		fprintf(stderr,"failed to load connection: %s\n",dbase);
		char	*error=dl.getError();
		fprintf(stderr,"%s\n",error);
		delete[] error;
		return NULL;
	}

	sqlrconnection_svr	*conn=(*newConn)(this);
	if (!conn) {
		fprintf(stderr,"failed to create connection: %s\n",dbase);
		char	*error=dl.getError();
		fprintf(stderr,"%s\n",error);
		delete[] error;
	}
	return conn;
}

void sqlrcontroller_svr::setUserAndGroup() {

	// get the user that we're currently running as
	char	*currentuser=NULL;
	passwdentry::getName(process::getEffectiveUserId(),&currentuser);

	// get the group that we're currently running as
	char	*currentgroup=NULL;
	groupentry::getName(process::getEffectiveGroupId(),&currentgroup);

	// switch groups, but only if we're not currently running as the
	// group that we should switch to
	if (charstring::compare(currentgroup,cfgfl->getRunAsGroup()) &&
					!runAsGroup(cfgfl->getRunAsGroup())) {
		fprintf(stderr,"Warning: could not change group to %s\n",
						cfgfl->getRunAsGroup());
	}

	// switch users, but only if we're not currently running as the
	// user that we should switch to
	if (charstring::compare(currentuser,cfgfl->getRunAsUser()) &&
					!runAsUser(cfgfl->getRunAsUser())) {
		fprintf(stderr,"Warning: could not change user to %s\n",
						cfgfl->getRunAsUser());
	}

	// clean up
	delete[] currentuser;
	delete[] currentgroup;
}

void sqlrcontroller_svr::setUnixSocketDirectory() {
	size_t	unixsocketlen=tmpdir->getLength()+31;
	unixsocket=new char[unixsocketlen];
	snprintf(unixsocket,unixsocketlen,"%s/sockets/",tmpdir->getString());
	unixsocketptr=unixsocket+tmpdir->getLength()+8+1;
}

bool sqlrcontroller_svr::handlePidFile() {

	// check for listener's pid file
	// (Look a few times.  It might not be there right away.  The listener
	// writes it out after forking and it's possible that the connection
	// might start up after the sqlr-listener has forked, but before it
	// writes out the pid file)
	size_t	listenerpidfilelen=tmpdir->getLength()+20+
				charstring::length(cmdl->getId())+1;
	char	*listenerpidfile=new char[listenerpidfilelen];
	snprintf(listenerpidfile,listenerpidfilelen,
				"%s/pids/sqlr-listener-%s",
				tmpdir->getString(),cmdl->getId());

	bool	retval=true;
	bool	found=false;
	for (uint8_t i=0; !found && i<10; i++) {
		if (i) {
			snooze::microsnooze(0,100000);
		}
		found=(checkForPidFile(listenerpidfile)!=-1);
	}
	if (!found) {
		fprintf(stderr,"\nsqlr-connection error:\n");
		fprintf(stderr,"	The pid file %s",listenerpidfile);
		fprintf(stderr," was not found.\n");
		fprintf(stderr,"	This usually means "
					"that the sqlr-listener \n");
		fprintf(stderr,"is not running.\n");
		fprintf(stderr,"	The sqlr-listener must be running ");
		fprintf(stderr,"for the sqlr-connection to start.\n\n");
		retval=false;
	}

	delete[] listenerpidfile;

	return retval;
}

void sqlrcontroller_svr::initDatabaseAvailableFileName() {

	// initialize the database up/down filename
	size_t	updownlen=charstring::length(tmpdir->getString())+5+
					charstring::length(cmdl->getId())+1+
					charstring::length(connectionid)+1;
	updown=new char[updownlen];
	snprintf(updown,updownlen,"%s/ipc/%s-%s",
			tmpdir->getString(),cmdl->getId(),connectionid);
}

bool sqlrcontroller_svr::attemptLogIn(bool printerrors) {

	dbgfile.debugPrint("connection",0,"logging in...");

	// log in
	if (!logIn(printerrors)) {
		dbgfile.debugPrint("connection",0,"log in failed");
		if (printerrors) {
			fprintf(stderr,"Couldn't log into database.\n");
		}
		return false;
	}

	// get stats
	struct timeval	tv;
	gettimeofday(&tv,NULL);
	loggedinsec=tv.tv_sec;
	loggedinusec=tv.tv_usec;

	dbgfile.debugPrint("connection",0,"done logging in");
	return true;
}

bool sqlrcontroller_svr::initCursors(int32_t count) {

	dbgfile.debugPrint("connection",0,"initializing cursors...");

	cursorcount=count;
	if (!cur) {
		cur=new sqlrcursor_svr *[maxcursorcount];
		rawbuffer::zero(cur,maxcursorcount*sizeof(sqlrcursor_svr *));
	}

	for (int32_t i=0; i<cursorcount; i++) {

		dbgfile.debugPrint("connection",1,i);

		if (!cur[i]) {
			cur[i]=initCursor();
		}
		if (!cur[i]->openInternal(i)) {

			dbgfile.debugPrint("connection",1,
					"cursor init failure...");

			logOut();
			return false;
		}
	}

	dbgfile.debugPrint("connection",0,"done initializing cursors");

	return true;
}

sqlrcursor_svr *sqlrcontroller_svr::initCursor() {
	sqlrcursor_svr	*cursor=conn->initCursor();
	if (cursor) {
		incrementOpenDatabaseCursors();
	}
	return cursor;
}

bool sqlrcontroller_svr::createSharedMemoryAndSemaphores(const char *tmpdir,
							const char *id) {

	size_t	idfilenamelen=charstring::length(tmpdir)+5+
					charstring::length(id)+1;
	char	*idfilename=new char[idfilenamelen];
	snprintf(idfilename,idfilenamelen,"%s/ipc/%s",tmpdir,id);

	dbgfile.debugPrint("connection",0,"attaching to shared memory and semaphores");
	dbgfile.debugPrint("connection",0,"id filename: ");
	dbgfile.debugPrint("connection",0,idfilename);

	// connect to the shared memory
	dbgfile.debugPrint("connection",1,"attaching to shared memory...");
	idmemory=new sharedmemory();
	if (!idmemory->attach(file::generateKey(idfilename,1))) {
		char	*err=error::getErrorString();
		fprintf(stderr,"Couldn't attach to shared memory segment: ");
		fprintf(stderr,"%s\n",err);
		delete[] err;
		delete idmemory;
		idmemory=NULL;
		delete[] idfilename;
		return false;
	}
	shm=(shmdata *)idmemory->getPointer();
	if (!shm) {
		fprintf(stderr,"failed to get pointer to shmdata\n");
		delete idmemory;
		idmemory=NULL;
		delete[] idfilename;
		return false;
	}

	// connect to the semaphore set
	dbgfile.debugPrint("connection",1,"attaching to semaphores...");
	semset=new semaphoreset();
	if (!semset->attach(file::generateKey(idfilename,1),11)) {
		char	*err=error::getErrorString();
		fprintf(stderr,"Couldn't attach to semaphore set: ");
		fprintf(stderr,"%s\n",err);
		delete[] err;
		delete semset;
		delete idmemory;
		semset=NULL;
		idmemory=NULL;
		delete[] idfilename;
		return false;
	}

	dbgfile.debugPrint("connection",0,
			"done attaching to shared memory and semaphores");

	delete[] idfilename;

	return true;
}

void sqlrcontroller_svr::waitForListenerToRequireAConnection() {
	dbgfile.debugPrint("connection",1,"waiting for the listener to require a connection");
	semset->wait(11);
	//semset->waitWithUndo(11);
	dbgfile.debugPrint("connection",1,"done waiting for the listener to require a connection");
}

void sqlrcontroller_svr::acquireAnnounceMutex() {
	dbgfile.debugPrint("connection",1,"acquiring announce mutex");
	updateState(WAIT_SEMAPHORE);
	semset->waitWithUndo(0);
	dbgfile.debugPrint("connection",1,"done acquiring announce mutex");
}

shmdata *sqlrcontroller_svr::getAnnounceBuffer() {
	return (shmdata *)idmemory->getPointer();
}

void sqlrcontroller_svr::releaseAnnounceMutex() {
	dbgfile.debugPrint("connection",1,"releasing announce mutex");
	semset->signalWithUndo(0);
	dbgfile.debugPrint("connection",1,"done releasing announce mutex");
}

void sqlrcontroller_svr::signalListenerToRead() {
	dbgfile.debugPrint("connection",1,"signalling listener to read");
	semset->signal(2);
	dbgfile.debugPrint("connection",1,"done signalling listener to read");
}

void sqlrcontroller_svr::waitForListenerToFinishReading() {
	dbgfile.debugPrint("connection",1,"waiting for listener");
	semset->wait(3);
	// Reset this semaphore to 0.
	// It can get left incremented if another sqlr-connection is killed
	// between calls to signalListenerToRead() and this method.
	// It's ok to reset it here becuase no one except this process has
	// access to this semaphore at this time because of the lock on
	// AnnounceMutex (semaphore 0).
	semset->setValue(3,0);
	dbgfile.debugPrint("connection",1,"done waiting for listener");
}

void sqlrcontroller_svr::acquireConnectionCountMutex() {
	dbgfile.debugPrint("connection",1,"acquiring connection count mutex");
	semset->waitWithUndo(4);
	dbgfile.debugPrint("connection",1,"done acquiring connection count mutex");
}

void sqlrcontroller_svr::releaseConnectionCountMutex() {
	dbgfile.debugPrint("connection",1,"releasing connection count mutex");
	semset->signalWithUndo(4);
	dbgfile.debugPrint("connection",1,"done releasing connection count mutex");
}

void sqlrcontroller_svr::signalScalerToRead() {
	dbgfile.debugPrint("connection",1,"signalling scaler to read");
	semset->signal(8);
	dbgfile.debugPrint("connection",1,"done signalling scaler to read");
}

bool sqlrcontroller_svr::openSockets() {

	dbgfile.debugPrint("connection",0,"listening on sockets...");

	// get the next available unix socket and open it
	if (cfgfl->getListenOnUnix() && unixsocketptr && unixsocketptr[0]) {

		if (!serversockun) {
			serversockun=new unixserversocket();
			if (serversockun->listen(unixsocket,0000,5)) {

				size_t	stringlen=26+
					charstring::length(unixsocket)+1;
				char	*string=new char[stringlen];
				snprintf(string,stringlen,
					"listening on unix socket: %s",
								unixsocket);
				dbgfile.debugPrint("connection",1,string);
				delete[] string;

				addFileDescriptor(serversockun);

			} else {
				fprintf(stderr,"Could not listen on ");
				fprintf(stderr,"unix socket: ");
				fprintf(stderr,"%s\n",unixsocket);
				fprintf(stderr,"Make sure that the file and ");
				fprintf(stderr,"directory are readable ");
				fprintf(stderr,"and writable.\n\n");
				delete serversockun;
				return false;
			}
		}
	}

	// open the next available inet socket
	if (cfgfl->getListenOnInet()) {

		if (!serversockin) {
			const char * const *addresses=cfgfl->getAddresses();
			serversockincount=cfgfl->getAddressCount();
			serversockin=new inetserversocket *[serversockincount];
			bool	failed=false;
			for (uint64_t index=0;
					index<serversockincount;
					index++) {
				serversockin[index]=NULL;
				if (failed) {
					continue;
				}
				serversockin[index]=new inetserversocket();
				if (serversockin[index]->
					listen(addresses[index],inetport,5)) {

					if (!inetport) {
						inetport=serversockin[index]->
								getPort();
					}

					char	string[33];
					snprintf(string,33,
						"listening on inet socket: %d",
						inetport);
					dbgfile.debugPrint("connection",1,string);
	
					addFileDescriptor(serversockin[index]);

				} else {
					fprintf(stderr,"Could not listen on ");
					fprintf(stderr,"inet socket: ");
					fprintf(stderr,"%d\n\n",inetport);
					failed=true;
				}
			}
			if (failed) {
				for (uint64_t index=0;
						index<serversockincount;
						index++) {
					delete serversockin[index];
				}
				delete[] serversockin;
				serversockincount=0;
				return false;
			}
		}
	}

	dbgfile.debugPrint("connection",0,"done listening on sockets");

	return true;
}

bool sqlrcontroller_svr::listen() {

	uint16_t	sessioncount=0;
	bool		clientconnectfailed=false;

	for (;;) {

		waitForAvailableDatabase();
		initSession();
		announceAvailability(tmpdir->getString(),
						cfgfl->getPassDescriptor(),
						unixsocket,
						inetport,
						connectionid);

		// loop to handle suspended sessions
		bool	loopback=false;
		for (;;) {

			int	success=waitForClient();

			if (success==1) {

				suspendedsession=false;

				// have a session with the client
				clientSession();

				// break out of the loop unless the client
				// suspended the session
				if (!suspendedsession) {
					break;
				}

			} else if (success==2) {

				// this is a special case, basically it means
				// that the listener wants the connection to
				// reconnect to the database, just loop back
				// so that can be handled naturally
				loopback=true;
				break;

			} else if (success==-1) {

				// if waitForClient() errors out, break out of
				// the suspendedsession loop and loop back
				// for another session and close connection if
				// it is possible otherwise wait for session,
				// but it seems that on hard load it's
				// impossible to change handoff socket for pid
				clientconnectfailed=true;
				break;

			} else {

				// if waitForClient() times out waiting for
				// someone to pick up the suspended
				// session, roll it back and kill it
				if (suspendedsession) {
					if (conn->isTransactional()) {
						rollback();
					}
					suspendedsession=false;
				}
			}
		}

		if (!loopback && cfgfl->getDynamicScaling()) {

			decrementConnectedClientCount();

			if (scalerspawned) {

				if (clientconnectfailed) {
					return false;
				}

				if (ttl==0) {
					return true;
				}

				if (ttl>0 && cfgfl->getMaxSessionCount()) {
					sessioncount++;
					if (sessioncount==
						cfgfl->getMaxSessionCount()) {
						return true;
					}
				}
			}
		}
	}
}

void sqlrcontroller_svr::waitForAvailableDatabase() {

	dbgfile.debugPrint("connection",0,"waiting for available database...");

	updateState(WAIT_FOR_AVAIL_DB);

	if (!availableDatabase()) {
		reLogIn();
		markDatabaseAvailable();
	}

	dbgfile.debugPrint("connection",0,"database is available");
}

bool sqlrcontroller_svr::availableDatabase() {

	// return whether the file "updown" is there or not
	if (file::exists(updown)) {
		dbgfile.debugPrint("connection",0,"database is available");
		return true;
	} else {
		dbgfile.debugPrint("connection",0,"database is not available");
		return false;
	}
}

void sqlrcontroller_svr::initSession() {

	dbgfile.debugPrint("connection",0,"initializing session...");

	commitorrollback=false;
	suspendedsession=false;
	for (int32_t i=0; i<cursorcount; i++) {
		cur[i]->state=SQLRCURSORSTATE_AVAILABLE;
	}
	accepttimeout=5;

	dbgfile.debugPrint("connection",0,"done initializing session...");
}

int32_t sqlrcontroller_svr::waitForClient() {

	dbgfile.debugPrint("connection",0,"waiting for client...");

	updateState(WAIT_CLIENT);

	// FIXME: listen() checks for 2,1,0 or -1 from this method, but this
	// method only returns 2, 1 or -1.  0 should indicate that a suspended
	// session timed out.

	// Unless we're in the middle of a suspended session, if we're passing 
	// file descriptors around, wait for one to be passed to us, otherwise,
	// accept on the unix/inet sockets. 
	if (!suspendedsession && cfgfl->getPassDescriptor()) {

		// get what we're supposed to do...
		uint16_t	command;
		if (handoffsockun.read(&command)!=sizeof(uint16_t)) {
			dbgfile.debugPrint("connection",1,"read failed");
			dbgfile.debugPrint("connection",0,
						"done waiting for client");
			// If this fails, then the listener most likely died
			// because sqlr-stop was run.  Arguably this condition
			// should initiate a shut down of this process as well,
			// but for now we'll just wait to be shut down manually.
			// Unfortunatley, that means looping over and over,
			// with that read failing every time.  We'll sleep so
			// as not to slam the machine while we loop.
			snooze::microsnooze(0,100000);
			return -1;
		}

		// if we're supposed to reconnect, then just do that...
		if (command==HANDOFF_RECONNECT) {
			return 2;
		}

		// receive the descriptor and use it, if we failed to get the
		// descriptor, delete the socket and return failure
		int32_t	descriptor;
		if (!receiveFileDescriptor(&descriptor)) {
			dbgfile.debugPrint("connection",1,"pass failed");
			dbgfile.debugPrint("connection",0,
						"done waiting for client");
			// If this fails, then the listener most likely died
			// because sqlr-stop was run.  Arguably this condition
			// should initiate a shut down of this process as well,
			// but for now we'll just wait to be shut down manually.
			// Unfortunatley, that means looping over and over,
			// with that read above failing every time, thus the
			//  sleep so as not to slam the machine while we loop.
			return -1;
		}

		clientsock=new filedescriptor;
		clientsock->setFileDescriptor(descriptor);

		// For some reason, at least on OpenBSD 4.9, this
		// filedescriptor is getting created in non-blocking mode.
		// Force it into to blocking mode.
		clientsock->useBlockingMode();

		dbgfile.debugPrint("connection",1,"pass succeeded");
		dbgfile.debugPrint("connection",0,"done waiting for client");

	} else {

		if (waitForNonBlockingRead(accepttimeout,0)<1) {
			dbgfile.debugPrint("connection",0,
					"wait for non blocking read failed");
			// FIXME: I think this should return 0
			return -1;
		}

		// get the first socket that had data available...
		filedescriptor	*fd=NULL;
		if (!getReadyList()->getDataByIndex(0,&fd)) {
			dbgfile.debugPrint("connection",0,
						"ready list was empty");
			// FIXME: I think this should return 0
			return -1;
		}

		inetserversocket	*iss=NULL;
		for (uint64_t index=0; index<serversockincount; index++) {
			if (fd==serversockin[index]) {
				iss=serversockin[index];
			}
		}
		if (iss) {
			clientsock=iss->accept();
		} else if (fd==serversockun) {
			clientsock=serversockun->accept();
		}

		if (fd) {
			dbgfile.debugPrint("connection",1,
						"reconnect succeeded");
		} else {
			dbgfile.debugPrint("connection",1,
						"reconnect failed");
		}
		dbgfile.debugPrint("connection",0,"done waiting for client");

		if (!fd) {
			// FIXME: I think this should return 0
			return -1;
		}
	}

	clientsock->translateByteOrder();
	clientsock->dontUseNaglesAlgorithm();
	// FIXME: use bandwidth delay product to tune these
	// SO_SNDBUF=0 causes no data to ever be sent on openbsd
	//clientsock->setTcpReadBufferSize(8192);
	//clientsock->setTcpWriteBufferSize(0);
	clientsock->setReadBufferSize(8192);
	clientsock->setWriteBufferSize(8192);
	return 1;
}

void sqlrcontroller_svr::announceAvailability(const char *tmpdir,
					bool passdescriptor,
					const char *unixsocket,
					unsigned short inetport,
					const char *connectionid) {

	dbgfile.debugPrint("connection",0,"announcing availability...");

	// if we're passing around file descriptors, connect to listener 
	// if we haven't already and pass it this process's pid
	if (passdescriptor && !connected) {
		registerForHandoff(tmpdir);
	}

	// handle time-to-live
	if (ttl>0) {
		signalmanager::alarm(ttl);
	}

	acquireAnnounceMutex();

	// cancel time-to-live alarm
	//
	// It's important to cancel the alarm here.
	//
	// Connections which successfully announce themselves to the listener
	// cannot then die off.
	//
	// If handoff=pass, the listener can handle it if a connection dies off,
	// but not if handoff=reconnect, there's no way for it to know the
	// connection is gone.
	//
	// But, if the connection signals the listener to read the registration
	// and dies off before it receives a return signal from the listener
	// indicating that the listener has read it, then it can cause
	// problems.  And we can't simply call waitWithUndo() and
	// signalWithUndo().  Not only could the undo counter could overflow,
	// but we really only want to undo the signal() if the connection shuts
	// down before doing the wait() and there's no way to optionally
	// undo a semaphore.
	//
	// What a mess.
	if (ttl>0) {
		signalmanager::alarm(0);
	}

	updateState(ANNOUNCE_AVAILABILITY);

	// get a pointer to the shared memory segment
	shmdata	*idmemoryptr=getAnnounceBuffer();

	// first, write the connectionid into the segment
	charstring::copy(idmemoryptr->connectionid,
				connectionid,MAXCONNECTIONIDLEN);

	// if we're passing descriptors around, write the 
	// pid to the segment otherwise write ports
	if (passdescriptor) {

		dbgfile.debugPrint("connection",1,"handoff=pass");

		// write the pid into the segment
		idmemoryptr->connectioninfo.connectionpid=
						process::getProcessId();

	} else {

		dbgfile.debugPrint("connection",1,"handoff=reconnect");

		// convert the port to network byte order and write it into
		// the segment.
		idmemoryptr->connectioninfo.sockets.inetport=inetport;

		// write the unix socket name into the segment
		if (unixsocket && unixsocket[0]) {
			charstring::copy(idmemoryptr->connectioninfo.
							sockets.unixsocket,
							unixsocket,
							MAXUNIXSOCKETLEN);
		}
	}

	signalListenerToRead();

	waitForListenerToFinishReading();

	releaseAnnounceMutex();

	dbgfile.debugPrint("connection",0,"done announcing availability...");
}

void sqlrcontroller_svr::registerForHandoff(const char *tmpdir) {

	dbgfile.debugPrint("connection",0,"registering for handoff...");

	// construct the name of the socket to connect to
	size_t	handoffsocknamelen=charstring::length(tmpdir)+9+
				charstring::length(cmdl->getId())+8+1;
	char	*handoffsockname=new char[handoffsocknamelen];
	snprintf(handoffsockname,handoffsocknamelen,
			"%s/sockets/%s-handoff",tmpdir,cmdl->getId());

	size_t	stringlen=17+charstring::length(handoffsockname)+1;
	char	*string=new char[stringlen];
	snprintf(string,stringlen,"handoffsockname: %s",handoffsockname);
	dbgfile.debugPrint("connection",1,string);
	delete[] string;

	// Try to connect over and over forever on 1 second intervals.
	// If the connect succeeds but the write fails, loop back and
	// try again.
	connected=false;
	for (;;) {

		dbgfile.debugPrint("connection",1,"trying...");

		if (handoffsockun.connect(handoffsockname,-1,-1,1,0)==
							RESULT_SUCCESS) {
			if (handoffsockun.write(
				(uint32_t)process::getProcessId())==
							sizeof(uint32_t)) {
				connected=true;
				break;
			}
			deRegisterForHandoff(tmpdir);
		}
		snooze::macrosnooze(1);
	}

	dbgfile.debugPrint("connection",0,"done registering for handoff");

	delete[] handoffsockname;
}

bool sqlrcontroller_svr::receiveFileDescriptor(int32_t *descriptor) {
	bool	retval=handoffsockun.receiveFileDescriptor(descriptor);
	if (!retval) {
		// FIXME: should we just close here, or re-connect?
		handoffsockun.close();
		connected=false;
	}
	return retval;
}

void sqlrcontroller_svr::deRegisterForHandoff(const char *tmpdir) {
	
	dbgfile.debugPrint("connection",0,"de-registering for handoff...");

	// construct the name of the socket to connect to
	size_t	removehandoffsocknamelen=charstring::length(tmpdir)+9+
					charstring::length(cmdl->getId())+14+1;
	char	*removehandoffsockname=new char[removehandoffsocknamelen];
	snprintf(removehandoffsockname,removehandoffsocknamelen,
			"%s/sockets/%s-removehandoff",tmpdir,cmdl->getId());

	size_t	stringlen=23+charstring::length(removehandoffsockname)+1;
	char	*string=new char[stringlen];
	snprintf(string,stringlen,
			"removehandoffsockname: %s",removehandoffsockname);
	dbgfile.debugPrint("connection",1,string);
	delete[] string;

	// attach to the socket and write the process id
	unixclientsocket	removehandoffsockun;
	removehandoffsockun.connect(removehandoffsockname,-1,-1,0,1);
	removehandoffsockun.write((uint32_t)process::getProcessId());

	dbgfile.debugPrint("connection",0,"done de-registering for handoff");

	delete[] removehandoffsockname;
}

void sqlrcontroller_svr::clientSession() {

	dbgfile.debugPrint("connection",0,"client session...");

	inclientsession=true;

	// update various stats
	updateState(SESSION_START);
	updateClientAddr();
	updateClientSessionStartTime();
	incrementOpenClientConnections();

	// During each session, the client will send a series of commands.
	// The session ends when the client ends it or when certain commands
	// fail.
	bool	loop=true;
	bool	endsession=true;
	do {

		// get a command from the client
		uint16_t	command;
		if (!getCommand(&command)) {
			break;
		}

		// get the command start time
		timeval		tv;
		gettimeofday(&tv,NULL);

		// these commands are all handled at the connection level
		if (command==AUTHENTICATE) {
			incrementAuthenticateCount();
			if (authenticateCommand()) {
				sessionStartQueries();
				continue;
			}
			endsession=false;
			break;
		} else if (command==SUSPEND_SESSION) {
			incrementSuspendSessionCount();
			suspendSessionCommand();
			endsession=false;
			break;
		} else if (command==END_SESSION) {
			incrementEndSessionCount();
			break;
		} else if (command==PING) {
			incrementPingCount();
			pingCommand();
			continue;
		} else if (command==IDENTIFY) {
			incrementIdentifyCount();
			identifyCommand();
			continue;
		} else if (command==AUTOCOMMIT) {
			incrementAutocommitCount();
			autoCommitCommand();
			continue;
		} else if (command==BEGIN) {
			incrementBeginCount();
			beginCommand();
			continue;
		} else if (command==COMMIT) {
			incrementCommitCount();
			commitCommand();
			continue;
		} else if (command==ROLLBACK) {
			incrementRollbackCount();
			rollbackCommand();
			continue;
		} else if (command==DBVERSION) {
			incrementDbVersionCount();
			dbVersionCommand();
			continue;
		} else if (command==BINDFORMAT) {
			incrementBindFormatCount();
			bindFormatCommand();
			continue;
		} else if (command==SERVERVERSION) {
			incrementServerVersionCount();
			serverVersionCommand();
			continue;
		} else if (command==SELECT_DATABASE) {
			incrementSelectDatabaseCount();
			selectDatabaseCommand();
			continue;
		} else if (command==GET_CURRENT_DATABASE) {
			incrementGetCurrentDatabaseCount();
			getCurrentDatabaseCommand();
			continue;
		} else if (command==GET_LAST_INSERT_ID) {
			incrementGetLastInsertIdCount();
			getLastInsertIdCommand();
			continue;
		}

		// For the rest of the commands,
		// the client will request a cursor
		sqlrcursor_svr	*cursor=getCursor(command);
		if (!cursor) {
			noAvailableCursors(command);
			continue;
		}

		// keep track of the command start time
		cursor->commandstartsec=tv.tv_sec;
		cursor->commandstartusec=tv.tv_usec;

		// these commands are all handled at the cursor level
		if (command==NEW_QUERY) {
			incrementNewQueryCount();
			loop=newQueryCommand(cursor);
		} else if (command==REEXECUTE_QUERY) {
			incrementReexecuteQueryCount();
			loop=reExecuteQueryCommand(cursor);
		} else if (command==FETCH_FROM_BIND_CURSOR) {
			incrementFetchFromBindCursorCount();
			loop=fetchFromBindCursorCommand(cursor);
		} else if (command==FETCH_RESULT_SET) {
			incrementFetchResultSetCount();
			loop=fetchResultSetCommand(cursor);
		} else if (command==ABORT_RESULT_SET) {
			incrementAbortResultSetCount();
			abortResultSetCommand(cursor);
		} else if (command==SUSPEND_RESULT_SET) {
			incrementSuspendResultSetCount();
			suspendResultSetCommand(cursor);
		} else if (command==RESUME_RESULT_SET) {
			incrementResumeResultSetCount();
			loop=resumeResultSetCommand(cursor);
		} else if (command==GETDBLIST) {
			incrementGetDbListCount();
			loop=getDatabaseListCommand(cursor);
		} else if (command==GETTABLELIST) {
			incrementGetTableListCount();
			loop=getTableListCommand(cursor);
		} else if (command==GETCOLUMNLIST) {
			incrementGetColumnListCount();
			loop=getColumnListCommand(cursor);
		} else if (command==GET_QUERY_TREE) {
			incrementGetQueryTreeCount();
			loop=getQueryTreeCommand(cursor);
		} else {
			loop=false;
		}

		// get the command end-time
		gettimeofday(&tv,NULL);
		cursor->commandendsec=tv.tv_sec;
		cursor->commandendusec=tv.tv_usec;

		// log query-related commands
		// FIXME: should we really log bind cursor fetches?
		// FIXME: this won't log triggers
		if (sqlrlg &&
			(command==NEW_QUERY ||
			command==REEXECUTE_QUERY ||
			command==FETCH_FROM_BIND_CURSOR)) {
			sqlrlg->runLoggers(conn,cursor);
		}

	} while (loop);

	if (endsession) {
		endSession();
	}

	closeClientSocket();

	closeSuspendedSessionSockets();

	decrementOpenClientConnections();
	inclientsession=false;

	dbgfile.debugPrint("connection",0,"done with client session");
}

bool sqlrcontroller_svr::getCommand(uint16_t *command) {

	dbgfile.debugPrint("connection",1,"getting command...");

	updateState(GET_COMMAND);

	// get the command
	if (clientsock->read(command,idleclienttimeout,0)!=sizeof(uint16_t)) {
		dbgfile.debugPrint("connection",1,
		"getting command failed: client sent bad command or timed out");
		return false;
	}

	dbgfile.debugPrint("connection",1,"done getting command");
	return true;
}

sqlrcursor_svr *sqlrcontroller_svr::getCursor(uint16_t command) {

	dbgfile.debugPrint("connection",1,"getting a cursor...");

	// does the client need a cursor or does it already have one
	uint16_t	neednewcursor=DONT_NEED_NEW_CURSOR;
	if ((command==NEW_QUERY ||
		command==GETDBLIST ||
		command==GETTABLELIST ||
		command==GETCOLUMNLIST ||
		command==ABORT_RESULT_SET ||
		command==GET_QUERY_TREE) &&
		clientsock->read(&neednewcursor,
				idleclienttimeout,0)!=sizeof(uint16_t)) {
		dbgfile.debugPrint("connection",2,
			"client cursor request failed, need new cursor stage");
		return NULL;
	}

	sqlrcursor_svr	*cursor=NULL;

	if (neednewcursor==DONT_NEED_NEW_CURSOR) {

		// which cursor is the client requesting?
		uint16_t	id;
		if (clientsock->read(&id,
				idleclienttimeout,0)!=sizeof(uint16_t)) {
			dbgfile.debugPrint("connection",2,
				"client cursor request failed, "
				"cursor id stage");
			return NULL;
		}

		// get the current cursor that they requested.
		bool	found=false;
		for (uint16_t i=0; i<cursorcount; i++) {
			if (cur[i]->id==id) {
				cursor=cur[i];
				incrementTimesCursorReused();
				found=true;
				break;
			}
		}

		// don't allow the client to request a cursor 
		// beyond the end of the array.
		if (!found) {
			dbgfile.debugPrint("connection",2,
				"client requested an invalid cursor:");
			dbgfile.debugPrint("connection",3,(int32_t)id);
			return NULL;
		}

	} else {

		// find an available cursor
		cursor=findAvailableCursor();

 		// mark this as a new cursor being used
		if (cursor) {
			incrementTimesNewCursorUsed();
		}
	}

	dbgfile.debugPrint("connection",1,"done getting a cursor");
	return cursor;
}

sqlrcursor_svr *sqlrcontroller_svr::findAvailableCursor() {

	// find an available cursor
	for (uint16_t i=0; i<cursorcount; i++) {
		if (cur[i]->state==SQLRCURSORSTATE_AVAILABLE) {
			dbgfile.debugPrint("connection",2,"available cursor:");
			dbgfile.debugPrint("connection",3,(int32_t)i);
			cur[i]->state=SQLRCURSORSTATE_BUSY;
			return cur[i];
		}
	}

	// apparently there weren't any available cursors...

	// if we can't create any new cursors then return an error
	if (cursorcount==maxcursorcount) {
		dbgfile.debugPrint("connection",2,"all cursors are busy");
		return NULL;
	}

	// create new cursors
	uint16_t	expandto=cursorcount+cfgfl->getCursorsGrowBy();
	if (expandto>=maxcursorcount) {
		expandto=maxcursorcount;
	}
	uint16_t	firstnewcursor=cursorcount;
	do {
		cur[cursorcount]=initCursor();
		cur[cursorcount]->state=SQLRCURSORSTATE_AVAILABLE;
		if (!cur[cursorcount]->openInternal(cursorcount)) {
			dbgfile.debugPrint("connection",1,
					"cursor init failure...");
			logOut();
			return NULL;
		}
		cursorcount++;
	} while (cursorcount<expandto);
	
	// return the first new cursor that we created
	cur[firstnewcursor]->state=SQLRCURSORSTATE_BUSY;
	return cur[firstnewcursor];
}

void sqlrcontroller_svr::noAvailableCursors(uint16_t command) {

	// If no cursor was available, the client
	// cound send an entire query and bind vars
	// before it reads the error and closes the
	// socket.  We have to absorb all of that
	// data.  We shouldn't just loop forever
	// though, that would provide a point of entry
	// for a DOS attack.  We'll read the maximum
	// number of bytes that could be sent.
	uint32_t	size=(
				// query size and query
				sizeof(uint32_t)+maxquerysize+
				// input bind var count
				sizeof(uint16_t)+
				// input bind vars
				maxbindcount*(2*sizeof(uint16_t)+
						maxbindnamelength)+
				// output bind var count
				sizeof(uint16_t)+
				// output bind vars
				maxbindcount*(2*sizeof(uint16_t)+
						maxbindnamelength)+
				// get column info
				sizeof(uint16_t)+
				// skip/fetch
				2*sizeof(uint32_t));

	clientsock->useNonBlockingMode();
	unsigned char	*dummy=new unsigned char[size];
	clientsock->read(dummy,size,idleclienttimeout,0);
	clientsock->useBlockingMode();
	delete[] dummy;

	// indicate that an error has occurred
	clientsock->write((uint16_t)ERROR_OCCURRED);

	// send the error code
	clientsock->write((uint64_t)SQLR_ERROR_NOCURSORS);

	// send the error itself
	uint16_t	len=charstring::length(SQLR_ERROR_NOCURSORS_STRING);
	clientsock->write(len);
	clientsock->write(SQLR_ERROR_NOCURSORS_STRING,len);
	flushWriteBuffer();
}

void sqlrcontroller_svr::closeClientSocket() {

	// Sometimes the server sends the result set and closes the socket
	// while part of it is buffered but not yet transmitted.  This causes
	// the client to receive a partial result set or error.  Telling the
	// socket to linger doesn't always fix it.  Doing a read here should 
	// guarantee that the client will close its end of the connection 
	// before the server closes its end; the server will wait for data 
	// from the client (which it will never receive) and when the client 
	// closes its end (which it will only do after receiving the entire
	// result set) the read will fall through.  This should guarantee 
	// that the client will get the the entire result set without
	// requiring the client to send data back indicating so.
	dbgfile.debugPrint("connection",1,
			"waiting for client to close the connection...");
	uint16_t	dummy;
	clientsock->read(&dummy,idleclienttimeout,0);
	dbgfile.debugPrint("connection",1,
			"done waiting for client to close the connection");

	// close the client socket
	dbgfile.debugPrint("connection",1,
			"closing the client socket...");
	clientsock->close();
	delete clientsock;
	dbgfile.debugPrint("connection",1,
			"done closing the client socket");
}

void sqlrcontroller_svr::closeSuspendedSessionSockets() {

	// If we're no longer in a suspended session and we we're passing 
	// around file descriptors but had to open a set of sockets to handle 
	// a suspended session, close those sockets here.
	if (!suspendedsession && cfgfl->getPassDescriptor()) {
		dbgfile.debugPrint("connection",1,
			"closing sockets from a previously "
			"suspended session...");
		if (serversockun) {
			removeFileDescriptor(serversockun);
			delete serversockun;
			serversockun=NULL;
		}
		if (serversockin) {
			for (uint64_t index=0;
					index<serversockincount;
					index++) {
				removeFileDescriptor(serversockin[index]);
				delete serversockin[index];
				serversockin[index]=NULL;
			}
			delete[] serversockin;
			serversockin=NULL;
			serversockincount=0;
		}
		dbgfile.debugPrint("connection",1,
			"done closing sockets from a previously "
			"suspended session...");
	}
}

void sqlrcontroller_svr::sessionStartQueries() {
	// run a configurable set of queries at the start of each session
	for (stringlistnode *node=
		cfgfl->getSessionStartQueries()->getFirstNode();
						node; node=node->getNext()) {
		sessionQuery(node->getData());
	}
}

void sqlrcontroller_svr::sessionEndQueries() {
	// run a configurable set of queries at the end of each session
	for (stringlistnode *node=
		cfgfl->getSessionEndQueries()->getFirstNode();
						node; node=node->getNext()) {
		sessionQuery(node->getData());
	}
}

void sqlrcontroller_svr::sessionQuery(const char *query) {

	// create the select database query
	size_t	querylen=charstring::length(query);

	sqlrcursor_svr	*cur=initCursor();

	// since we're creating a new cursor for this, make sure it
	// can't have an ID that might already exist
	if (cur->openInternal(cursorcount+1) &&
		cur->prepareQuery(query,querylen) &&
		executeQuery(cur,query,querylen)) {
		cur->cleanUpData();
	}
	cur->close();
	deleteCursor(cur);
}

bool sqlrcontroller_svr::authenticateCommand() {

	dbgfile.debugPrint("connection",1,"authenticate");

	// get the user/password from the client and authenticate
	if (!getUserFromClient() ||
		!getPasswordFromClient() ||
		!authenticate()) {

		// indicate that an error has occurred
		clientsock->write((uint16_t)ERROR_OCCURRED);
		clientsock->write((uint64_t)SQLR_ERROR_AUTHENTICATIONERROR);
		clientsock->write((uint16_t)charstring::length(
					SQLR_ERROR_AUTHENTICATIONERROR_STRING));
		clientsock->write(SQLR_ERROR_AUTHENTICATIONERROR_STRING);
		flushWriteBuffer();
		conn->endSession();
		return false;
	}

	// indicate that no error has occurred
	clientsock->write((uint16_t)NO_ERROR_OCCURRED);
	flushWriteBuffer();
	return true;
}

bool sqlrcontroller_svr::getUserFromClient() {
	uint32_t	size=0;
	if (clientsock->read(&size,idleclienttimeout,0)==sizeof(uint32_t) &&
		size<sizeof(userbuffer) &&
		(uint32_t)(clientsock->read(userbuffer,size,
						idleclienttimeout,0))==size) {
		userbuffer[size]='\0';
		return true;
	}
	dbgfile.debugPrint("connection",1,
		"authentication failed: user size is wrong");
	return false;
}

bool sqlrcontroller_svr::getPasswordFromClient() {
	uint32_t size=0;
	if (clientsock->read(&size,idleclienttimeout,0)==sizeof(uint32_t) &&
		size<sizeof(passwordbuffer) &&
		(uint32_t)(clientsock->read(passwordbuffer,size,
						idleclienttimeout,0))==size) {
		passwordbuffer[size]='\0';
		return true;
	}
	dbgfile.debugPrint("connection",1,
		"authentication failed: password size is wrong");
	return false;
}

bool sqlrcontroller_svr::authenticate() {

	dbgfile.debugPrint("connection",1,"authenticate...");

	// authenticate on the approprite tier
	bool	authondb=(cfgfl->getAuthOnDatabase() &&
				conn->supportsAuthOnDatabase());
	bool	authonconnection=(cfgfl->getAuthOnConnection() ||
					(cfgfl->getAuthOnDatabase() &&
					!conn->supportsAuthOnDatabase()));
	if (authonconnection) {
		return connectionBasedAuth(userbuffer,passwordbuffer);
	} else if (authondb) {
		return databaseBasedAuth(userbuffer,passwordbuffer);
	}

	dbgfile.debugPrint("connection",1,
				"authentication was done on listener");
	return true;
}

bool sqlrcontroller_svr::connectionBasedAuth(const char *userbuffer,
						const char *passwordbuffer) {

	// handle connection-based authentication
	int	retval=authc->authenticate(userbuffer,passwordbuffer);
	if (retval) {
		dbgfile.debugPrint("connection",1,
			"connection-based authentication succeeded");
	} else {
		dbgfile.debugPrint("connection",1,
			"connection-based authentication failed: invalid user/password");
	}
	return retval;
}

bool sqlrcontroller_svr::databaseBasedAuth(const char *userbuffer,
						const char *passwordbuffer) {

	// if the user we want to change to is different from the
	// user that's currently proxied, try to change to that user
	bool	authsuccess;
	if ((!lastuserbuffer[0] && !lastpasswordbuffer[0]) || 
		charstring::compare(lastuserbuffer,userbuffer) ||
		charstring::compare(lastpasswordbuffer,passwordbuffer)) {

		// change authentication 
		dbgfile.debugPrint("connection",2,"change user");
		authsuccess=conn->changeUser(userbuffer,passwordbuffer);

		// keep a record of which user we're changing to
		// and whether that user was successful in 
		// authenticating
		charstring::copy(lastuserbuffer,userbuffer);
		charstring::copy(lastpasswordbuffer,passwordbuffer);
		lastauthsuccess=authsuccess;
	}

	if (lastauthsuccess) {
		dbgfile.debugPrint("connection",1,
			"database-based authentication succeeded");
	} else {
		dbgfile.debugPrint("connection",1,
			"database-based authentication failed: invalid user/password");
	}
	return lastauthsuccess;
}

void sqlrcontroller_svr::suspendSessionCommand() {

	dbgfile.debugPrint("connection",1,"suspending session...");

	// mark the session suspended
	suspendedsession=true;

	// we can't wait forever for the client to resume, set a timeout
	accepttimeout=cfgfl->getSessionTimeout();

	// abort all cursors that aren't suspended...
	dbgfile.debugPrint("connection",2,"aborting busy cursors...");
	for (int32_t i=0; i<cursorcount; i++) {
		if (cur[i]->state==SQLRCURSORSTATE_BUSY) {
			cur[i]->abort();
		}
	}
	dbgfile.debugPrint("connection",2,"done aborting busy cursors");

	// If we're passing file descriptors around, we'll have to listen on a 
	// set of ports so the suspended client has something to resume to.
	// It's possible that the current session is just a resumed session
	// though.  In that case, no new sockets will be opened by the call to
	// openSockets(), the old ones will just be reused.
	if (cfgfl->getPassDescriptor()) {

		// open sockets to resume on
		dbgfile.debugPrint("connection",2,
					"opening sockets to resume on...");
		uint16_t	unixsocketsize=0;
		uint16_t	inetportnumber=0;
		if (openSockets()) {
			if (serversockun) {
				unixsocketsize=charstring::length(unixsocket);
			}
			inetportnumber=inetport;
		}
		dbgfile.debugPrint("connection",2,
					"done opening sockets to resume on");

		// pass the socket info to the client
		dbgfile.debugPrint("connection",2,
				"passing socket info to client...");
		clientsock->write(unixsocketsize);
		if (unixsocketsize) {
			clientsock->write(unixsocket,unixsocketsize);
		}
		clientsock->write(inetportnumber);
		flushWriteBuffer();
		dbgfile.debugPrint("connection",2,
				"done passing socket info to client");
	}

	dbgfile.debugPrint("connection",1,"done suspending session");
}

void sqlrcontroller_svr::endSession() {

	dbgfile.debugPrint("connection",2,"ending session...");

	updateState(SESSION_END);

	dbgfile.debugPrint("connection",2,"aborting all cursors...");
	for (int32_t i=0; i<cursorcount; i++) {
		if (cur[i]) {
			cur[i]->abort();
		}
	}
	dbgfile.debugPrint("connection",2,"done aborting all cursors");

	// truncate/drop temp tables
	// (Do this before running the end-session queries becuase
	// with oracle, it may be necessary to log out and log back in to
	// drop a temp table.  With each log-out the session end queries
	// are run and with each log-in the session start queries are run.)
	truncateTempTables(cur[0],&sessiontemptablesfortrunc);
	dropTempTables(cur[0],&sessiontemptablesfordrop);

	sessionEndQueries();

	// must set suspendedsession to false here so resumed sessions won't 
	// automatically re-suspend
	suspendedsession=false;

	// if we're faking transaction blocks and the session was ended but we
	// haven't ended the transaction block, then we need to rollback and
	// end the block
	if (intransactionblock) {

		rollback();
		intransactionblock=false;

	} else if (conn->isTransactional() && commitorrollback) {

		// otherwise, commit or rollback as necessary
		if (cfgfl->getEndOfSessionCommit()) {
			dbgfile.debugPrint("connection",2,
						"committing...");
			commit();
			dbgfile.debugPrint("connection",2,
						"done committing...");
		} else {
			dbgfile.debugPrint("connection",2,
						"rolling back...");
			rollback();
			dbgfile.debugPrint("connection",2,
						"done rolling back...");
		}
	}

	// reset database/schema
	if (dbselected) {
		// FIXME: we're ignoring the result and error,
		// should we do something if there's an error?
		conn->selectDatabase(originaldb);
		dbselected=false;
	}

	// reset autocommit behavior
	setAutoCommit(conn->autocommit);

	// set isolation level
	conn->setIsolationLevel(isolationlevel);

	// reset sql translation
	if (sqlt) {
		sqlt->endSession();
	}

	// shrink the cursor array, if necessary
	while (cursorcount>mincursorcount) {
		cursorcount--;
		deleteCursor(cur[cursorcount]);
		cur[cursorcount]=NULL;
	}

	// end the session
	conn->endSession();

	dbgfile.debugPrint("connection",1,"done ending session");
}

void sqlrcontroller_svr::cleanUpAllCursorData() {
	dbgfile.debugPrint("connection",2,"cleaning up all cursors...");
	for (int32_t i=0; i<cursorcount; i++) {
		if (cur[i]) {
			cur[i]->cleanUpData();
		}
	}
	dbgfile.debugPrint("connection",2,"done cleaning up all cursors");
}

void sqlrcontroller_svr::selectDatabaseCommand() {

	dbgfile.debugPrint("connection",1,"select database");

	// get length of db parameter
	uint32_t	dblen;
	if (clientsock->read(&dblen,idleclienttimeout,0)!=sizeof(uint32_t)) {
		dbgfile.debugPrint("connection",2,
			"get list failed: client sent bad db length");
		clientsock->write(false);
		return;
	}

	// bounds checking
	if (dblen>maxquerysize) {
		dbgfile.debugPrint("connection",2,
			"get list failed: client sent bad db length");
		clientsock->write(false);
		return;
	}

	// read the db parameter into the buffer
	char	*db=new char[dblen+1];
	if (dblen) {
		if ((uint32_t)(clientsock->read(db,dblen,
					idleclienttimeout,0))!=dblen) {
			dbgfile.debugPrint("connection",2,
				"get list failed: "
				"client sent short db parameter");
			clientsock->write(false);
			flushWriteBuffer();
			delete[] db;
			return;
		}
	}
	db[dblen]='\0';
	
	// Select the db and send back the result.  If we've been told to
	// ignore these calls, skip the actual call but act like it succeeded.
	bool	result=(ignoreselectdb)?true:conn->selectDatabase(db);
	clientsock->write(result);

	// if there was an error, send it back
	if (!result) {
		clientsock->write(conn->errorlength);
		clientsock->write(conn->error,conn->errorlength);
	}

	flushWriteBuffer();
	delete[] db;

	return;
}

void sqlrcontroller_svr::getCurrentDatabaseCommand() {

	dbgfile.debugPrint("connection",1,"get current database");

	// get the current database
	char	*currentdb=conn->getCurrentDatabase();

	// send it to the client
	uint16_t	currentdbsize=charstring::length(currentdb);
	clientsock->write(currentdbsize);
	clientsock->write(currentdb,currentdbsize);
	flushWriteBuffer();

	// clean up
	delete[] currentdb;
}

void sqlrcontroller_svr::getLastInsertIdCommand() {

	dbgfile.debugPrint("connection",1,"getting last insert id");

	// get the last insert id
	uint64_t	id;
	bool	success=conn->getLastInsertId(&id);

	// send success/failure
	clientsock->write(success);
	if (success) {

		// return the id
		clientsock->write(id);

	} else {

		// return the error
		uint16_t	errorlen=charstring::length(conn->error);
		clientsock->write(errorlen);
		clientsock->write(conn->error,errorlen);
	}

	flushWriteBuffer();
}

void sqlrcontroller_svr::pingCommand() {
	dbgfile.debugPrint("connection",1,"ping");
	bool	pingresult=conn->ping();
	clientsock->write(pingresult);
	flushWriteBuffer();
	if (!pingresult) {
		reLogIn();
	}
}

void sqlrcontroller_svr::identifyCommand() {

	dbgfile.debugPrint("connection",1,"identify");

	// get the identification
	const char	*ident=conn->identify();

	// send it to the client
	uint16_t	idlen=charstring::length(ident);
	clientsock->write(idlen);
	clientsock->write(ident,idlen);
	flushWriteBuffer();
}

void sqlrcontroller_svr::beginCommand() {
	dbgfile.debugPrint("connection",1,"begin...");
	if (begin()) {
		dbgfile.debugPrint("connection",1,"begin succeeded");
		clientsock->write((uint16_t)NO_ERROR_OCCURRED);
	} else {
		dbgfile.debugPrint("connection",1,"begin failed");
		returnError(!conn->liveconnection);
	}
	flushWriteBuffer();
}

bool sqlrcontroller_svr::begin() {
	// if we're faking transaction blocks, do that,
	// otherwise run an actual begin query
	return (faketransactionblocks)?
			beginFakeTransactionBlock():conn->begin();
}

void sqlrcontroller_svr::setAutoCommitBehavior(bool ac) {
	conn->autocommit=ac;
}

void sqlrcontroller_svr::autoCommitCommand() {
	dbgfile.debugPrint("connection",1,"autocommit...");
	bool	on;
	if (clientsock->read(&on,idleclienttimeout,0)==sizeof(bool)) {
		if (on) {
			dbgfile.debugPrint("connection",2,"autocommit on");
			clientsock->write(autoCommitOn());
		} else {
			dbgfile.debugPrint("connection",2,"autocommit off");
			clientsock->write(autoCommitOff());
		}
	}
	flushWriteBuffer();
}

void sqlrcontroller_svr::setAutoCommit(bool ac) {
	dbgfile.debugPrint("connection",0,"setting autocommit...");
	if (ac) {
		if (!autoCommitOn()) {
			dbgfile.debugPrint("connection",0,
					"setting autocommit on failed");
			fprintf(stderr,"Couldn't set autocommit on.\n");
			return;
		}
	} else {
		if (!autoCommitOff()) {
			dbgfile.debugPrint("connection",0,
					"setting autocommit off failed");
			fprintf(stderr,"Couldn't set autocommit off.\n");
			return;
		}
	}
	dbgfile.debugPrint("connection",0,"done setting autocommit");
}

bool sqlrcontroller_svr::autoCommitOn() {
	autocommitforthissession=true;
	return conn->autoCommitOn();
}

bool sqlrcontroller_svr::autoCommitOff() {
	autocommitforthissession=false;
	return conn->autoCommitOff();
}

void sqlrcontroller_svr::commitCommand() {
	dbgfile.debugPrint("connection",1,"commit...");
	if (commit()) {
		dbgfile.debugPrint("connection",1,"commit succeeded");
		clientsock->write((uint16_t)NO_ERROR_OCCURRED);
	} else {
		dbgfile.debugPrint("connection",1,"commit failed");
		returnError(!conn->liveconnection);
	}
	flushWriteBuffer();
}

bool sqlrcontroller_svr::commit() {
	if (conn->commit()) {
		endFakeTransactionBlock();
		return true;
	}
	return false;
}

void sqlrcontroller_svr::incrementConnectionCount() {

	dbgfile.debugPrint("connection",0,"incrementing connection count...");

	if (scalerspawned) {

		dbgfile.debugPrint("connection",0,"scaler will do the job");
		signalScalerToRead();

	} else {

		acquireConnectionCountMutex();

		// increment the counter
		shm->totalconnections++;
		decrementonclose=true;

		dbgfile.debugPrint("connection",1,shm->totalconnections);

		releaseConnectionCountMutex();
	}

	dbgfile.debugPrint("connection",0,"done incrementing connection count");
}

void sqlrcontroller_svr::decrementConnectionCount() {

	dbgfile.debugPrint("connection",0,"decrementing connection count...");

	if (scalerspawned) {

		dbgfile.debugPrint("connection",0,"scaler will do the job");

	} else {

		acquireConnectionCountMutex();

		if (shm->totalconnections) {
			shm->totalconnections--;
		}
		decrementonclose=false;

		dbgfile.debugPrint("connection",1,shm->totalconnections);

		releaseConnectionCountMutex();
	}

	dbgfile.debugPrint("connection",0,"done decrementing connection count");
}

void sqlrcontroller_svr::decrementConnectedClientCount() {

	dbgfile.debugPrint("connection",0,"decrementing session count...");

	if (!semset->waitWithUndo(5)) {
		// FIXME: bail somehow
	}

	// increment the connections-in-use count
	if (shm->connectedclients) {
		shm->connectedclients--;
	}

	// update the peak connections-in-use count
	if (shm->connectedclients>shm->peak_connectedclients) {
		shm->peak_connectedclients=shm->connectedclients;
	}

	// update the peak connections-in-use over the previous minute count
	datetime	dt;
	dt.getSystemDateAndTime();
	if (shm->connectedclients>shm->peak_connectedclients_1min ||
		dt.getEpoch()/60>shm->peak_connectedclients_1min_time/60) {
		shm->peak_connectedclients_1min=shm->connectedclients;
		shm->peak_connectedclients_1min_time=dt.getEpoch();
	}

	dbgfile.debugPrint("connection",1,shm->connectedclients);

	if (!semset->signalWithUndo(5)) {
		// FIXME: bail somehow
	}

	dbgfile.debugPrint("connection",0,"done decrementing session count");
}

void sqlrcontroller_svr::rollbackCommand() {
	dbgfile.debugPrint("connection",1,"rollback...");
	if (rollback()) {
		dbgfile.debugPrint("connection",1,"rollback succeeded");
		clientsock->write((uint16_t)NO_ERROR_OCCURRED);
	} else {
		dbgfile.debugPrint("connection",1,"rollback failed");
		returnError(!conn->liveconnection);
	}
	flushWriteBuffer();
}

bool sqlrcontroller_svr::rollback() {
	if (conn->rollback()) {
		endFakeTransactionBlock();
		return true;
	}
	return false;
}

void sqlrcontroller_svr::setFakeTransactionBlocksBehavior(bool ftb) {
	faketransactionblocks=ftb;
}

bool sqlrcontroller_svr::handleFakeTransactionQueries(sqlrcursor_svr *cursor,
						bool *wasfaketransactionquery) {

	*wasfaketransactionquery=false;

	// Intercept begins and handle them.  If we're faking begins, commit
	// and rollback queries also need to be intercepted as well, otherwise
	// the query will be sent directly to the db and endFakeBeginTransaction
	// won't get called.
	if (isBeginTransactionQuery(cursor)) {
		*wasfaketransactionquery=true;
		cursor->inbindcount=0;
		cursor->outbindcount=0;
		sendcolumninfo=DONT_SEND_COLUMN_INFO;
		if (intransactionblock) {
			charstring::copy(cursor->error,
				"begin while in transaction block");
			cursor->errorlength=charstring::length(cursor->error);
			cursor->errnum=999999;
			return false;
		}
		return begin();
		// FIXME: if the begin fails and the db api doesn't support
		// a begin command then the connection-level error needs to
		// be copied to the cursor so handleQueryOrBindCursor can
		// report it
	} else if (isCommitQuery(cursor)) {
		*wasfaketransactionquery=true;
		cursor->inbindcount=0;
		cursor->outbindcount=0;
		sendcolumninfo=DONT_SEND_COLUMN_INFO;
		if (!intransactionblock) {
			charstring::copy(cursor->error,
				"commit while not in transaction block");
			cursor->errorlength=charstring::length(cursor->error);
			cursor->errnum=999998;
			return false;
		}
		return commit();
		// FIXME: if the commit fails and the db api doesn't support
		// a commit command then the connection-level error needs to
		// be copied to the cursor so handleQueryOrBindCursor can
		// report it
	} else if (isRollbackQuery(cursor)) {
		*wasfaketransactionquery=true;
		cursor->inbindcount=0;
		cursor->outbindcount=0;
		sendcolumninfo=DONT_SEND_COLUMN_INFO;
		if (!intransactionblock) {
			charstring::copy(cursor->error,
				"rollback while not in transaction block");
			cursor->errorlength=charstring::length(cursor->error);
			cursor->errnum=999997;
			return false;
		}
		return rollback();
		// FIXME: if the rollback fails and the db api doesn't support
		// a rollback command then the connection-level error needs to
		// be copied to the cursor so handleQueryOrBindCursor can
		// report it
	}
	return false;
}

bool sqlrcontroller_svr::isBeginTransactionQuery(sqlrcursor_svr *cursor) {

	// find the start of the actual query
	const char	*ptr=cursor->skipWhitespaceAndComments(
						cursor->querybuffer);

	// See if it was any of the different queries used to start a
	// transaction.  IMPORTANT: don't just look for the first 5 characters
	// to be "BEGIN", make sure it's the entire query.  Many db's use
	// "BEGIN" to start a stored procedure block, but in those cases,
	// something will follow it.
	if (!charstring::compareIgnoringCase(ptr,"BEGIN",5)) {

		// make sure there are only spaces, comments or the word "work"
		// after the begin
		const char	*spaceptr=
				cursor->skipWhitespaceAndComments(ptr+5);
		
		if (!charstring::compareIgnoringCase(spaceptr,"WORK",4) ||
			*spaceptr=='\0') {
			return true;
		}
		return false;

	} else if (!charstring::compareIgnoringCase(ptr,"START ",6)) {
		return true;
	}
	return false;
}

bool sqlrcontroller_svr::beginFakeTransactionBlock() {

	// save the current autocommit state
	faketransactionblocksautocommiton=autocommitforthissession;

	// if autocommit is on, turn it off
	if (autocommitforthissession) {
		if (!autoCommitOff()) {
			return false;
		}
	}
	intransactionblock=true;
	return true;
}

bool sqlrcontroller_svr::endFakeTransactionBlock() {

	// if we're faking begins and autocommit is on,
	// reset autocommit behavior
	if (faketransactionblocks && faketransactionblocksautocommiton) {
		if (!autoCommitOn()) {
			return false;
		}
	}
	intransactionblock=false;
	return true;
}

bool sqlrcontroller_svr::isCommitQuery(sqlrcursor_svr *cursor) {
	return !charstring::compareIgnoringCase(
			cursor->skipWhitespaceAndComments(
						cursor->querybuffer),
			"commit",6);
}

bool sqlrcontroller_svr::isRollbackQuery(sqlrcursor_svr *cursor) {
	return !charstring::compareIgnoringCase(
			cursor->skipWhitespaceAndComments(
						cursor->querybuffer),
			"rollback",8);
}

bool sqlrcontroller_svr::newQueryCommand(sqlrcursor_svr *cursor) {
	dbgfile.debugPrint("connection",1,"new query");
	return handleQueryOrBindCursor(cursor,false,false,true);
}

bool sqlrcontroller_svr::getDatabaseListCommand(sqlrcursor_svr *cursor) {
 	dbgfile.debugPrint("connection",1,"get db list...");
	bool	retval=getListCommand(cursor,0,false);
 	dbgfile.debugPrint("connection",1,"done getting db list");
	return retval;
}

bool sqlrcontroller_svr::getTableListCommand(sqlrcursor_svr *cursor) {
 	dbgfile.debugPrint("connection",1,"get table list...");
	bool	retval=getListCommand(cursor,1,false);
 	dbgfile.debugPrint("connection",1,"done getting table list");
	return retval;
}

bool sqlrcontroller_svr::getColumnListCommand(sqlrcursor_svr *cursor) {
 	dbgfile.debugPrint("connection",1,"get column list...");
	bool	retval=getListCommand(cursor,2,true);
 	dbgfile.debugPrint("connection",1,"done getting column list");
	return retval;
}

bool sqlrcontroller_svr::getListCommand(sqlrcursor_svr *cursor,
						int which, bool gettable) {

	// clean up any custom query cursors
	if (cursor->customquerycursor) {
		cursor->customquerycursor->close();
		delete cursor->customquerycursor;
		cursor->customquerycursor=NULL;
	}

	// clean up whatever result set the cursor might have been busy with
	cursor->cleanUpData();

	// get length of wild parameter
	uint32_t	wildlen;
	if (clientsock->read(&wildlen,idleclienttimeout,0)!=sizeof(uint32_t)) {
		dbgfile.debugPrint("connection",2,
			"get list failed: client sent bad wild length");
		return false;
	}

	// bounds checking
	if (wildlen>maxquerysize) {
		dbgfile.debugPrint("connection",2,
			"get list failed: client sent bad wild length");
		return false;
	}

	// read the wild parameter into the buffer
	char	*wild=new char[wildlen+1];
	if (wildlen) {
		if ((uint32_t)(clientsock->read(wild,wildlen,
					idleclienttimeout,0))!=wildlen) {
			dbgfile.debugPrint("connection",2,
				"get list failed: "
				"client sent short wild parameter");
			return false;
		}
	}
	wild[wildlen]='\0';

	// read the table parameter into the buffer
	char	*table=NULL;
	if (gettable) {

		// get length of table parameter
		uint32_t	tablelen;
		if (clientsock->read(&tablelen,
				idleclienttimeout,0)!=sizeof(uint32_t)) {
			dbgfile.debugPrint("connection",2,
				"get list failed: "
				"client sent bad table length");
			return false;
		}

		// bounds checking
		if (tablelen>maxquerysize) {
			dbgfile.debugPrint("connection",2,
				"get list failed: "
				"client sent bad table length");
			return false;
		}

		// read the table parameter into the buffer
		table=new char[tablelen+1];
		if (tablelen) {
			if ((uint32_t)(clientsock->read(table,tablelen,
					idleclienttimeout,0))!=tablelen) {
				dbgfile.debugPrint("connection",2,
					"get list failed: "
					"client sent short table parameter");
				return false;
			}
		}
		table[tablelen]='\0';

		// some apps aren't well behaved, trim spaces off of both sides
		charstring::bothTrim(table);

		// translate table name, if necessary
		if (sqlt) {
			const char	*newname=NULL;
			if (sqlt->getReplacementTableName(NULL,NULL,
							table,&newname)) {
				delete[] table;
				table=charstring::duplicate(newname);
			}
		}
	}

	// set the values that we won't get from the client
	cursor->inbindcount=0;
	cursor->outbindcount=0;
	sendcolumninfo=SEND_COLUMN_INFO;

	// get the list and return it
	bool	result=true;
	if (conn->getListsByApiCalls()) {
		result=getListByApiCall(cursor,which,table,wild);
	} else {
		result=getListByQuery(cursor,which,table,wild);
	}

	// clean up
	delete[] wild;
	delete[] table;

	return result;
}

bool sqlrcontroller_svr::getListByApiCall(sqlrcursor_svr *cursor,
						int which,
						const char *table,
						const char *wild) {

	// initialize flags andbuffers
	bool	success=false;

	// get the appropriate list
	switch (which) {
		case 0:
			success=conn->getDatabaseList(cursor,wild);
			break;
		case 1:
			success=conn->getTableList(cursor,wild);
			break;
		case 2:
			success=conn->getColumnList(cursor,table,wild);
			break;
	}

	// if an error occurred...
	if (!success) {
		cursor->errorMessage(cursor->error,
					maxerrorlength,
					&(cursor->errorlength),
					&(cursor->errnum),
					&(cursor->liveconnection));
		returnError(cursor,!cursor->liveconnection);

		// this is actually OK, only return false on a network error
		return true;
	}

	// indicate that no error has occurred
	clientsock->write((uint16_t)NO_ERROR_OCCURRED);

	// send the client the id of the 
	// cursor that it's going to use
	clientsock->write(cursor->id);

	// tell the client that this is not a
	// suspended result set
	clientsock->write((uint16_t)NO_SUSPENDED_RESULT_SET);

	// if the query processed ok then send a result set header and return...
	returnResultSetHeader(cursor);
	if (!returnResultSetData(cursor)) {
		return false;
	}
	return true;
}

bool sqlrcontroller_svr::getListByQuery(sqlrcursor_svr *cursor,
						int which,
						const char *table,
						const char *wild) {

	// build the appropriate query
	const char	*query=NULL;
	bool		havewild=charstring::length(wild);
	switch (which) {
		case 0:
			query=conn->getDatabaseListQuery(havewild);
			break;
		case 1:
			query=conn->getTableListQuery(havewild);
			break;
		case 2:
			query=conn->getColumnListQuery(havewild);
			break;
	}

	// FIXME: this can fail
	buildListQuery(cursor,query,wild,table);

	// run it like a normal query, but don't request the query,
	// binds or column info status from the client
	return handleQueryOrBindCursor(cursor,false,false,false);
}

bool sqlrcontroller_svr::buildListQuery(sqlrcursor_svr *cursor,
						const char *query,
						const char *wild,
						const char *table) {

	// clean up buffers to avoid SQL injection
	stringbuffer	wildbuf;
	escapeParameter(&wildbuf,wild);
	stringbuffer	tablebuf;
	escapeParameter(&tablebuf,table);

	// bounds checking
	cursor->querylength=charstring::length(query)+
					wildbuf.getStringLength()+
					tablebuf.getStringLength();
	if (cursor->querylength>maxquerysize) {
		return false;
	}

	// fill the query buffer and update the length
	if (tablebuf.getStringLength()) {
		snprintf(cursor->querybuffer,maxquerysize+1,
			query,tablebuf.getString(),wildbuf.getString());
	} else {
		snprintf(cursor->querybuffer,maxquerysize+1,
					query,wildbuf.getString());
	}
	cursor->querylength=charstring::length(cursor->querybuffer);
	return true;
}

void sqlrcontroller_svr::escapeParameter(stringbuffer *buffer,
						const char *parameter) {

	if (!parameter) {
		return;
	}

	// escape single quotes
	for (const char *ptr=parameter; *ptr; ptr++) {
		if (*ptr=='\'') {
			buffer->append('\'');
		}
		buffer->append(*ptr);
	}
}

bool sqlrcontroller_svr::handleQueryOrBindCursor(sqlrcursor_svr *cursor,
							bool reexecute,
							bool bindcursor,
							bool getquery) {


	dbgfile.debugPrint("connection",1,"handling query...");

	// decide whether to use the cursor itself
	// or an attached custom query cursor
	if (cursor->customquerycursor) {
		if (reexecute) {
			cursor=cursor->customquerycursor;
		} else {
			cursor->customquerycursor->close();
			delete cursor->customquerycursor;
			cursor->customquerycursor=NULL;
		}
	}

	// re-init error data
	cursor->clearError();

	// clear bind mappings and reset the fake input binds flag
	// (do this here because getInput/OutputBinds uses the bindmappingspool)
	if (!bindcursor && !reexecute) {
		bindmappingspool->free();
		inbindmappings->clear();
		outbindmappings->clear();
		cursor->fakeinputbindsforthisquery=fakeinputbinds;
	}

	// clean up whatever result set the cursor might have been busy with
	cursor->cleanUpData();

	// get the query and bind data from the client...
	bool	usingcustomquerycursor=false;
	if (getquery) {

		// re-init buffers
		if (!reexecute && !bindcursor) {
			clientinfo[0]='\0';
			clientinfolen=0;
			cursor->querybuffer[0]='\0';
			cursor->querylength=0;
		}
		if (!bindcursor) {
			cursor->inbindcount=0;
			cursor->outbindcount=0;
			for (uint16_t i=0; i<maxbindcount; i++) {
				rawbuffer::zero(&(cursor->inbindvars[i]),
							sizeof(bindvar_svr));
				rawbuffer::zero(&(cursor->outbindvars[i]),
							sizeof(bindvar_svr));
			}
		}

		// get the data
		bool	success=true;
		if (!reexecute && !bindcursor) {
			success=(getClientInfo(cursor) &&
					getQuery(cursor));

			// do we need to use a custom query
			// handler for this query?
			if (success && sqlrq) {
				cursor->customquerycursor=
					sqlrq->match(conn,
						cursor->querybuffer,
						cursor->querylength);
				
			}

			if (cursor->customquerycursor) {

				// open the cursor
				cursor->customquerycursor->openInternal(
								cursor->id);

				// copy the query that we just got into the
				// custom query cursor
				charstring::copy(
					cursor->customquerycursor->querybuffer,
					cursor->querybuffer);
				cursor->customquerycursor->querylength=
							cursor->querylength;

				// set the cursor state
				cursor->customquerycursor->state=
						SQLRCURSORSTATE_BUSY;

				// reset the rest of this method to use
				// the custom query cursor
				cursor=cursor->customquerycursor;

				usingcustomquerycursor=true;
			}
		}
		if (success && !bindcursor) {
			success=(getInputBinds(cursor) &&
					getOutputBinds(cursor));
		}
		if (success) {
			success=getSendColumnInfo();
		}
		if (!success) {
			// The client is apparently sending us something we
			// can't handle.  Return an error if there was one,
			// instruct the client to disconnect and return false
			// to end the session on this side.
			if (cursor->errnum) {
				returnError(cursor,true);
			}
			dbgfile.debugPrint("connection",1,
						"failed to handle query");
			return false;
		}
	}

	updateState((usingcustomquerycursor)?PROCESS_CUSTOM:PROCESS_SQL);

	// loop here to handle down databases
	for (;;) {

		// process the query
		bool	success=false;
		bool	wasfaketransactionquery=false;
		if (!reexecute && !bindcursor && faketransactionblocks) {
			success=handleFakeTransactionQueries(cursor,
						&wasfaketransactionquery);
		}
		if (!wasfaketransactionquery) {
			success=processQuery(cursor,reexecute,bindcursor);
		}

		if (success) {

			// indicate that no error has occurred
			clientsock->write((uint16_t)NO_ERROR_OCCURRED);

			// send the client the id of the 
			// cursor that it's going to use
			clientsock->write(cursor->id);

			// tell the client that this is not a
			// suspended result set
			clientsock->write((uint16_t)NO_SUSPENDED_RESULT_SET);

			// if the query processed 
			// ok then send a result set
			// header and return...
			returnResultSetHeader(cursor);

			// free memory used by binds
			bindpool->free();

			dbgfile.debugPrint("connection",1,
						"handle query succeeded");

			// reinit lastrow
			cursor->lastrowvalid=false;

			// return the result set
			return returnResultSetData(cursor);

		} else {

			// if the db is still up, or if we're not waiting
			// for them if they're down, then return the error
			if (cursor->liveconnection ||
				!cfgfl->getWaitForDownDatabase()) {

				// return the error
				returnError(cursor,!cursor->liveconnection);
			}

			// if the error was a dead connection
			// then re-establish the connection
			if (!cursor->liveconnection) {

				dbgfile.debugPrint("connection",3,
							"database is down...");
				reLogIn();

				// if we're waiting for down databases then
				// loop back and try the query again
				if (cfgfl->getWaitForDownDatabase()) {
					continue;
				}
			}

			return true;
		}
	}
}

bool sqlrcontroller_svr::getClientInfo(sqlrcursor_svr *cursor) {

	dbgfile.debugPrint("connection",2,"getting client info...");

	// init
	clientinfolen=0;
	clientinfo[0]='\0';

	// get the length of the client info
	if (clientsock->read(&clientinfolen)!=sizeof(uint64_t)) {
		dbgfile.debugPrint("connection",2,
			"getting client info failed: "
			"client sent bad client info size");
		clientinfolen=0;
		return false;
	}

	// bounds checking
	if (clientinfolen>maxclientinfolength) {

		stringbuffer	err;
		err.append(SQLR_ERROR_MAXCLIENTINFOLENGTH_STRING);
		err.append(" (")->append(clientinfolen)->append('>');
		err.append(maxclientinfolength)->append(')');
		cursor->setError(err.getString(),
				SQLR_ERROR_MAXCLIENTINFOLENGTH,true);

		clientinfolen=0;

		dbgfile.debugPrint("connection",2,
			"getting client info failed: "
			"client sent bad client info size");
		return false;
	}

	// read the client info into the buffer
	if ((uint64_t)clientsock->read(clientinfo,clientinfolen)!=
							clientinfolen) {
		dbgfile.debugPrint("connection",2,
			"getting client info failed: "
			"client sent short client info");
		clientinfolen=0;
		clientinfo[0]='\0';
		return false;
	}
	clientinfo[clientinfolen]='\0';

	dbgfile.debugPrint("connection",3,"clientinfolen:");
	dbgfile.debugPrint("connection",4,(int32_t)clientinfolen);
	dbgfile.debugPrint("connection",3,"clientinfo:");
	dbgfile.debugPrint("connection",0,clientinfo);
	dbgfile.debugPrint("connection",2,"getting clientinfo succeeded");

	// update the stats with the client info
	updateClientInfo(clientinfo,clientinfolen);

	return true;
}

bool sqlrcontroller_svr::getQuery(sqlrcursor_svr *cursor) {

	dbgfile.debugPrint("connection",2,"getting query...");

	// init
	cursor->querylength=0;
	cursor->querybuffer[0]='\0';

	// get the length of the query
	if (clientsock->read(&cursor->querylength,
				idleclienttimeout,0)!=sizeof(uint32_t)) {
		dbgfile.debugPrint("connection",2,
			"getting query failed: client sent bad query size");
		cursor->querylength=0;
		return false;
	}

	// bounds checking
	if (cursor->querylength>maxquerysize) {

		stringbuffer	err;
		err.append(SQLR_ERROR_MAXQUERYLENGTH_STRING);
		err.append(" (")->append(cursor->querylength)->append('>');
		err.append(maxquerysize)->append(')');
		cursor->setError(err.getString(),
				SQLR_ERROR_MAXQUERYLENGTH,true);

		cursor->querylength=0;

		dbgfile.debugPrint("connection",2,
			"getting query failed: client sent bad query size");
		return false;
	}

	// read the query into the buffer
	if ((uint32_t)clientsock->read(
				cursor->querybuffer,
				cursor->querylength,
				idleclienttimeout,0)!=cursor->querylength) {
		dbgfile.debugPrint("connection",2,
			"getting query failed: client sent short query");
		cursor->querylength=0;
		cursor->querybuffer[0]='\0';
		return false;
	}
	cursor->querybuffer[cursor->querylength]='\0';

	dbgfile.debugPrint("connection",3,"querylength:");
	dbgfile.debugPrint("connection",4,(int32_t)cursor->querylength);
	dbgfile.debugPrint("connection",3,"query:");
	dbgfile.debugPrint("connection",0,cursor->querybuffer);
	dbgfile.debugPrint("connection",2,"getting query succeeded");

	// update the stats with the current query
	updateCurrentQuery(cursor->querybuffer,cursor->querylength);

	return true;
}

bool sqlrcontroller_svr::getSendColumnInfo() {

	dbgfile.debugPrint("connection",2,"getting send column info...");

	if (clientsock->read(&sendcolumninfo,
				idleclienttimeout,0)!=sizeof(uint16_t)) {
		dbgfile.debugPrint("connection",2,
					"getting send column info failed");
		return false;
	}

	if (sendcolumninfo==SEND_COLUMN_INFO) {
		dbgfile.debugPrint("connection",3,"send column info");
	} else {
		dbgfile.debugPrint("connection",3,"don't send column info");
	}
	dbgfile.debugPrint("connection",2,"done getting send column info...");

	return true;
}

bool sqlrcontroller_svr::processQuery(sqlrcursor_svr *cursor,
					bool reexecute, bool bindcursor) {

	dbgfile.debugPrint("connection",2,"processing query...");

	// on reexecute, translate bind variables from mapping
	if (reexecute) {
		translateBindVariablesFromMappings(cursor);
	}

	bool	success=false;
	bool	supportsnativebinds=cursor->supportsNativeBinds();

	if (reexecute &&
		!cursor->fakeinputbindsforthisquery &&
		supportsnativebinds) {

		// if we're reexecuting and not faking binds then
		// the statement doesn't need to be prepared again,
		// just execute it...

		dbgfile.debugPrint("connection",3,"re-executing...");
		success=(handleBinds(cursor) && executeQuery(cursor,
							cursor->querybuffer,
							cursor->querylength));

	} else if (bindcursor) {

		// if we're handling a bind cursor...
		dbgfile.debugPrint("connection",3,"bind cursor...");
		success=cursor->fetchFromBindCursor();

	} else {

		// otherwise, prepare and execute the query...
		// generally this a first time query but it could also be
		// a reexecute if we're faking binds

		dbgfile.debugPrint("connection",3,"preparing/executing...");

		// rewrite the query, if necessary
		rewriteQuery(cursor);

		// buffers and pointers...
		stringbuffer	outputquery;
		const char	*query=cursor->querybuffer;
		uint32_t	querylen=cursor->querylength;

		// fake input binds if necessary
		// NOTE: we can't just overwrite the querybuffer when
		// faking binds or we'll lose the original query and
		// end up rerunning the modified query when reexecuting
		if (cursor->fakeinputbindsforthisquery ||
					!supportsnativebinds) {
			dbgfile.debugPrint("connection",3,"faking binds...");
			if (cursor->fakeInputBinds(&outputquery)) {
				query=outputquery.getString();
				querylen=outputquery.getStringLength();
			}
		}

		// prepare
		success=cursor->prepareQuery(query,querylen);

		// if we're not faking binds then
		// handle the binds for real
		if (success &&
			!cursor->fakeinputbindsforthisquery &&
			cursor->supportsNativeBinds()) {
			success=handleBinds(cursor);
		}

		// execute
		if (success) {
			success=executeQuery(cursor,query,querylen);
		}
	}

	// was the query a commit or rollback?
	commitOrRollback(cursor);

	// On success, autocommit if necessary.
	// Connection classes could override autoCommitOn() and autoCommitOff()
	// to do database API-specific things, but will not set 
	// fakeautocommit, so this code won't get called at all for those 
	// connections.
	// FIXME: when faking autocommit, a BEGIN on a db that supports them
	// could cause commit to be called immediately
	if (success && conn->isTransactional() && commitorrollback &&
				conn->fakeautocommit && conn->autocommit) {
		dbgfile.debugPrint("connection",3,"commit necessary...");
		success=commit();
	}
	
	// if the query failed, get the error (unless it's already been set)
	if (!success && !cursor->errnum) {
		// FIXME: errors for queries run by triggers won't be set here
		cursor->errorMessage(cursor->error,
					maxerrorlength,
					&(cursor->errorlength),
					&(cursor->errnum),
					&(cursor->liveconnection));
	}

	if (success) {
		dbgfile.debugPrint("connection",2,"processing query succeeded");
	} else {
		dbgfile.debugPrint("connection",2,"processing query failed");
	}
	dbgfile.debugPrint("connection",2,"done processing query");


	return success;
}

bool sqlrcontroller_svr::executeQuery(sqlrcursor_svr *curs,
						const char *query,
						uint32_t length) {

	// handle before-triggers
	if (sqltr) {
		sqltr->runBeforeTriggers(conn,curs,curs->querytree);
	}

	// variables for query timing
	timeval		tv;
	struct timezone	tz;

	// get the query start time
	gettimeofday(&tv,&tz);
	curs->querystartsec=tv.tv_sec;
	curs->querystartusec=tv.tv_usec;

	// execute the query
	curs->queryresult=curs->executeQuery(query,length);

	// get the query end time
	gettimeofday(&tv,&tz);
	curs->queryendsec=tv.tv_sec;
	curs->queryendusec=tv.tv_usec;

	// update query and error counts
	incrementQueryCounts(curs->queryType(query,length));
	if (!curs->queryresult) {
		incrementTotalErrors();
	}

	// handle after-triggers
	if (sqltr) {
		sqltr->runAfterTriggers(conn,curs,curs->querytree,true);
	}

	return curs->queryresult;
}

void sqlrcontroller_svr::commitOrRollback(sqlrcursor_svr *cursor) {

	dbgfile.debugPrint("connection",2,"commit or rollback check...");

	// if the query was a commit or rollback, set a flag indicating so
	if (conn->isTransactional()) {
		if (cursor->queryIsCommitOrRollback()) {
			dbgfile.debugPrint("connection",3,
					"commit or rollback not needed");
			commitorrollback=false;
		} else if (cursor->queryIsNotSelect()) {
			dbgfile.debugPrint("connection",3,
					"commit or rollback needed");
			commitorrollback=true;
		}
	}

	dbgfile.debugPrint("connection",2,"done with commit or rollback check");
}

void sqlrcontroller_svr::setFakeInputBinds(bool fake) {
	fakeinputbinds=fake;
}

bool sqlrcontroller_svr::handleBinds(sqlrcursor_svr *cursor) {

	bindvar_svr	*bind=NULL;
	
	// iterate through the arrays, binding values to variables
	for (int16_t i=0; i<cursor->inbindcount; i++) {

		bind=&cursor->inbindvars[i];

		// bind the value to the variable
		if (bind->type==STRING_BIND || bind->type==NULL_BIND) {
			if (!cursor->inputBind(
					bind->variable,
					bind->variablesize,
					bind->value.stringval,
					bind->valuesize,
					&bind->isnull)) {
				return false;
			}
		} else if (bind->type==INTEGER_BIND) {
			if (!cursor->inputBind(
					bind->variable,
					bind->variablesize,
					&bind->value.integerval)) {
				return false;
			}
		} else if (bind->type==DOUBLE_BIND) {
			if (!cursor->inputBind(
					bind->variable,
					bind->variablesize,
					&bind->value.doubleval.value,
					bind->value.doubleval.precision,
					bind->value.doubleval.scale)) {
				return false;
			}
		} else if (bind->type==DATE_BIND) {
			if (!cursor->inputBind(
					bind->variable,
					bind->variablesize,
					bind->value.dateval.year,
					bind->value.dateval.month,
					bind->value.dateval.day,
					bind->value.dateval.hour,
					bind->value.dateval.minute,
					bind->value.dateval.second,
					bind->value.dateval.microsecond,
					bind->value.dateval.tz,
					bind->value.dateval.buffer,
					bind->value.dateval.buffersize,
					&bind->isnull)) {
				return false;
			}
		} else if (bind->type==BLOB_BIND) {
			if (!cursor->inputBindBlob(
					bind->variable,
					bind->variablesize,
					bind->value.stringval,
					bind->valuesize,
					&bind->isnull)) {
				return false;
			}
		} else if (bind->type==CLOB_BIND) {
			if (!cursor->inputBindClob(
					bind->variable,
					bind->variablesize,
					bind->value.stringval,
					bind->valuesize,
					&bind->isnull)) {
				return false;
			}
		}
	}

	for (int16_t i=0; i<cursor->outbindcount; i++) {

		bind=&cursor->outbindvars[i];

		// bind the value to the variable
		if (bind->type==STRING_BIND) {
			if (!cursor->outputBind(
					bind->variable,
					bind->variablesize,
					bind->value.stringval,
					bind->valuesize+1,
					&bind->isnull)) {
				return false;
			}
		} else if (bind->type==INTEGER_BIND) {
			if (!cursor->outputBind(
					bind->variable,
					bind->variablesize,
					&bind->value.integerval,
					&bind->isnull)) {
				return false;
			}
		} else if (bind->type==DOUBLE_BIND) {
			if (!cursor->outputBind(
					bind->variable,
					bind->variablesize,
					&bind->value.doubleval.value,
					&bind->value.doubleval.precision,
					&bind->value.doubleval.scale,
					&bind->isnull)) {
				return false;
			}
		} else if (bind->type==DATE_BIND) {
			if (!cursor->outputBind(
					bind->variable,
					bind->variablesize,
					&bind->value.dateval.year,
					&bind->value.dateval.month,
					&bind->value.dateval.day,
					&bind->value.dateval.hour,
					&bind->value.dateval.minute,
					&bind->value.dateval.second,
					&bind->value.dateval.microsecond,
					(const char **)&bind->value.dateval.tz,
					bind->value.dateval.buffer,
					bind->value.dateval.buffersize,
					&bind->isnull)) {
				return false;
			}
		} else if (bind->type==BLOB_BIND) {
			if (!cursor->outputBindBlob(
					bind->variable,
					bind->variablesize,i,
					&bind->isnull)) {
				return false;
			}
		} else if (bind->type==CLOB_BIND) {
			if (!cursor->outputBindClob(
					bind->variable,
					bind->variablesize,i,
					&bind->isnull)) {
				return false;
			}
		} else if (bind->type==CURSOR_BIND) {

			bool	found=false;

			// find the cursor that we acquird earlier...
			for (uint16_t j=0; j<cursorcount; j++) {

				if (cur[j]->id==bind->value.cursorid) {
					found=true;

					// bind the cursor
					if (!cursor->outputBindCursor(
							bind->variable,
							bind->variablesize,
							cur[j])) {
						return false;
					}
					break;
				}
			}

			// this shouldn't happen, but if it does, return false
			if (!found) {
				return false;
			}
		}
	}
	return true;
}

void sqlrcontroller_svr::returnOutputBindBlob(sqlrcursor_svr *cursor,
							uint16_t index) {
	return sendLobOutputBind(cursor,index);
}

void sqlrcontroller_svr::returnOutputBindClob(sqlrcursor_svr *cursor,
							uint16_t index) {
	return sendLobOutputBind(cursor,index);
}

#define MAX_BYTES_PER_CHAR	4

void sqlrcontroller_svr::sendLobOutputBind(sqlrcursor_svr *cursor,
							uint16_t index) {

	// Get lob length.  If this fails, send a NULL field.
	uint64_t	loblength;
	if (!cursor->getLobOutputBindLength(index,&loblength)) {
		sendNullField();
		return;
	}

	// for lobs of 0 length
	if (!loblength) {
		startSendingLong(0);
		sendLongSegment("",0);
		endSendingLong();
		return;
	}

	// initialize sizes and status
	uint64_t	charstoread=sizeof(cursor->lobbuffer)/
						MAX_BYTES_PER_CHAR;
	uint64_t	charsread=0;
	uint64_t	offset=0;
	bool		start=true;

	for (;;) {

		// read a segment from the lob
		if (!cursor->getLobOutputBindSegment(
					index,
					cursor->lobbuffer,
					sizeof(cursor->lobbuffer),
					offset,charstoread,&charsread) ||
					!charsread) {

			// if we fail to get a segment or got nothing...
			// if we haven't started sending yet, then send a NULL,
			// otherwise just end normally
			if (start) {
				sendNullField();
			} else {
				endSendingLong();
			}
			return;

		} else {

			// if we haven't started sending yet, then do that now
			if (start) {
				startSendingLong(loblength);
				start=false;
			}

			// send the segment we just got
			sendLongSegment(cursor->lobbuffer,charsread);

			// FIXME: or should this be charsread?
			offset=offset+charstoread;
		}
	}
}

enum queryparsestate_t {
	IN_QUERY=0,
	IN_QUOTES,
	BEFORE_BIND,
	IN_BIND
};

void sqlrcontroller_svr::rewriteQuery(sqlrcursor_svr *cursor) {

	if (sqlp && sqlt && sqlw) {
		if (!translateQuery(cursor)) {
			// FIXME: do something?
		}
	}

	if (translatebinds) {
		translateBindVariables(cursor);
	}

	if (conn->supportsTransactionBlocks()) {
		translateBeginTransaction(cursor);
	}
}

bool sqlrcontroller_svr::translateQuery(sqlrcursor_svr *cursor) {

	if (debugsqltranslation) {
		printf("original:\n\"%s\"\n\n",cursor->querybuffer);
	}

	// parse the query
	bool	parsed=sqlp->parse(cursor->querybuffer);

	// get the parsed tree
	delete cursor->querytree;
	cursor->querytree=sqlp->detachTree();
	if (!cursor->querytree) {
		return false;
	}

	if (debugsqltranslation) {
		printf("before translation:\n");
		xmldomnode::print(cursor->querytree->getRootNode());
		printf("\n");
	}

	if (!parsed) {
		if (debugsqltranslation) {
			printf("parse failed, using original:\n\"%s\"\n\n",
							cursor->querybuffer);
		}
		delete cursor->querytree;
		cursor->querytree=NULL;
		return false;
	}

	// apply translation rules
	if (!sqlt->runTranslations(conn,cursor,cursor->querytree)) {
		return false;
	}

	if (debugsqltranslation) {
		printf("after translation:\n");
		xmldomnode::print(cursor->querytree->getRootNode());
		printf("\n");
	}

	// write the query back out
	stringbuffer	translatedquery;
	if (!sqlw->write(conn,cursor,cursor->querytree,&translatedquery)) {
		return false;
	}

	if (debugsqltranslation) {
		printf("translated:\n\"%s\"\n\n",
				translatedquery.getString());
	}

	// copy the translated query into query buffer
	if (translatedquery.getStringLength()>maxquerysize) {
		// the translated query was too large
		return false;
	}
	charstring::copy(cursor->querybuffer,
			translatedquery.getString(),
			translatedquery.getStringLength());
	cursor->querylength=translatedquery.getStringLength();
	cursor->querybuffer[cursor->querylength]='\0';
	return true;
}

void sqlrcontroller_svr::translateBindVariables(sqlrcursor_svr *cursor) {

	// debug
	dbgfile.debugPrint("connection",1,"translating bind variables...");
	dbgfile.debugPrint("connection",2,"original:");
	dbgfile.debugPrint("connection",2,cursor->querybuffer);
	dbgfile.debugPrint("connection",2,"input binds:");
	if (dbgfile.debugEnabled()) {
		for (uint16_t i=0; i<cursor->inbindcount; i++) {
			dbgfile.debugPrint("connection",3,
					cursor->inbindvars[i].variable);
		}
	}
	dbgfile.debugPrint("connection",2,"output binds:");
	if (dbgfile.debugEnabled()) {
		for (uint16_t i=0; i<cursor->outbindcount; i++) {
			dbgfile.debugPrint("connection",3,
					cursor->outbindvars[i].variable);
		}
	}

	// convert queries from whatever bind variable format they currently
	// use to the format required by the database...

	bool			convert=false;
	queryparsestate_t	parsestate=IN_QUERY;
	stringbuffer	newquery;
	stringbuffer	currentbind;
	const char	*endptr=cursor->querybuffer+cursor->querylength-1;

	// use 1-based index for bind variables
	uint16_t	bindindex=1;
	
	// run through the querybuffer...
	char *c=cursor->querybuffer;
	do {

		// if we're in the query...
		if (parsestate==IN_QUERY) {

			// if we find a quote, we're in quotes
			if (*c=='\'') {
				parsestate=IN_QUOTES;
			}

			// if we find whitespace or a couple of other things
			// then the next thing could be a bind variable
			if (character::isWhitespace(*c) ||
					*c==',' || *c=='(' || *c=='=') {
				parsestate=BEFORE_BIND;
			}

			// append the character
			newquery.append(*c);
			c++;
			continue;
		}

		// copy anything in quotes verbatim
		if (parsestate==IN_QUOTES) {
			if (*c=='\'') {
				parsestate=IN_QUERY;
			}
			newquery.append(*c);
			c++;
			continue;
		}

		if (parsestate==BEFORE_BIND) {

			// if we find a bind variable...
			// (make sure to catch @'s but not @@'s
			if (*c=='?' || *c==':' ||
				(*c=='@' && *(c+1)!='@') || *c=='$') {
				parsestate=IN_BIND;
				currentbind.clear();
				continue;
			}

			// if we didn't find a bind variable then we're just
			// back in the query
			parsestate=IN_QUERY;
			continue;
		}

		// if we're in a bind variable...
		if (parsestate==IN_BIND) {

			// If we find whitespace or a few other things
			// then we're done with the bind variable.  Process it.
			// Otherwise get the variable itself in another buffer.
			bool	isspecialchar=(character::isWhitespace(*c) ||
						*c==',' || *c==')' || *c==';');
			if (isspecialchar || c==endptr) {

				// special case if we hit the end of the string
				// an it's not one of the special chars
				if (c==endptr && !isspecialchar) {
					currentbind.append(*c);
					c++;
				}

				// Bail if the current bind variable format
				// matches the db bind format.
				if (matchesNativeBindFormat(
						currentbind.getString())) {
					return;
				}

				// translate...
				convert=true;
				translateBindVariableInStringAndArray(cursor,
								&currentbind,
								bindindex,
								&newquery);
				bindindex++;

				parsestate=IN_QUERY;

			} else {
				currentbind.append(*c);
				c++;
			}
			continue;
		}

	} while (c<=endptr);

	if (!convert) {
		return;
	}


	// if we made it here then some conversion
	// was done - update the querybuffer...
	const char	*newq=newquery.getString();
	cursor->querylength=newquery.getStringLength();
	if (cursor->querylength>maxquerysize) {
		cursor->querylength=maxquerysize;
	}
	charstring::copy(cursor->querybuffer,newq,cursor->querylength);
	cursor->querybuffer[cursor->querylength]='\0';


	// debug
	if (debugsqltranslation) {
		printf("bind translation:\n\"%s\"\n",cursor->querybuffer);
		for (uint16_t i=0; i<cursor->inbindcount; i++) {
			printf("  inbind: \"%s\"\n",
					cursor->inbindvars[i].variable);
		}
		for (uint16_t i=0; i<cursor->outbindcount; i++) {
			printf("  outbind: \"%s\"\n",
					cursor->outbindvars[i].variable);
		}
		printf("\n");
	}
	dbgfile.debugPrint("connection",2,"converted:");
	dbgfile.debugPrint("connection",2,cursor->querybuffer);
	dbgfile.debugPrint("connection",2,"input binds:");
	if (dbgfile.debugEnabled()) {
		for (uint16_t i=0; i<cursor->inbindcount; i++) {
			dbgfile.debugPrint("connection",3,
					cursor->inbindvars[i].variable);
		}
	}
	dbgfile.debugPrint("connection",2,"output binds:");
	if (dbgfile.debugEnabled()) {
		for (uint16_t i=0; i<cursor->outbindcount; i++) {
			dbgfile.debugPrint("connection",3,
					cursor->outbindvars[i].variable);
		}
	}
}

bool sqlrcontroller_svr::matchesNativeBindFormat(const char *bind) {

	const char	*bindformat=conn->bindFormat();
	size_t		bindformatlen=charstring::length(bindformat);

	// the bind variable name matches the format if...
	// * the first character of the bind variable name matches the 
	//   first character of the bind format
	//
	//	and...
	//
	// * the format is just a single character
	// 	or..
	// * the second character of the format is a 1 and the second character
	//   of the bind variable name is a digit
	// 	or..
	// * the second character of the format is a * and the second character
	//   of the bind varaible name is alphanumeric
	return (bind[0]==bindformat[0]  &&
		(bindformatlen==1 ||
		(bindformat[1]=='1' && character::isDigit(bind[1])) ||
		(bindformat[1]=='*' && !character::isAlphanumeric(bind[1]))));
}

void sqlrcontroller_svr::translateBindVariableInStringAndArray(
						sqlrcursor_svr *cursor,
						stringbuffer *currentbind,
						uint16_t bindindex,
						stringbuffer *newquery) {

	const char	*bindformat=conn->bindFormat();
	size_t		bindformatlen=charstring::length(bindformat);

	// append the first character of the bind format to the new query
	newquery->append(bindformat[0]);

	if (bindformatlen==1) {

		// replace bind variable itself with number
		translateBindVariableInArray(cursor,NULL,bindindex);

	} else if (bindformat[1]=='1' &&
			!charstring::isNumber(currentbind->getString()+1)) {

		// replace bind variable in string with number
		newquery->append(bindindex);

		// replace bind variable itself with number
		translateBindVariableInArray(cursor,
					currentbind->getString(),
					bindindex);

	} else {

		// if the bind variable contained a name or number then use
		// it, otherwise replace the bind variable in the string and
		// the bind variable itself with a number 
		if (currentbind->getStringLength()>1) {
			newquery->append(currentbind->getString()+1,
					currentbind->getStringLength()-1);
		} else {
			// replace bind variable in string with number
			newquery->append(bindindex);

			// replace bind variable itself with number
			translateBindVariableInArray(cursor,
						currentbind->getString(),
						bindindex);
		}
	}
}

void sqlrcontroller_svr::translateBindVariableInArray(sqlrcursor_svr *cursor,
						const char *currentbind,
						uint16_t bindindex) {

	// run two passes
	for (uint16_t i=0; i<2; i++) {

		// first pass for input binds, second pass for output binds
		uint16_t	count=(!i)?cursor->inbindcount:
						cursor->outbindcount;
		bindvar_svr	*vars=(!i)?cursor->inbindvars:
						cursor->outbindvars;
		namevaluepairs	*mappings=(!i)?inbindmappings:outbindmappings;

		for (uint16_t j=0; j<count; j++) {

			// get the bind var
			bindvar_svr	*b=&(vars[j]);

			// If a bind var name was passed in, look for a bind
			// variable with a matching name.
			// If no name was passed in then the bind vars are
			// numeric; get the variable who's numeric name matches
			// the index passed in.
			if ((currentbind &&
				!charstring::compare(currentbind,
							b->variable)) ||
				(charstring::toInteger((b->variable)+1)==
								bindindex)) {

				// create the new bind var
				// name and get its length
				char		*tempnumber=charstring::
							parseNumber(bindindex);
				uint16_t	tempnumberlen=charstring::
							length(tempnumber);

				// keep track of the old name
				char	*oldvariable=b->variable;

				// allocate memory for the new name
				b->variable=(char *)bindmappingspool->
							malloc(tempnumberlen+2);

				// replace the existing bind var name and size
				b->variable[0]=conn->bindVariablePrefix();
				charstring::copy(b->variable+1,tempnumber);
				b->variable[tempnumberlen+1]='\0';
				b->variablesize=tempnumberlen+1;

				// create bind variable mappings
				mappings->setData(oldvariable,b->variable);
				
				// clean up
				delete[] tempnumber;
			}
		}
	}
}

void sqlrcontroller_svr::translateBindVariablesFromMappings(
						sqlrcursor_svr *cursor) {

	// run two passes
	for (uint16_t i=0; i<2; i++) {

		// first pass for input binds, second pass for output binds
		uint16_t	count=(!i)?cursor->inbindcount:
						cursor->outbindcount;
		bindvar_svr	*vars=(!i)?cursor->inbindvars:
						cursor->outbindvars;
		namevaluepairs	*mappings=(!i)?inbindmappings:outbindmappings;

		for (uint16_t j=0; j<count; j++) {

			// get the bind var
			bindvar_svr	*b=&(vars[j]);

			// remap it
			char	*newvariable;
			if (mappings->getData(b->variable,&newvariable)) {
				b->variable=newvariable;
			}
		}
	}

	// debug
	dbgfile.debugPrint("connection",2,"remapped input binds:");
	if (dbgfile.debugEnabled()) {
		for (uint16_t i=0; i<cursor->inbindcount; i++) {
			dbgfile.debugPrint("connection",3,
					cursor->inbindvars[i].variable);
		}
	}
	dbgfile.debugPrint("connection",2,"remapped output binds:");
	if (dbgfile.debugEnabled()) {
		for (uint16_t i=0; i<cursor->outbindcount; i++) {
			dbgfile.debugPrint("connection",3,
					cursor->outbindvars[i].variable);
		}
	}
}

void sqlrcontroller_svr::translateBeginTransaction(sqlrcursor_svr *cursor) {

	if (!isBeginTransactionQuery(cursor)) {
		return;
	}

	// debug
	dbgfile.debugPrint("connection",1,"translating begin tx query...");
	dbgfile.debugPrint("connection",2,"original:");
	dbgfile.debugPrint("connection",2,cursor->querybuffer);

	// translate query
	const char	*beginquery=conn->beginTransactionQuery();
	cursor->querylength=charstring::length(beginquery);
	charstring::copy(cursor->querybuffer,beginquery,cursor->querylength);
	cursor->querybuffer[cursor->querylength]='\0';

	// debug
	dbgfile.debugPrint("connection",2,"converted:");
	dbgfile.debugPrint("connection",2,cursor->querybuffer);
}

bool sqlrcontroller_svr::getColumnNames(const char *query,
					stringbuffer *output) {

	// sanity check on the query
	if (!query) {
		return false;
	}

	size_t		querylen=charstring::length(query);

	sqlrcursor_svr	*gcncur=initCursor();
	// since we're creating a new cursor for this, make sure it can't
	// have an ID that might already exist
	bool	retval=false;
	if (gcncur->openInternal(cursorcount+1) &&
		gcncur->prepareQuery(query,querylen) &&
		executeQuery(gcncur,query,querylen)) {

		// build column list...
		retval=gcncur->getColumnNameList(output);

	}
	gcncur->cleanUpData();
	gcncur->close();
	deleteCursor(gcncur);
	return retval;
}

bool sqlrcontroller_svr::getInputBinds(sqlrcursor_svr *cursor) {

	dbgfile.debugPrint("connection",2,"getting input binds...");

	// get the number of input bind variable/values
	if (!getBindVarCount(cursor,&(cursor->inbindcount))) {
		return false;
	}
	
	// fill the buffers
	for (uint16_t i=0; i<cursor->inbindcount && i<maxbindcount; i++) {

		bindvar_svr	*bv=&(cursor->inbindvars[i]);

		// get the variable name and type
		if (!(getBindVarName(cursor,bv) && getBindVarType(bv))) {
			return false;
		}

		// get the value
		if (bv->type==NULL_BIND) {
			getNullBind(bv);
		} else if (bv->type==STRING_BIND) {
			if (!getStringBind(cursor,bv)) {
				return false;
			}
		} else if (bv->type==INTEGER_BIND) {
			if (!getIntegerBind(bv)) {
				return false;
			}
		} else if (bv->type==DOUBLE_BIND) {
			if (!getDoubleBind(bv)) {
				return false;
			}
		} else if (bv->type==DATE_BIND) {
			if (!getDateBind(bv)) {
				return false;
			}
		} else if (bv->type==BLOB_BIND) {
			// can't fake blob binds
			cursor->fakeinputbindsforthisquery=false;
			if (!getLobBind(cursor,bv)) {
				return false;
			}
		} else if (bv->type==CLOB_BIND) {
			if (!getLobBind(cursor,bv)) {
				return false;
			}
		}		  
	}

	dbgfile.debugPrint("connection",2,"done getting input binds");
	return true;
}

bool sqlrcontroller_svr::getOutputBinds(sqlrcursor_svr *cursor) {

	dbgfile.debugPrint("connection",2,"getting output binds...");

	// get the number of output bind variable/values
	if (!getBindVarCount(cursor,&(cursor->outbindcount))) {
		return false;
	}

	// fill the buffers
	for (uint16_t i=0; i<cursor->outbindcount && i<maxbindcount; i++) {

		bindvar_svr	*bv=&(cursor->outbindvars[i]);

		// get the variable name and type
		if (!(getBindVarName(cursor,bv) && getBindVarType(bv))) {
			return false;
		}

		// get the size of the value
		if (bv->type==STRING_BIND) {
			bv->value.stringval=NULL;
			if (!getBindSize(cursor,bv,&maxstringbindvaluelength)) {
				return false;
			}
			// This must be a calloc because oracle8 gets angry if
			// these aren't initialized to NULL's.  It's possible
			// that just the first character needs to be NULL, but
			// for now I'm just going to go ahead and use calloc
			bv->value.stringval=
				(char *)bindpool->calloc(bv->valuesize+1);
			dbgfile.debugPrint("connection",4,"STRING");
		} else if (bv->type==INTEGER_BIND) {
			dbgfile.debugPrint("connection",4,"INTEGER");
		} else if (bv->type==DOUBLE_BIND) {
			dbgfile.debugPrint("connection",4,"DOUBLE");
			// these don't typically get set, but they get used
			// when building debug strings, so we need to
			// initialize them
			bv->value.doubleval.precision=0;
			bv->value.doubleval.scale=0;
		} else if (bv->type==DATE_BIND) {
			dbgfile.debugPrint("connection",4,"DATE");
			bv->value.dateval.year=0;
			bv->value.dateval.month=0;
			bv->value.dateval.day=0;
			bv->value.dateval.hour=0;
			bv->value.dateval.minute=0;
			bv->value.dateval.second=0;
			bv->value.dateval.microsecond=0;
			bv->value.dateval.tz=NULL;
			// allocate enough space to store the date/time string
			// or whatever buffer a child might need to store a
			// date 512 bytes ought to be enough
			bv->value.dateval.buffersize=512;
			bv->value.dateval.buffer=(char *)bindpool->malloc(
						bv->value.dateval.buffersize);
		} else if (bv->type==BLOB_BIND || bv->type==CLOB_BIND) {
			if (!getBindSize(cursor,bv,&maxlobbindvaluelength)) {
				return false;
			}
			if (bv->type==BLOB_BIND) {
				dbgfile.debugPrint("connection",4,"BLOB");
			} else if (bv->type==CLOB_BIND) {
				dbgfile.debugPrint("connection",4,"CLOB");
			}
		} else if (bv->type==CURSOR_BIND) {
			dbgfile.debugPrint("connection",4,"CURSOR");
			sqlrcursor_svr	*curs=findAvailableCursor();
			if (!curs) {
				// FIXME: set error here
				return false;
			}
			curs->state=SQLRCURSORSTATE_BUSY;
			bv->value.cursorid=curs->id;
		}

		// init the null indicator
		bv->isnull=conn->nonNullBindValue();
	}

	dbgfile.debugPrint("connection",2,"done getting output binds");
	return true;
}

bool sqlrcontroller_svr::getBindVarCount(sqlrcursor_svr *cursor,
						uint16_t *count) {

	// init
	*count=0;

	// get the number of input bind variable/values
	if (clientsock->read(count,idleclienttimeout,0)!=sizeof(uint16_t)) {
		dbgfile.debugPrint("connection",2,
			"getting binds failed: "
			"client sent bad bind count size");
		*count=0;
		return false;
	}

	// bounds checking
	if (*count>maxbindcount) {

		stringbuffer	err;
		err.append(SQLR_ERROR_MAXBINDCOUNT_STRING);
		err.append(" (")->append(*count)->append('>');
		err.append(maxbindcount)->append(')');
		cursor->setError(err.getString(),SQLR_ERROR_MAXBINDCOUNT,true);

		*count=0;

		dbgfile.debugPrint("connection",2,
			"getting binds failed: "
			"client tried to send too many binds:");
		dbgfile.debugPrint("connection",3,(int32_t)*count);
		return false;
	}

	return true;
}

bool sqlrcontroller_svr::getBindVarName(sqlrcursor_svr *cursor,
						bindvar_svr *bv) {

	// init
	bv->variablesize=0;
	bv->variable=NULL;

	// get the variable name size
	uint16_t	bindnamesize;
	if (clientsock->read(&bindnamesize,
				idleclienttimeout,0)!=sizeof(uint16_t)) {
		dbgfile.debugPrint("connection",2,
			"getting binds failed: bad variable name length size");
		return false;
	}

	// bounds checking
	if (bindnamesize>maxbindnamelength) {

		stringbuffer	err;
		err.append(SQLR_ERROR_MAXBINDNAMELENGTH_STRING);
		err.append(" (")->append(bindnamesize)->append('>');
		err.append(maxbindnamelength)->append(')');
		cursor->setError(err.getString(),
					SQLR_ERROR_MAXBINDNAMELENGTH,true);

		dbgfile.debugPrint("connection",2,
			"getting binds failed: bad variable name length");
		return false;
	}

	// get the variable name
	bv->variablesize=bindnamesize+1;
	bv->variable=(char *)bindmappingspool->malloc(bindnamesize+2);
	bv->variable[0]=conn->bindVariablePrefix();
	if (clientsock->read(bv->variable+1,bindnamesize,
					idleclienttimeout,0)!=bindnamesize) {
		dbgfile.debugPrint("connection",2,
			"getting binds failed: bad variable name");
		bv->variablesize=0;
		bv->variable[0]='\0';
		return false;
	}
	bv->variable[bindnamesize+1]='\0';

	dbgfile.debugPrint("connection",4,bv->variable);

	return true;
}

bool sqlrcontroller_svr::getBindVarType(bindvar_svr *bv) {

	// get the type
	if (clientsock->read(&bv->type,idleclienttimeout,0)!=sizeof(uint16_t)) {
		dbgfile.debugPrint("connection",2,
				"getting binds failed: bad type size");
		return false;
	}
	return true;
}

bool sqlrcontroller_svr::getBindSize(sqlrcursor_svr *cursor,
					bindvar_svr *bv, uint32_t *maxsize) {

	// init
	bv->valuesize=0;

	// get the size of the value
	if (clientsock->read(&(bv->valuesize),
				idleclienttimeout,0)!=sizeof(uint32_t)) {
		dbgfile.debugPrint("connection",
			2,"getting binds failed: bad value length size");
		bv->valuesize=0;
		return false;
	}

	// bounds checking
	if (bv->valuesize>*maxsize) {
		if (maxsize==&maxstringbindvaluelength) {
			stringbuffer	err;
			err.append(SQLR_ERROR_MAXSTRINGBINDVALUELENGTH_STRING);
			err.append(" (")->append(bv->valuesize)->append('>');
			err.append(*maxsize)->append(')');
			cursor->setError(err.getString(),
				SQLR_ERROR_MAXSTRINGBINDVALUELENGTH,true);
		} else {
			stringbuffer	err;
			err.append(SQLR_ERROR_MAXLOBBINDVALUELENGTH_STRING);
			err.append(" (")->append(bv->valuesize)->append('>');
			err.append(*maxsize)->append(')');
			cursor->setError(err.getString(),
				SQLR_ERROR_MAXLOBBINDVALUELENGTH,true);
		}
		dbgfile.debugPrint("connection",2,
				"getting binds failed: bad value length");
		dbgfile.debugPrint("connection",3,bv->valuesize);
		return false;
	}

	return true;
}

void sqlrcontroller_svr::getNullBind(bindvar_svr *bv) {

	dbgfile.debugPrint("connection",4,"NULL");

	bv->value.stringval=(char *)bindpool->malloc(1);
	bv->value.stringval[0]='\0';
	bv->valuesize=0;
	bv->isnull=conn->nullBindValue();
}

bool sqlrcontroller_svr::getStringBind(sqlrcursor_svr *cursor,
						bindvar_svr *bv) {

	dbgfile.debugPrint("connection",4,"STRING");

	// init
	bv->value.stringval=NULL;

	// get the size of the value
	if (!getBindSize(cursor,bv,&maxstringbindvaluelength)) {
		return false;
	}

	// allocate space to store the value
	bv->value.stringval=(char *)bindpool->malloc(bv->valuesize+1);

	// get the bind value
	if ((uint32_t)(clientsock->read(bv->value.stringval,
					bv->valuesize,
					idleclienttimeout,0))!=
						(uint32_t)(bv->valuesize)) {
		dbgfile.debugPrint("connection",2,
					"getting binds failed: bad value");
		bv->value.stringval[0]='\0';
		return false;
	}
	bv->value.stringval[bv->valuesize]='\0';
	bv->isnull=conn->nonNullBindValue();

	dbgfile.debugPrint("connection",4,bv->value.stringval);

	return true;
}

bool sqlrcontroller_svr::getIntegerBind(bindvar_svr *bv) {

	dbgfile.debugPrint("connection",4,"INTEGER");

	// get the value itself
	uint64_t	value;
	if (clientsock->read(&value,idleclienttimeout,0)!=sizeof(uint64_t)) {
		dbgfile.debugPrint("connection",2,
					"getting binds failed: bad value");
		return false;
	}

	// set the value
	bv->value.integerval=(int64_t)value;

	dbgfile.debugPrint("connection",4,(int32_t)bv->value.integerval);

	return true;
}

bool sqlrcontroller_svr::getDoubleBind(bindvar_svr *bv) {

	dbgfile.debugPrint("connection",4,"DOUBLE");

	// get the value
	if (clientsock->read(&(bv->value.doubleval.value),
				idleclienttimeout,0)!=sizeof(double)) {
		dbgfile.debugPrint("connection",2,
					"getting binds failed: bad value");
		return false;
	}

	// get the precision
	if (clientsock->read(&(bv->value.doubleval.precision),
				idleclienttimeout,0)!=sizeof(uint32_t)) {
		dbgfile.debugPrint("connection",2,
					"getting binds failed: bad precision");
		return false;
	}

	// get the scale
	if (clientsock->read(&(bv->value.doubleval.scale),
				idleclienttimeout,0)!=sizeof(uint32_t)) {
		dbgfile.debugPrint("connection",2,
					"getting binds failed: bad scale");
		return false;
	}

	dbgfile.debugPrint("connection",4,bv->value.doubleval.value);

	return true;
}

bool sqlrcontroller_svr::getDateBind(bindvar_svr *bv) {

	dbgfile.debugPrint("connection",4,"DATE");

	// init
	bv->value.dateval.tz=NULL;

	uint16_t	temp;

	// get the year
	if (clientsock->read(&temp,idleclienttimeout,0)!=sizeof(uint16_t)) {
		dbgfile.debugPrint("connection",2,
					"getting binds failed: bad year");
		return false;
	}
	bv->value.dateval.year=(int16_t)temp;

	// get the month
	if (clientsock->read(&temp,idleclienttimeout,0)!=sizeof(uint16_t)) {
		dbgfile.debugPrint("connection",2,
					"getting binds failed: bad month");
		return false;
	}
	bv->value.dateval.month=(int16_t)temp;

	// get the day
	if (clientsock->read(&temp,idleclienttimeout,0)!=sizeof(uint16_t)) {
		dbgfile.debugPrint("connection",2,
					"getting binds failed: bad day");
		return false;
	}
	bv->value.dateval.day=(int16_t)temp;

	// get the hour
	if (clientsock->read(&temp,idleclienttimeout,0)!=sizeof(uint16_t)) {
		dbgfile.debugPrint("connection",2,
					"getting binds failed: bad hour");
		return false;
	}
	bv->value.dateval.hour=(int16_t)temp;

	// get the minute
	if (clientsock->read(&temp,idleclienttimeout,0)!=sizeof(uint16_t)) {
		dbgfile.debugPrint("connection",2,
					"getting binds failed: bad minute");
		return false;
	}
	bv->value.dateval.minute=(int16_t)temp;

	// get the second
	if (clientsock->read(&temp,idleclienttimeout,0)!=sizeof(uint16_t)) {
		dbgfile.debugPrint("connection",2,
					"getting binds failed: bad second");
		return false;
	}
	bv->value.dateval.second=(int16_t)temp;

	// get the microsecond
	uint32_t	temp32;
	if (clientsock->read(&temp32,idleclienttimeout,0)!=sizeof(uint32_t)) {
		dbgfile.debugPrint("connection",2,
				"getting binds failed: bad microsecond");
		return false;
	}
	bv->value.dateval.microsecond=(int32_t)temp32;

	// get the size of the time zone
	uint16_t	length;
	if (clientsock->read(&length,idleclienttimeout,0)!=sizeof(uint16_t)) {
		return false;
	}

	// allocate space to store the time zone
	bv->value.dateval.tz=(char *)bindpool->malloc(length+1);

	// get the time zone
	if ((uint16_t)(clientsock->read(bv->value.dateval.tz,length,
					idleclienttimeout,0))!=length) {
		dbgfile.debugPrint("connection",2,
					"getting binds failed: bad tz");
		bv->value.dateval.tz[0]='\0';
		return false;
	}
	bv->value.dateval.tz[length]='\0';

	// allocate enough space to store the date/time string
	// 64 bytes ought to be enough
	bv->value.dateval.buffersize=64;
	bv->value.dateval.buffer=(char *)bindpool->malloc(
					bv->value.dateval.buffersize);

	if (dbgfile.debugEnabled()) {
		stringbuffer	str;
		str.append(bv->value.dateval.year)->append("-");
		str.append(bv->value.dateval.month)->append("-");
		str.append(bv->value.dateval.day)->append(" ");
		str.append(bv->value.dateval.hour)->append(":");
		str.append(bv->value.dateval.minute)->append(":");
		str.append(bv->value.dateval.second)->append(":");
		str.append(bv->value.dateval.microsecond)->append(" ");
		str.append(bv->value.dateval.tz);
		dbgfile.debugPrint("connection",4,str.getString());
	}

	return true;
}

bool sqlrcontroller_svr::getLobBind(sqlrcursor_svr *cursor, bindvar_svr *bv) {

	// init
	bv->value.stringval=NULL;

	if (bv->type==BLOB_BIND) {
		dbgfile.debugPrint("connection",4,"BLOB");
	}
	if (bv->type==CLOB_BIND) {
		dbgfile.debugPrint("connection",4,"CLOB");
	}

	// get the size of the value
	if (!getBindSize(cursor,bv,&maxlobbindvaluelength)) {
		return false;
	}

	// allocate space to store the value
	// (the +1 is to store the NULL-terminator for CLOBS)
	bv->value.stringval=(char *)bindpool->malloc(bv->valuesize+1);

	// get the bind value
	if ((uint32_t)(clientsock->read(bv->value.stringval,
					bv->valuesize,
					idleclienttimeout,0))!=
						(uint32_t)(bv->valuesize)) {
		dbgfile.debugPrint("connection",2,
				"getting binds failed: bad value");
		bv->value.stringval[0]='\0';
		return false;
	}

	// It shouldn't hurt to NULL-terminate the lob because the actual size
	// (which doesn't include the NULL terminator) should be used when
	// binding.
	bv->value.stringval[bv->valuesize]='\0';
	bv->isnull=conn->nonNullBindValue();

	/*if (bv->type==BLOB_BIND) {
		dbgfile.debugPrintBlob(bv->value.stringval,bv->valuesize);
	}
	if (bv->type==CLOB_BIND) {
		dbgfile.debugPrintClob(bv->value.stringval,bv->valuesize);
	}*/

	return true;
}

bool sqlrcontroller_svr::reExecuteQueryCommand(sqlrcursor_svr *cursor) {
	dbgfile.debugPrint("connection",1,"rexecute query");
	return handleQueryOrBindCursor(cursor,true,false,true);
}

bool sqlrcontroller_svr::fetchFromBindCursorCommand(sqlrcursor_svr *cursor) {
	dbgfile.debugPrint("connection",1,"fetch from bind cursor");
	return handleQueryOrBindCursor(cursor,false,true,true);
}

bool sqlrcontroller_svr::fetchResultSetCommand(sqlrcursor_svr *cursor) {
	dbgfile.debugPrint("connection",1,"fetching result set...");
	bool	retval=returnResultSetData(cursor);
	dbgfile.debugPrint("connection",1,"done fetching result set");
	return retval;
}

void sqlrcontroller_svr::returnResultSetHeader(sqlrcursor_svr *cursor) {

	dbgfile.debugPrint("connection",2,"returning result set header...");

	// return the row counts
	dbgfile.debugPrint("connection",3,"returning row counts...");
	sendRowCounts(cursor->knowsRowCount(),cursor->rowCount(),
			cursor->knowsAffectedRows(),cursor->affectedRows());
	dbgfile.debugPrint("connection",3,"done returning row counts");


	// write a flag to the client indicating whether 
	// or not the column information will be sent
	clientsock->write(sendcolumninfo);

	if (sendcolumninfo==SEND_COLUMN_INFO) {
		dbgfile.debugPrint("connection",3,
					"column info will be sent");
	} else {
		dbgfile.debugPrint("connection",3,
					"column info will not be sent");
	}


	// return the column count
	dbgfile.debugPrint("connection",3,"returning column counts...");
	clientsock->write(cursor->colCount());
	dbgfile.debugPrint("connection",3,"done returning column counts");


	if (sendcolumninfo==SEND_COLUMN_INFO) {

		// return the column type format
		dbgfile.debugPrint("connection",2,
					"sending column type format...");
		uint16_t	format=cursor->columnTypeFormat();
		if (format==COLUMN_TYPE_IDS) {
			dbgfile.debugPrint("connection",3,"id's");
		} else {
			dbgfile.debugPrint("connection",3,"names");
		}
		clientsock->write(format);
		dbgfile.debugPrint("connection",2,
					"done sending column type format");

		// return the column info
		dbgfile.debugPrint("connection",3,"returning column info...");
		returnColumnInfo(cursor,format);
		dbgfile.debugPrint("connection",3,"done returning column info");
	}


	// return the output bind vars
	returnOutputBindValues(cursor);


	// terminate the bind vars
	clientsock->write((uint16_t)END_BIND_VARS);

	flushWriteBuffer();

	dbgfile.debugPrint("connection",2,"done returning result set header");
}

void sqlrcontroller_svr::returnColumnInfo(sqlrcursor_svr *cursor,
							uint16_t format) {

	for (uint32_t i=0; i<cursor->colCount(); i++) {

		const char	*name=cursor->getColumnName(i);
		uint16_t	namelen=cursor->getColumnNameLength(i);
		uint32_t	length=cursor->getColumnLength(i);
		uint32_t	precision=cursor->getColumnPrecision(i);
		uint32_t	scale=cursor->getColumnScale(i);
		uint16_t	nullable=cursor->getColumnIsNullable(i);
		uint16_t	primarykey=cursor->getColumnIsPrimaryKey(i);
		uint16_t	unique=cursor->getColumnIsUnique(i);
		uint16_t	partofkey=cursor->getColumnIsPartOfKey(i);
		uint16_t	unsignednumber=cursor->getColumnIsUnsigned(i);
		uint16_t	zerofill=cursor->getColumnIsZeroFilled(i);
		uint16_t	binary=cursor->getColumnIsBinary(i);
		uint16_t	autoincrement=
					cursor->getColumnIsAutoIncrement(i);

		if (format==COLUMN_TYPE_IDS) {
			sendColumnDefinition(name,namelen,
					cursor->getColumnType(i),
					length,precision,scale,
					nullable,primarykey,unique,partofkey,
					unsignednumber,zerofill,binary,
					autoincrement);
		} else {
			sendColumnDefinitionString(name,namelen,
					cursor->getColumnTypeName(i),
					cursor->getColumnTypeNameLength(i),
					length,precision,scale,
					nullable,primarykey,unique,partofkey,
					unsignednumber,zerofill,binary,
					autoincrement);
		}
	}
}

void sqlrcontroller_svr::sendRowCounts(bool knowsactual, uint64_t actual,
					bool knowsaffected, uint64_t affected) {

	dbgfile.debugPrint("connection",2,"sending row counts...");

	// send actual rows, if that is known
	if (knowsactual) {

		char	string[30];
		snprintf(string,30,"actual rows: %lld",(long long)actual);
		dbgfile.debugPrint("connection",3,string);

		clientsock->write((uint16_t)ACTUAL_ROWS);
		clientsock->write(actual);

	} else {

		dbgfile.debugPrint("connection",3,"actual rows unknown");

		clientsock->write((uint16_t)NO_ACTUAL_ROWS);
	}

	
	// send affected rows, if that is known
	if (knowsaffected) {

		char	string[46];
		snprintf(string,46,"affected rows: %lld",(long long)affected);
		dbgfile.debugPrint("connection",3,string);

		clientsock->write((uint16_t)AFFECTED_ROWS);
		clientsock->write(affected);

	} else {

		dbgfile.debugPrint("connection",3,"affected rows unknown");

		clientsock->write((uint16_t)NO_AFFECTED_ROWS);
	}

	dbgfile.debugPrint("connection",2,"done sending row counts");
}

void sqlrcontroller_svr::returnOutputBindValues(sqlrcursor_svr *cursor) {

	dbgfile.debugPrint("connection",2,"returning output bind values");
	dbgfile.debugPrint("connection",3,(int32_t)cursor->outbindcount);

	// run through the output bind values, sending them back
	for (uint16_t i=0; i<cursor->outbindcount; i++) {

		bindvar_svr	*bv=&(cursor->outbindvars[i]);

		if (dbgfile.debugEnabled()) {
			debugstr=new stringbuffer();
			debugstr->append(i);
			debugstr->append(":");
		}

		if (conn->bindValueIsNull(bv->isnull)) {

			if (dbgfile.debugEnabled()) {
				debugstr->append("NULL");
			}

			clientsock->write((uint16_t)NULL_DATA);

		} else if (bv->type==BLOB_BIND) {

			if (dbgfile.debugEnabled()) {
				debugstr->append("BLOB:\n");
			}

			returnOutputBindBlob(cursor,i);

		} else if (bv->type==CLOB_BIND) {

			if (dbgfile.debugEnabled()) {
				debugstr->append("CLOB:\n");
			}

			returnOutputBindClob(cursor,i);

		} else if (bv->type==STRING_BIND) {

			if (dbgfile.debugEnabled()) {
				debugstr->append("STRING:\n");
				debugstr->append(bv->value.stringval);
			}

			clientsock->write((uint16_t)STRING_DATA);
			bv->valuesize=charstring::length(
						(char *)bv->value.stringval);
			clientsock->write(bv->valuesize);
			clientsock->write(bv->value.stringval,bv->valuesize);

		} else if (bv->type==INTEGER_BIND) {

			if (dbgfile.debugEnabled()) {
				debugstr->append("INTEGER:\n");
				debugstr->append(bv->value.integerval);
			}

			clientsock->write((uint16_t)INTEGER_DATA);
			clientsock->write((uint64_t)bv->value.integerval);

		} else if (bv->type==DOUBLE_BIND) {

			if (dbgfile.debugEnabled()) {
				debugstr->append("DOUBLE:\n");
				debugstr->append(bv->value.doubleval.value);
				debugstr->append("(");
				debugstr->append(bv->value.doubleval.precision);
				debugstr->append(",");
				debugstr->append(bv->value.doubleval.scale);
				debugstr->append(")");
			}

			clientsock->write((uint16_t)DOUBLE_DATA);
			clientsock->write(bv->value.doubleval.value);
			clientsock->write((uint32_t)bv->value.
						doubleval.precision);
			clientsock->write((uint32_t)bv->value.
						doubleval.scale);

		} else if (bv->type==DATE_BIND) {

			if (dbgfile.debugEnabled()) {
				debugstr->append("DATE:\n");
				debugstr->append(bv->value.dateval.year);
				debugstr->append("-");
				debugstr->append(bv->value.dateval.month);
				debugstr->append("-");
				debugstr->append(bv->value.dateval.day);
				debugstr->append(" ");
				debugstr->append(bv->value.dateval.hour);
				debugstr->append(":");
				debugstr->append(bv->value.dateval.minute);
				debugstr->append(":");
				debugstr->append(bv->value.dateval.second);
				debugstr->append(":");
				debugstr->append(bv->value.dateval.microsecond);
				debugstr->append(" ");
				debugstr->append(bv->value.dateval.tz);
			}

			clientsock->write((uint16_t)DATE_DATA);
			clientsock->write((uint16_t)bv->value.dateval.year);
			clientsock->write((uint16_t)bv->value.dateval.month);
			clientsock->write((uint16_t)bv->value.dateval.day);
			clientsock->write((uint16_t)bv->value.dateval.hour);
			clientsock->write((uint16_t)bv->value.dateval.minute);
			clientsock->write((uint16_t)bv->value.dateval.second);
			clientsock->write((uint32_t)bv->value.
							dateval.microsecond);
			uint16_t	length=charstring::length(
							bv->value.dateval.tz);
			clientsock->write(length);
			clientsock->write(bv->value.dateval.tz,length);

		} else if (bv->type==CURSOR_BIND) {

			if (dbgfile.debugEnabled()) {
				debugstr->append("CURSOR:\n");
				debugstr->append(bv->value.cursorid);
			}

			clientsock->write((uint16_t)CURSOR_DATA);
			clientsock->write(bv->value.cursorid);
		}

		if (dbgfile.debugEnabled()) {
			dbgfile.debugPrint("connection",3,
						debugstr->getString());
			delete debugstr;
		}
	}

	dbgfile.debugPrint("connection",2,"done returning output bind values");
}

bool sqlrcontroller_svr::sendColumnInfo() {
	if (sendcolumninfo==SEND_COLUMN_INFO) {
		return true;
	}
	return false;
}

void sqlrcontroller_svr::sendColumnDefinition(const char *name,
						uint16_t namelen,
						uint16_t type, 
						uint32_t size,
						uint32_t precision,
						uint32_t scale,
						uint16_t nullable,
						uint16_t primarykey,
						uint16_t unique,
						uint16_t partofkey,
						uint16_t unsignednumber,
						uint16_t zerofill,
						uint16_t binary,
						uint16_t autoincrement) {

	if (dbgfile.debugEnabled()) {
		debugstr=new stringbuffer();
		for (uint16_t i=0; i<namelen; i++) {
			debugstr->append(name[i]);
		}
		debugstr->append(":");
		debugstr->append(type);
		debugstr->append(":");
		debugstr->append(size);
		debugstr->append(" (");
		debugstr->append(precision);
		debugstr->append(",");
		debugstr->append(scale);
		debugstr->append(") ");
		if (!nullable) {
			debugstr->append("NOT NULL ");
		}
		if (primarykey) {
			debugstr->append("Primary key ");
		}
		if (unique) {
			debugstr->append("Unique");
		}
		dbgfile.debugPrint("connection",3,debugstr->getString());
		delete debugstr;
	}

	clientsock->write(namelen);
	clientsock->write(name,namelen);
	clientsock->write(type);
	clientsock->write(size);
	clientsock->write(precision);
	clientsock->write(scale);
	clientsock->write(nullable);
	clientsock->write(primarykey);
	clientsock->write(unique);
	clientsock->write(partofkey);
	clientsock->write(unsignednumber);
	clientsock->write(zerofill);
	clientsock->write(binary);
	clientsock->write(autoincrement);
}

void sqlrcontroller_svr::sendColumnDefinitionString(const char *name,
						uint16_t namelen,
						const char *type, 
						uint16_t typelen,
						uint32_t size,
						uint32_t precision,
						uint32_t scale,
						uint16_t nullable,
						uint16_t primarykey,
						uint16_t unique,
						uint16_t partofkey,
						uint16_t unsignednumber,
						uint16_t zerofill,
						uint16_t binary,
						uint16_t autoincrement) {

	if (dbgfile.debugEnabled()) {
		debugstr=new stringbuffer();
		for (uint16_t i=0; i<namelen; i++) {
			debugstr->append(name[i]);
		}
		debugstr->append(":");
		for (uint16_t i=0; i<typelen; i++) {
			debugstr->append(type[i]);
		}
		debugstr->append(":");
		debugstr->append(size);
		debugstr->append(" (");
		debugstr->append(precision);
		debugstr->append(",");
		debugstr->append(scale);
		debugstr->append(") ");
		if (!nullable) {
			debugstr->append("NOT NULL ");
		}
		if (primarykey) {
			debugstr->append("Primary key ");
		}
		if (unique) {
			debugstr->append("Unique");
		}
		dbgfile.debugPrint("connection",3,debugstr->getString());
		delete debugstr;
	}

	clientsock->write(namelen);
	clientsock->write(name,namelen);
	clientsock->write(typelen);
	clientsock->write(type,typelen);
	clientsock->write(size);
	clientsock->write(precision);
	clientsock->write(scale);
	clientsock->write(nullable);
	clientsock->write(primarykey);
	clientsock->write(unique);
	clientsock->write(partofkey);
	clientsock->write(unsignednumber);
	clientsock->write(zerofill);
	clientsock->write(binary);
	clientsock->write(autoincrement);
}

bool sqlrcontroller_svr::returnResultSetData(sqlrcursor_svr *cursor) {

	dbgfile.debugPrint("connection",2,"returning result set data...");

	updateState(RETURN_RESULT_SET);

	// decide whether to use the cursor itself
	// or an attached custom query cursor
	if (cursor->customquerycursor) {
		cursor=cursor->customquerycursor;
	}

	// get the number of rows to skip
	uint64_t	skip;
	if (clientsock->read(&skip,idleclienttimeout,0)!=sizeof(uint64_t)) {
		dbgfile.debugPrint("connection",2,
				"returning result set data failed");
		return false;
	}

	// get the number of rows to fetch
	uint64_t	fetch;
	if (clientsock->read(&fetch,idleclienttimeout,0)!=sizeof(uint64_t)) {
		dbgfile.debugPrint("connection",2,
				"returning result set data failed");
		return false;
	}

	// reinit cursor state (in case it was suspended)
	cursor->state=SQLRCURSORSTATE_BUSY;

	// for some queries, there are no rows to return, 
	if (cursor->noRowsToReturn()) {
		clientsock->write((uint16_t)END_RESULT_SET);
		flushWriteBuffer();
		dbgfile.debugPrint("connection",2,
				"done returning result set data");
		return true;
	}

	// skip the specified number of rows
	if (!skipRows(cursor,skip)) {
		clientsock->write((uint16_t)END_RESULT_SET);
		flushWriteBuffer();
		dbgfile.debugPrint("connection",2,
				"done returning result set data");
		return true;
	}


	if (dbgfile.debugEnabled()) {
		debugstr=new stringbuffer();
		debugstr->append("fetching ");
		debugstr->append(fetch);
		debugstr->append(" rows...");
		dbgfile.debugPrint("connection",2,debugstr->getString());
		delete debugstr;
	}

	// send the specified number of rows back
	for (uint64_t i=0; (!fetch || i<fetch); i++) {

		if (!cursor->fetchRow()) {
			clientsock->write((uint16_t)END_RESULT_SET);
			flushWriteBuffer();
			dbgfile.debugPrint("connection",2,
					"done returning result set data");
			return true;
		}

		if (dbgfile.debugEnabled()) {
			debugstr=new stringbuffer();
		}

		returnRow(cursor);

		if (dbgfile.debugEnabled()) {
			dbgfile.debugPrint("connection",3,
						debugstr->getString());
			delete debugstr;
		}

		if (cursor->lastrowvalid) {
			cursor->lastrow++;
		} else {
			cursor->lastrowvalid=true;
			cursor->lastrow=0;
		}
	}
	flushWriteBuffer();

	dbgfile.debugPrint("connection",2,"done returning result set data");
	return true;
}

bool sqlrcontroller_svr::skipRows(sqlrcursor_svr *cursor, uint64_t rows) {

	if (dbgfile.debugEnabled()) {
		debugstr=new stringbuffer();
		debugstr->append("skipping ");
		debugstr->append(rows);
		debugstr->append(" rows...");
		dbgfile.debugPrint("connection",2,debugstr->getString());
		delete debugstr;
	}

	for (uint64_t i=0; i<rows; i++) {

		dbgfile.debugPrint("connection",3,"skip...");

		if (cursor->lastrowvalid) {
			cursor->lastrow++;
		} else {
			cursor->lastrowvalid=true;
			cursor->lastrow=0;
		}

		if (!cursor->skipRow()) {
			dbgfile.debugPrint("connection",2,
				"skipping rows hit the end of the result set");
			return false;
		}
	}

	dbgfile.debugPrint("connection",2,"done skipping rows");
	return true;
}

void sqlrcontroller_svr::returnRow(sqlrcursor_svr *cursor) {

	// run through the columns...
	for (uint32_t i=0; i<cursor->colCount(); i++) {

		// init variables
		const char	*field=NULL;
		uint64_t	fieldlength=0;
		bool		blob=false;
		bool		null=false;

		// get the field
		cursor->getField(i,&field,&fieldlength,&blob,&null);

		// send data to the client
		if (null) {
			sendNullField();
		} else if (blob) {
			sendLobField(cursor,i);
			cursor->cleanUpLobField(i);
		} else {
			sendField(field,fieldlength);
		}
	}

	// get the next row
	cursor->nextRow();
}

void sqlrcontroller_svr::sendField(const char *data, uint32_t size) {

	if (dbgfile.debugEnabled()) {
		debugstr->append("\"");
		debugstr->append(data,size);
		debugstr->append("\",");
	}

	clientsock->write((uint16_t)STRING_DATA);
	clientsock->write(size);
	clientsock->write(data,size);
}

void sqlrcontroller_svr::sendNullField() {

	if (dbgfile.debugEnabled()) {
		debugstr->append("NULL");
	}

	clientsock->write((uint16_t)NULL_DATA);
}

#define MAX_BYTES_PER_CHAR	4

void sqlrcontroller_svr::sendLobField(sqlrcursor_svr *cursor, uint32_t col) {

	// Get lob length.  If this fails, send a NULL field.
	uint64_t	loblength;
	if (!cursor->getLobFieldLength(col,&loblength)) {
		sendNullField();
		return;
	}

	// for lobs of 0 length
	if (!loblength) {
		startSendingLong(0);
		sendLongSegment("",0);
		endSendingLong();
		return;
	}

	// initialize sizes and status
	uint64_t	charstoread=sizeof(cursor->lobbuffer)/
						MAX_BYTES_PER_CHAR;
	uint64_t	charsread=0;
	uint64_t	offset=0;
	bool		start=true;

	for (;;) {

		// read a segment from the lob
		if (!cursor->getLobFieldSegment(col,
					cursor->lobbuffer,
					sizeof(cursor->lobbuffer),
					offset,charstoread,&charsread) ||
					!charsread) {

			// if we fail to get a segment or got nothing...
			// if we haven't started sending yet, then send a NULL,
			// otherwise just end normally
			if (start) {
				sendNullField();
			} else {
				endSendingLong();
			}
			return;

		} else {

			// if we haven't started sending yet, then do that now
			if (start) {
				startSendingLong(loblength);
				start=false;
			}

			// send the segment we just got
			sendLongSegment(cursor->lobbuffer,charsread);

			// FIXME: or should this be charsread?
			offset=offset+charstoread;
		}
	}
}

void sqlrcontroller_svr::startSendingLong(uint64_t longlength) {
	clientsock->write((uint16_t)START_LONG_DATA);
	clientsock->write(longlength);
}

void sqlrcontroller_svr::sendLongSegment(const char *data, uint32_t size) {

	if (dbgfile.debugEnabled()) {
		debugstr->append(data,size);
	}

	clientsock->write((uint16_t)STRING_DATA);
	clientsock->write(size);
	clientsock->write(data,size);
}

void sqlrcontroller_svr::endSendingLong() {

	if (dbgfile.debugEnabled()) {
		debugstr->append(",");
	}

	clientsock->write((uint16_t)END_LONG_DATA);
}

void sqlrcontroller_svr::abortResultSetCommand(sqlrcursor_svr *cursor) {

	dbgfile.debugPrint("connection",1,"aborting result set...");

	// Very important...
	// Do not cleanUpData() here, otherwise result sets that were suspended
	// after the entire result set was fetched won't be able to return
	// column data when resumed.
	cursor->abort();

	dbgfile.debugPrint("connection",1,"done aborting result set");
}

void sqlrcontroller_svr::suspendResultSetCommand(sqlrcursor_svr *cursor) {
	dbgfile.debugPrint("connection",1,"suspend result set...");
	cursor->state=SQLRCURSORSTATE_SUSPENDED;
	if (cursor->customquerycursor) {
		cursor->customquerycursor->state=SQLRCURSORSTATE_SUSPENDED;
	}
	dbgfile.debugPrint("connection",1,"done suspending result set");
}

bool sqlrcontroller_svr::resumeResultSetCommand(sqlrcursor_svr *cursor) {
	dbgfile.debugPrint("connection",1,"resume result set...");

	bool	retval=true;

	if (cursor->state==SQLRCURSORSTATE_SUSPENDED) {

		dbgfile.debugPrint("connection",2,
				"previous result set was suspended");

		// indicate that no error has occurred
		clientsock->write((uint16_t)NO_ERROR_OCCURRED);

		// send the client the id of the 
		// cursor that it's going to use
		clientsock->write(cursor->id);
		clientsock->write((uint16_t)SUSPENDED_RESULT_SET);

		// if the requested cursor really had a suspended
		// result set, send the lastrow of it to the client
		// then resume the result set
		clientsock->write(cursor->lastrow);

		returnResultSetHeader(cursor);
		retval=returnResultSetData(cursor);

	} else {

		dbgfile.debugPrint("connection",2,
				"previous result set was not suspended");

		// indicate that an error has occurred
		clientsock->write((uint16_t)ERROR_OCCURRED);

		// send the error code (zero for now)
		clientsock->write((uint64_t)SQLR_ERROR_RESULTSETNOTSUSPENDED);

		// send the error itself
		uint16_t	len=charstring::length(
				SQLR_ERROR_RESULTSETNOTSUSPENDED_STRING);
		clientsock->write(len);
		clientsock->write(SQLR_ERROR_RESULTSETNOTSUSPENDED_STRING,len);

		retval=false;
	}

	dbgfile.debugPrint("connection",1,"done resuming result set");
	return retval;
}

void sqlrcontroller_svr::closeConnection() {

	if (inclientsession) {
		endSession();
		decrementOpenClientConnections();
		inclientsession=false;
	}

	// decrement the connection counter
	if (decrementonclose && cfgfl->getDynamicScaling() &&
						semset && idmemory) {
		decrementConnectionCount();
	}

	// deregister and close the handoff socket if necessary
	if (cfgfl->getPassDescriptor()) {
		deRegisterForHandoff(tmpdir->getString());
	}

	// close the cursors
	closeCursors(true);


	// try to log out
	dbgfile.debugPrint("connection",0,"logging out...");
	logOut();
	dbgfile.debugPrint("connection",0,"done logging out");


	// clear the pool
	dbgfile.debugPrint("connection",0,"removing all sockets...");
	removeAllFileDescriptors();
	dbgfile.debugPrint("connection",0,"done removing all sockets");


	// close, clean up all sockets
	dbgfile.debugPrint("connection",0,"deleting unix socket...");
	delete serversockun;
	dbgfile.debugPrint("connection",0,"done deleting unix socket");


	dbgfile.debugPrint("connection",0,"deleting inetsockets...");
	for (uint64_t index=0; index<serversockincount; index++) {
		delete serversockin[index];
	}
	delete[] serversockin;
	dbgfile.debugPrint("connection",0,"done deleting inet socket");
}

void sqlrcontroller_svr::closeCursors(bool destroy) {

	dbgfile.debugPrint("connection",0,"closing cursors...");

	if (cur) {
		while (cursorcount) {
			cursorcount--;

			dbgfile.debugPrint("connection",1,(int32_t)cursorcount);

			if (cur[cursorcount]) {
				cur[cursorcount]->cleanUpData();
				cur[cursorcount]->close();
				if (destroy) {
					deleteCursor(cur[cursorcount]);
					cur[cursorcount]=NULL;
				}
			}
		}
		if (destroy) {
			delete[] cur;
			cur=NULL;
		}
	}

	dbgfile.debugPrint("connection",0,"done closing cursors...");
}

void sqlrcontroller_svr::deleteCursor(sqlrcursor_svr *curs) {
	conn->deleteCursor(curs);
	decrementOpenDatabaseCursors();
}

void sqlrcontroller_svr::addSessionTempTableForDrop(const char *table) {
	sessiontemptablesfordrop.append(charstring::duplicate(table));
}

void sqlrcontroller_svr::addTransactionTempTableForDrop(const char *table) {
	transtemptablesfordrop.append(charstring::duplicate(table));
}

void sqlrcontroller_svr::addSessionTempTableForTrunc(const char *table) {
	sessiontemptablesfortrunc.append(charstring::duplicate(table));
}

void sqlrcontroller_svr::addTransactionTempTableForTrunc(const char *table) {
	transtemptablesfortrunc.append(charstring::duplicate(table));
}

void sqlrcontroller_svr::dropTempTables(sqlrcursor_svr *cursor,
					stringlist *tablelist) {

	// some databases require us to re-login before dropping temp tables
	if (tablelist==&sessiontemptablesfordrop &&
			tablelist->getLength() &&
			conn->tempTableDropReLogIn()) {
		reLogIn();
	}

	// run through the temp table list, dropping tables
	for (stringlistnode *sln=tablelist->getFirstNode();
			sln; sln=(stringlistnode *)sln->getNext()) {
		dropTempTable(cursor,sln->getData());
		delete[] sln->getData();
	}
	tablelist->clear();
}

void sqlrcontroller_svr::dropTempTable(sqlrcursor_svr *cursor,
					const char *tablename) {
	stringbuffer	dropquery;
	dropquery.append("drop table ");
	dropquery.append(conn->tempTableDropPrefix());
	dropquery.append(tablename);

	// FIXME: I need to refactor all of this so that this just gets
	// run as a matter of course instead of explicitly getting run here
	// FIXME: freetds/sybase override this method but don't do this
	if (sqltr) {
		if (sqlp->parse(dropquery.getString())) {
			sqltr->runBeforeTriggers(conn,cursor,sqlp->getTree());
		}
	}

	if (cursor->prepareQuery(dropquery.getString(),
					dropquery.getStringLength())) {
		executeQuery(cursor,dropquery.getString(),
					dropquery.getStringLength());
	}
	cursor->cleanUpData();

	// FIXME: I need to refactor all of this so that this just gets
	// run as a matter of course instead of explicitly getting run here
	// FIXME: freetds/sybase override this method but don't do this
	if (sqltr) {
		sqltr->runAfterTriggers(conn,cursor,sqlp->getTree(),true);
	}
}

void sqlrcontroller_svr::truncateTempTables(sqlrcursor_svr *cursor,
						stringlist *tablelist) {

	// run through the temp table list, truncateing tables
	for (stringlistnode *sln=tablelist->getFirstNode();
			sln; sln=(stringlistnode *)sln->getNext()) {
		truncateTempTable(cursor,sln->getData());
		delete[] sln->getData();
	}
	tablelist->clear();
}

void sqlrcontroller_svr::truncateTempTable(sqlrcursor_svr *cursor,
						const char *tablename) {
	stringbuffer	truncatequery;
	truncatequery.append("delete from ")->append(tablename);
	if (cursor->prepareQuery(truncatequery.getString(),
					truncatequery.getStringLength())) {
		executeQuery(cursor,truncatequery.getString(),
					truncatequery.getStringLength());
	}
	cursor->cleanUpData();
}

bool sqlrcontroller_svr::logIn(bool printerrors) {

	// don't do anything if we're already logged in
	if (loggedin) {
		return true;
	}

	// attempt to log in
	if (!conn->logIn(printerrors)) {
		return false;
	}

	// success... update stats
	incrementOpenDatabaseConnections();

	loggedin=true;
	return true;
}

void sqlrcontroller_svr::logOut() {

	// don't do anything if we're already logged out
	if (!loggedin) {
		return;
	}

	// log out
	conn->logOut();

	// update stats
	decrementOpenDatabaseConnections();

	loggedin=false;
}

void sqlrcontroller_svr::reLogIn() {

	markDatabaseUnavailable();

	// run the session end queries
	sessionEndQueries();

	// get the current db so we can restore it
	char	*currentdb=conn->getCurrentDatabase();

	// FIXME: get the isolation level so we can restore it

	dbgfile.debugPrint("connection",4,"relogging in...");

	// attempt to log in over and over, once every 5 seconds
	int32_t	oldcursorcount=cursorcount;
	closeCursors(false);
	logOut();
	for (;;) {
			
		dbgfile.debugPrint("connection",5,"trying...");

		incrementReLogInCount();

		if (logIn(false)) {
			if (!initCursors(oldcursorcount)) {
				closeCursors(false);
				logOut();
			} else {
				break;
			}
		}
		snooze::macrosnooze(5);
	}

	dbgfile.debugPrint("connection",4,"done relogging in");

	// run the session-start queries
	sessionStartQueries();

	// restore the db
	conn->selectDatabase(currentdb);
	delete[] currentdb;

	// restore autocommit
	if (conn->autocommit) {
		conn->autoCommitOn();
	} else {
		conn->autoCommitOff();
	}

	// FIXME: restore the isolation level

	markDatabaseAvailable();
}

void sqlrcontroller_svr::markDatabaseAvailable() {

	size_t	stringlen=9+charstring::length(updown)+1;
	char	*string=new char[stringlen];
	snprintf(string,stringlen,"creating %s",updown);
	dbgfile.debugPrint("connection",4,string);
	delete[] string;

	// the database is up if the file is there, 
	// opening and closing it will create it
	file	fd;
	fd.create(updown,permissions::ownerReadWrite());
}

void sqlrcontroller_svr::markDatabaseUnavailable() {

	// if the database is behind a load balancer, don't mark it unavailable
	if (constr->getBehindLoadBalancer()) {
		return;
	}

	size_t	stringlen=10+charstring::length(updown)+1;
	char	*string=new char[stringlen];
	snprintf(string,stringlen,"unlinking %s",updown);
	dbgfile.debugPrint("connection",4,string);
	delete[] string;

	// the database is down if the file isn't there
	file::remove(updown);
}

const char *sqlrcontroller_svr::connectStringValue(const char *variable) {

	// If we're using password encryption and the password is requested,
	// then return the decrypted version of it, otherwise just return
	// the value as-is.
	const char	*peid=constr->getPasswordEncryption();
	if (sqlrpe && charstring::length(peid) &&
			!charstring::compare(variable,"password")) {
		sqlrpwdenc	*pe=sqlrpe->getPasswordEncryptionById(peid);
		delete[] decrypteddbpassword;
		decrypteddbpassword=pe->decrypt(
			constr->getConnectStringValue(variable));
		return decrypteddbpassword;
	}
	return constr->getConnectStringValue(variable);
}

bool sqlrcontroller_svr::getUnixSocket(const char *tmpdir, char *unixsocketptr) {

	dbgfile.debugPrint("connection",0,"getting unix socket...");

	file	sockseq;
	if (!openSequenceFile(&sockseq,tmpdir,unixsocketptr) ||
						!lockSequenceFile(&sockseq)) {
		return false;
	}
	if (!getAndIncrementSequenceNumber(&sockseq,unixsocketptr)) {
		unLockSequenceFile(&sockseq);
		sockseq.close();
		return false;
	}
	if (!unLockSequenceFile(&sockseq)) {
		sockseq.close();
		return false;
	}
	if (!sockseq.close()) {
		return false;
	}

	dbgfile.debugPrint("connection",0,"done getting unix socket");

	return true;
}

bool sqlrcontroller_svr::openSequenceFile(file *sockseq,
				const char *tmpdir, char *unixsocketptr) {

	// open the sequence file and get the current port number
	size_t	sockseqnamelen=charstring::length(tmpdir)+9;
	char	*sockseqname=new char[sockseqnamelen];
	snprintf(sockseqname,sockseqnamelen,"%s/sockseq",tmpdir);

	size_t	stringlen=8+charstring::length(sockseqname)+1;
	char	*string=new char[stringlen];
	snprintf(string,stringlen,"opening %s",sockseqname);
	dbgfile.debugPrint("connection",1,string);
	delete[] string;

	mode_t	oldumask=umask(011);
	bool	success=sockseq->open(sockseqname,O_RDWR|O_CREAT,
				permissions::everyoneReadWrite());
	umask(oldumask);

	// handle error
	if (!success) {
		fprintf(stderr,"Could not open: %s\n",sockseqname);
		fprintf(stderr,"Make sure that the file and directory are \n");
		fprintf(stderr,"readable and writable.\n\n");
		unixsocketptr[0]='\0';

		stringlen=14+charstring::length(sockseqname)+1;
		string=new char[stringlen];
		snprintf(string,stringlen,"couldn't open %s",sockseqname);
		dbgfile.debugPrint("connection",1,string);
		delete[] string;
	}

	delete[] sockseqname;

	return success;
}

bool sqlrcontroller_svr::lockSequenceFile(file *sockseq) {

	dbgfile.debugPrint("connection",1,"locking...");

	return sockseq->lockFile(F_WRLCK);
}


bool sqlrcontroller_svr::getAndIncrementSequenceNumber(file *sockseq,
							char *unixsocketptr) {

	// get the sequence number from the file
	int32_t	buffer;
	if (sockseq->read(&buffer)!=sizeof(int32_t)) {
		buffer=0;
	}
	sprintf(unixsocketptr,"%d",buffer);

	size_t	stringlen=21+charstring::length(unixsocketptr)+1;
	char	*string=new char[stringlen];
	snprintf(string,stringlen,"got sequence number: %s",unixsocketptr);
	dbgfile.debugPrint("connection",1,string);
	delete[] string;

	// increment the sequence number
	// (the (double) cast is required for solaris with -compat=4)
	if (buffer==pow((double)2,31)) {
		buffer=0;
	} else {
		buffer=buffer+1;
	}

	string=new char[50];
	snprintf(string,50,"writing new sequence number: %d",buffer);
	dbgfile.debugPrint("connection",1,string);
	delete[] string;

	// write the sequence number back to the file
	if (sockseq->setPositionRelativeToBeginning(0)==-1) {
		return false;
	}
	return (sockseq->write(buffer)==sizeof(int32_t));
}

bool sqlrcontroller_svr::unLockSequenceFile(file *sockseq) {

	// unlock and close the file in a platform-independent manner
	dbgfile.debugPrint("connection",1,"unlocking...");

	return sockseq->unlockFile();
}

void sqlrcontroller_svr::bindFormatCommand() {

	dbgfile.debugPrint("connection",1,"bind format");

	// get the bind format
	const char	*bf=conn->bindFormat();

	// send it to the client
	uint16_t	bflen=charstring::length(bf);
	clientsock->write(bflen);
	clientsock->write(bf,bflen);
	flushWriteBuffer();
}

void sqlrcontroller_svr::dbVersionCommand() {

	dbgfile.debugPrint("connection",1,"db version");

	// get the db version
	const char	*dbversion=conn->dbVersion();

	// send it to the client
	uint16_t	dbvlen=charstring::length(dbversion);
	clientsock->write(dbvlen);
	clientsock->write(dbversion,dbvlen);
	flushWriteBuffer();
}

void sqlrcontroller_svr::serverVersionCommand() {

	dbgfile.debugPrint("connection",1,"server version");

	// get the server version
	const char	*svrversion=SQLR_VERSION;

	// send it to the client
	uint16_t	svrvlen=charstring::length(svrversion);
	clientsock->write(svrvlen);
	clientsock->write(svrversion,svrvlen);
	flushWriteBuffer();
}

signalhandler *sqlrcontroller_svr::handleSignals(
					void (*shutdownfunction)(int32_t)) {

	// handle kill signals (SIGINT, SIGTERM)
	this->handleShutDown(shutdownfunction);

	// handle crash signals (SIGSEGV)
	// read or write outside the memory that is allocated

	// We don't want to catch SEGV, we want core to be dumped
	//this->handleCrash(shutdownfunction);

	// but daemonprocess sets it's own handler for SIGSEGV
	// Since rudiments does not provide us with service to set
	// default signal handler, we should do it by direct call to signal(2)
	//signal (SIGSEGV, SIG_DFL);

	signalhandler *sighandler=new signalhandler();
	sighandler->setHandler(shutdownfunction);


	sighandler->handleSignal(SIGALRM); // handle alarm for timeouts

#ifdef HAVE_SIGQUIT
	sighandler->handleSignal(SIGQUIT);
#endif

	// handle program bugs
#ifdef HAVE_SIGBUS
	// This signal is generated when an invalid pointer is dereferenced.
	sighandler->handleSignal(SIGBUS);
#endif
#ifdef HAVE_SIGFPE
	// fatal arithmetic error
	sighandler->handleSignal(SIGFPE);
#endif
#ifdef HAVE_SIGILL
	// trying to execute garbage or a privileged instruction
	sighandler->handleSignal(SIGILL);
#endif
#ifdef HAVE_SIGABRT
	// calling abort
	sighandler->handleSignal(SIGABRT);
#endif
#ifdef HAVE_SIGIOT
	// Generated by the PDP-11 “iot” instruction. On most machines, this is just another name for SIGABRT
	sighandler->handleSignal(SIGIOT);
#endif
#ifdef HAVE_SIGTRAP
	// program will probably only see SIGTRAP if it is somehow executing bad instructions
	sighandler->handleSignal(SIGTRAP);
#endif
#ifdef HAVE_SIGEMT
	// unimplemented instructions which might be emulated in software
	sighandler->handleSignal(SIGEMT);
#endif
#ifdef HAVE_SIGSYS
	// Bad system call
	sighandler->handleSignal(SIGSYS);
#endif
#ifdef HAVE_SIGXCPU
	// CPU consumption "soft limit"
	sighandler->handleSignal(SIGXCPU);
#endif
#ifdef HAVE_SIGXFSZ
	// File size limit exceeded
	sighandler->handleSignal(SIGXFSZ);
#endif
#ifdef HAVE_SIGPWR
	// power failure
	sighandler->handleSignal(SIGPWR);
#endif

	// ignore others
	signalset	set;
	set.removeAllSignals();

	// HangUp
#ifdef HAVE_SIGHUP
	set.addSignal(SIGHUP);
#endif
	// Operation Error Signals
#ifdef HAVE_SIGPIPE
	set.addSignal(SIGPIPE);
#endif
#ifdef HAVE_SIGLOST
	set.addSignal(SIGLOST);
#endif
	// Miscellaneous Signals
#ifdef HAVE_SIGTTIN
	set.addSignal(SIGTTIN);
#endif
#ifdef HAVE_SIGTTOU
	set.addSignal(SIGTTOU);
#endif
#ifdef HAVE_SIGVTALRM
	set.addSignal(SIGVTALRM);
#endif
#ifdef HAVE_SIGPROF
	set.addSignal(SIGPROF);
#endif
#ifdef HAVE_SIGIO
	set.addSignal(SIGIO);
#endif
#ifdef HAVE_SIGPOLL
	set.addSignal(SIGPOLL);
#endif
	signalmanager::ignoreSignals(&set);

	return sighandler;
}

void sqlrcontroller_svr::returnError(bool disconnect) {

	// Get the error data if none is set already
	if (!conn->error) {
		conn->errorMessage(conn->error,maxerrorlength,
				&conn->errorlength,&conn->errnum,
				&conn->liveconnection);
		if (!conn->liveconnection) {
			disconnect=true;
		}
	}

	// send the appropriate error status
	if (disconnect) {
		clientsock->write((uint16_t)ERROR_OCCURRED_DISCONNECT);
	} else {
		clientsock->write((uint16_t)ERROR_OCCURRED);
	}

	// send the error code and error string
	clientsock->write((uint64_t)conn->errnum);

	// send the error string
	clientsock->write((uint16_t)conn->errorlength);
	clientsock->write(conn->error,conn->errorlength);
}

void sqlrcontroller_svr::returnError(sqlrcursor_svr *cursor, bool disconnect) {

	dbgfile.debugPrint("connection",2,"returning error...");

	// send the appropriate error status
	if (disconnect) {
		clientsock->write((uint16_t)ERROR_OCCURRED_DISCONNECT);
	} else {
		clientsock->write((uint16_t)ERROR_OCCURRED);
	}

	// send the error code
	clientsock->write((uint64_t)cursor->errnum);

	// send the error string
	clientsock->write((uint16_t)cursor->errorlength);
	clientsock->write(cursor->error,cursor->errorlength);

	// client will be sending skip/fetch, better get
	// it even though we're not going to use it
	uint64_t	skipfetch;
	clientsock->read(&skipfetch,idleclienttimeout,0);
	clientsock->read(&skipfetch,idleclienttimeout,0);

	// Even though there was an error, we still 
	// need to send the client the id of the 
	// cursor that it's going to use.
	clientsock->write(cursor->id);
	flushWriteBuffer();

	dbgfile.debugPrint("connection",2,"done returning error");
}

void sqlrcontroller_svr::setUser(const char *user) {
	this->user=user;
}

void sqlrcontroller_svr::setPassword(const char *password) {
	this->password=password;
}

const char *sqlrcontroller_svr::getUser() {
	return user;
}

const char *sqlrcontroller_svr::getPassword() {
	return password;
}

void sqlrcontroller_svr::initConnStats() {

	semset->waitWithUndo(9);

	// Find an available location in the connstats array.
	// It shouldn't be possible for sqlr-start or sqlr-scaler to start
	// more than MAXCONNECTIONS, so unless someone started one manually,
	// it should always be possible to find an open one.
	for (uint32_t i=0; i<MAXCONNECTIONS; i++) {
		connstats=&shm->connstats[i];
		if (!connstats->processid) {

			semset->signalWithUndo(9);

			// initialize the connection stats
			clearConnStats();
			updateState(INIT);
			connstats->index=i;
			connstats->processid=process::getProcessId();
			connstats->logged_in_tv.tv_sec=loggedinsec;
			connstats->logged_in_tv.tv_usec=loggedinusec;
			return;
		}
	}

	semset->signalWithUndo(9);

	// in case someone started a connection manually and
	// exceeded MAXCONNECTIONS, set this NULL here
	connstats=NULL;
}

void sqlrcontroller_svr::clearConnStats() {
	if (!connstats) {
		return;
	}
	rawbuffer::zero(connstats,sizeof(struct sqlrconnstatistics));
}

void sqlrcontroller_svr::updateState(enum sqlrconnectionstate_t state) {
	if (!connstats) {
		return;
	}
	connstats->state=state;
	gettimeofday(&connstats->state_start_tv,NULL);
}

void sqlrcontroller_svr::updateClientSessionStartTime() {
	if (!connstats) {
		return;
	}
	gettimeofday(&connstats->clientsession_tv,NULL);
}

void sqlrcontroller_svr::updateCurrentQuery(const char *query,
						uint32_t querylen) {
	if (!connstats) {
		return;
	}
	uint32_t	len=querylen;
	if (len>STATSQLTEXTLEN-1) {
		len=STATSQLTEXTLEN-1;
	}
	charstring::copy(connstats->sqltext,query,len);
	connstats->sqltext[len]='\0';
}

void sqlrcontroller_svr::updateClientInfo(const char *info, uint32_t infolen) {
	if (!connstats) {
		return;
	}
	uint64_t	len=infolen;
	if (len>STATCLIENTINFOLEN-1) {
		len=STATCLIENTINFOLEN-1;
	}
	charstring::copy(connstats->clientinfo,info,len);
	connstats->clientinfo[len]='\0';
}

void sqlrcontroller_svr::updateClientAddr() {
	if (!connstats) {
		return;
	}
	if (clientsock) {
		char	*clientaddrbuf=clientsock->getPeerAddress();
		if (clientaddrbuf) {
			charstring::copy(connstats->clientaddr,clientaddrbuf);
			delete[] clientaddrbuf;
		} else {
			charstring::copy(connstats->clientaddr,"UNIX");
		}
	} else {
		charstring::copy(connstats->clientaddr,"internal");
	}
}

void sqlrcontroller_svr::incrementOpenDatabaseConnections() {
	semset->waitWithUndo(9);
	shm->open_db_connections++;
	shm->opened_db_connections++;
	semset->signalWithUndo(9);
}

void sqlrcontroller_svr::decrementOpenDatabaseConnections() {
	semset->waitWithUndo(9);
	if (shm->open_db_connections) {
		shm->open_db_connections--;
	}
	semset->signalWithUndo(9);
}

void sqlrcontroller_svr::incrementOpenClientConnections() {
	semset->waitWithUndo(9);
	shm->open_cli_connections++;
	shm->opened_cli_connections++;
	semset->signalWithUndo(9);
	if (!connstats) {
		return;
	}
	connstats->nconnect++;
}

void sqlrcontroller_svr::decrementOpenClientConnections() {
	semset->waitWithUndo(9);
	if (shm->open_cli_connections) {
		shm->open_cli_connections--;
	}
	semset->signalWithUndo(9);
}

void sqlrcontroller_svr::incrementOpenDatabaseCursors() {
	semset->waitWithUndo(9);
	shm->open_db_cursors++;
	shm->opened_db_cursors++;
	semset->signalWithUndo(9);
}

void sqlrcontroller_svr::decrementOpenDatabaseCursors() {
	semset->waitWithUndo(9);
	if (shm->open_db_cursors) {
		shm->open_db_cursors--;
	}
	semset->signalWithUndo(9);
}

void sqlrcontroller_svr::incrementTimesNewCursorUsed() {
	semset->waitWithUndo(9);
	shm->times_new_cursor_used++;
	semset->signalWithUndo(9);
}

void sqlrcontroller_svr::incrementTimesCursorReused() {
	semset->waitWithUndo(9);
	shm->times_cursor_reused++;
	semset->signalWithUndo(9);
}

void sqlrcontroller_svr::incrementQueryCounts(sqlrquerytype_t querytype) {

	semset->waitWithUndo(9);

	// update total queries
	shm->total_queries++;

	// update queries-per-second stats...

	// re-init stats if necessary
	datetime	dt;
	dt.getSystemDateAndTime();
	time_t	now=dt.getEpoch();
	int	index=now%STATQPSKEEP;
	if (shm->timestamp[index]!=now) {
		shm->timestamp[index]=now;
		shm->qps_select[index]=0;
		shm->qps_update[index]=0;
		shm->qps_insert[index]=0;
		shm->qps_delete[index]=0;
		shm->qps_create[index]=0;
		shm->qps_drop[index]=0;
		shm->qps_alter[index]=0;
		shm->qps_custom[index]=0;
		shm->qps_etc[index]=0;
	}

	// increment per-query-type stats
	switch (querytype) {
		case SQLRQUERYTYPE_SELECT:
			shm->qps_select[index]++;
			break;
		case SQLRQUERYTYPE_INSERT:
			shm->qps_insert[index]++;
			break;
		case SQLRQUERYTYPE_UPDATE:
			shm->qps_update[index]++;
			break;
		case SQLRQUERYTYPE_DELETE:
			shm->qps_delete[index]++;
			break;
		case SQLRQUERYTYPE_CREATE:
			shm->qps_create[index]++;
			break;
		case SQLRQUERYTYPE_DROP:
			shm->qps_drop[index]++;
			break;
		case SQLRQUERYTYPE_ALTER:
			shm->qps_alter[index]++;
			break;
		case SQLRQUERYTYPE_CUSTOM:
			shm->qps_custom[index]++;
			break;
		case SQLRQUERYTYPE_ETC:
		default:
			shm->qps_etc[index]++;
			break;
	}

	semset->signalWithUndo(9);

	if (!connstats) {
		return;
	}
	if (querytype==SQLRQUERYTYPE_CUSTOM) {
		connstats->ncustomsql++;
	} else {
		connstats->nsql++;
	}
}

void sqlrcontroller_svr::incrementTotalErrors() {
	semset->waitWithUndo(9);
	shm->total_errors++;
	semset->signalWithUndo(9);
}

void sqlrcontroller_svr::incrementAuthenticateCount() {
	if (!connstats) {
		return;
	}
	connstats->nauthenticate++;
}

void sqlrcontroller_svr::incrementSuspendSessionCount() {
	if (!connstats) {
		return;
	}
	connstats->nsuspend_session++;
}

void sqlrcontroller_svr::incrementEndSessionCount() {
	if (!connstats) {
		return;
	}
	connstats->nend_session++;
}

void sqlrcontroller_svr::incrementPingCount() {
	if (!connstats) {
		return;
	}
	connstats->nping++;
}

void sqlrcontroller_svr::incrementIdentifyCount() {
	if (!connstats) {
		return;
	}
	connstats->nidentify++;
}

void sqlrcontroller_svr::incrementAutocommitCount() {
	if (!connstats) {
		return;
	}
	connstats->nautocommit++;
}

void sqlrcontroller_svr::incrementBeginCount() {
	if (!connstats) {
		return;
	}
	connstats->nbegin++;
}

void sqlrcontroller_svr::incrementCommitCount() {
	if (!connstats) {
		return;
	}
	connstats->ncommit++;
}

void sqlrcontroller_svr::incrementRollbackCount() {
	if (!connstats) {
		return;
	}
	connstats->nrollback++;
}

void sqlrcontroller_svr::incrementDbVersionCount() {
	if (!connstats) {
		return;
	}
	connstats->ndbversion++;
}

void sqlrcontroller_svr::incrementBindFormatCount() {
	if (!connstats) {
		return;
	}
	connstats->nbindformat++;
}

void sqlrcontroller_svr::incrementServerVersionCount() {
	if (!connstats) {
		return;
	}
	connstats->nserverversion++;
}

void sqlrcontroller_svr::incrementSelectDatabaseCount() {
	if (!connstats) {
		return;
	}
	connstats->nselectdatabase++;
}

void sqlrcontroller_svr::incrementGetCurrentDatabaseCount() {
	if (!connstats) {
		return;
	}
	connstats->ngetcurrentdatabase++;
}

void sqlrcontroller_svr::incrementGetLastInsertIdCount() {
	if (!connstats) {
		return;
	}
	connstats->ngetlastinsertid++;
}

void sqlrcontroller_svr::incrementNewQueryCount() {
	if (!connstats) {
		return;
	}
	connstats->nnewquery++;
}

void sqlrcontroller_svr::incrementReexecuteQueryCount() {
	if (!connstats) {
		return;
	}
	connstats->nreexecutequery++;
}

void sqlrcontroller_svr::incrementFetchFromBindCursorCount() {
	if (!connstats) {
		return;
	}
	connstats->nfetchfrombindcursor++;
}

void sqlrcontroller_svr::incrementFetchResultSetCount() {
	if (!connstats) {
		return;
	}
	connstats->nfetchresultset++;
}

void sqlrcontroller_svr::incrementAbortResultSetCount() {
	if (!connstats) {
		return;
	}
	connstats->nabortresultset++;
}

void sqlrcontroller_svr::incrementSuspendResultSetCount() {
	if (!connstats) {
		return;
	}
	connstats->nsuspendresultset++;
}

void sqlrcontroller_svr::incrementResumeResultSetCount() {
	if (!connstats) {
		return;
	}
	connstats->nresumeresultset++;
}

void sqlrcontroller_svr::incrementGetDbListCount() {
	if (!connstats) {
		return;
	}
	connstats->ngetdblist++;
}

void sqlrcontroller_svr::incrementGetTableListCount() {
	if (!connstats) {
		return;
	}
	connstats->ngettablelist++;
}

void sqlrcontroller_svr::incrementGetColumnListCount() {
	if (!connstats) {
		return;
	}
	connstats->ngetcolumnlist++;
}

void sqlrcontroller_svr::incrementGetQueryTreeCount() {
	if (!connstats) {
		return;
	}
	connstats->ngetquerytree++;
}

void sqlrcontroller_svr::incrementReLogInCount() {
	if (!connstats) {
		return;
	}
	connstats->nrelogin++;
}

bool sqlrcontroller_svr::getQueryTreeCommand(sqlrcursor_svr *cursor) {

	dbgfile.debugPrint("connection",1,"getting query tree");

	// get the tree as a string
	xmldomnode	*tree=(cursor->querytree)?
				cursor->querytree->getRootNode():NULL;
	stringbuffer	*xml=(tree)?tree->xml():NULL;
	const char	*xmlstring=(xml)?xml->getString():NULL;
	uint64_t	xmlstringlen=(xml)?xml->getStringLength():0;

	// send the tree
	clientsock->write(xmlstringlen);
	clientsock->write(xmlstring,xmlstringlen);
	flushWriteBuffer();

	// clean up
	delete xml;

	return true;
}
