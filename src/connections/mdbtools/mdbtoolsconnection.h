// Copyright (c) 2000-2001  David Muse
// See the file COPYING for more information

#ifndef MDBTOOLSCONNECTION_H
#define MDBTOOLSCONNECTION_H

#define NUM_CONNECT_STRING_VARS 1

#include <sqlrconnection.h>

extern "C" {
	#include <mdbsql.h>
}

class mdbtoolsconnection;

class mdbtoolscursor : public sqlrcursor {
	friend class mdbtoolsconnection;
	private:
			mdbtoolscursor(sqlrconnection *conn);
		bool	openCursor(int id);
		bool	closeCursor();
		bool	executeQuery(const char *query,
					long length,
					bool execute);
		char	*getErrorMessage(bool *liveconnection);
		void	returnRowCounts();
		void	returnColumnCount();
		void	returnColumnInfo();
		bool	noRowsToReturn();
		bool	skipRow();
		bool	fetchRow();
		void	returnRow();
		void	cleanUpData(bool freeresult, bool freebinds);

		mdbtoolsconnection	*mdbtoolsconn;

		MdbSQL	mdbsql;
};

class mdbtoolsconnection : public sqlrconnection {
	friend class mdbtoolscursor;
	public:
			mdbtoolsconnection();
	private:
		int	getNumberOfConnectStringVars();
		void	handleConnectString();
		bool	logIn();
		sqlrcursor	*initCursor();
		void	deleteCursor(sqlrcursor *curs);
		void	logOut();
		bool	isTransactional();
		bool	ping();
		char	*identify();
		bool	autoCommitOn();
		bool	autoCommitOff();
		bool	commit();
		bool	rollback();

		char	*db;
};

#endif
