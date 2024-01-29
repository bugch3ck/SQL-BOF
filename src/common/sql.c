#include <windows.h>
#include <sql.h>
#include "bofdefs.h"

//
// prints a SQL error message
//
void ShowError(unsigned int handletype, const SQLHANDLE* handle)
{
    SQLCHAR sqlstate[1024];
    SQLCHAR message[1024];
    if (SQL_SUCCESS == ODBC32$SQLGetDiagRec(handletype, (SQLHANDLE)handle, 1, sqlstate, NULL, message, 1024, NULL))
    {
        internal_printf("[-] Message: %s \n[-] SQL State: %s\n", message, sqlstate);
    }
}

//
// Actual query execution
//
BOOL ExecuteQuery(SQLHSTMT stmt, SQLCHAR* query)
{
    SQLRETURN ret;

    ret = ODBC32$SQLExecDirect(stmt, query, SQL_NTS);
    if (!SQL_SUCCEEDED(ret))
    {
        internal_printf("[!] Error executing query\n");
        ShowError(SQL_HANDLE_STMT, stmt);
        return FALSE;
    }

    return TRUE;
}

//
// Preps a linked query and calls ExecuteQuery
//
BOOL ExecuteLQuery(SQLHSTMT stmt, SQLCHAR* query, char* link)
{
    char* linkPrefix = "SELECT * FROM OPENQUERY(\"";
    char* linksuffix = "\", '";
    char* querySuffix = "')";

    // append linkPrefix, link, linksuffix, query, querySuffix
    char* lQuery = (char*)MSVCRT$malloc((MSVCRT$strlen(linkPrefix) + MSVCRT$strlen(link) + MSVCRT$strlen(linksuffix) + MSVCRT$strlen((char*)query) + MSVCRT$strlen(querySuffix) + 1) * sizeof(char));
    MSVCRT$strcpy(lQuery, linkPrefix);
    MSVCRT$strcat(lQuery, link);
    MSVCRT$strcat(lQuery, linksuffix);
    MSVCRT$strcat(lQuery, (char*)query);
    MSVCRT$strcat(lQuery, querySuffix);

    return ExecuteQuery(stmt, (SQLCHAR*)lQuery);
}

//
// Preps and impersonated query and calls ExecuteQuery
//
BOOL ExecuteIQuery(SQLHSTMT stmt, SQLCHAR* query, char* impersonate)
{
    char* prefix = "EXECUTE AS LOGIN = '";
    char* suffix = "'; ";

    // append prefix, impersonate, suffix and query
    char* iQuery = (char*)MSVCRT$malloc((MSVCRT$strlen(prefix) + MSVCRT$strlen(impersonate) + MSVCRT$strlen(suffix) + MSVCRT$strlen((char*)query) + 1) * sizeof(char));
    MSVCRT$strcpy(iQuery, prefix);
    MSVCRT$strcat(iQuery, impersonate);
    MSVCRT$strcat(iQuery, suffix);
    MSVCRT$strcat(iQuery, (char*)query);

    return ExecuteQuery(stmt, (SQLCHAR*)iQuery);
}

//
// Main query handler to detemine if running standard, linked, or impersonated query
// (for BOFs that support more than just standard queries)
//
BOOL HandleQuery(SQLHSTMT stmt, SQLCHAR* query, char* link, char* impersonate)
{
    if (link != NULL)
    {
        return ExecuteLQuery(stmt, query, link);
    }
    else if (impersonate != NULL)
    {
        return ExecuteIQuery(stmt, query, impersonate);
    }
    else
    {
        return ExecuteQuery(stmt, query);
    }
}

//
// Check if link and impersonate were provided (can't use both)
//
BOOL UsingLinkAndImpersonate(char* link, char* impersonate)
{
    if (link != NULL && impersonate != NULL)
    {
        internal_printf("[!] Cannot use both link and impersonate.\n");
        printoutput(TRUE);
        return TRUE;
    }

    // linked server usage will be printed in the individual BOFs
    if (impersonate != NULL)
    {
        internal_printf("[*] Using impersonation: %s\n", impersonate);
    }

    return FALSE;
}

//
// returns an array of results (1st column only)
//
char** GetMultipleResults(SQLHSTMT stmt, BOOL hasHeader)
{
    char** results = (char**)MSVCRT$malloc(1024 * sizeof(char*));
    SQLCHAR buf[1024];
    SQLLEN indicator;
    int i = 0;

    while (ODBC32$SQLFetch(stmt) == SQL_SUCCESS)
    {
        ODBC32$SQLGetData(stmt, 1, SQL_C_CHAR, buf, sizeof(buf), &indicator);
        if (hasHeader && i == 0)
        {
            i++;
            continue;
        }
        results[i] = (char*)MSVCRT$malloc((MSVCRT$strlen((char*)buf) + 1) * sizeof(char));
        MSVCRT$strcpy(results[i], (char*)buf);
        i++;
    }
    results[i] = NULL; // Null terminate the array
    return results;
}

