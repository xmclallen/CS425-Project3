Xavier McLallen         xaviermclallen@email.arizona.edu

Step 2 has been completed, and sits inside of the step_2 subdirectory.

Step 3 has completed:
    + Heartbeat
    + Timeouts
    + Reconnections (tries every 10 seconds until succes)
    + ACKs

    Did not complete the queue. For some reason, when we started to add that
     part in, occasionally the packets started to become malformed. This then caused
     either the server or client to read incorrect data and crash.


