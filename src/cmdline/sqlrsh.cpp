// Copyright (c) 1999-2012  David Muse
// See the file COPYING for more information

#include <config.h>
#include <defaults.h>

#include <sqlrelay/sqlrclient.h>
#include <rudiments/commandline.h>
#include <rudiments/file.h>
#include <rudiments/filesystem.h>
#include <rudiments/filedescriptor.h>
#include <rudiments/process.h>
#include <rudiments/environment.h>
#include <rudiments/datetime.h>
#include <rudiments/signalclasses.h>
#include <sqlrconfigfile.h>

// for clock()
#include <time.h>

// for printf, fflush
#include <stdio.h>

#ifdef RUDIMENTS_NAMESPACE
using namespace rudiments;
#endif


#ifdef HAVE_READLINE
	#include <rudiments/charstring.h>
	// This is an interesting story...
	// readline 2's include files don't list any parameters for any of
	// the functions.  This is fine for C, but not C++, at least not with
	// the compiler I'm using, even with the extern "C" {} bit.
	// This is fixed in readline 4, but, to maintain compatibility with 
	// readline 2, I define the functions myself.
	extern "C" {
		extern char *readline(char *prompt);
		extern void add_history(char *line);
		extern void read_history(char *file);
		extern void write_history(char *file);
		extern void history_truncate_file(char *file, int line);
	}
#endif

class sqlrshbindvalue {
	public:
		union {
			char	*stringval;
			int64_t	integerval;
			struct {
				double		value;
				uint32_t	precision;
				uint32_t	scale;
			} doubleval;
			struct {
				int16_t		year;
				int16_t		month;
				int16_t		day;
				int16_t		hour;
				int16_t		minute;
				int16_t		second;
				int32_t		microsecond;
				const char	*tz;
			} dateval;
		};
		bindtype	type;
		uint32_t	outputstringbindlength;
};

class sqlrshenv {
	public:
			sqlrshenv();
			~sqlrshenv();
		void	 clearbinds(
			stringdictionary< sqlrshbindvalue * > *binds);

		bool		headers;
		bool		stats;
		bool		debug;
		uint64_t	rsbs;
		bool		final;
		bool		autocommit;
		char		delimiter;
		stringdictionary< sqlrshbindvalue * >	inputbinds;
		stringdictionary< sqlrshbindvalue * >	outputbinds;
};

sqlrshenv::sqlrshenv() {
	headers=true;
	stats=true;
	debug=false;
	rsbs=100;
	final=false;
	autocommit=false;
	delimiter=';';
}

sqlrshenv::~sqlrshenv() {
	clearbinds(&inputbinds);
	clearbinds(&outputbinds);
}

void sqlrshenv::clearbinds(stringdictionary< sqlrshbindvalue * > *binds) {

	for (stringdictionarylistnode< sqlrshbindvalue * > *node=
		(stringdictionarylistnode< sqlrshbindvalue *> *)
		binds->getList()->getFirstNode(); node;
		node=(stringdictionarylistnode< sqlrshbindvalue *> *)
							node->getNext()) {

		delete[] node->getData()->getKey();
		sqlrshbindvalue	*bv=node->getData()->getData();
		if (bv->type==STRING_BIND) {
			delete[] bv->stringval;
		}
		delete node->getData();
	}
	binds->clear();
}

enum querytype_t {
	SHOW_DATABASES_QUERY=0,
	SHOW_TABLES_QUERY,
	SHOW_COLUMNS_QUERY,
	DESCRIBE_QUERY
};

class	sqlrsh {
	public:
#ifndef HAVE_READLINE
			sqlrsh();
#endif
		void	execute(int argc, const char **argv);
	private:
		void	startupMessage(sqlrshenv *env,
					const char *host, uint16_t port,
					const char *user);
		void	systemRcFile(sqlrconnection *sqlrcon, 
					sqlrcursor *sqlrcur, 
					sqlrshenv *env);
		void	userRcFile(sqlrconnection *sqlrcon, 
					sqlrcursor *sqlrcur, 
					sqlrshenv *env);
		void	runScript(sqlrconnection *sqlrcon,
					sqlrcursor *sqlrcur, sqlrshenv *env, 
					const char *filename, bool returnerror,
					bool displaycommand);
		bool	getCommandFromFile(file *fl,
					stringbuffer *cmdbuffer,
					sqlrshenv *env);
		bool	runCommand(sqlrconnection *sqlrcon, 
					sqlrcursor *sqlrcur, 
					sqlrshenv *env, const char *command);
		int	commandType(const char *command);
		void	internalCommand(sqlrconnection *sqlrcon,
					sqlrcursor *sqlrcur, sqlrshenv *env,
					const char *command);
		void	externalCommand(sqlrconnection *sqlrcon,
					sqlrcursor *sqlrcur, sqlrshenv *env, 
					const char *command);
		void	executeQuery(sqlrcursor *sqlrcur, sqlrshenv *env);
		char	*getWild(const char *command);
		char	*getTable(enum querytype_t querytype,
					const char *command);
		void	initStats(sqlrshenv *env);
		void	displayError(sqlrshenv *env,
					const char *message,
					const char *error,
					int64_t errornumber);
		void	displayHeader(sqlrcursor *sqlrcur, sqlrshenv *env);
		void	displayResultSet(sqlrcursor *sqlrcur, sqlrshenv *env);
		void	displayStats(sqlrcursor *sqlrcur, sqlrshenv *env);
		void	ping(sqlrconnection *sqlrcon, sqlrshenv *env);
		void	identify(sqlrconnection *sqlrcon, sqlrshenv *env);
		void	dbversion(sqlrconnection *sqlrcon, sqlrshenv *env);
		void	clientversion(sqlrconnection *sqlrcon,
						sqlrshenv *env);
		void	serverversion(sqlrconnection *sqlrcon,
						sqlrshenv *env);
		bool	lastinsertid(sqlrconnection *sqlrcon, sqlrshenv *env);
		void	inputbind(sqlrcursor *sqlrcur,
						sqlrshenv *env,
						const char *command);
		void	outputbind(sqlrcursor *sqlrcur,
						sqlrshenv *env,
						const char *command);
		void	printbinds(const char *type,
				stringdictionary< sqlrshbindvalue * >
								*binds);
		void	clearbinds(stringdictionary< sqlrshbindvalue * >
								*binds);
		void	setclientinfo(sqlrconnection *sqlrcon,
						const char *command);
		void	getclientinfo(sqlrconnection *sqlrcon);
		void	displayHelp(sqlrshenv *env);
		void	interactWithUser(sqlrconnection *sqlrcon,
					sqlrcursor *sqlrcur, sqlrshenv *env);
		void	prompt(unsigned long promptcount);
		void	error(const char *errstring);

#ifndef HAVE_READLINE
		filedescriptor	standardin;
#endif
};

