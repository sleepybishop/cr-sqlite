use crate::c::crsql_ExtData;
use crate::c::CPointer;
use crate::util::Countable;
use alloc::boxed::Box;
use alloc::ffi::CString;
use alloc::format;
use alloc::vec;
use alloc::vec::Vec;
use core::ffi::c_char;
use core::mem;
use num_traits::ToPrimitive;
use sqlite_nostd as sqlite;
use sqlite_nostd::Connection;
use sqlite_nostd::ResultCode;
use sqlite_nostd::StrRef;

pub struct TableInfo {
    pub tbl_name: String,
    pub pks: Vec<ColumnInfo>,
    pub non_pks: Vec<ColumnInfo>,
}

pub struct ColumnInfo {
    pub cid: i32,
    pub name: String,
    pub type_: String,
    pub notnull: bool,
    // > 0 if it is a primary key columns
    // the value refers to the position in the `PRIMARY KEY (cols...)` statement
    pub pk: i32,
}

#[no_mangle]
pub extern "C" fn crsql_init_table_info_vec(ext_data: *mut crsql_ExtData) {
    let vec = vec![];
    let (ptr, len, cap) = vec.into_raw_parts();
    unsafe {
        (*ext_data).tableInfos = ptr;
        (*ext_data).tableInfosLen = len;
        (*ext_data).tableInfosCap = cap;
    }
}

/**
 * Given a table name, return the table info that describes that table.
 * TableInfo is a struct that represents the results
 * of pragma_table_info, pragma_index_list, pragma_index_info on a given table
 * and its indices as well as some extra fields to facilitate crr creation.
 */
pub fn pull_table_info(
    db: *mut sqlite::sqlite3,
    table: &str,
    err: *mut *mut c_char,
) -> Result<TableInfo, ResultCode> {
    let sql = format!("SELECT count(*) FROM pragma_table_info('{table}')");
    let columns_len = match db.prepare_v2(&sql).and_then(|stmt| {
        stmt.step()?;
        stmt.column_int(0)?.to_usize().ok_or(ResultCode::ERROR)
    }) {
        Ok(count) => count,
        Err(code) => {
            err.set(&format!("Failed to find columns for crr -- {table}"));
            return Err(code);
        }
    };

    let sql = format!(
        "SELECT \"cid\", \"name\", \"type\", \"notnull\", \"pk\"
         FROM pragma_table_info('{table}') ORDER BY cid ASC"
    );
    let column_infos = match db.prepare_v2(&sql) {
        Ok(stmt) => {
            let mut cols: Vec<ColumnInfo> = vec![];

            while stmt.step()? == ResultCode::ROW {
                cols.push(ColumnInfo {
                    type_: stmt.column_text(2)?.to_string(),
                    name: stmt.column_text(1)?.to_string(),
                    notnull: stmt.column_int(3)?,
                    cid: stmt.column_int(0)?,
                    pk: stmt.column_int(4)?,
                });
            }

            if cols.len() != columns_len {
                err.set("Number of fetched columns did not match expected number of columns");
                return Err(ResultCode::ERROR);
            }
            cols
        }
        Err(code) => {
            err.set(&format!("Failed to prepare select for crr -- {table}"));
            return Err(code);
        }
    };

    let (mut pks, non_pks): (Vec<_>, Vec<_>) =
        column_infos.clone().into_iter().partition(|x| x.pk > 0);
    pks.sort_by_key(|x| x.pk);

    return Ok(TableInfo {
        tbl_name: table.to_string(),
        pks,
        non_pks,
    });
}

pub fn is_table_compatible(
    db: *mut sqlite::sqlite3,
    table: &str,
    err: *mut *mut c_char,
) -> Result<bool, ResultCode> {
    // No unique indices besides primary key
    if db.count(&format!(
        "SELECT count(*) FROM pragma_index_list('{table}')
            WHERE \"origin\" != 'pk' AND \"unique\" = 1"
    ))? != 0
    {
        err.set(&format!(
            "Table {table} has unique indices besides\
                        the primary key. This is not allowed for CRRs"
        ));
        return Ok(false);
    }

    // Must have a primary key
    if db.count(&format!(
        // pragma_index_list does not include primary keys that alias rowid...
        // hence why we cannot use
        // `select * from pragma_index_list where origin = pk`
        "SELECT count(*) FROM pragma_table_info('{table}')
        WHERE \"pk\" > 0"
    ))? == 0
    {
        err.set(&format!(
            "Table {table} has no primary key. \
            CRRs must have a primary key"
        ));
        return Ok(false);
    }

    // No auto-increment primary keys
    let stmt = db.prepare_v2(&format!(
        "SELECT 1 FROM sqlite_master WHERE name = ? AND type = 'table' AND sql
            LIKE '%autoincrement%' limit 1"
    ))?;
    stmt.bind_text(1, table, sqlite::Destructor::STATIC)?;
    if stmt.step()? == ResultCode::ROW {
        err.set(&format!(
            "{table} has auto-increment primary keys. This is likely a mistake as two \
                concurrent nodes will assign unrelated rows the same primary key. \
                Either use a primary key that represents the identity of your row or \
                use a database friendly UUID such as UUIDv7"
        ));
        return Ok(false);
    };

    // No checked foreign key constraints
    if db.count(&format!(
        "SELECT count(*) FROM pragma_foreign_key_list('{table}')"
    ))? != 0
    {
        err.set(&format!(
            "Table {table} has checked foreign key constraints. \
            CRRs may have foreign keys but must not have \
            checked foreign key constraints as they can be violated \
            by row level security or replication."
        ));
        return Ok(false);
    }

    // Check for default value or nullable
    if db.count(&format!(
        "SELECT count(*) FROM pragma_table_xinfo('{table}')
        WHERE \"notnull\" = 1 AND \"dflt_value\" IS NULL AND \"pk\" = 0"
    ))? != 0
    {
        err.set(&format!(
            "Table {table} has a NOT NULL column without a DEFAULT VALUE. \
            This is not allowed as it prevents forwards and backwards \
            compatibility between schema versions. Make the column \
            nullable or assign a default value to it."
        ));
        return Ok(false);
    }

    return Ok(true);
}
