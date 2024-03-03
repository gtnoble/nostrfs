const {NostrDb} = require("./db")
const tap = require("tap")
const testEvents = require("./testEvents.json")

tap.test("Test db", async (tt) => {
    const db = new NostrDb("./test.sqlite3")
    for (const event of testEvents) {
        await db.insertEvent(event)
    }
})