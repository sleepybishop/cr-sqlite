// https://www.sqlite.org/unionvtab.html
// https://www.sqlite.org/swarmvtab.html#overview
// https://www.sqlite.org/carray.html
#if !defined(SQLITEINT_H)
#include "sqlite3ext.h"
#endif
SQLITE_EXTENSION_INIT3
#include <string.h>
#include <assert.h>
#include "consts.h"
#include "util.h"

/**
 * vtab usage:
 * SELECT * FROM cfsql_chages WHERE requestor = SITE_ID AND curr_version > V
 *
 * returns:
 * table_name, quote-concated pks ~'~, json-encoded vals, json-encoded versions, curr version
 */

typedef struct cfsql_ChangesSince_vtab cfsql_ChangesSince_vtab;
struct cfsql_ChangesSince_vtab
{
  sqlite3_vtab base; /* Base class - must be first */
  sqlite3 *db;
};

/* A subclass of sqlite3_vtab_cursor which will
** serve as the underlying representation of a cursor that scans
** over rows of the result
*/
typedef struct cfsql_ChangesSince_cursor cfsql_ChangesSince_cursor;
struct cfsql_ChangesSince_cursor
{
  sqlite3_vtab_cursor base; /* Base class - must be first */

  cfsql_ChangesSince_vtab *pTab;
  cfsql_TableInfo **tableInfos;
  int tableInfosLen;

  // The statement that is returning the identifiers
  // of what has changed
  sqlite3_stmt *pChangesStmt;

  // The statement that fetches the singular row for what change is
  // currently being processed
  // we actually likely don't need to save it here
  // since we're custom packing returned values from rows.
  sqlite3_stmt *pRowStmt;

  // char *tbl;
  // char *pks;
  // char *colVals;
  // char *colVsns;
  // sqlite3_int64 minv;
};

/*
** The changesSinceVtabConnect() method is invoked to create a new
** template virtual table.
**
** Think of this routine as the constructor for templatevtab_vtab objects.
**
** All this routine needs to do is:
**
**    (1) Allocate the templatevtab_vtab object and initialize all fields.
**
**    (2) Tell SQLite (via the sqlite3_declare_vtab() interface) what the
**        result set of queries against the virtual table will look like.
*/
static int changesSinceConnect(
    sqlite3 *db,
    void *pAux,
    int argc, const char *const *argv,
    sqlite3_vtab **ppVtab,
    char **pzErr)
{
  cfsql_ChangesSince_vtab *pNew;
  int rc;

  // TODO: future improvement to include txid
  rc = sqlite3_declare_vtab(
      db,
      "CREATE TABLE x([table], [pks], [col_vals], [col_versions], [curr_version], [requestor] hidden)");
#define CHANGES_SINCE_VTAB_TBL 0
#define CHANGES_SINCE_VTAB_PKS 1
#define CHANGES_SINCE_VTAB_COL_VLS 2
#define CHANGES_SINCE_VTAB_COL_VRSNS 3
#define CHANGES_SINCE_VTAB_VRSN 4
  if (rc == SQLITE_OK)
  {
    pNew = sqlite3_malloc(sizeof(*pNew));
    *ppVtab = (sqlite3_vtab *)pNew;
    if (pNew == 0)
    {
      return SQLITE_NOMEM;
    }
    memset(pNew, 0, sizeof(*pNew));
    pNew->db = db;
  }
  return rc;
}

/*
** Destructor for ChangesSince_vtab objects
*/
static int changesSinceDisconnect(sqlite3_vtab *pVtab)
{
  cfsql_ChangesSince_vtab *p = (cfsql_ChangesSince_vtab *)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

/*
** Constructor for a new ChangesSince cursors object.
*/
static int changesSinceOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor)
{
  cfsql_ChangesSince_cursor *pCur;
  pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur == 0)
  {
    return SQLITE_NOMEM;
  }
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

static int changesSinceCrsrFinalize(cfsql_ChangesSince_cursor *crsr)
{
  int rc = SQLITE_OK;
  rc += sqlite3_finalize(crsr->pChangesStmt);
  rc += sqlite3_finalize(crsr->pRowStmt);
  crsr->pChangesStmt = 0;
  crsr->pRowStmt = 0;
  for (int i = 0; i < crsr->tableInfosLen; ++i) {
    cfsql_freeTableInfo(crsr->tableInfos[i]);
  }
  sqlite3_free(crsr->tableInfos);
  
  return rc;
}