//
// returns a single result (1st column/1st row only)
//
char* GetSingleResult(SQLHSTMT stmt, BOOL hasHeader)
{
    SQLCHAR* buf = (SQLCHAR*)MSVCRT$malloc(1024 * sizeof(SQLCHAR));
    SQLLEN indicator;
    ODBC32$SQLFetch(stmt);
    ODBC32$SQLGetData(stmt, 1, SQL_C_CHAR, buf, 1024, &indicator);
    if (hasHeader)
    {
        ODBC32$SQLFetch(stmt);
        ODBC32$SQLGetData(stmt, 1, SQL_C_CHAR, buf, 1024, &indicator);
    }
    return (char*)buf;
}

//
// prints the results of a query
//
void PrintQueryResults(SQLHSTMT stmt, BOOL hasHeader)
{
    SQLSMALLINT columns;
    SQLRETURN ret;
    
    // Get the number of columns in the result set
    ret = ODBC32$SQLNumResultCols(stmt, &columns);
    if (!SQL_SUCCEEDED(ret))
    {
        internal_printf("Error retrieving column count.\n");
        return;
    }

    SQLCHAR buffer[1024];
    SQLSMALLINT columnNameLength, dataType, decimalDigits, nullable;
    SQLULEN columnSize;
    int totalLength = 0;

    // Print column headers
    if (hasHeader)
    {
        for (SQLSMALLINT i = 1; i <= columns; i++)
        {
            ODBC32$SQLDescribeCol(stmt, i, buffer, sizeof(buffer), &columnNameLength, &dataType, &columnSize, &decimalDigits, &nullable);
            internal_printf("%s | ", buffer);
            totalLength += columnNameLength + 3;
        }
    }

    if (hasHeader)
    {
        internal_printf("\n");
        for (int i = 0; i < totalLength; i++)
        {
            internal_printf("-");
        }
        internal_printf("\n");
    }
    
    // Iterate over each row
    while (SQL_SUCCEEDED(ret = ODBC32$SQLFetch(stmt)))
    {
        // Iterate over each column
        for (SQLSMALLINT i = 1; i <= columns; i++)
        {
            SQLLEN indicator;
            // Retrieve column data
            ret = ODBC32$SQLGetData(stmt, i, SQL_C_CHAR, buffer, sizeof(buffer), &indicator);
            if (SQL_SUCCEEDED(ret))
            {
                // Print column data
                if (indicator == SQL_NULL_DATA) MSVCRT$strcpy((char*)buffer, "NULL");
                internal_printf("%s | ", buffer);
            }
        }
        internal_printf("\n");
    }
}

//
// connects to a SQL server
//
SQLHDBC ConnectToSqlServer(SQLHENV* env, char* server, char* dbName)
{
    SQLRETURN ret;
    SQLCHAR connstr[1024];
    SQLHDBC dbc = NULL;

    //
    // Allocate an environment handle and set ODBC version
    //
    ret = ODBC32$SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, env);
    ret = ODBC32$SQLSetEnvAttr(*env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);

    //
    // Allocate a connection handle
    //
    ret = ODBC32$SQLAllocHandle(SQL_HANDLE_DBC, *env, &dbc);
    
    MSVCRT$sprintf((char*)connstr, "DRIVER={SQL Server};SERVER=%s;DATABASE=%s;Trusted_Connection=Yes;", server, dbName);

    //
    // connect to the sql server
    //
    internal_printf("[*] Connecting to %s:1433\n", server);
    ret = ODBC32$SQLDriverConnect(dbc, NULL, connstr, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_COMPLETE);
    if (SQL_SUCCEEDED(ret))
    {
        internal_printf("[+] Successfully connected to database\n");
    }
    else
    {
        internal_printf("[-] Error connecting to database.\n");
        ShowError(SQL_HANDLE_DBC, dbc);
        dbc = NULL; // Set dbc to NULL to indicate failure
    }

    return dbc;
}

//
// closes the connection to a SQL server
//
void DisconnectSqlServer(SQLHENV env, SQLHDBC dbc, SQLHSTMT stmt)
{
	internal_printf("\n[*] Disconnecting from server\n");

	if (stmt != NULL)
    {
		ODBC32$SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	}

	if (dbc != NULL)
    {
		ODBC32$SQLDisconnect(dbc);
		ODBC32$SQLFreeHandle(SQL_HANDLE_DBC, dbc);
	}

	if (env != NULL)
    {
		ODBC32$SQLFreeHandle(SQL_HANDLE_ENV, env);
	}

}