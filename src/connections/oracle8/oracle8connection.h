// Copyright (c) 2000-2001  David Muse
// See the file COPYING for more information

#ifndef ORACLECONNECTION_H
#define ORACLECONNECTION_H

#define FETCH_AT_ONCE		10
#define MAX_SELECT_LIST_SIZE	256
#define MAX_ITEM_BUFFER_SIZE	4096

#define NUM_CONNECT_STRING_VARS 5

#include <sqlrconnection.h>

extern "C" {
	#include <oci.h>

	#define VARCHAR2_TYPE 1
	#define	NUMBER_TYPE 2
	#define	LONG_TYPE 8
	#define ROWID_TYPE 11
	#define DATE_TYPE 12
	#define RAW_TYPE 23
	#define LONG_RAW_TYPE 24
	#define CHAR_TYPE 96
	#define MLSLABEL_TYPE 105
	#define CLOB_TYPE 112
	#define BLOB_TYPE 113
	#define BFILE_TYPE 114

	#define LONG_BIND_TYPE 3
	#define DOUBLE_BIND_TYPE 4
	#define NULL_TERMINATED_STRING 5
}

struct describe {
	OCIParam	*paramd;
	sb4		dbsize;
	sb2		dbtype;
	text		*buf;
	sb4		buflen;
};

class oracle8connection;

class oracle8cursor : public sqlrcursor {
	friend class oracle8connection;
	private:
			oracle8cursor(sqlrconnection *conn);
			~oracle8cursor();
		int	openCursor(int id);
		int	closeCursor();
		int	prepareQuery(const char *query, long length);
		int	inputBindString(const char *variable, 
					unsigned short variablesize,
					const char *value, 
					unsigned short valuesize,
					short *isnull);
		int	inputBindLong(const char *variable, 
					unsigned short variablesize,
					unsigned long *value);
		int	inputBindDouble(const char *variable, 
					unsigned short variablesize,
					double *value,
					unsigned short precision,
					unsigned short scale);
		int	inputBindBlob(const char *variable, 
					unsigned short variablesize,
					const char *value, 
					unsigned long valuesize,
					short *isnull);
		int	inputBindClob(const char *variable, 
					unsigned short variablesize,
					const char *value, 
					unsigned long valuesize,
					short *isnull);
		int	inputBindGenericLob(const char *variable, 
					unsigned short variablesize,
					const char *value, 
					unsigned long valuesize,
					short *isnull,
					ub1 temptype,
					ub2 type);
		int	outputBindString(const char *variable, 
					unsigned short variablesize,
					char *value,
					unsigned short valuesize,
					short *isnull);
		int	outputBindBlob(const char *variable, 
					unsigned short variablesize,
					int index,
					short *isnull);
		int	outputBindClob(const char *variable, 
					unsigned short variablesize,
					int index,
					short *isnull);
		int	outputBindGenericLob(const char *variable, 
					unsigned short variablesize,
					int index,
					short *isnull,
					ub2 type);
		int	outputBindCursor(const char *variable,
					unsigned short variablesize,
					sqlrcursor *cursor);
		void	returnOutputBindBlob(int index);
		void	returnOutputBindClob(int index);
		void	returnOutputBindGenericLob(int index);
		int	executeQuery(const char *query, long length,
					unsigned short execute);
		int	queryIsNotSelect();
		int	queryIsCommitOrRollback();
		char	*getErrorMessage(int *liveconnection);
		void	returnRowCounts();
		void	returnColumnCount();
		void	returnColumnInfo();
		int	noRowsToReturn();
		int	skipRow();
		int	fetchRow();
		void	returnRow();
		void	cleanUpData();

		void	checkRePrepare();

		OCIStmt		*stmt;
		ub2		stmttype;
		sword		ncols;
		stringbuffer	*errormessage;

		describe	desc[MAX_SELECT_LIST_SIZE];
		OCIDefine	*def[MAX_SELECT_LIST_SIZE];
		OCILobLocator	*def_lob[MAX_SELECT_LIST_SIZE][FETCH_AT_ONCE];
		ub1		def_buf[MAX_SELECT_LIST_SIZE]
					[FETCH_AT_ONCE][MAX_ITEM_BUFFER_SIZE];
		sb2		def_indp[MAX_SELECT_LIST_SIZE][FETCH_AT_ONCE];
		ub2		def_col_retlen[MAX_SELECT_LIST_SIZE]
						[FETCH_AT_ONCE];
		ub2		def_col_retcode[MAX_SELECT_LIST_SIZE]
						[FETCH_AT_ONCE];

		OCILobLocator	*inbind_lob[MAXVAR];
		OCILobLocator	*outbind_lob[MAXVAR];
		int		inbindlobcount;
		int		outbindlobcount;

		int		row;
		int		maxrow;
		int		totalrows;

		int		fetchatonce;

		char		*query;
		int		length;
		int		prepared;

		oracle8connection	*oracle8conn;
};
	
class oracle8connection : public sqlrconnection {
	friend class oracle8cursor;
	public:
				oracle8connection();
	private:
		int	getNumberOfConnectStringVars();
		void	handleConnectString();
		int	logIn();
		sqlrcursor	*initCursor();
		void	deleteCursor(sqlrcursor *curs);
		void	logOut();
#ifdef OCI_ATTR_PROXY_CREDENTIALS
		int		changeUser(const char *newuser,
						const char *newpassword);
#endif
		unsigned short	autoCommitOn();
		unsigned short	autoCommitOff();
		int	commit();
		int	rollback();
		int	ping();
		char	*identify();

		ub4		statementmode;

		OCIEnv		*env;
		OCIServer	*srv;
		OCIError	*err;
		OCISvcCtx	*svc;
		OCISession	*session;
		OCITrans	*trans;

#ifdef OCI_ATTR_PROXY_CREDENTIALS
		OCISession	*newsession;
#endif

		char		*home;
		char		*sid;
		int		autocommit;

		char		*oraclehomeenv;
		char		*oraclesidenv;
		char		*twotaskenv;
};

#endif