#ifndef HAVE_READLINE
sqlrsh::sqlrsh() {
	standardin.setFileDescriptor(0);
	standardin.allowShortReads();
}
#endif

void sqlrsh::systemRcFile(sqlrconnection *sqlrcon, sqlrcursor *sqlrcur, 
						sqlrshenv *env) {
	runScript(sqlrcon,sqlrcur,env,SYSTEM_SQLRSHRC,false,false);
}

void sqlrsh::userRcFile(sqlrconnection *sqlrcon, sqlrcursor *sqlrcur, 
						sqlrshenv *env) {

	// get user's home directory
	const char	*home=environment::getValue("HOME");
	if (!home) {
		home="~";
	}

	// build rcfilename
	size_t	userrcfilelen=charstring::length(home)+10+1;
	char	*userrcfile=new char[userrcfilelen];
	charstring::copy(userrcfile,home);
	charstring::append(userrcfile,"/.sqlrshrc");

	// process the file
	runScript(sqlrcon,sqlrcur,env,userrcfile,false,false);
	delete[] userrcfile;
}

void sqlrsh::runScript(sqlrconnection *sqlrcon, sqlrcursor *sqlrcur, 
			sqlrshenv *env, const char *filename,
			bool returnerror, bool displaycommand) {

	char	*trimmedfilename=charstring::duplicate(filename);
	charstring::bothTrim(trimmedfilename);

	// open the file
	file	scriptfile;
	if (scriptfile.open(trimmedfilename,O_RDONLY)) {

		// optimize
		filesystem	fs;
		if (fs.initialize(trimmedfilename)) {
			scriptfile.setReadBufferSize(
				fs.getOptimumTransferBlockSize());
		}

		for (;;) {
		
			// get a command
			stringbuffer	command;
			if (!getCommandFromFile(&scriptfile,&command,env)) {
				break;
			}

			if (displaycommand) {
				printf("%s\n",command.getString());
			}

			// run the command
			runCommand(sqlrcon,sqlrcur,env,command.getString());
		}

		// close the file
		scriptfile.close();
	} else {

		// error message
		if (returnerror) {
			stringbuffer	errmesg;
			errmesg.append("Couldn't open file: ");
			errmesg.append(trimmedfilename);
			error(errmesg.getString());
		}
	}

	delete[] trimmedfilename;
}

bool sqlrsh::getCommandFromFile(file *fl, stringbuffer *cmdbuffer,
						sqlrshenv *env) {

	bool	insinglequotes=false;
	bool	indoublequotes=false;
	char	character;
	
	for (;;) {

		// get a character from the file
		if (fl->read(&character)!=sizeof(character)) {
			return false;
		}

		// handle single-quoted strings, with escaping
		if (character=='\'') {
			if (insinglequotes) {
				cmdbuffer->append(character);
				if (fl->read(&character)!=sizeof(character)) {
					return false;
				}
				if (character!='\'') {
					insinglequotes=false;
				}
			} else {
				insinglequotes=true;
			}
		}

		// handle double-quoted strings, with escaping
		if (character=='"') {
			if (indoublequotes) {
				cmdbuffer->append(character);
				if (fl->read(&character)!=sizeof(character)) {
					return false;
				}
				if (character!='"') {
					indoublequotes=false;
				}
			} else {
				indoublequotes=true;
			}
		}

		// look for an end of command delimiter
		if (!insinglequotes && !indoublequotes &&
					character==env->delimiter) {
			return true;
		}

		// write character to buffer and move on
		cmdbuffer->append(character);
	}
		
}

bool sqlrsh::runCommand(sqlrconnection *sqlrcon, sqlrcursor *sqlrcur, 
					sqlrshenv *env, const char *command) {

	int	cmdtype=commandType(command);

	if (cmdtype>0) {
		// if the command an internal command, run it as one
		internalCommand(sqlrcon,sqlrcur,env,command);
		return true;
	} else if (cmdtype==0) {
		// if the command is not an internal command, 
		// execute it as a query and display the result set
		externalCommand(sqlrcon,sqlrcur,env,command);
		return true;
	} else {
		return false;
	}
}

