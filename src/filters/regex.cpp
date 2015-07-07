// Copyright (c) 2015  David Muse
// See the file COPYING for more information

#include <sqlrelay/sqlrserver.h>
#include <rudiments/regularexpression.h>
#include <debugprint.h>

class SQLRSERVER_DLLSPEC regex : public sqlrfilter {
	public:
			regex(sqlrfilters *sqlrfs,
					xmldomnode *parameters,
					bool debug);
		bool	run(sqlrserverconnection *sqlrcon,
					sqlrservercursor *sqlrcur,
					const char *query);
	private:
		regularexpression	re;
		const char		*pattern;

		bool	enabled;
};

regex::regex(sqlrfilters *sqlrfs,
			xmldomnode *parameters,
			bool debug) : sqlrfilter(sqlrfs,parameters,debug) {
	debugFunction();

	enabled=charstring::compareIgnoringCase(
			parameters->getAttributeValue("enabled"),"no");
	if (!enabled) {
		return;
	}

	pattern=parameters->getAttributeValue("pattern");
	re.compile(pattern);
	re.study();
}

bool regex::run(sqlrserverconnection *sqlrcon,
				sqlrservercursor *sqlrcur,
				const char *query) {
	debugFunction();

	if (!enabled) {
		return true;
	}

	if (re.match(query)) {
		if (debug) {
			stdoutput.printf("regex: matches pattern \"%s\"\n\n",
								pattern);
		}
		return false;
	}
	return true;
}

extern "C" {
	SQLRSERVER_DLLSPEC sqlrfilter *new_sqlrfilter_regex(sqlrfilters *sqlrfs,
							xmldomnode *parameters,
							bool debug) {
		return new regex(sqlrfs,parameters,debug);
	}
}