/*
** Destructor for a ChangesSince cursor.
*/
static int changesSinceClose(sqlite3_vtab_cursor *cur)
{
  cfsql_ChangesSince_cursor *pCur = (cfsql_ChangesSince_cursor *)cur;
  changesSinceCrsrFinalize(pCur);
  sqlite3_free(pCur);
  return SQLITE_OK;
}

/*
** Advance a ChangesSince_cursor to its next row of output.
*/
static int changesSinceNext(sqlite3_vtab_cursor *cur)
{
  cfsql_ChangesSince_cursor *pCur = (cfsql_ChangesSince_cursor *)cur;
  int rc = SQLITE_OK;

  // finalize the prior row statement if it exists
  rc = sqlite3_finalize(pCur->pRowStmt);

  // step to next
  // if not row, tear down (finalize) statements
  // set statements to null
  if (rc != SQLITE_OK || pCur->pChangesStmt == 0)
  {
    return rc || SQLITE_ERROR;
  }

  if (sqlite3_step(pCur->pChangesStmt) != SQLITE_ROW)
  {
    // tear down since we're done
    return changesSinceCrsrFinalize(pCur);
  }

  // else -- prep row statement based on changeSrc columns

  return rc;
}

/*
** Return values of columns for the row at which the templatevtab_cursor
** is currently pointing.
*/
static int changesSinceColumn(
    sqlite3_vtab_cursor *cur, /* The cursor */
    sqlite3_context *ctx,     /* First argument to sqlite3_result_...() */
    int i                     /* Which column to return */
)
{
  cfsql_ChangesSince_cursor *pCur = (cfsql_ChangesSince_cursor *)cur;
  sqlite3_result_value(ctx, sqlite3_column_value(pCur->pRowStmt, i));
  return SQLITE_OK;
}

/*
** Return the rowid for the current row.  In this implementation, the
** rowid is the same as the output value.
*/
static int changesSinceRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid)
{
  cfsql_ChangesSince_cursor *pCur = (cfsql_ChangesSince_cursor *)cur;
  // *pRowid = pCur->minv;
  return SQLITE_OK;
}

/*
** Return TRUE if the cursor has been moved off of the last
** row of output.
*/
static int changesSinceEof(sqlite3_vtab_cursor *cur)
{
  cfsql_ChangesSince_cursor *pCur = (cfsql_ChangesSince_cursor *)cur;
  return pCur->pChangesStmt == 0;
}

char *cfsql_changesUnionQuery(
    int numRows,
    char **tableNames)
{
  char *unionsArr[numRows];
  char *unionsStr;
  char *ret;
  int i = 0;

  for (i = 0; i < numRows; ++i)
  {
    unionsArr[i] = sqlite3_mprintf(
        "SELECT max(__cfsql_version) as version FROM \"%w\" %s ",
        // the first result in tableNames is the column heading
        // so skip that
        tableNames[i + 1],
        // If we have more tables to process, union them in
        i < numRows - 1 ? UNION : "");
  }

  // move the array of strings into a single string
  unionsStr = cfsql_join(unionsArr, numRows);
  // free the array of strings
  for (i = 0; i < numRows; ++i)
  {
    sqlite3_free(unionsArr[i]);
  }

  // compose the final query
  // and update the pointer to the string to point to it.
  ret = sqlite3_mprintf(
      "SELECT max(version) as version FROM (%z)",
      unionsStr);
  // %z frees unionsStr https://www.sqlite.org/printf.html#percentz
  return ret;
}