int sqlrsh::commandType(const char *command) {

	// skip white space
	char	*ptr=(char *)command;
	while (*ptr==' ' || *ptr=='	' || *ptr=='\n') {
		ptr++;
	}

	// compare to known internal commands
	if (!charstring::compareIgnoringCase(ptr,"headers",7) ||
		!charstring::compareIgnoringCase(ptr,"stats",5) ||
		!charstring::compareIgnoringCase(ptr,"debug",5) ||
		!charstring::compareIgnoringCase(ptr,"autocommit",10) ||
		!charstring::compareIgnoringCase(ptr,"final",5) ||
		!charstring::compareIgnoringCase(ptr,"help") ||
		!charstring::compareIgnoringCase(ptr,"ping") ||
		!charstring::compareIgnoringCase(ptr,"identify") ||
		!charstring::compareIgnoringCase(ptr,"dbversion") ||
		!charstring::compareIgnoringCase(ptr,"clientversion") ||
		!charstring::compareIgnoringCase(ptr,"serverversion") ||
		!charstring::compareIgnoringCase(ptr,"use ",4) ||
		!charstring::compareIgnoringCase(ptr,"currentdb") ||
		!charstring::compareIgnoringCase(ptr,"run",3) ||
		!charstring::compareIgnoringCase(ptr,"@",1) ||
		!charstring::compareIgnoringCase(ptr,"delimiter",9) ||
		!charstring::compareIgnoringCase(ptr,"delimeter",9) ||
		!charstring::compareIgnoringCase(ptr,"inputbind ",10) ||
		!charstring::compareIgnoringCase(ptr,"outputbind ",11) ||
		!charstring::compareIgnoringCase(ptr,"printinputbind",14) ||
		!charstring::compareIgnoringCase(ptr,"printoutputbind",15) ||
		!charstring::compareIgnoringCase(ptr,"printbinds") ||
		!charstring::compareIgnoringCase(ptr,"clearinputbind",14) ||
		!charstring::compareIgnoringCase(ptr,"clearoutputbind",15) ||
		!charstring::compareIgnoringCase(ptr,"clearbinds") ||
		!charstring::compareIgnoringCase(ptr,"lastinsertid") ||
		!charstring::compareIgnoringCase(ptr,"setclientinfo ",14) ||
		!charstring::compareIgnoringCase(ptr,"getclientinfo") ||
		!charstring::compareIgnoringCase(ptr,
					"setresultsetbuffersize ",23) ||
		!charstring::compareIgnoringCase(ptr,
					"getresultsetbuffersize")) {

		// return value of 1 is internal command
		return 1;
	}

	// look for an exit command
	if (!charstring::compareIgnoringCase(ptr,"quit",4) ||
		!charstring::compareIgnoringCase(ptr,"exit",4)) {
		return -1;
	}

	// return value of 0 is external command
	return 0;
}

void sqlrsh::internalCommand(sqlrconnection *sqlrcon, sqlrcursor *sqlrcur,
					sqlrshenv *env, const char *command) {

	// skip white space
	char	*ptr=(char *)command;
	while (*ptr==' ' || *ptr=='	' || *ptr=='\n') {
		ptr++;
	}

	// compare to known internal commands
	int	cmdtype=0;
	if (!charstring::compareIgnoringCase(ptr,"headers",7)) {
		ptr=ptr+7;
		cmdtype=2;
	} else if (!charstring::compareIgnoringCase(ptr,"stats",5)) {	
		ptr=ptr+5;
		cmdtype=3;
	} else if (!charstring::compareIgnoringCase(ptr,"debug",5)) {	
		ptr=ptr+5;
		cmdtype=4;
	} else if (!charstring::compareIgnoringCase(ptr,"autocommit",10)) {	
		ptr=ptr+10;
		cmdtype=8;
	} else if (!charstring::compareIgnoringCase(ptr,"final",5)) {	
		ptr=ptr+5;
		cmdtype=5;
	} else if (!charstring::compareIgnoringCase(ptr,"help")) {	
		displayHelp(env);
		return;
	} else if (!charstring::compareIgnoringCase(ptr,"ping")) {	
		ping(sqlrcon,env);
		return;
	} else if (!charstring::compareIgnoringCase(ptr,"use ",4)) {	
		if (!sqlrcon->selectDatabase(ptr+4)) {
			displayError(env,NULL,
					sqlrcon->errorMessage(),
					sqlrcon->errorNumber());
		}
		return;
	} else if (!charstring::compareIgnoringCase(ptr,"currentdb")) {	
		printf("%s\n",sqlrcon->getCurrentDatabase());
		return;
	} else if (!charstring::compareIgnoringCase(ptr,"run",3)) {	
		ptr=ptr+3;
		cmdtype=6;
	} else if (!charstring::compareIgnoringCase(ptr,"@",1)) {	
		ptr=ptr+1;
		cmdtype=6;
	} else if (!charstring::compareIgnoringCase(ptr,"delimiter",9) ||
			!charstring::compareIgnoringCase(ptr,"delimeter",9)) {	
		ptr=ptr+9;
		cmdtype=7;
	} else if (!charstring::compareIgnoringCase(ptr,"identify")) {	
		identify(sqlrcon,env);
		return;
	} else if (!charstring::compareIgnoringCase(ptr,"dbversion")) {	
		dbversion(sqlrcon,env);
		return;
	} else if (!charstring::compareIgnoringCase(ptr,"clientversion")) {	
		clientversion(sqlrcon,env);
		return;
	} else if (!charstring::compareIgnoringCase(ptr,"serverversion")) {	
		serverversion(sqlrcon,env);
		return;
	} else if (!charstring::compareIgnoringCase(ptr,"inputbind ",10)) {	
		inputbind(sqlrcur,env,command);
		return;
	} else if (!charstring::compareIgnoringCase(ptr,"outputbind ",11)) {	
		outputbind(sqlrcur,env,command);
		return;
	} else if (!charstring::compareIgnoringCase(ptr,"printbinds")) {	
		printbinds("Input",&env->inputbinds);
		printf("\n");
		printbinds("Output",&env->outputbinds);
		return;
	} else if (!charstring::compareIgnoringCase(ptr,"clearinputbind",14)) {	
		env->clearbinds(&env->inputbinds);
		return;
	} else if (!charstring::compareIgnoringCase(
					ptr,"clearoutputbind",15)) {	
		env->clearbinds(&env->outputbinds);
		return;
	} else if (!charstring::compareIgnoringCase(ptr,"clearbinds")) {	
		env->clearbinds(&env->inputbinds);
		env->clearbinds(&env->outputbinds);
		return;
	} else if (!charstring::compareIgnoringCase(ptr,"lastinsertid")) {	
		if (!lastinsertid(sqlrcon,env)) {
			displayError(env,NULL,
					sqlrcon->errorMessage(),
					sqlrcon->errorNumber());
		}
		return;
	} else if (!charstring::compareIgnoringCase(ptr,"setclientinfo ",14)) {	
		setclientinfo(sqlrcon,command);
		return;
	} else if (!charstring::compareIgnoringCase(ptr,"getclientinfo")) {	
		getclientinfo(sqlrcon);
		return;
	} else if (!charstring::compareIgnoringCase(
					ptr,"setresultsetbuffersize ",23)) {	
		ptr=ptr+23;
		env->rsbs=charstring::toInteger(ptr);
		if (!env->rsbs) {
			env->rsbs=100;
		}
		return;
	} else if (!charstring::compareIgnoringCase(
					ptr,"getresultsetbuffersize")) {	
		printf("%lld\n",env->rsbs);
		return;
	} else {
		return;
	}

	// skip white space
	while (*ptr==' ' || *ptr=='	' || *ptr=='\n') {
		ptr++;
	}

	// handle scripts
	if (cmdtype==6) {
		runScript(sqlrcon,sqlrcur,env,ptr,true,false);
		return;
	}

	// on or off?
	bool	toggle=false;
	if (!charstring::compareIgnoringCase(ptr,"on",2)) {
		toggle=true;
	}

	// set parameter
	if (cmdtype==2) {
		env->headers=toggle;
	} else if (cmdtype==3) {
		env->stats=toggle;
	} else if (cmdtype==4) {
		env->debug=toggle;
	} else if (cmdtype==5) {
		env->final=toggle;
	} else if (cmdtype==7) {
		env->delimiter=ptr[0];
		printf("Delimiter set to %c\n",env->delimiter);
	} else if (cmdtype==8) {
		printf("Autocommit set ");
		if (toggle) {
			sqlrcon->autoCommitOn();
			printf("on\n");
		} else {
			sqlrcon->autoCommitOff();
			printf("off\n");
		}
	}
}

