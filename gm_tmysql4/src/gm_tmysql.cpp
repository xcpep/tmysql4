#include "gm_tmysql.h"

using namespace GarrysMod::Lua;

#define DATABASE_NAME "Database"
#define DATABASE_ID 200

int iRefDatabases;

void DisconnectDB(lua_State* state, Database* mysqldb);
void DispatchCompletedQueries(lua_State* state, Database* mysqldb);
void HandleQueryCallback(lua_State* state, Query* query);
void PopulateTableFromQuery(lua_State* state, Query* query);

bool in_shutdown = false;

int initialize(lua_State* state)
{
	const char* host = LUA->CheckString(1);
	const char* user = LUA->CheckString(2);
	const char* pass = LUA->CheckString(3);
	const char* db = LUA->CheckString(4);

	int port = 3306;

	if (LUA->IsType(5, Type::NUMBER))
		port = (int) LUA->GetNumber(5);

	LUA->ReferencePush(iRefDatabases);
	LUA->GetField(-1, db);

	if (LUA->IsType(-1, DATABASE_ID))
		return 1; // Return the already existing connection...

	Database* mysqldb = new Database(host, user, pass, db, port, LUA->IsType(6, Type::STRING) ? LUA->GetString(6) : NULL);
	
	std::string error;

	if ( !mysqldb->Initialize( error ) )
	{
		LUA->PushBool( false );
		LUA->PushString(error.c_str());
		delete mysqldb;
		return 2;
	}

	UserData* userdata = (UserData*)LUA->NewUserdata(sizeof(UserData));
	userdata->data = mysqldb;
	userdata->type = DATABASE_ID;

	int uData = LUA->ReferenceCreate();

	LUA->ReferencePush(iRefDatabases);
	LUA->ReferencePush(uData);
	LUA->SetField(-2, db);

	LUA->ReferencePush(uData);
	LUA->ReferenceFree(uData);
	LUA->CreateMetaTableType(DATABASE_NAME, DATABASE_ID);
	LUA->SetMetaTable(-2);
	return 1;
}

int gettable(lua_State* state)
{
	LUA->ReferencePush(iRefDatabases);
	return 1;
}

int getdatabase(lua_State* state)
{
	LUA->ReferencePush(iRefDatabases);
	LUA->GetField(-1, LUA->CheckString(1));
	return 1;
}

int escape(lua_State* state)
{
	const char* query = LUA->CheckString( 1 );

	size_t len = strlen( query );
	char* escaped = new char[len*2+1];

	mysql_escape_string( escaped, query, len );	

	LUA->PushString( escaped );

	delete escaped;
	return 1;
}

int dbescape(lua_State* state)
{
	LUA->CheckType(1, DATABASE_ID);

	UserData* userdata = (UserData*)LUA->GetUserdata(1);
	Database *mysqldb = (Database*)userdata->data;

	if ( !mysqldb )
		return 0;

	const char* query = LUA->CheckString( 2 );

	char* escaped = mysqldb->Escape( query );
	LUA->PushString( escaped );

	delete escaped;
	return 1;
}

int disconnect(lua_State* state)
{
	LUA->CheckType( 1, DATABASE_ID );

	UserData* userdata = (UserData*) LUA->GetUserdata(1);
	Database *mysqldb = (Database*) userdata->data;

	if (!mysqldb)
		return 0;

	LUA->ReferencePush(iRefDatabases);
		LUA->PushNil();
		LUA->SetField(-2, mysqldb->m_strDB);
	LUA->Pop();

	DisconnectDB( state, mysqldb );

	userdata->data = NULL;
	return 0;
}

int setcharset(lua_State* state)
{
	LUA->CheckType( 1, DATABASE_ID );

	UserData* userdata = (UserData*)LUA->GetUserdata(1);
	Database *mysqldb = (Database*)userdata->data;

	if ( !mysqldb )
		return 0;

	const char* set = LUA->CheckString(2);

	std::string error;
	mysqldb->SetCharacterSet( set, error );
	return 0;
}

