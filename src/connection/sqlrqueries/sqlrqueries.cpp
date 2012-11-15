// Copyright (c) 2012  David Muse
// See the file COPYING for more information

#include <sqlrqueries.h>
#include <sqlrconnection.h>
#include <sqlrcursor.h>
#include <debugprint.h>

#include <rudiments/xmldomnode.h>

#ifdef RUDIMENTS_NAMESPACE
using namespace rudiments;
#endif

sqlrqueries::sqlrqueries() {
	debugFunction();
	xmld=NULL;
}

sqlrqueries::~sqlrqueries() {
	debugFunction();
	unloadQueries();
	delete xmld;
}

bool sqlrqueries::loadQueries(const char *queries) {
	debugFunction();

	unloadQueries();

	// create the parser
	delete xmld;
	xmld=new xmldom();

	// parse the queries
	if (!xmld->parseString(queries)) {
		return false;
	}

	// get the queries tag
	xmldomnode	*queriesnode=
			xmld->getRootNode()->getFirstTagChild("queries");
	if (queriesnode->isNullNode()) {
		return false;
	}

	// run through the query list
	for (xmldomnode *query=queriesnode->getFirstTagChild();
		!query->isNullNode(); query=query->getNextTagSibling()) {

		debugPrintf("loading query ...\n");

		// load query
		loadQuery(query);
	}
	return true;
}

void sqlrqueries::unloadQueries() {
	debugFunction();
	for (linkedlistnode< sqlrqueryplugin * > *node=
				llist.getFirstNode();
					node; node=node->getNext()) {
		sqlrqueryplugin	*sqlrlp=node->getData();
		delete sqlrlp->qr;
		delete sqlrlp->dl;
		delete sqlrlp;
	}
	llist.clear();
}

void sqlrqueries::loadQuery(xmldomnode *query) {

	debugFunction();

	// ignore non-queries
	if (charstring::compare(query->getName(),"query")) {
		return;
	}

	// get the query name
	const char	*file=query->getAttributeValue("file");
	if (!charstring::length(file)) {
		return;
	}

	debugPrintf("loading query: %s\n",file);

	// load the query module
	stringbuffer	modulename;
	modulename.append(LIBDIR);
	modulename.append("/libsqlrelay_sqlrquery_");
	modulename.append(file)->append(".so");
	dynamiclib	*dl=new dynamiclib();
	if (!dl->open(modulename.getString(),true,true)) {
		printf("failed to load query module: %s\n",file);
		char	*error=dl->getError();
		printf("%s\n",error);
		delete[] error;
		delete dl;
		return;
	}

	// load the query itself
	stringbuffer	functionname;
	functionname.append("new_")->append(file);
	sqlrquery *(*newQuery)(xmldomnode *)=
			(sqlrquery *(*)(xmldomnode *))
				dl->getSymbol(functionname.getString());
	if (!newQuery) {
		printf("failed to create query: %s\n",file);
		char	*error=dl->getError();
		printf("%s\n",error);
		delete[] error;
		dl->close();
		delete dl;
		return;
	}
	sqlrquery	*qr=(*newQuery)(query);

	// add the plugin to the list
	sqlrqueryplugin	*sqlrlp=new sqlrqueryplugin;
	sqlrlp->qr=qr;
	sqlrlp->dl=dl;
	llist.append(sqlrlp);
}

void sqlrqueries::initQueries(sqlrconnection_svr *sqlrcon) {
	debugFunction();
	for (linkedlistnode< sqlrqueryplugin * > *node=llist.getFirstNode();
						node; node=node->getNext()) {
		node->getData()->qr->init(sqlrcon);
	}
}

sqlrquery *sqlrqueries::match(sqlrconnection_svr *sqlrcon,
					sqlrcursor_svr *sqlrcur,
					const char *querystring) {
	debugFunction();
	for (linkedlistnode< sqlrqueryplugin * > *node=llist.getFirstNode();
						node; node=node->getNext()) {
		sqlrquery	*qr=node->getData()->qr;
		if (qr->match(sqlrcon,sqlrcur,querystring)) {
			return qr;
		}
	}
	return NULL;
}
