// Copyright (c) 2015  David Muse
// See the file COPYING for more information

#include <sqlrelay/sqlrserver.h>
#include <rudiments/charstring.h>

class SQLRSERVER_DLLSPEC sqlrauth_database : public sqlrauth {
	public:
			sqlrauth_database(xmldomnode *parameters,
					sqlrpwdencs *sqlrpe);
		bool	auth(sqlrserverconnection *sqlrcon,
					const char *user, const char *password);
};

sqlrauth_database::sqlrauth_database(xmldomnode *parameters,
					sqlrpwdencs *sqlrpe) :
					sqlrauth(parameters,sqlrpe) {
}

bool sqlrauth_database::auth(sqlrserverconnection *sqlrcon,
				const char *user, const char *password) {

	sqlrservercontroller	*cont=sqlrcon->cont;

	// if the user we want to change to is different from the user
	// that's currently logged in, try to change to that user
	bool	success=true;
	const char	*lastuser=cont->getLastUser();
	const char	*lastpassword=cont->getLastPassword();
	if ((charstring::isNullOrEmpty(lastuser) &&
		charstring::isNullOrEmpty(lastpassword)) || 
		charstring::compare(lastuser,user) ||
		charstring::compare(lastpassword,password)) {

		// change user
		success=cont->changeUser(user,password);

		// keep a record of which user we're changing to
		// and whether that user was successful in auth
		cont->setLastUser((success)?user:NULL);
		cont->setLastPassword((success)?password:NULL);
	}
	return success;
}

extern "C" {
	SQLRSERVER_DLLSPEC sqlrauth *new_sqlrauth_database(
						xmldomnode *users,
						sqlrpwdencs *sqlrpe) {
		return new sqlrauth_database(users,sqlrpe);
	}
}
