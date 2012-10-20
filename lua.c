/*
 * Copyright (c) 2012 Rob Hoelz <rob@hoelz.ro>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
*/

#include "sqlite3ext.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

SQLITE_EXTENSION_INIT1;

static void
handle_lua_error(lua_State *L, sqlite3_context *ctx)
{
    const char *error;
    size_t error_len;

    error = lua_tolstring(L, -1, &error_len);
    sqlite3_result_error(ctx, error, error_len);
    lua_pop(L, 1);
}

static void
convert_sqlite_values_to_lua(lua_State *L, int nargs, sqlite3_value **values)
{
    int i;

    for(i = 0; i < nargs; i++) {
        switch(sqlite3_value_type(values[i])) {
            case SQLITE_INTEGER:
                lua_pushinteger(L, sqlite3_value_int(values[i]));
                break;
            case SQLITE_FLOAT:
                lua_pushnumber(L, sqlite3_value_double(values[i]));
                break;
            case SQLITE_NULL:
                lua_pushnil(L);
                break;
            case SQLITE_BLOB:
            case SQLITE_TEXT: {
                size_t length;

                length = sqlite3_value_bytes(values[i]);
                lua_pushlstring(L, (const char *) sqlite3_value_text(values[i]), length);
                break;
            }
        }
    }
}

static void
convert_lua_value_to_sqlite(lua_State *L, sqlite3_context *ctx)
{
    switch(lua_type(L, -1)) {
        case LUA_TSTRING: {
            size_t length;
            const char *value;

            value = lua_tolstring(L, -1, &length);

            sqlite3_result_text(ctx, value, length, SQLITE_TRANSIENT);
            break;
        }
        case LUA_TNUMBER:
            sqlite3_result_double(ctx, lua_tonumber(L, -1));
            break;
        case LUA_TBOOLEAN:
            sqlite3_result_int(ctx, lua_toboolean(L, -1));
            break;
        case LUA_TNIL:
            sqlite3_result_null(ctx);
            break;

        case LUA_TTABLE:
        case LUA_TFUNCTION:
        case LUA_TTHREAD:
        case LUA_TUSERDATA: {
            char *error = NULL;

            error = sqlite3_mprintf("Invalid return type from lua(): %s", lua_typename(L, lua_type(L, -1)));

            sqlite3_result_error(ctx, error, -1);
            sqlite3_free(error);
        }
    }

    lua_pop(L, 1);
}

static void
insert_args_into_globals(lua_State *L, int nargs)
{
    int i;
    int stack_offset = lua_gettop(L) - nargs;

    lua_createtable(L, nargs, 0);
    for(i = 1; i <= nargs; i++) {
        lua_pushinteger(L, i);
        lua_pushvalue(L, stack_offset + i);
        lua_settable(L, -3);
    }

    lua_setglobal(L, "arg");
}

static void
sqlite_lua(sqlite3_context *ctx, int nargs, sqlite3_value **args)
{
    lua_State *L = (lua_State *) sqlite3_user_data(ctx);
    const char *lua_source;
    int status;

    if(nargs < 1) {
        const char error[] = "No argument passed to lua()";
        sqlite3_result_error(ctx, error, sizeof(error) - 1);
        return;
    }

    lua_source = (const char *) sqlite3_value_text(args[0]);

    /* we prefix "return " first to enable automatic returning of values */
    lua_pushliteral(L, "return ");
    lua_pushstring(L, lua_source);
    lua_concat(L, 2);

    status = luaL_loadstring(L, lua_tostring(L, -1));
    lua_remove(L, -2);

    if(status == LUA_ERRSYNTAX) {
        lua_pop(L, 1);
        status = luaL_loadstring(L, lua_source);
    }

    if(status) {
        handle_lua_error(L, ctx);
        return;
    }

    convert_sqlite_values_to_lua(L, nargs - 1, args + 1);
    insert_args_into_globals(L, nargs - 1);

    status = lua_pcall(L, nargs - 1, 1, 0);

    if(status) {
        handle_lua_error(L, ctx);
        return;
    }

    convert_lua_value_to_sqlite(L, ctx);
}

static void *
sqlite_lua_allocator(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void) ud;
    (void) osize;

    return sqlite3_realloc(ptr, nsize);
}

int
sqlite3_extension_init(sqlite3 *db, char **error, const sqlite3_api_routines *api)
{
    lua_State *L;

    SQLITE_EXTENSION_INIT2(api);

    L = lua_newstate(sqlite_lua_allocator, NULL);
    luaL_openlibs(L);

    if(! L) {
        *error = sqlite3_mprintf("Unable to create Lua state");
        return SQLITE_ERROR;
    }

    sqlite3_create_function_v2(db, "lua", -1, SQLITE_UTF8, L, sqlite_lua,
        NULL, NULL, (void (*)(void*)) lua_close);

    return SQLITE_OK;
}
