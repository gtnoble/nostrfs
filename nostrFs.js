const Fuse = require("fuse-native")
const PosixPath = require("node:path")
const Buffer = require("node:buffer")

const { NostrDb } = require("./db");

const EVENT_ID_DIR_NAME = "e"

const db = new NostrDb("./testdb.sqlite3")

const filename = (path) => {
    return PosixPath.basename(path)
}

const parentDirName = (path) => {
    return filename(PosixPath.dirname(path))
}

const isEventDir = (path) => {
    return parentDirName(path) === EVENT_ID_DIR_NAME
}

const isInEventDir = (path) => {
    return isEventIdDir(PosixPath.dirname(path))
}


const eventFiles = {
    id: true,
    pubkey: true,
    kind: true,
    tags: true,
    content: true,
    sig: true
}

const toFopenFlag = (flags) => {
    flags = flags & 3
    if (flags === 0) return 'r'
    if (flags === 1) return 'w'
    return 'r+'
}


const openEventFile = (path, flags, cb) => {
    if (toFopenFlag(flags) != 'r') {
        return Fuse.EACCES
    }
    if (fileHandler) {
        return cb(0, getNextFileDescriptor(path))
    }
    else {
        return Fuse.ENOENT
    }

}

fileDescriptors = []

const getNextFileDescriptor = (path) => {
    for (let i = 3;; i++) {
        if(! fileDescriptors[i]) {
            fileDescriptors[i] = path;
            return i
        }
    }
}

const readEventFile = (path, fd, buffer, length, position, cb) => {
    const id = parentDirName(fileDescriptors[fd])
    const event = db.getEventById(id)
    data = event[filename(path)]
    const rawData = Buffer.from(data.toString(), 'utf8')
    if (position >= rawData.length) {
        return cb(0)
    }
    const selectedData = rawData.slice(position, position + length)
    selectedData.copy(buffer)
    cb(selectedData.length)
}

const isEventFile = (path) => {
    return isInEventDir && eventFiles[filename(path)]
}


const ops = {
    readdir: (path, cb) => {
        if (path === "/")
            return cb(null, ["/e"])
        else if (path === "/e")
            return cb(null, db.getAllEventIds())
        else if (PosixPath.basename(path) == "/e")
            return cb(null, Object.keys(eventFiles))
        else
            return Fuse.ENOENT
    },
    getattr: (path, cb) => {
        let filestat
        if (isEventFile(path)) {
            const createdAt = db.eventCreatedAt(filename(path))
            if (createdAt) {
                return cb(null, stat({
                    mtime: createdAt,
                    atime: new Date(),
                    ctime: createdAt,
                    mode: 'file',
                    size: 11
                }))
            }
            else
                return Fuse.ENOENT
        }
        else if (isEventDir(path) ) {
            return cb(null, stat({
                mtime: createdAt,
                atime: new Date(),
                ctime: createdAt,
                size: 12,
                mode: 'dir',
            }))
        }
        else if (path === "/e" || path === "/") {
            return cb(null, stat({
                mtime: new Date(),
                atime: new Date(),
                ctime: new Date(),
                size: 12,
                mode: 'dir',
            }))
        }
        else 
            return Fuse.ENOENT
    },
    open: (path, flags, cb) => {
        if (isEventFile(path)) {
            openEventFile(path, flags, cb)
        }
        else {
            return Fuse.ENOENT
        }
    },
    read: (path, fd, buffer, length, position, cb) => {
        readEventFile(path, fd, buffer, length, position, cb)
    }

}

const fuse = new Fuse('./mnt', ops, { debug: true, displayFolder: true })
fuse.mount(err => {
  if (err) throw err
  console.log('filesystem mounted on ' + fuse.mnt)
})

process.once('SIGINT', function () {
  fuse.unmount(err => {
    if (err) {
      console.log('filesystem at ' + fuse.mnt + ' not unmounted', err)
    } else {
      console.log('filesystem at ' + fuse.mnt + ' unmounted')
    }
  })
})