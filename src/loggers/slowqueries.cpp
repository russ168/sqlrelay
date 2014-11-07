// Copyright (c) 2012  David Muse
// See the file COPYING for more information

#include <sqlrelay/sqlrlistener.h>
#include <sqlrelay/sqlrservercontroller.h>
#include <sqlrelay/sqlrserverconnection.h>
#include <sqlrelay/sqlrservercursor.h>
#include <sqlrelay/sqlrlogger.h>
#include <rudiments/process.h>
#include <rudiments/charstring.h>
#include <rudiments/file.h>
#include <rudiments/permissions.h>
#include <rudiments/filesystem.h>
#include <rudiments/stringbuffer.h>

class slowqueries : public sqlrlogger {
	public:
			slowqueries(xmldomnode *parameters);
			~slowqueries();

		bool	init(sqlrlistener *sqlrl, sqlrserverconnection *sqlrcon);
		bool	run(sqlrlistener *sqlrl,
					sqlrserverconnection *sqlrcon,
					sqlrservercursor *sqlrcur,
					sqlrlogger_loglevel_t level,
					sqlrlogger_eventtype_t event,
					const char *info);
	private:
		char		*querylogname;
		file		querylog;
		uint64_t	sec;
		uint64_t	usec;
		uint64_t	totalusec;
};

slowqueries::slowqueries(xmldomnode *parameters) : sqlrlogger(parameters) {
	querylogname=NULL;
	sec=charstring::toInteger(parameters->getAttributeValue("sec"));
	usec=charstring::toInteger(parameters->getAttributeValue("usec"));
	totalusec=sec*1000000+usec;
}

slowqueries::~slowqueries() {
	querylog.flushWriteBuffer(-1,-1);
	delete[] querylogname;
}

bool slowqueries::init(sqlrlistener *sqlrl, sqlrserverconnection *sqlrcon) {

	// don't log anything for the listener
	if (!sqlrcon) {
		return true;
	}

	// get the pid
	pid_t	pid=process::getProcessId();

	// build up the query log name
	size_t	querylognamelen=
			charstring::length(sqlrcon->cont->getLogDir())+17+
			charstring::length(sqlrcon->cont->getId())+10+20+1;
	delete[] querylogname;
	querylogname=new char[querylognamelen];
	charstring::printf(querylogname,querylognamelen,
				"%s/sqlr-connection-%s-querylog.%ld",
				sqlrcon->cont->getLogDir(),
				sqlrcon->cont->getId(),(long)pid);

	// remove any old log file
	file::remove(querylogname);

	// create the new log file
	if (!querylog.create(querylogname,
				permissions::evalPermString("rw-------"))) {
		return false;
	}

	// optimize
	filesystem	fs;
	fs.initialize(querylogname);
	querylog.setWriteBufferSize(fs.getOptimumTransferBlockSize());
	return true;
}

bool slowqueries::run(sqlrlistener *sqlrl,
				sqlrserverconnection *sqlrcon,
				sqlrservercursor *sqlrcur,
				sqlrlogger_loglevel_t level,
				sqlrlogger_eventtype_t event,
				const char *info) {

	// don't log anything for the listener
	if (!sqlrcon) {
		return true;
	}

	// don't do anything unless we got INFO/QUERY
	if (level!=SQLRLOGGER_LOGLEVEL_INFO ||
		event!=SQLRLOGGER_EVENTTYPE_QUERY) {
		return true;
	}

	// reinit the log if the file was switched
	file	querylog2;
	if (querylog2.open(querylogname,O_RDONLY)) {
		ino_t	inode1=querylog.getInode();
		ino_t	inode2=querylog2.getInode();
		querylog2.close();
		if (inode1!=inode2) {
			querylog.flushWriteBuffer(-1,-1);
			querylog.close();
			init(sqlrl,sqlrcon);
		}
	}

	uint64_t	querysec=sqlrcur->getQueryEndSec()-
					sqlrcur->getQueryStartSec();
	uint64_t	queryusec=sqlrcur->getQueryEndUSec()-
					sqlrcur->getQueryStartUSec();
	uint64_t	querytotalusec=querysec*1000000+queryusec;

	if (querytotalusec>totalusec) {

		stringbuffer	logentry;
		logentry.append("query:\n")->append(sqlrcur->getQueryBuffer());
		logentry.append("\n");
		logentry.append("time: ")->append(querysec);
		logentry.append(".");
		char	*usecstr=charstring::parseNumber(queryusec,6);
		logentry.append(usecstr);
		delete[] usecstr;
		logentry.append("\n");
		if ((size_t)querylog.write(logentry.getString(),
					logentry.getStringLength())!=
						logentry.getStringLength()) {
			return false;
		}
	}
	return true;
}

extern "C" {
	sqlrlogger	*new_slowqueries(xmldomnode *parameters) {
		return new slowqueries(parameters);
	}
}
