// Copyright (c) 1999-2012  David Muse
// See the file COPYING for more information

#include <sqltriggers.h>
#include <sqlrconnection.h>
#include <sqlrcursor.h>
#include <debugprint.h>

#include <rudiments/dynamiclib.h>
#include <rudiments/xmldomnode.h>

sqltriggers::sqltriggers() {
	debugFunction();
	xmld=NULL;
}

sqltriggers::~sqltriggers() {
	debugFunction();
	unloadTriggers();
	delete xmld;
}

bool sqltriggers::loadTriggers(const char *triggers) {
	debugFunction();

	unloadTriggers();

	// create the parser
	delete xmld;
	xmld=new xmldom();

	// parse the triggers
	if (!xmld->parseString(triggers)) {
		return false;
	}

	// get the triggers tag
	xmldomnode	*triggersnode=
			xmld->getRootNode()->getFirstTagChild("triggers");
	if (triggersnode->isNullNode()) {
		return false;
	}

	// run through the trigger list
	for (xmldomnode *trigger=triggersnode->getFirstTagChild();
		!trigger->isNullNode(); trigger=trigger->getNextTagSibling()) {

		// get whether to run before or after a query
		bool	before=!charstring::compare(
					trigger->getAttributeValue("when"),
					"before");

		debugPrintf("loading trigger %s ...\n",
				(before)?"before":"after");

		// determine which list to put the trigger in
		linkedlist< sqltrigger * >	*list=
				(before)?&beforetriggers:&aftertriggers;

		// load trigger
		sqltrigger	*tr=loadTrigger(trigger);
		if (tr) {
			list->append(tr);
		}
	}
	return true;
}

void sqltriggers::unloadTriggers() {
	debugFunction();
	for (linkedlistnode< sqltrigger * > *node=
			beforetriggers.getFirstNode();
				node; node=node->getNext()) {
		delete node->getData();
	}
	beforetriggers.clear();
	for (linkedlistnode< sqltrigger * > *node=
			aftertriggers.getFirstNode();
				node; node=node->getNext()) {
		delete node->getData();
	}
	aftertriggers.clear();
}

sqltrigger *sqltriggers::loadTrigger(xmldomnode *trigger) {
	debugFunction();

	// ignore non-triggers
	if (charstring::compare(trigger->getName(),"trigger")) {
		return NULL;
	}

	// get the trigger name
	const char	*file=trigger->getAttributeValue("file");
	if (!charstring::length(file)) {
		return NULL;
	}

	debugPrintf("loading trigger: %s\n",file);

	// load the trigger module
	stringbuffer	modulename;
	modulename.append(LIBDIR);
	modulename.append("/libsqlrelay_sqltrigger_");
	modulename.append(file)->append(".so");
	dynamiclib	*dl=new dynamiclib();
	if (!dl->open(modulename.getString(),true,true)) {
		printf("failed to load trigger module: %s\n",file);
		char	*error=dl->getError();
		printf("%s\n",error);
		delete[] error;
		delete dl;
		return NULL;
	}

	// load the trigger itself
	stringbuffer	functionname;
	functionname.append("new_")->append(file);
	sqltrigger *(*newTrigger)(xmldomnode *)=
			(sqltrigger *(*)(xmldomnode *))
				dl->getSymbol(functionname.getString());
	if (!newTrigger) {
		printf("failed to create trigger: %s\n",file);
		char	*error=dl->getError();
		printf("%s\n",error);
		delete[] error;
		dl->close();
		delete dl;
		return NULL;
	}
	sqltrigger	*tr=(*newTrigger)(trigger);

	// attach the module so deleting the trigger also closes the library
	tr->attachModule(dl);

	return tr;
}

void sqltriggers::runBeforeTriggers(sqlrconnection_svr *sqlrcon,
					sqlrcursor_svr *sqlrcur,
					xmldom *querytree) {
	debugFunction();
	runTriggers(sqlrcon,sqlrcur,querytree,&beforetriggers,true,true);
}

void sqltriggers::runAfterTriggers(sqlrconnection_svr *sqlrcon,
						sqlrcursor_svr *sqlrcur,
						xmldom *querytree,
						bool success) {
	debugFunction();
	runTriggers(sqlrcon,sqlrcur,querytree,&aftertriggers,false,success);
}

void sqltriggers::runTriggers(sqlrconnection_svr *sqlrcon,
					sqlrcursor_svr *sqlrcur,
					xmldom *querytree,
					linkedlist< sqltrigger * > *list,
					bool before,
					bool success) {
	debugFunction();
	if (!querytree) {
		return;
	}
	for (linkedlistnode< sqltrigger * > *node=list->getFirstNode();
						node; node=node->getNext()) {
		node->getData()->run(sqlrcon,sqlrcur,querytree,before,success);
	}
}