/*
** This method is called to "rewind" the templatevtab_cursor object back
** to the first row of output.  This method is always called at least
** once prior to any call to templatevtabColumn() or templatevtabRowid() or
** templatevtabEof().
*/
static int changesSinceFilter(
    sqlite3_vtab_cursor *pVtabCursor,
    int idxNum, const char *idxStr,
    int argc, sqlite3_value **argv)
{
  int rc = SQLITE_OK;
  cfsql_ChangesSince_cursor *pCrsr = (cfsql_ChangesSince_cursor *)pVtabCursor;
  cfsql_ChangesSince_vtab *pTab = pCrsr->pTab;
  sqlite3 *db = pTab->db;
  char **rClockTableNames = 0;
  int rNumRows = 0;
  int rNumCols = 0;
  char *err = 0;

  // Find all clock tables
  rc = sqlite3_get_table(
      db,
      CLOCK_TABLES_SELECT,
      &rClockTableNames,
      &rNumRows,
      &rNumCols,
      0);

  if (rc != SQLITE_OK)
  {
    sqlite3_free_table(rClockTableNames);
    return rc;
  }

  if (rNumRows == 0)
  {
    pCrsr->pChangesStmt = 0;
    pCrsr->pRowStmt = 0;
    sqlite3_free_table(rClockTableNames);
    return rc;
  }

  // construct table infos for each table
  // we'll need to attach these table infos
  // to our cursor
  // we should preclude index info from them
  cfsql_TableInfo **tableInfos = sqlite3_malloc(rNumRows * sizeof(cfsql_TableInfo*));
  memset(tableInfos, 0, rNumRows * sizeof *tableInfos);
  for (int i = 0; i < rNumRows; ++i) {
    rc = cfsql_getTableInfo(db, rClockTableNames[i + 1], &tableInfos[i], &err);

    if (rc != SQLITE_OK) {
      for (int j = 0; j < rNumRows; ++j) {
        cfsql_freeTableInfo(tableInfos[j]);
      }
      sqlite3_free(tableInfos);
      sqlite3_free(err);
      return rc;
    }
  }

  // put table infos into our cursor for later use on row fetches
  pCrsr->tableInfos = tableInfos;

  // now construct and prepare our union for fetching changes
  // union gets:
  // 1. name of table
  // 2. pks of changed row
  // 3. column versions
  // 4. originator (or 0 for local)
  // char *zSql = cfsql_changesUnionQuery();

  // run our query here??
  // https://sqlite.org/src/file/ext/misc/unionvtab.c
  // templatevtab_cursor *pCur = (templatevtab_cursor *)pVtabCursor;
  // pCur->iRowid = 1;
  return SQLITE_OK;
}

/*
** SQLite will invoke this method one or more times while planning a query
** that uses the virtual table.  This routine needs to create
** a query plan for each invocation and compute an estimated cost for that
** plan.
*/
static int templatevtabBestIndex(
    sqlite3_vtab *tab,
    sqlite3_index_info *pIdxInfo)
{
  pIdxInfo->estimatedCost = (double)10;
  pIdxInfo->estimatedRows = 10;
  return SQLITE_OK;
}

/*
** This following structure defines all the methods for the
** virtual table.
*/
static sqlite3_module templatevtabModule = {
    /* iVersion    */ 0,
    /* xCreate     */ 0,
    /* xConnect    */ changesSinceConnect,
    /* xBestIndex  */ templatevtabBestIndex,
    /* xDisconnect */ changesSinceDisconnect,
    /* xDestroy    */ 0,
    /* xOpen       */ changesSinceOpen,
    /* xClose      */ changesSinceClose,
    /* xFilter     */ changesSinceFilter,
    /* xNext       */ changesSinceNext,
    /* xEof        */ changesSinceEof,
    /* xColumn     */ changesSinceColumn,
    /* xRowid      */ changesSinceRowid,
    /* xUpdate     */ 0,
    /* xBegin      */ 0,
    /* xSync       */ 0,
    /* xCommit     */ 0,
    /* xRollback   */ 0,
    /* xFindMethod */ 0,
    /* xRename     */ 0,
    /* xSavepoint  */ 0,
    /* xRelease    */ 0,
    /* xRollbackTo */ 0,
    /* xShadowName */ 0};

// #ifdef _WIN32
// __declspec(dllexport)
// #endif
// int sqlite3_templatevtab_init(
//   sqlite3 *db,
//   char **pzErrMsg,
//   const sqlite3_api_routines *pApi
// ){
//   int rc = SQLITE_OK;
//   SQLITE_EXTENSION_INIT2(pApi);
//   rc = sqlite3_create_module(db, "templatevtab", &templatevtabModule, 0);
//   return rc;
// }