int query(lua_State* state)
{
	LUA->CheckType(1, DATABASE_ID);

	UserData* userdata = (UserData*)LUA->GetUserdata(1);
	Database *mysqldb = (Database*)userdata->data;

	if ( !mysqldb )
		return 0;

	const char* query = LUA->CheckString(2);

	int callbackfunc = -1;
	if (LUA->GetType(3) == Type::FUNCTION)
	{
		LUA->Push(3);
		callbackfunc = LUA->ReferenceCreate();
	}

	int flags = (int) LUA->GetNumber(4);

	int callbackref = -1;
	int callbackobj = LUA->GetType(5);
	if (callbackobj != Type::NIL)
	{
		LUA->Push(5);
		callbackref = LUA->ReferenceCreate();
	}

	mysqldb->QueueQuery( query, callbackfunc, flags, callbackref );
	return 0;
}

int poll(lua_State* state)
{
	LUA->CheckType(1, DATABASE_ID);

	UserData* userdata = (UserData*)LUA->GetUserdata(1);
	Database *mysqldb = (Database*)userdata->data;

	if (!mysqldb)
		return 0;

	DispatchCompletedQueries(state, mysqldb);
	return 0;
}

int pollall(lua_State* state)
{
	LUA->ReferencePush(iRefDatabases);
	LUA->PushNil();

	while (LUA->Next(-2))
	{
		LUA->Push(-2);

		if (!LUA->IsType(-2, DATABASE_ID))
			continue;

		UserData* userdata = (UserData*)LUA->GetUserdata(-2);
		Database *mysqldb = (Database*)userdata->data;

		if (!mysqldb)
			continue;

		DispatchCompletedQueries(state, mysqldb);

		LUA->Pop(2);
	}
	LUA->Pop();
	return 0;
}

void DisconnectDB(lua_State* state,  Database* mysqldb )
{
	if (mysqldb)
	{
		mysqldb->Shutdown();
		DispatchCompletedQueries(state, mysqldb);

		while (mysqldb->RunShutdownWork())
		{
			DispatchCompletedQueries(state, mysqldb);
		}

		mysqldb->Release();
		delete mysqldb;
	}
}

void DispatchCompletedQueries(lua_State* state, Database* mysqldb)
{
	Query* completed = mysqldb->GetCompletedQueries();

	while (completed)
	{
		Query* query = completed;

		if (query->GetCallback() >= 0)
			HandleQueryCallback(state, query);

		if (query->GetResult() != NULL)
			mysql_free_result(query->GetResult());

		completed = query->next;
		delete query;
	}
}

void HandleQueryCallback(lua_State* state, Query* query)
{
	LUA->ReferencePush(query->GetCallback());
	LUA->ReferenceFree(query->GetCallback());

	if (!LUA->IsType(-1, Type::FUNCTION))
	{
		LUA->Pop();
		LUA->ReferenceFree(query->GetCallbackRef());
		return;
	}

	int args = 3;
	if (query->GetCallbackRef() >= 0)
	{
		args = 4;
		LUA->ReferencePush(query->GetCallbackRef());
		LUA->ReferenceFree(query->GetCallbackRef());
	}

	LUA->CreateTable();
	PopulateTableFromQuery(state, query);

	LUA->PushBool(query->GetStatus());

	if (query->GetStatus())
		LUA->PushNumber(query->GetLastID());
	else
		LUA->PushString(query->GetError().c_str());

	if (LUA->PCall(args, 0, 0) != 0 && !in_shutdown)
	{
		const char* err = LUA->GetString(-1);
		LUA->ThrowError(err);
	}
}

void PopulateTableFromQuery(lua_State* state, Query* query)
{
	MYSQL_RES* result = query->GetResult();

	// no result to push, continue, this isn't fatal
	if (result == NULL)
		return;

	MYSQL_ROW row = mysql_fetch_row(result);
	int field_count = mysql_num_fields(result);

	int rowid = 1;

	while (row != NULL)
	{
		// black magic warning: we use a temp and assign it so that we avoid consuming all the temp objects and causing horrible disasters
		LUA->CreateTable();
		int resultrow = LUA->ReferenceCreate();

		LUA->PushNumber(rowid);
		LUA->ReferencePush(resultrow);

		for (int i = 0; i < field_count; i++)
		{
			MYSQL_FIELD* field = mysql_fetch_field_direct(result, i);

			bool accos = query->GetFlags() & QUERY_FLAG_ASSOC;

			if (!accos)
				LUA->PushNumber(i + 1);

			if (row[i] == NULL)
				LUA->PushNil();
			else if (field->type == MYSQL_TYPE_DECIMAL ||
				field->type == MYSQL_TYPE_NEWDECIMAL ||
				field->type == MYSQL_TYPE_FLOAT ||
				field->type == MYSQL_TYPE_DOUBLE ||
				field->type == MYSQL_TYPE_TINY ||
				field->type == MYSQL_TYPE_SHORT ||
				field->type == MYSQL_TYPE_INT24 ||
				field->type == MYSQL_TYPE_BIT ||
				field->type == MYSQL_TYPE_LONG)
				LUA->PushNumber(atof(row[i]));
			else
				LUA->PushString(row[i]);

			if (accos)
				LUA->SetField(-2, field->name);
			else
				LUA->SetTable(-3);
		}

		LUA->SetTable(-3);

		LUA->ReferenceFree(resultrow);

		row = mysql_fetch_row(result);
		rowid++;
	}
}

