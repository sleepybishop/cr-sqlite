import { test, expect, beforeAll } from "vitest";
import DB from "../DB";
import TestConfig from "../../config/TestConfig";
import util from "../util";
import ServiceDB from "../ServiceDB";

let sdb: ServiceDB;

beforeAll(() => {
  sdb = new ServiceDB(TestConfig, true);
  try {
    sdb.addSchema(
      "ns",
      "test.sql",
      "1",
      `CREATE TABLE foo (a primary key, b);
      SELECT crsql_as_crr('foo');`
    );
    sdb.addSchema(
      "ns",
      "test.sql",
      "2",
      `CREATE TABLE IF NOT EXISTS foo (
        a primary key,
        b,
        c
      );
      
      SELECT crsql_as_crr('foo');`
    );
  } catch (e) {
    console.error(e);
  }
});

test("db loads", () => {
  const dbid = util.uuidToBytes(crypto.randomUUID());
  const db = new DB(TestConfig, dbid, (name, version) =>
    sdb.getSchema("ns", name, version)
  );

  expect(db).toBeDefined();
});

test("db bootstraps with correct dbid", () => {
  const dbid = util.uuidToBytes(crypto.randomUUID());
  const db = new DB(TestConfig, dbid, (name, version) =>
    sdb.getSchema("ns", name, version)
  );

  const dbidFromDb = db
    .__testsOnly()
    .prepare("SELECT crsql_siteid()")
    .pluck()
    .get();
  expect(Uint8Array.from(dbidFromDb as any)).toEqual(dbid);
});

test("db can bootstrap a new schema", async () => {
  const dbid = util.uuidToBytes(crypto.randomUUID());
  const db = new DB(TestConfig, dbid, (name, version) =>
    sdb.getSchema("ns", name, version)
  );

  await db.migrateTo("test.sql", "1");

  const footbl = db
    .__testsOnly()
    .prepare(
      `SELECT name FROM sqlite_master WHERE name = 'foo' AND type = 'table'`
    )
    .pluck()
    .get();
  expect(footbl).toBe("foo");
});

test("migrating to the same schema & version is a no-op", async () => {
  const dbid = util.uuidToBytes(crypto.randomUUID());
  const db = new DB(TestConfig, dbid, (name, version) =>
    sdb.getSchema("ns", name, version)
  );

  const result1 = await db.migrateTo("test.sql", "1");
  const result2 = await db.migrateTo("test.sql", "1");

  expect(result1).toBe("apply");
  expect(result2).toBe("noop");
});

test("migrating to an unrelated schema is an error", async () => {
  const dbid = util.uuidToBytes(crypto.randomUUID());
  const db = new DB(TestConfig, dbid, (name, version) =>
    sdb.getSchema("ns", name, version)
  );

  await db.migrateTo("test.sql", "1");

  await expect(() => db.migrateTo("test2.sql", "1")).toThrow();
});

test("db can migrate to a new schema", async () => {
  const dbid = util.uuidToBytes(crypto.randomUUID());
  const db = new DB(TestConfig, dbid, (name, version) =>
    sdb.getSchema("ns", name, version)
  );

  const result1 = await db.migrateTo("test.sql", "1");
  const result2 = await db.migrateTo("test.sql", "2");

  expect(result1).toBe("apply");
  expect(result2).toBe("migrate");

  // should have 3 cols now
  expect(() =>
    db.__testsOnly().prepare(`INSERT INTO foo (a, b, c) VALUES (1, 2, 3)`).run()
  ).not.toThrow();
});

test("db can read and write a changeset", async () => {
  const dbid1 = util.uuidToBytes(crypto.randomUUID());
  const db1 = new DB(TestConfig, dbid1, (name, version) =>
    sdb.getSchema("ns", name, version)
  );
  const dbid2 = util.uuidToBytes(crypto.randomUUID());
  const db2 = new DB(TestConfig, dbid2, (name, version) =>
    sdb.getSchema("ns", name, version)
  );

  await db1.migrateTo("test.sql", "1");
  await db2.migrateTo("test.sql", "1");
  db1.__testsOnly().exec(`INSERT INTO foo VALUES (1, 2)`);

  const changesFrom1 = [...db1.getChanges(dbid2, 0n)];
  db2.applyChanges(dbid1, changesFrom1);
});