// Copyright (c) 1999-2012  David Muse
// See the file COPYING for more information

#include <sqlrconnection.h>
#include <sqlrcursor.h>
#include <sqlparser.h>
#include <sqltranslation.h>
#include <debugprint.h>

#ifdef RUDIMENTS_NAMESPACE
using namespace rudiments;
#endif

class serialtoidentity : public sqltranslation {
	public:
			serialtoidentity(sqltranslations *sqlts,
						xmldomnode *parameters);
		bool	run(sqlrconnection_svr *sqlrcon,
					sqlrcursor_svr *sqlrcur,
					xmldom *querytree);
};

serialtoidentity::serialtoidentity(sqltranslations *sqlts,
						xmldomnode *parameters) :
					sqltranslation(sqlts,parameters) {
}

bool serialtoidentity::run(sqlrconnection_svr *sqlrcon,
					sqlrcursor_svr *sqlrcur,
					xmldom *querytree) {
	debugFunction();

	// for each column of a create query
	for (xmldomnode *columnnode=querytree->getRootNode()->
				getFirstTagChild(sqlparser::_create)->
				getFirstTagChild(sqlparser::_table)->
				getFirstTagChild(sqlparser::_columns)->
				getFirstTagChild(sqlparser::_column);
		!columnnode->isNullNode();
		columnnode=columnnode->getNextTagSibling(sqlparser::_column)) {

		// get the type node
		xmldomnode	*typenode=
			columnnode->getFirstTagChild(sqlparser::_type);
		if (typenode->isNullNode()) {
			continue;
		}

		// skip non-serial types
		const char	*coltype=
			typenode->getAttributeValue(sqlparser::_value);
		bool	serial=!charstring::compare(coltype,"serial");
		bool	serial8=!charstring::compare(coltype,"serial8");
		if (!serial && !serial8) {
			continue;
		}

		// replace column type
		const char	*newcoltype="int";
		if (serial8) {
			// FIXME: for some db's this needs to be a
			// type other than "int" such as long, quad or
			// something else
		}
		typenode->setAttributeValue(sqlparser::_value,newcoltype);

		// find the constraints node or create one
		xmldomnode	*constraintsnode=
			columnnode->getFirstTagChild(sqlparser::_constraints);
		if (constraintsnode->isNullNode()) {
			constraintsnode=sqlts->newNode(columnnode,
						sqlparser::_constraints);
		}

		// add identity constraint
		sqlts->newNode(constraintsnode,sqlparser::_identity);
	}

	return true;
}

extern "C" {
	sqltranslation	*new_serialtoidentity(sqltranslations *sqlts,
					xmldomnode *parameters) {
		return new serialtoidentity(sqlts,parameters);
	}
}