void sqlrsh::externalCommand(sqlrconnection *sqlrcon,
				sqlrcursor *sqlrcur, sqlrshenv *env, 
				const char *command) {

	// init stats
	initStats(env);

	// handle debug
	if (env->debug) {
		sqlrcon->debugOn();
	}

	// handle begin, commit and rollback
	if (!charstring::compareIgnoringCase(command,"begin")) {

		if (!sqlrcon->begin()) {
			displayError(env,NULL,
					sqlrcon->errorMessage(),
					sqlrcon->errorNumber());
		}

	} else if (!charstring::compareIgnoringCase(command,"commit")) {

		if (!sqlrcon->commit()) {
			displayError(env,NULL,
					sqlrcon->errorMessage(),
					sqlrcon->errorNumber());
		}

	} else if (!charstring::compareIgnoringCase(command,"rollback")) {

		if (!sqlrcon->rollback()) {
			displayError(env,NULL,
					sqlrcon->errorMessage(),
					sqlrcon->errorNumber());
		}

	} else if (!charstring::compareIgnoringCase(command,"fields ",7)) {

		char	*table=getTable(DESCRIBE_QUERY,command);
		sqlrcur->getColumnList(table,NULL);
		delete[] table;

		for (uint64_t j=0; j<sqlrcur->rowCount(); j++) {
			if (j>0) {
				printf(",");
			}
			printf("%s",sqlrcur->getField(j,(uint32_t)0));
		}
		printf("\n");

		if (env->final) {
			sqlrcon->endSession();
		}

	} else {

		sqlrcur->setResultSetBufferSize(env->rsbs);

		// send the query
		if (!charstring::compareIgnoringCase(command,
							"show databases",14)) {
			char	*wild=getWild(command);
			sqlrcur->getDatabaseList(wild);
			delete[] wild;
		} else if (!charstring::compareIgnoringCase(command,
							"show tables",11)) {
			char	*wild=getWild(command);
			sqlrcur->getTableList(wild);
			delete[] wild;
		} else if (!charstring::compareIgnoringCase(command,
							"show columns",12)) {
			char	*table=getTable(SHOW_COLUMNS_QUERY,command);
			char	*wild=getWild(command);
			sqlrcur->getColumnList(table,wild);
			delete[] table;
			delete[] wild;
		} else if (!charstring::compareIgnoringCase(command,
							"describe ",9)) {
			char	*table=getTable(DESCRIBE_QUERY,command);
			char	*wild=getWild(command);
			sqlrcur->getColumnList(table,wild);
			delete[] table;
			delete[] wild;
		} else if (!charstring::compareIgnoringCase(command,
							"reexecute")) {	
			executeQuery(sqlrcur,env);
		} else {
			sqlrcur->prepareQuery(command);
			executeQuery(sqlrcur,env);
		}

		// look for an error
		if (sqlrcur->errorMessage()) {

			// display the error
			displayError(env,NULL,
					sqlrcur->errorMessage(),
					sqlrcur->errorNumber());

		} else {

			// display the header
			displayHeader(sqlrcur,env);

			// display the result set
			displayResultSet(sqlrcur,env);
		}

		if (env->final) {
			sqlrcon->endSession();
		}
	}

	// set debug back off
	sqlrcon->debugOff();

	// display statistics
	displayStats(sqlrcur,env);
}

