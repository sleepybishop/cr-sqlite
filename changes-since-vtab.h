#if !defined(SQLITEINT_H)
#include "sqlite3ext.h"
#endif
SQLITE_EXTENSION_INIT3

#include "tableinfo.h"

sqlite3_module cfsql_changesSinceModule;

char *cfsql_changeQueryForTable(cfsql_TableInfo *tableInfo);
char *cfsql_changesUnionQuery(
    cfsql_TableInfo **tableInfos,
    int tableInfosLen);
cfsql_ColumnInfo *cfsql_pickColumnInfosFromVersionsMap(const char *colVersions, int *rLen);
char *cfsql_rowPatchDataQuery(
    cfsql_TableInfo *tblInfo,
    const char *colVrsns,
    char *pks);