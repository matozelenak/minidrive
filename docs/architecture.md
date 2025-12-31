# MiniDrive Architecture

I implemented:
- LIST, UPLOAD, DOWNLOAD, REMOVE, CD, MKDIR, RMDIR, COPY, MOVE
- public/private mode, storing hashed password with salt
- multiple simultaneous clients
- should not crash on invalid input
- logging

I did not implement:
- SYNC
- resuming uploads, downloads
- multiple user sessions


## Server operation

### main.cpp

Firstly, in main.cpp, arguments are parsed, signal handler get set up (to catch SIGINT, SIGTERM and SIGHUP), thread pool gets created and the Server class gets instantiated.

### Server

The Server class is responsible for accepting client connections and owning Sessions. When a new client connects, the Server::accept() function creates a new Session and appends it to a list of running sessions.

### Session

The Session class represents one client connection. It owns the asio::ip::tcp::socket and has registered callbacks from AsyncSocket to handle incoming messages and errors. In Session::start(), the callbacks are set up. When a new message arrives, the callback calls processMessage() when the message type is COMMAND or processData() otherwise.
The Session::processMessage() parses the JSON and call functions to handle all individual commands. Those functions are defined in message_handlers.cpp.
The Session::processData() handles receiving file chunks sent by the client during file uploads. It appends the bytes to the currenty open file. If it detects that the upload is finished, it removes the .part extension from the file. The function also sends back akcnowledgements of receiving the chunk (more in [UPLOAD](#upload)).

### AsyncSocket

AsyncSocket class is a wrapper for asio socket, and also handles message framing by having two functions, readHeader() and readPayload(), which it calls automatically to correctly received messages. It also has a write queue for messages, which is protected by a mutex. By calling its sendMessage() functions, the message gets inserted into the queue and then sent. This is because the asio::async_write() function should not be called again until the asynchronous write operation completes. The AsyncSocket class is used by both the server and client.

## Client operation

The Client class is basically a state machine. Firstly, in the Client::start() function, callback for receiving messages and errors are set up. It uses the AsyncSocket too. It also starts a new thread which then handles all the possible states. It can ether react to a server reply or handle user input.
- reactToReply() function -  handles printing the status of authentication, user registration, and whether a command was successfull or not. Most importantly it handles file upload by sending the chunks and file download by confirming the download has started. Writing chunks to the file is handled in processData() described later.
- processUserInput() function - it asks for user input depending on the current state, such as asking for password during authentication or waiting for a command.

In the callback for receiving messages, depending on the message type, processMessage() or processData() is called. processData() handles receiving file chunks during file downloads and processMessage() handles parsing the JSON message and remembering it.


## UPLOAD

Client sends a request containing path of the local file (src), path to the desired destination file (dst), and the file size. The server remembers that in the Transfer struct defined in transfer.hpp, opens the destination file for writing and sends back a reply containing the sequence number. The sequence number ranges from 0 to file size, and it represents how many bytes of the file has the server received. It starts at 0, and each time a new chunk of size N arrives, it gets incremented by N. When it reaches file size, it means the transfer is complete.

## DOWNLOAD

Client sends a request containing path of the remote file (src) and path to the desired destination (local) file (dst). The function Session::handleDOWNLOAD() defined in command_handlers.cpp handles both sending file chunks and checking of the requested file actually exists on the server (and permissions also). If everything is alright, the server sends a reply containing the file size and sequence number (same as in [UPLOAD](#upload)). The client then requests a file chunk by sending a request containing the sequence number and server replies by sending that chunk. Chunk size is defined as 4096B.


## Directory Layout

```
.
├── CMakeLists.txt            # Root build orchestrator
├── cmake/                    # Toolchain and dependency helpers
├── external/                 # Vendored single-header libraries (Asio, JSON)
├── client/
│   ├── include/
│   ├── src/
│   └── CMakeLists.txt
├── server/
│   ├── include/
│   ├── src/
│   └── CMakeLists.txt
├── shared/
│   ├── include/
│   ├── src/
│   └── CMakeLists.txt
├── tests/
│   ├── integration/
│   └── CMakeLists.txt
├── data/
│   └── server_root/          # Default runtime root for server
│       ├── users.json        # User database (JSON)
│       ├── _public/          # Public directory
│       └── user_data/        # Directory containing home dirs of each user
│             ├── <username>/
│             └── <username>/
├── docs/                     # Documentation
│   ├── architecture.md
│   ├── protocol.md
│   └── requirements.md
└── README.md
```
