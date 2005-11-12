// Copyright (c) 2000-2001  David Muse
// See the file COPYING for more information

#ifndef ODBCCCONNECTION_H
#define ODBCCCONNECTION_H

#define FETCH_AT_ONCE		10
#define MAX_SELECT_LIST_SIZE	256
#define MAX_ITEM_BUFFER_SIZE	4096

#define NUM_CONNECT_STRING_VARS 3

#ifdef HAVE_IODBC
	#include <iodbcinst.h>
#endif
#ifdef __CYGWIN__
	#include <windows.h>
	#include <w32api/sql.h>
	#include <w32api/sqlext.h>
	#include <w32api/sqltypes.h>
#else
	#include <sql.h>
	#include <sqlext.h>
	#include <sqltypes.h>
#endif

// note that sqlrconnection.h must be included after sqltypes.h to
// get around a problem with CHAR/xmlChar in gnome-xml
#include <sqlrconnection.h>

#include <config.h>

struct column {
	char		name[MAX_ITEM_BUFFER_SIZE];
	uint16_t	namelength;
	// SQLColAttribute requires that these are signed, 32 bit integers
	int32_t		type;
	int32_t		length;
	int32_t		precision;
	int32_t		scale;
	int32_t		nullable;
	uint16_t	primarykey;
	uint16_t	unique;
	uint16_t	partofkey;
	uint16_t	unsignednumber;
	uint16_t	zerofill;
	uint16_t	binary;
	uint16_t	autoincrement;
};

class odbcconnection;

class odbccursor : public sqlrcursor {
	friend class odbcconnection;
	private:
				odbccursor(sqlrconnection *conn);
				~odbccursor();
		bool		prepareQuery(const char *query,
						uint32_t length);
		bool		inputBindString(const char *variable, 
						uint16_t variablesize,
						const char *value, 
						uint16_t valuesize,
						short *isnull);
		bool		inputBindInteger(const char *variable, 
						uint16_t variablesize,
						int64_t *value);
		bool		inputBindDouble(const char *variable, 
						uint16_t variablesize,
						double *value, 
						uint32_t precision,
						uint32_t scale);
		bool		outputBindString(const char *variable, 
						uint16_t variablesize,
						const char *value, 
						uint16_t valuesize,
						short *isnull);
		short		nonNullBindValue();
		short		nullBindValue();
		bool		bindValueIsNull(short isnull);
		bool		executeQuery(const char *query,
						uint32_t length,
						bool execute);
		const char	*getErrorMessage(bool *liveconnection);
		void		returnRowCounts();
		void		returnColumnCount();
		void		returnColumnInfo();
		bool		noRowsToReturn();
		bool		skipRow();
		bool		fetchRow();
		void		returnRow();


		SQLRETURN	erg;
		SQLHSTMT	stmt;
		SQLSMALLINT	ncols;
		SQLINTEGER 	affectedrows;

// this code is here in case unixodbc ever 
// successfully supports array fetches

/*#ifdef HAVE_UNIXODBC
		char		field[MAX_SELECT_LIST_SIZE]
					[FETCH_AT_ONCE]
					[MAX_ITEM_BUFFER_SIZE];
		SQLINTEGER	indicator[MAX_SELECT_LIST_SIZE]
						[FETCH_AT_ONCE];
#else*/
		char		field[MAX_SELECT_LIST_SIZE]
					[MAX_ITEM_BUFFER_SIZE];
		SQLINTEGER	indicator[MAX_SELECT_LIST_SIZE];
//#endif
		column 		col[MAX_SELECT_LIST_SIZE];

		uint32_t	row;
		uint32_t	maxrow;
		uint32_t	totalrows;
		uint32_t	rownumber;

		stringbuffer	*errormsg;

		odbcconnection	*odbcconn;
};

class odbcconnection : public sqlrconnection {
	friend class odbccursor;
	private:
		uint16_t	getNumberOfConnectStringVars();
		void		handleConnectString();
		bool		logIn();
		sqlrcursor	*initCursor();
		void		deleteCursor(sqlrcursor *curs);
		void		logOut();
#if (ODBCVER>=0x0300)
		bool		autoCommitOn();
		bool		autoCommitOff();
		bool		commit();
		bool		rollback();
#endif
		bool		ping();
		const char	*identify();

		SQLRETURN	erg;
		SQLHENV		env;
		SQLHDBC		dbc;

		const char	*dsn;
};

#endif
