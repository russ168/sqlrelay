// Copyright (c) 2012  David Muse
// See the file COPYING for more information

#ifndef SQLRPWDENCS_H
#define SQLRPWDENCS_H

#include <sqlrelay/private/sqlrserverdll.h>

#include <rudiments/xmldom.h>
#include <rudiments/singlylinkedlist.h>
#include <rudiments/dynamiclib.h>
#include <sqlrelay/sqlrpwdenc.h>

class SQLRSERVER_DLLSPEC sqlrpwdencplugin {
	public:
		sqlrpwdenc	*pe;
		dynamiclib	*dl;
};

class SQLRSERVER_DLLSPEC sqlrpwdencs {
	public:
			sqlrpwdencs();
			~sqlrpwdencs();

		bool		loadPasswordEncryptions(const char *pwdencs);
		sqlrpwdenc	*getPasswordEncryptionById(const char *id);
	private:
		void	unloadPasswordEncryptions();
		void	loadPasswordEncryption(xmldomnode *pwdenc);

		xmldom					*xmld;
		singlylinkedlist< sqlrpwdencplugin * >	llist;
};

#endif
