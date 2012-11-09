// Copyright (c) 1999-2001  David Muse
// See the file COPYING for more information

#include <sqlrconnection.h>

sqlrcursor_svr::sqlrcursor_svr(sqlrconnection_svr *conn) {

	this->conn=conn;
	inbindcount=0;
	outbindcount=0;

	stats.query=NULL;
	stats.result=false;
	stats.error=NULL;
	stats.errnum=0;
	stats.sec=0;
	stats.usec=0;
	
	busy=false;

	createtemp.compile("(create|CREATE|declare|DECLARE)[ \\t\\r\\n]+((global|GLOBAL|local|LOCAL)?[ \\t\\r\\n]+)?(temp|TEMP|temporary|TEMPORARY)?[ \\t\\r\\n]+(table|TABLE)[ \\t\\r\\n]+");

	querybuffer=new char[conn->maxquerysize+1];
	querylength=0;
	querytree=NULL;
	fakeinputbindsforthisquery=false;

	sid_sqlrcur=NULL;
	if (conn->cfgfl->getSidEnabled()) {
		sid_sqlrcur=new sqlrcursor(conn->sid_sqlrcon);
		sql_injection_detection_parameters();
	}
	sid_egress=true;
}
