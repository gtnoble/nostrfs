const {RelayPool} = require('nostr')
const fs = require("node:fs")

const TEST_RELAYS = ["wss://relay.damus.io"]


pool = RelayPool(TEST_RELAYS)


const testEvents = []

pool.on('open', relay => {
    relay.subscribe("testSub", {limit: 10})
})

pool.on('eose', relay => {
    relay.close()
    fs.writeFileSync("testEvents.json", JSON.stringify(testEvents))
})

pool.on('event', (relay, sub_id, nostrEvent) => {
    console.log(nostrEvent)
    testEvents.push(nostrEvent)
})