void sqlrsh::executeQuery(sqlrcursor *sqlrcur, sqlrshenv *env) {

	sqlrcur->clearBinds();

	if (env->inputbinds.getList()->getLength()) {

		for (stringdictionarylistnode< sqlrshbindvalue * > *node=
			(stringdictionarylistnode< sqlrshbindvalue *> *)
			env->inputbinds.getList()->getFirstNode(); node;
			node=(stringdictionarylistnode< sqlrshbindvalue *> *)
							node->getNext()) {

			const char	*name=node->getData()->getKey();
			sqlrshbindvalue	*bv=node->getData()->getData();
			if (bv->type==STRING_BIND) {
				sqlrcur->inputBind(name,bv->stringval);
			} else if (bv->type==INTEGER_BIND) {
				sqlrcur->inputBind(name,bv->integerval);
			} else if (bv->type==DOUBLE_BIND) {
				sqlrcur->inputBind(name,bv->doubleval.value,
							bv->doubleval.precision,
							bv->doubleval.scale);
			} else if (bv->type==DATE_BIND) {
				sqlrcur->inputBind(name,
						bv->dateval.year,
						bv->dateval.month,
						bv->dateval.day,
						bv->dateval.hour,
						bv->dateval.minute,
						bv->dateval.second,
						bv->dateval.microsecond,
						bv->dateval.tz);
			} else if (bv->type==NULL_BIND) {
				sqlrcur->inputBind(name,(const char *)NULL);
			}
		}
	}

	if (env->outputbinds.getList()->getLength()) {

		for (stringdictionarylistnode< sqlrshbindvalue * > *node=
			(stringdictionarylistnode< sqlrshbindvalue *> *)
			env->outputbinds.getList()->getFirstNode(); node;
			node=(stringdictionarylistnode< sqlrshbindvalue *> *)
							node->getNext()) {

			const char	*name=node->getData()->getKey();
			sqlrshbindvalue	*bv=node->getData()->getData();
			if (bv->type==STRING_BIND) {
				// FIXME: make buffer length variable
				sqlrcur->defineOutputBindString(name,
						bv->outputstringbindlength);
			} else if (bv->type==INTEGER_BIND) {
				sqlrcur->defineOutputBindInteger(name);
			} else if (bv->type==DOUBLE_BIND) {
				sqlrcur->defineOutputBindDouble(name);
			} else if (bv->type==DATE_BIND) {
				sqlrcur->defineOutputBindDate(name);
			}
		}
	}

	sqlrcur->executeQuery();

	if (env->outputbinds.getList()->getLength()) {

		for (stringdictionarylistnode< sqlrshbindvalue * > *node=
			(stringdictionarylistnode< sqlrshbindvalue *> *)
			env->outputbinds.getList()->getFirstNode(); node;
			node=(stringdictionarylistnode< sqlrshbindvalue *> *)
							node->getNext()) {

			const char	*name=node->getData()->getKey();
			sqlrshbindvalue	*bv=node->getData()->getData();
			if (bv->type==STRING_BIND) {
				delete[] bv->stringval;
				bv->stringval=charstring::duplicate(
					sqlrcur->getOutputBindString(name));
			} else if (bv->type==INTEGER_BIND) {
				bv->integerval=
					sqlrcur->getOutputBindInteger(name);
			} else if (bv->type==DOUBLE_BIND) {
				bv->doubleval.value=
					sqlrcur->getOutputBindDouble(name);
			} else if (bv->type==DATE_BIND) {
				sqlrcur->getOutputBindDate(name,
						&(bv->dateval.year),
						&(bv->dateval.month),
						&(bv->dateval.day),
						&(bv->dateval.hour),
						&(bv->dateval.minute),
						&(bv->dateval.second),
						&(bv->dateval.microsecond),
						&(bv->dateval.tz));
			}
		}
	}
}

char *sqlrsh::getWild(const char *command) {
	const char	*wildptr=charstring::findFirst(command,"'");
	if (!wildptr) {
		return NULL;
	}
	wildptr++;
	const char	*endptr=charstring::findLast(wildptr,"'");
	if (!endptr) {
		return NULL;
	}

	// unescape single quotes
	stringbuffer	output;
	for (const char *ch=wildptr; ch<endptr; ch++) {
		if (*ch=='\'' && *(ch+1)=='\'') {
			ch++;
		}
		output.append(*ch);
	}

	return output.detachString();
}

char *sqlrsh::getTable(querytype_t querytype, const char *command) {
	const char	*tableptr=NULL;
	if (querytype==SHOW_COLUMNS_QUERY) {
		tableptr=charstring::findFirst(command," in ");
		if (!tableptr) {
			return NULL;
		}
		tableptr=tableptr+4;
		const char	*endptr=charstring::findFirst(tableptr," ");
		if (!endptr) {
			return charstring::duplicate(tableptr);
		}
		return charstring::duplicate(tableptr,endptr-tableptr);
	} else if (querytype==DESCRIBE_QUERY) {
		tableptr=charstring::findFirst(command," ");
		if (!tableptr) {
			return NULL;
		}
		return charstring::duplicate(tableptr+1);
	}
	return NULL;
}

void sqlrsh::initStats(sqlrshenv *env) {

	if (!env->stats) {
		return;
	}

	// call clock here or something
	clock();
}

void sqlrsh::displayError(sqlrshenv *env,
				const char *message,
				const char *error,
				int64_t errornumber) {
	if (charstring::length(message)) {
		printf("%s\n",message);
	}
	printf("%lld:\n",errornumber);
	if (charstring::length(error)) {
		printf("%s\n\n",error);
	}
}

void sqlrsh::displayHeader(sqlrcursor *sqlrcur, sqlrshenv *env) {

	if (!env->headers) {
		return;
	}

	// display column names
	uint32_t	charcount=0;
	uint32_t	colcount=sqlrcur->colCount();
	const char	*name;
	uint32_t	namelen;
	uint32_t	longest;

	if (!colcount) {
		return;
	}

	// iterate through columns
	for (uint32_t i=0; i<sqlrcur->colCount(); i++) {

		// write the column name
		name=sqlrcur->getColumnName(i);
		printf("%s",name);

		// which is longer, field name or longest field
		namelen=charstring::length(name);
		longest=sqlrcur->getLongest(i);
		if (namelen>longest) {
			longest=namelen;
		}
		charcount=charcount+longest;

		// pad after the name with spaces
		for (uint32_t j=namelen; j<longest; j++) {
			printf(" ");
		}

		// put an extra space between names
		if (i<colcount-1) {
			printf(" ");
			charcount=charcount+1;
		}
	}
	printf("\n");

	// display delimiter
	for (uint32_t i=0; i<charcount; i++) {
		printf("=");
	}
	printf("\n");
}

void sqlrsh::displayResultSet(sqlrcursor *sqlrcur, sqlrshenv *env) {

	// display column names
	uint32_t	colcount=sqlrcur->colCount();
	uint32_t	namelen;
	uint32_t	longest;

	if (!colcount) {
		return;
	}

	uint32_t	i=0;
	const char	*field="";
	while (field && colcount) {
		for (uint32_t j=0; j<colcount; j++) {

			if (!(field=sqlrcur->getField(i,j))) {
				break;
			}

			// write the column value
			printf("%s",field);

			// which is longer, field name or longest field
			longest=sqlrcur->getLongest(j);
			if (env->headers) {
				namelen=charstring::length(
					sqlrcur->getColumnName(j));
				if (namelen>longest) {
					longest=namelen;
				}
			}

			// pad after the name with spaces
			for (uint32_t k=sqlrcur->getFieldLength(i,j); 
							k<longest; k++) {
				printf(" ");
			}

			// put an extra space between names
			if (j<colcount-1) {
				printf(" ");
			}
		}
		if (field) {
			printf("\n");
		}
		i++;
	}
}

