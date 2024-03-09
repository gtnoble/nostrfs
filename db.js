const { verifyEvent } = require("nostr")

Sqlite3 = require("better-sqlite3")


const CREATE_NOSTR_EVENTS_TABLE_TEMPLATE =
`
CREATE TABLE IF NOT EXISTS
  nostrEvents (
    id TEXT PRIMARY KEY,
    pubkey TEXT,
    created_at INTEGER,
    kind INTEGER,
    content TEXT,
    sig TEXT
  )
;
`
const CREATE_NOSTR_TAGS_TABLE_TEMPLATE =
`
CREATE TABLE IF NOT EXISTS
    tags (
      id TEXT,
      key TEXT,
      tag_index INTEGER,
      value_index INTEGER,
      value TEXT,
      FOREIGN KEY(id) REFERENCES nostrEvents(id)
    )
;
`

const INSERT_NOSTR_EVENT_TEMPLATE =
`
INSERT OR IGNORE INTO
    nostrEvents (
      id,
      pubkey,
      created_at,
      kind,
      content,
      sig
    )
VALUES
    (?, ?, ?, ?, ?, ?)
;
`

const INSERT_NOSTR_TAG_TEMPLATE =
`
INSERT OR IGNORE INTO
    tags (
      id,
      key,
      tag_index,
      value_index,
      value
    )
VALUES
    (?, ?, ?, ?, ?)
;
`

const EVENT_CREATED_TEMPLATE =
`
SELECT 
  created_at
FROM
  nostrEvents
WHERE
  id = ?
`

const EVENT_IDS_QUERY_TEMPLATE =
`
SELECT
  id
FROM
  nostrEvents
`

const EVENT_BY_ID_TEMPLATE =
`
SELECT
  *
FROM
  nostrEvents
WHERE
  id = ?
;
`

class NostrDb {
  constructor(databaseFilename) {
    this.db = new Sqlite3(databaseFilename)
    this.db.pragma("foreign_keys = ON")
    this.db.pragma("journal_mode = WAL")

    this.db.prepare(CREATE_NOSTR_EVENTS_TABLE_TEMPLATE).run()
    this.db.prepare(CREATE_NOSTR_TAGS_TABLE_TEMPLATE).run()

    this.insertEventQuery = this.db.prepare(INSERT_NOSTR_EVENT_TEMPLATE)
    this.insertTagQuery = this.db.prepare(INSERT_NOSTR_TAG_TEMPLATE)
    this.eventCreatedAtQuery = this.db.prepare(EVENT_CREATED_TEMPLATE)
    this.getAllEventIdsQuery = this.db.prepare(EVENT_IDS_QUERY_TEMPLATE)
    this.getEventByIdQuery = this.db.prepare(EVENT_BY_ID_TEMPLATE)

  }

  async insertEvent(nostrEvent) {
    if (await verifyEvent(nostrEvent)) {
      this.insertEventQuery.run(
        nostrEvent.id, 
        nostrEvent.pubkey, 
        nostrEvent.created_at,
        nostrEvent.kind,
        nostrEvent.content,
        nostrEvent.sig
      )
      for (let tagSequence = 0; tagSequence < nostrEvent.tags.length; tagSequence++) {
        const tag = nostrEvent.tags[tagSequence]
        const tagKey = tag[0]
        const tagValues = tag.slice(1)
        for (let tagValueIndex = 0; tagValueIndex < tagValues.length; tagValueIndex++) {
          this
            .insertTagQuery
            .run(nostrEvent.id, tagKey, tagSequence, tagValueIndex, tagValues[tagValueIndex])
        }
      }
      return true;
    }
    else {
      return false
    }
  }

  eventCreatedAt(eventId) {
    return this.eventCreatedAtQuery.get(eventId)?.created_at
  }

  getAllEventIds() {
    return this.getAllEventIdsQuery.all().map((row) => row.id)
  }

  getEventById(eventId) {
    return this.getEventByIdQuery.get(eventId)
  }


}

module.exports = {NostrDb}