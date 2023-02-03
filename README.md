# UNIX_dss_multicast_queue
This assignment was given and completed apart from the work done in the work place during my long-term internship in Turkish Aerospace 2021-2022. 

In this assignment a shared memory based multicasting queue is implemented. A server process creates the shared memory and constructs the queue data structure. Afterwards, it starts accepting UNIX domain stream connections. A client process connecting the socket gets the information about the shared memory of the multicast queue. Also clients send their messages to the server using the socket connection. Each message sent by a client is inserted at the multicast queue and readable by all connected processes (including the original sender). Server forks a new process per connection and this child process serves the commands from the client.

The socket connection maintains the session and lets clients push messages where the multicast queue communications is established over the shared memory.

When the system working there is one server instance, n client instances and n agents which are children of the server. Note that the server and agent code is the same. The agents primary responsibility is to provide the multicast semantics, update shared memory data structures so that n readers can read the same message.The client only reads from the UNIX domain socket at the beginning of the connection. For the remainder of the operation, it only reads from stdin and writes on socket or the stdout based on the user request.

When the client gets a SEND command from user, it writes the command to push the message to multi-cast queue. The other commands do not use the socket. The client also interacts with the shared queue in two basic modes: AUTO and NOAUTO. In the NOAUTO mode, the queue read is on demand basis, user enters a blocking FETCH command to read next message on the queue. Also there is a FETCHIF command that fetches a message if available, otherwise reports ‘No new message‘ on stdout. In the AUTO mode, client automatically fetches messages as soon as they arrive with help of the shared memory synchronization between the processes.

When client terminates, there might be messages that it did not fetch yet. Client cleans them up so no messages will be left garbage.

All data structures for the queue in this assignment is implemented by me as the requirement of the assignment. 

There is an important restriction of the implementation, the messages should not be duplicated in the shared memory. The message has single copy and the queue has n references to the message. A typical data structure that can be used is the circular buffer. It can be chosen to have n circular buffers or implement n connections in a single circular buffer. However multiple buffers should contain the references to messages, not the messages themselves. Otherwise single copy restriction is violated.

The data structures like queues are dynamic where the size of the shared memory is fixed when it is created. In order to solve this problem, the following assumptions are made:
 	• Number of maximum alive (not completely read) messages is bounded by 1024. 
  • Number of concurrent connections is bounded by 100. 
  • Maximum length of a message is 512 bytes.

Lastly, the code is free of deadlocks, busy waits and sleeping delays. The clients are unblocked as soon as a message is available.