void sqlrsh::displayStats(sqlrcursor *sqlrcur, sqlrshenv *env) {

	if (!env->stats) {
		return;
	}

	// call clock again, display results
	printf("	Rows Returned   : ");
	printf("%lld\n",sqlrcur->rowCount());
	printf("	Fields Returned : ");
	printf("%lld\n",sqlrcur->rowCount()*sqlrcur->colCount());
	printf("	System time     : ");
	printf("%ld\n",clock());
	printf("\n");
}

void sqlrsh::ping(sqlrconnection *sqlrcon, sqlrshenv *env) {
	printf((sqlrcon->ping())?"	The database is up.\n":
				"	The database is down.\n");
}

bool sqlrsh::lastinsertid(sqlrconnection *sqlrcon, sqlrshenv *env) {
	bool		retval=false;
	uint64_t	id=sqlrcon->getLastInsertId();
	if (id!=0 || !sqlrcon->errorMessage()) {
		printf("%lld\n",id);
		retval=true;
	}
	return retval;
}

void sqlrsh::identify(sqlrconnection *sqlrcon, sqlrshenv *env) {
	printf("%s\n",sqlrcon->identify());
}

void sqlrsh::dbversion(sqlrconnection *sqlrcon, sqlrshenv *env) {
	printf("%s\n",sqlrcon->dbVersion());
}

void sqlrsh::clientversion(sqlrconnection *sqlrcon, sqlrshenv *env) {
	printf("%s\n",sqlrcon->clientVersion());
}

void sqlrsh::serverversion(sqlrconnection *sqlrcon, sqlrshenv *env) {
	printf("%s\n",sqlrcon->serverVersion());
}

void sqlrsh::inputbind(sqlrcursor *sqlrcur,
				sqlrshenv *env, const char *command) {

	// sanity check
	const char	*ptr=command+10;
	const char	*space=charstring::findFirst(ptr,' ');
	if (!space) {
		printf("usage: inputbind [variable] = [value]\n");
		return;
	}

	// get the variable name
	char	*variable=charstring::duplicate(ptr,space-ptr);

	// move on
	ptr=space;
	if (*(ptr+1)=='=' && *(ptr+2)==' ') {
		ptr=ptr+3;
	} else {
		printf("usage: inputbind [variable] = [value]\n");
		return;
	}
		
	// get the value
	char	*value=charstring::duplicate(ptr);
	charstring::bothTrim(value);
	size_t	valuelen=charstring::length(value);

	// if the bind variable is already defined, clear it...
	sqlrshbindvalue	*bv=NULL;
	if (env->inputbinds.getData(variable,&bv)) {
		delete[] bv;
	}

	// define the variable
	bv=new sqlrshbindvalue;

	// first handle nulls, then...
	// anything enclosed in quotes is a string
	// if it's unquoted, check to see if it's an integer, float or date
	// if it's not, then it's a string
	if (!value) {
		bv->type=NULL_BIND;
	} else if ((value[0]=='\'' && value[valuelen-1]=='\'') ||
			(value[0]=='"' && value[valuelen-1]=='"')) {

		bv->type=STRING_BIND;

		// trim off quotes
		char	*newvalue=charstring::duplicate(value+1);
		newvalue[valuelen-2]='\0';
		delete[] value;

		// unescape the string
		bv->stringval=charstring::unescape(newvalue);
		delete[] newvalue;

	} else if (charstring::contains(value,"/") && 
			charstring::contains(value,":")) {

		datetime	dt;
		dt.initialize(value);
		bv->type=DATE_BIND;
		bv->dateval.year=dt.getYear();
		bv->dateval.month=dt.getMonth();
		bv->dateval.day=dt.getDayOfMonth();
		bv->dateval.hour=dt.getHour();
		bv->dateval.minute=dt.getMinutes();
		bv->dateval.second=dt.getSeconds();
		bv->dateval.microsecond=charstring::toInteger(
					charstring::findLast(value,":")+1);
		bv->dateval.tz=dt.getTimeZoneString();
		delete[] value;

	} else if (charstring::isInteger(value)) {
		bv->type=INTEGER_BIND;
		bv->integerval=charstring::toInteger(value);
		delete[] value;
	} else if (charstring::isNumber(value)) {
		bv->type=DOUBLE_BIND;
		bv->doubleval.value=charstring::toFloat(value);
		bv->doubleval.precision=valuelen-((value[0]=='-')?2:1);
		bv->doubleval.scale=
			charstring::findFirst(value,'.')-value+
			((value[0]=='-')?0:1);
		delete[] value;
	} else {
		bv->type=STRING_BIND;
		bv->stringval=value;
	}

	// put the bind variable in the list
	env->inputbinds.setData(variable,bv);
}

void sqlrsh::outputbind(sqlrcursor *sqlrcur,
				sqlrshenv *env, const char *command) {

	// split the command on ' '
	char		**parts;
	uint64_t	partcount;
	charstring::split(command," ",true,&parts,&partcount);

	// sanity check...
	bool	sane=true;
	if (partcount>2 && !charstring::compare(parts[0],"outputbind")) {

		// if the bind variable is already defined, clear it...
		sqlrshbindvalue	*bv=NULL;
		if (env->outputbinds.getData(parts[1],&bv)) {
			delete[] bv;
		}

		// define the variable
		bv=new sqlrshbindvalue;

		if (!charstring::compareIgnoringCase(
						parts[2],"string") &&
						partcount==4) {
			bv->type=STRING_BIND;
			bv->stringval=NULL;
			bv->outputstringbindlength=
				charstring::toInteger(parts[3]);
		} else if (!charstring::compareIgnoringCase(
						parts[2],"integer") &&
						partcount==3) {
			bv->type=INTEGER_BIND;
			bv->integerval=0;
		} else if (!charstring::compareIgnoringCase(
						parts[2],"double") &&
						partcount==5) {
			bv->type=DOUBLE_BIND;
			bv->doubleval.value=0.0;
			bv->doubleval.precision=
				charstring::toInteger(parts[3]);
			bv->doubleval.scale=
				charstring::toInteger(parts[4]);
		} else if (!charstring::compareIgnoringCase(
						parts[2],"date") &&
						partcount==3) {
			bv->type=DATE_BIND;
			bv->dateval.year=0;
			bv->dateval.month=0;
			bv->dateval.day=0;
			bv->dateval.hour=0;
			bv->dateval.minute=0;
			bv->dateval.second=0;
			bv->dateval.microsecond=0;
			bv->dateval.tz="";
		} else {
			sane=false;
		}

		// put the bind variable in the list
		if (sane) {
			env->outputbinds.setData(parts[1],bv);
		}

	} else {
		sane=false;
	}

	// clean up
	if (sane) {
		delete[] parts[0];
	} else {
		printf("usage: outputbind [variable] [type] [length] [scale]\n");
		for (uint64_t i=0; i<partcount; i++) {
			delete[] parts[i];
		}
	}
	delete[] parts;
}

