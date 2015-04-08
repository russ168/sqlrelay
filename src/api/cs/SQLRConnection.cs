using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace SQLRClient
{

public class SQLRConnection : IDisposable
{
    /** Initiates a connection to "server" on "port" or to the unix "socket" on
     *  the local machine and authenticates with "user" and "password".  Failed
     *  connections will be retried for "tries" times, waiting "retrytime"
     *  seconds between each try.  If "tries" is 0 then retries will continue
     *  forever.  If "retrytime" is 0 then retries will be attempted on a
     *  default interval.  If the "socket" parameter is nether NULL nor "" then
     *  an attempt will be made to connect through it before attempting to
     *  connect to "server" on "port".  If it is NULL or "" then no attempt
     *  will be made to connect through the socket.*/
    public SQLRConnection(String server, UInt16 port, String socket, String user, String password, Int32 retrytime, Int32 tries)
    {
        sqlrconref = sqlrcon_alloc_copyrefs(server, port, socket, user, password, retrytime, tries, true);
    }
    
    /** Dispose framework */
    private Boolean disposed = false;
    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }
    protected virtual void Dispose(Boolean disposing)
    {
        if (!disposed)
        {
            sqlrcon_free(sqlrconref);
            disposed = true;
        }
    }

    /** Disconnects and ends the session if it hasn't been terminated
     *  already. */
    ~SQLRConnection()
    {
        Dispose(false);
    }
    
    
    
    /** Sets the server connect timeout in seconds and milliseconds.  Setting
     *  either parameter to -1 disables the timeout.  You can also set this
     *  timeout using the SQLR_CLIENT_CONNECT_TIMEOUT environment variable. */
    public void setConnectTimeout(Int32 timeoutsec, Int32 timeoutusec)
    {
        sqlrcon_setConnectTimeout(sqlrconref, timeoutsec, timeoutusec);
    }
    
    /** Sets the authentication timeout in seconds and milliseconds.  Setting
     *  either parameter to -1 disables the timeout.   You can also set this
     *  timeout using the SQLR_CLIENT_AUTHENTICATION_TIMEOUT environment
     *  variable. */
    public void setAuthenticationTimeout(Int32 timeoutsec, Int32 timeoutusec)
    {
        sqlrcon_setAuthenticationTimeout(sqlrconref, timeoutsec, timeoutusec);
    }
    
    /** Sets the response timeout (for queries, commits, rollbacks, pings,
      * etc.) in seconds and milliseconds.  Setting either parameter to -1
      * disables the timeout.  You can also set this timeout using the
      * SQLR_CLIENT_RESPONSE_TIMEOUT environment variable. */
    public void setResponseTimeout(Int32 timeoutsec, Int32 timeoutusec)
    {
        sqlrcon_setResponseTimeout(sqlrconref, timeoutsec, timeoutusec);
    }
    
    /** Ends the session. */
    public void endSession()
    {
        sqlrcon_endSession(sqlrconref);
    }
    
    /** Disconnects this connection from the current session but leaves the
     *  session open so that another connection can connect to it using
     *  sqlrcon_resumeSession(). */
    public Boolean suspendSession()
    {
        return sqlrcon_suspendSession(sqlrconref)!=0;
    }
    
    /** Returns the inet port that the connection is communicating over.  This
     *  parameter may be passed to another connection for use in the
     *  sqlrcon_resumeSession() command.  Note: The result this function returns
     *  is only valid after a call to suspendSession(). */
    public UInt16 getConnectionPort()
    {
        return sqlrcon_getConnectionPort(sqlrconref);
    }
    
    /** Returns the unix socket that the connection is communicating over.  This
     *  parameter may be passed to another connection for use in the
     *  sqlrcon_resumeSession() command.  Note: The result this function returns
     *  is only valid after a call to suspendSession(). */
    public String getConnectionSocket()
    {
    	return sqlrcon_getConnectionSocket(sqlrconref);
    }
    
    /** Resumes a session previously left open using sqlrcon_suspendSession().
     *  Returns true on success and false on failure. */
    public Boolean resumeSession(UInt16 port, String socket)
    {
    	return sqlrcon_resumeSession(sqlrconref, port, socket)!=0;
    }
    
    
    
    /** Returns true if the database is up and false if it's down. */
    public Boolean ping()
    {
        return sqlrcon_ping(sqlrconref)!=0;
    }
    
    /** Returns the type of database: oracle8, postgresql, mysql, etc. */
    public String identify()
    {
        return sqlrcon_identify(sqlrconref);
    }
    
    /** Returns the version of the database */
    public String dbVersion()
    {
    	return sqlrcon_dbVersion(sqlrconref);
    }
    
    /** Returns the host name of the database */
    public String dbHostName()
    {
    	return sqlrcon_dbHostName(sqlrconref);
    }
    
    /** Returns the ip address of the database */
    public String dbIpAddress()
    {
    	return sqlrcon_dbIpAddress(sqlrconref);
    }
    
    /** Returns the version of the sqlrelay server software. */
    public String serverVersion()
    {
        return sqlrcon_serverVersion(sqlrconref);
    }
    
    /** Returns the version of the sqlrelay client software. */
    public String clientVersion()
    {
        return sqlrcon_clientVersion(sqlrconref);
    }
    
    /** Returns a String representing the format
     *  of the bind variables used in the db. */
    public String bindFormat()
    {
        return sqlrcon_bindFormat(sqlrconref);
    }
    
    
    
    /** Sets the current database/schema to "database" */
    public Boolean selectDatabase(String database)
    {
        return sqlrcon_selectDatabase(sqlrconref, database)!=0;
    }
    
    /** Returns the database/schema that is currently in use. */
    public String getCurrentDatabase()
    {
        return sqlrcon_getCurrentDatabase(sqlrconref);
    }
    
    
    
    /** Returns the value of the autoincrement column for the last insert */
    public UInt64 getLastInsertId()
    {
        return sqlrcon_getLastInsertId(sqlrconref);
    }
    
    
    
    /** Instructs the database to perform a commit after every successful
     *  query. */
    public Boolean autoCommitOn()
    {
        return sqlrcon_autoCommitOn(sqlrconref)!=0;
    }
    
    /** Instructs the database to wait for the client to tell it when to
     *  commit. */
    public Boolean autoCommitOff()
    {
        return sqlrcon_autoCommitOff(sqlrconref)!=0;
    }

    /** Begins a transaction.  Returns true if the begin
     *  succeeded, false if it failed.  If the database
     *  automatically begins a new transaction when a
     *  commit or rollback is issued then this doesn't
     *  do anything unless SQL Relay is faking transaction
     *  blocks. */
    public Boolean begin()
    {
        return (sqlrcon_begin(sqlrconref) == 1);
    }
    
    
    /** Issues a commit.  Returns true if the commit succeeded and false if it failed. */
    public Boolean commit()
    {
        return (sqlrcon_commit(sqlrconref) == 1);
    }
    
    /** Issues a rollback.  Returns true if the rollback succeeded, false if it failed. */
    public Boolean rollback()
    {
        return (sqlrcon_rollback(sqlrconref) == 1);
    }
    
    
    
    /** If an operation failed and generated an error, the error message is
     *  available here.  If there is no error then this method returns NULL */
    public String errorMessage()
    {
        return sqlrcon_errorMessage(sqlrconref);
    }
    
    /** If an operation failed and generated an error, the error number is
     *  available here.  If there is no error then this method returns 0. */
    public Int64 errorNumber()
    {
        return sqlrcon_errorNumber(sqlrconref);
    }
    
    
    /** Causes verbose debugging information to be sent to standard output.
     *  Another way to do this is to start a query with "-- debug\n".
     *  Yet another way is to set the environment variable SQLR_CLIENT_DEBUG
     *  to "ON" */
    public void debugOn()
    {
        sqlrcon_debugOn(sqlrconref);
    }
    
    /** Turns debugging off. */
    public void debugOff()
    {
        sqlrcon_debugOff(sqlrconref);
    }
    
    /** Returns false if debugging is off and true if debugging is on. */
    public Boolean getDebug()
    {
        return sqlrcon_getDebug(sqlrconref)!=0;
    }

    /** Allows you to specify a file to write debug to.
     *  Setting "filename" to NULL or an empty string causes debug
     *  to be written to standard output (the default). */
    public void setDebugFile(String filename)
    {
        sqlrcon_setDebugFile(sqlrconref,filename);
    }

    /** Allows you to set a string that will be passed to the server and
     *  ultimately included in server-side logging along with queries that were
     *  run by this instance of the client. */
    public void setClientInfo(String clientinfo)
    {
        sqlrcon_setClientInfo(sqlrconref,clientinfo);
    }

    /** Returns the string that was set by setClientInfo(). */
    public String getClientInfo()
    {
        return sqlrcon_getClientInfo(sqlrconref);
    }

    /** Returns a pointer to the internal connection structure */
    public IntPtr getInternalConnectionStructure()
    {
        return sqlrconref;
    }

    private IntPtr sqlrconref;

    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr sqlrcon_alloc_copyrefs(String server, UInt16 port, String socket, String user, String password, Int32 retrytime, Int32 tries, Boolean copyreferences);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void sqlrcon_free(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void sqlrcon_setConnectTimeout(IntPtr sqlrconref, Int32 timeoutsec, Int32 timeoutusec);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void sqlrcon_setAuthenticationTimeout(IntPtr sqlrconref, Int32 timeoutsec, Int32 timeoutusec);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void sqlrcon_setResponseTimeout(IntPtr sqlrconref, Int32 timeoutsec, Int32 timeoutusec);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void sqlrcon_endSession(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern Int32 sqlrcon_suspendSession(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern UInt16 sqlrcon_getConnectionPort(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern String sqlrcon_getConnectionSocket(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern Int32 sqlrcon_resumeSession(IntPtr sqlrconref, UInt16 port, String socket);

    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern Int32 sqlrcon_ping(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern String sqlrcon_identify(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern String sqlrcon_dbVersion(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern String sqlrcon_dbHostName(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern String sqlrcon_dbIpAddress(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern String sqlrcon_serverVersion(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern String sqlrcon_clientVersion(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern String sqlrcon_bindFormat(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern Int32 sqlrcon_selectDatabase(IntPtr sqlrconref, String database);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern String sqlrcon_getCurrentDatabase(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern UInt64 sqlrcon_getLastInsertId(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern Int32 sqlrcon_autoCommitOn(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern Int32 sqlrcon_autoCommitOff(IntPtr sqlrconref);

    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern Int32 sqlrcon_begin(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern Int32 sqlrcon_commit(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern Int32 sqlrcon_rollback(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern String sqlrcon_errorMessage(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern Int64 sqlrcon_errorNumber(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void sqlrcon_debugOn(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void sqlrcon_debugOff(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern Int32 sqlrcon_getDebug(IntPtr sqlrconref);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void sqlrcon_setDebugFile(IntPtr sqlrconref, String filename);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void sqlrcon_setClientInfo(IntPtr sqlrconref, String clientinfo);
    
    [DllImport("libsqlrclientwrapper.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern String sqlrcon_getClientInfo(IntPtr sqlrconref);
}

}