GMOD_MODULE_OPEN()
{
	in_shutdown = false;

	mysql_library_init(0, NULL, NULL);

	LUA->CreateTable();
	iRefDatabases = LUA->ReferenceCreate();

	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
		LUA->PushBool(QUERY_SUCCESS);
		LUA->SetField(-2, "QUERY_SUCCESS");

		LUA->PushBool(QUERY_FAIL);
		LUA->SetField(-2, "QUERY_FAIL");

		LUA->PushNumber(QUERY_FLAG_ASSOC);
		LUA->SetField(-2, "QUERY_FLAG_ASSOC");

		LUA->PushNumber(QUERY_FLAG_LASTID);
		LUA->SetField(-2, "QUERY_FLAG_LASTID");

		LUA->PushNumber(mysql_get_client_version());
		LUA->SetField(-2, "MYSQL_VERSION");

		LUA->PushString(mysql_get_client_info());
		LUA->SetField(-2, "MYSQL_INFO");

		LUA->CreateTable();
			LUA->PushCFunction(initialize);
			LUA->SetField(-2, "initialize");
			LUA->PushCFunction(initialize);
			LUA->SetField(-2, "Connect");
			LUA->PushCFunction(escape);
			LUA->SetField(-2, "escape");
			LUA->PushCFunction(gettable);
			LUA->SetField(-2, "GetTable");
			LUA->PushCFunction(getdatabase);
			LUA->SetField(-2, "GetDatabase");
			LUA->PushCFunction(pollall);
			LUA->SetField(-2, "PollAll");
		LUA->SetField(-2, "tmysql");

		LUA->GetField(-1, "hook");
			LUA->GetField(-1, "Add");
				LUA->PushString("Tick");
				LUA->PushString("tmysql4");
				LUA->PushCFunction(pollall);
			LUA->Call(3,0);
		LUA->Pop();

	LUA->Pop();

	LUA->CreateMetaTableType(DATABASE_NAME, DATABASE_ID);
	{
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
		LUA->PushCFunction(disconnect);
		LUA->SetField(-2, "__gc");

		LUA->PushCFunction(query);
		LUA->SetField(-2, "Query");
		LUA->PushCFunction(dbescape);
		LUA->SetField(-2, "Escape");
		LUA->PushCFunction(disconnect);
		LUA->SetField(-2, "Disconnect");
		LUA->PushCFunction(setcharset);
		LUA->SetField(-2, "SetCharacterSet");
		LUA->PushCFunction(poll);
		LUA->SetField(-2, "Poll");
	}
	LUA->Pop(1);
	return 0;
}

void closeAllDatabases(lua_State* state)
{
	in_shutdown = true;

	LUA->ReferencePush(iRefDatabases);
	LUA->PushNil();

	while (LUA->Next(-2))
	{
		LUA->Push(-2);

		if (!LUA->IsType(-2, DATABASE_ID))
			continue;

		UserData* userdata = (UserData*)LUA->GetUserdata(-2);
		Database *mysqldb = (Database*)userdata->data;

		if (!mysqldb)
			continue;

		DisconnectDB(state, mysqldb);

		LUA->Pop(2);
	}
	LUA->Pop();
}

GMOD_MODULE_CLOSE()
{
	closeAllDatabases(state);
	LUA->ReferenceFree(iRefDatabases);
	mysql_library_end();
	return 0;
}