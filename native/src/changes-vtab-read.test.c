/**
 * Copyright 2022 One Law LLC. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "crsqlite.h"
#include "changes-vtab-read.h"
#include "consts.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int crsql_close(sqlite3 *db);

static void testChangesQueryForTable()
{
  printf("ChangeQueryForTable\n");
  int rc = SQLITE_OK;
  sqlite3 *db;
  char *err = 0;
  crsql_TableInfo *tblInfo = 0;
  rc = sqlite3_open(":memory:", &db);

  rc += sqlite3_exec(db, "create table foo (a primary key, b);", 0, 0, &err);
  rc += sqlite3_exec(db, "select crsql_as_crr('foo');", 0, 0, &err);
  rc += crsql_getTableInfo(db, "foo", &tblInfo, &err);
  assert(rc == SQLITE_OK);

  char *query = crsql_changesQueryForTable(tblInfo);

  assert(strcmp(
             query,
             "SELECT      \'foo\' as tbl,      quote(\"a\") as pks,      __crsql_col_name as cid,      __crsql_version as vrsn,      __crsql_site_id as site_id    FROM \"foo__crsql_clock\"    WHERE      site_id IS NOT ?    AND      vrsn > ?") == 0);
  sqlite3_free(query);

  printf("\t\e[0;32mSuccess\e[0m\n");
}

static void testChangesUnionQuery()
{
  printf("ChangesUnionQuery\n");

  int rc = SQLITE_OK;
  sqlite3 *db;
  char *err = 0;
  crsql_TableInfo **tblInfos = sqlite3_malloc(2 * sizeof(crsql_TableInfo *));
  rc = sqlite3_open(":memory:", &db);

  rc += sqlite3_exec(db, "create table foo (a primary key, b);", 0, 0, &err);
  rc += sqlite3_exec(db, "create table bar (\"x\" primary key, [y]);", 0, 0, &err);
  rc += sqlite3_exec(db, "select crsql_as_crr('foo');", 0, 0, &err);
  rc += sqlite3_exec(db, "select crsql_as_crr('bar');", 0, 0, &err);
  rc += crsql_getTableInfo(db, "foo", &tblInfos[0], &err);
  rc += crsql_getTableInfo(db, "bar", &tblInfos[1], &err);
  assert(rc == SQLITE_OK);

  char *query = crsql_changesUnionQuery(tblInfos, 2);

  assert(strcmp(query, "SELECT tbl, pks, cid, vrsn, site_id FROM (SELECT      \'foo\' as tbl,      quote(\"a\") as pks,      __crsql_col_name as cid,      __crsql_version as vrsn,      __crsql_site_id as site_id    FROM \"foo__crsql_clock\"    WHERE      site_id IS NOT ?    AND      vrsn > ? UNION SELECT      \'bar\' as tbl,      quote(\"x\") as pks,      __crsql_col_name as cid,      __crsql_version as vrsn,      __crsql_site_id as site_id    FROM \"bar__crsql_clock\"    WHERE      site_id IS NOT ?    AND      vrsn > ?) ORDER BY vrsn, tbl ASC") == 0);
  sqlite3_free(query);

  printf("\t\e[0;32mSuccess\e[0m\n");
}

static void testRowPatchDataQuery()
{
  printf("RowPatchDataQuery\n");

  int rc = SQLITE_OK;
  sqlite3 *db;
  char *err = 0;
  crsql_TableInfo *tblInfo = 0;
  rc = sqlite3_open(":memory:", &db);

  rc += sqlite3_exec(db, "create table foo (a primary key, b, c, d);", 0, 0, &err);
  rc += sqlite3_exec(db, "select crsql_as_crr('foo');", 0, 0, &err);
  rc += sqlite3_exec(db, "insert into foo values(1, 'cb', 'cc', 'cd')", 0, 0, &err);
  rc += crsql_getTableInfo(db, "foo", &tblInfo, &err);
  assert(rc == SQLITE_OK);

  // TC1: single pk table, 1 col change
  const char *cid = "b";
  char *pks = "1";
  char *q = crsql_rowPatchDataQuery(db, tblInfo, cid, pks);
  assert(strcmp(q, "SELECT quote(\"b\") FROM \"foo\" WHERE \"a\" = 1") == 0);
  sqlite3_free(q);

  printf("\t\e[0;32mSuccess\e[0m\n");
}

void crsqlChangesVtabReadTestSuite()
{
  printf("\e[47m\e[1;30mSuite: crsql_changesVtabRead\e[0m\n");
  testChangesQueryForTable();
  testChangesUnionQuery();
  testRowPatchDataQuery();
}