void sqlrsh::printbinds(const char *type,
			stringdictionary< sqlrshbindvalue * > *binds) {

	printf("%s bind variables:\n",type);

	for (stringdictionarylistnode< sqlrshbindvalue * > *node=
		(stringdictionarylistnode< sqlrshbindvalue *> *)
		binds->getList()->getFirstNode();
		node;
		node=(stringdictionarylistnode< sqlrshbindvalue *> *)
						node->getNext()) {

		printf("    %s ",node->getData()->getKey());
		sqlrshbindvalue	*bv=node->getData()->getData();
		if (bv->type==STRING_BIND) {
			printf("(STRING) = %s\n",bv->stringval);
		} else if (bv->type==INTEGER_BIND) {
			printf("(INTEGER) = %lld\n",bv->integerval);
		} else if (bv->type==DOUBLE_BIND) {
			printf("(DOUBLE %d,%d) = %*.*f\n",
						bv->doubleval.precision,
						bv->doubleval.scale,
						bv->doubleval.precision,
						bv->doubleval.scale,
						bv->doubleval.value);
		} else if (bv->type==DATE_BIND) {
			printf("(DATE) = %02d/%02d/%04d "
						"%02d:%02d:%02d:%03d %s\n",
						bv->dateval.month,
						bv->dateval.day,
						bv->dateval.year,
						bv->dateval.hour,
						bv->dateval.minute,
						bv->dateval.second,
						bv->dateval.microsecond,
						bv->dateval.tz);
		} else if (bv->type==NULL_BIND) {
			printf("NULL\n");
		}
	}
}

void sqlrsh::setclientinfo(sqlrconnection *sqlrcon, const char *command) {
	sqlrcon->setClientInfo(command+14);
}

void sqlrsh::getclientinfo(sqlrconnection *sqlrcon) {
	const char	*ci=sqlrcon->getClientInfo();
	printf("%s\n",(ci)?ci:"");
}

void sqlrsh::displayHelp(sqlrshenv *env) {

	printf("\n");
	printf("	To run a query, simply type it at the prompt,\n"
		"	followed by a semicolon.  Queries may be \n"
		"	split over multiple lines.\n\n");
	printf("	ping			- ");
	printf("pings the database\n");
	printf("	identify		- ");
	printf("returns the type of database\n");
	printf("	dbversion		- ");
	printf("returns the version of the database\n");
	printf("	clientversion		- ");
	printf("returns the version of the SQL Relay\n");
	printf("\t\t\t\t  client library\n");
	printf("	serverversion		- ");
	printf("returns the version of the SQL Relay server\n");
	printf("	use [database]		- ");
	printf("change the current database/schema\n");
	printf("	currentdb		- ");
	printf("shows the current database/schema\n");
	printf("	run script		- ");
	printf("runs commands contained in file \"script\"\n");
	printf("	headers on/off		- ");
	printf("toggles column descriptions before result set\n");
	printf("	stats on/off		- ");
	printf("toggles statistics after result set\n");
	printf("	debug on/off		- ");
	printf("toggles debug messages\n");
	printf("	autocommit on/off	- ");
	printf("toggles autocommit\n");
	printf("	final on/off		- ");
	printf("toggles use of one session per query\n");
	printf("	delimiter [character]	- ");
	printf("sets delimiter character to [character]\n\n");
	printf("	inputbind ...                 - ");
	printf("defines an input bind variable\n");
	printf("		inputbind [variable] = [stringvalue]\n");
	printf("		inputbind [variable] = [integervalue]\n");
	printf("		inputbind [variable] = [doublevalue]\n");
	printf("		inputbind [variable] = [MM/DD/YYYY HH:MM:SS:uS TZN]\n");
	printf("	outputbind ...                 - ");
	printf("defines an output bind variable\n");
	printf("		outputbind [variable] string [length]\n");
	printf("		outputbind [variable] integer\n");
	printf("		outputbind [variable] double [precision] [scale}\n");
	printf("		outputbind [variable] date\n");
	printf("	printbinds                     - ");
	printf("prints all bind variables\n");
	printf("	clearinputbind [variable]      - ");
	printf("clears an input bind variable\n");
	printf("	clearoutputbind [variable]     - ");
	printf("clears an output bind variable\n");
	printf("	clearbinds                     - ");
	printf("clears all bind variables\n");
	printf("	reexecute                      - ");
	printf("reexecutes the previous query\n\n");
	printf("	lastinsertid                   - ");
	printf("returns the value of the most recently\n");
	printf("\t\t\t\t\t updated auto-increment or identity\n");
	printf("\t\t\t\t\t column, if the database supports it\n\n");
	printf("	show databases [like pattern]		-\n");
	printf("		returns a list of known databases/schemas\n");
	printf("	show tables [like pattern]		-\n");
	printf("		returns a list of known tables\n");
	printf("	show columns in table [like pattern]	-\n");
	printf("		returns a list of column metadata for the table \"table\"\n");
	printf("	describe table				-\n");
	printf("		returns a list of column metadata for the table \"table\"\n");
	printf("	fields table				-\n");
	printf("		returns a list of column names for the table \"table\"\n\n");
	printf("	setclientinfo info	- sets the client info\n");
	printf("	getclientinfo		- displays the client info\n\n");
	printf("	setresultsetbuffersize size	- fetch size rows at a time\n");
	printf("	getresultsetbuffersize 		- shows rows fetched at a time\n\n");
	printf("	exit/quit		- ");
	printf("exits\n\n");
	printf("	All commands must be followed by the delimiter: %c\n",
								env->delimiter);
}

