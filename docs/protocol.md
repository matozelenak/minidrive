# MiniDrive Protocol


## Messages framing

Each message sent over the network has the following structure:

- message type (1B)
  - COMMAND (0x00) - means the payload is a JSON message
  - FILE DATA (0x01) - means the payload is a file chunk (used when uploading/downloading a file)
- payload length (4B) - 32-bit unsigned integer, indicates the number of bytes of the payload
- payload


Example message:
```
 message type
 |
 |    payload length
 |    |
|  |        |    {"cmd":"LIST","args":{"path":"."}}                              |
 00 22000000 7b22636d64223a224c495354222c202261726773223a7b2270617468223a22227d7d
```

## Communication

- If a username is provided, client asks the server if the user exists
  - if the user exists, client asks for password and sends it to the server
    - if the password was correct, client now waits for user input and accepts commands
    - if not, client asks for password again
  - If the user does not exist, client asks whether to register a new user
    - if yes, send request to the server and inform about results and exit
    - if no, exit

- If no username is provided, client is authenticated as public user and waits for user input and accepts commands

## JSON messages

- These are all requests sent by the client:
  ```json
  Checking if a user exists:
  {"cmd": "AUTH", "args": {"mode": "private", "username": "mrkva"}}

  Authenticate as private user:
  {"cmd": "AUTH", "args": {"mode": "private", "username": "mrkva", "password":"1234"}}

  Authenticate as public user:
  {"cmd": "AUTH", "args": {"mode": "public"}}

  Register new user:
  {"cmd": "REGISTER", "args": {"username": "mrkva", "password":"1234"}}

  {"cmd": "LIST", "args": {"path": "."}}

  {"cmd": "CD", "args": {"path": "."}}

  {"cmd": "REMOVE", "args": {"path": "."}}

  {"cmd": "MKDIR", "args": {"path": "."}}

  {"cmd": "RMDIR", "args": {"path": "."}}

  {"cmd": "COPY", "args": {"src": "file.txt", "dst": "file2.txt"}}
  
  {"cmd": "MOVE", "args": {"src": "file.txt", "dst": "file2.txt"}}

  Initiate file upload:
  {"cmd": "UPLOAD", "args": {"src": "file.txt", "dst": "remote_file.txt", "size": 100}}

  Initiate file download:
  {"cmd": "DOWNLOAD", "args": {"src": "remote_file.txT", "dst": "file.txt"}}


  
  ```
- Example response:
  ```json
  {"status": "OK", "code": 0, "message": "", "uwd": ".", "data": {}}

  {"status": "FAIL", "code": 1200, "message": "", "uwd": ".", "data": {}}

  Reply to the LIST command, server sends back directory contents:
  {"status": "OK", "code": 0, "message": "", "uwd": ".", "data": {"files": [
    {"name": "filename.txt", "size": 123, "type": <file_type::regular>},
    {"name": "a_directory", "size": 0, "type": <file_type::directory>}
    ]
  }}

  "uwd" is the current user working directory, relative to his home directory
  the "type" carries constants (as numbers) from the enum std::filesystem::file_type, only file_type::regular and file_type::directory are used
  "size" is zero when the entry is a directory

  
  ```