void sqlrsh::startupMessage(sqlrshenv *env, const char *host,
					uint16_t port, const char *user) {

	printf("SQLRShell - ");
	printf("Version 0.22\n");
	printf("	Connected to: ");
	printf("%s:%d as %s\n\n",host,port,user);
	printf("	type help; for help.\n\n");
}

void sqlrsh::interactWithUser(sqlrconnection *sqlrcon, sqlrcursor *sqlrcur, 
							sqlrshenv *env) {

	// init some variables
	stringbuffer	command;
	stringbuffer	prmpt;
	int		exitprogram=0;
	uint32_t	promptcount;

	while (!exitprogram) {

		// prompt the user
		promptcount=0;
		
		// get the command
		bool	done=false;
		while (!done) {
			#ifdef HAVE_READLINE
				prmpt.append(promptcount);
				prmpt.append("> ");
				char	*cmd=readline(const_cast<char *>(
							prmpt.getString()));
				prmpt.clear();
				if (cmd && cmd[0]) {
					charstring::rightTrim(cmd);
					add_history(cmd);
				} else {
					printf("\n");
				}
			#else
				prompt(promptcount);
				char	cmd[1024];
				ssize_t	bytes=standardin.read(cmd,1024);
				cmd[bytes-1]='\0';
				#ifdef ADD_NEWLINE_AFTER_READ_FROM_STDIN
					printf("\n");
				#endif
			#endif
			size_t	len=charstring::length(cmd);
			done=false;
			for (size_t i=0; i<len; i++) {
				if (i==len-1) {
				       if (cmd[i]==env->delimiter) {
						done=true;
					} else {
						command.append(cmd[i]);
					}
				} else if (cmd[i]>=32 || 
						cmd[i]=='	') {
					command.append(cmd[i]);
				}
			}
			if (!done) {
				promptcount++;
				command.append(" ");
			}
			#ifdef HAVE_READLINE
				delete[] cmd;
			#endif
		}

		// trim the command
		char	*cmd=command.detachString();
		charstring::bothTrim(cmd);

		// run the command
		if (!runCommand(sqlrcon,sqlrcur,env,cmd)) {	
			exitprogram=1;
		}

		// clean up
		delete[] cmd;
	}
}

void sqlrsh::prompt(unsigned long promptcount) {

	printf("%ld> ",promptcount);
	fflush(stdout);
}

void sqlrsh::error(const char *errstring) {

	// print the error
	printf("%s\n\n",errstring);
}

void sqlrsh::execute(int argc, const char **argv) {


	commandline	cmdline(argc,argv);
	sqlrconfigfile	cfgfile;

	const char	*config=cmdline.getValue("-config");
	if (!(config && config[0])) {
		config=DEFAULT_CONFIG_FILE;
	}
	const char	*id=cmdline.getValue("-id");
	const char	*host=cmdline.getValue("-host");
	uint16_t	port=charstring::toInteger(
					cmdline.getValue("-port"));
	const char	*socket=cmdline.getValue("-socket");
	const char	*user=cmdline.getValue("-user");
	const char	*password=cmdline.getValue("-password");
	const char	*script=cmdline.getValue("-script");
	const char	*command=cmdline.getValue("-command");

	if (!(charstring::length(id) ||
		((charstring::length(host) ||
			charstring::length(socket)) &&
				charstring::length(user) &&
				charstring::length(password)))) {

		printf("usage: sqlrsh -host host -port port -socket socket -user user -password password [-script script | -command command]\n"
			"  or   sqlrsh [-config configfile] -id id [-script script | -command command]\n");
		process::exit(1);
	}

	if (charstring::length(id) && cfgfile.parse(config,id)) {

		// get the host/port/socket/username/password
		host="localhost";
		port=cfgfile.getPort();
		socket=cfgfile.getUnixPort();
		// FIXME: this can return 0
		usercontainer	*currentnode=NULL;
		cfgfile.getUserList()->getDataByIndex(0,&currentnode);
		user=currentnode->getUser();
		password=currentnode->getPassword();
	}

	// connect to sql relay
	sqlrconnection	sqlrcon(host,port,socket,user,password,0,1);
	sqlrcursor	sqlrcur(&sqlrcon);

	// set up an sqlrshenv
	sqlrshenv	env;

	// process RC files
	systemRcFile(&sqlrcon,&sqlrcur,&env);
	userRcFile(&sqlrcon,&sqlrcur,&env);


	#ifdef HAVE_READLINE

		// handle the history file
		size_t		filenamelen;
		char		*filename;
		const char	*home=environment::getValue("HOME");
		if (home && home[0]) {
			filenamelen=charstring::length(home)+16+1;
			filename=new char[filenamelen];
			charstring::copy(filename,home);
			charstring::append(filename,"/.sqlrsh_history");

			// create the history file if it doesn't exist now
			FILE	*historyfile=fopen(filename,"a");
			if (historyfile) {
				fclose(historyfile);
				read_history(filename);
			}
		}
	#endif

	if (charstring::length(script)) {
		// if a script was specified, run it
		runScript(&sqlrcon,&sqlrcur,&env,script,true,false);
	} else if (charstring::length(command)) {
		// if a command was specified, run it
		runCommand(&sqlrcon,&sqlrcur,&env,command);
	} else {
		// otherwise go into interactive mode
		startupMessage(&env,host,port,user);
		interactWithUser(&sqlrcon,&sqlrcur,&env);
	}

	// clean up
	#ifdef HAVE_READLINE
		if (home && home[0]) {
			write_history(filename);
			history_truncate_file(filename,100);
			delete[] filename;
		}
	#endif
}

int main(int argc, const char **argv) {

	#include <version.h>

	// ignore SIGPIPE
	signalset	set;
	set.removeAllSignals();
	set.addSignal(SIGPIPE);
	signalmanager::ignoreSignals(&set);

	sqlrsh	s;
	s.execute(argc,argv);
	process::exit(0);
}
