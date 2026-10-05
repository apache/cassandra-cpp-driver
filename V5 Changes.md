Current state of trunk 
message bodies are done, framing is not

The driver has had `CPP-443 "Support for protocol v5"` since **2.7.0** (2019). That work
predates v5 going GA in Cassandra 4.0-beta5, and it covered only *message body* changes.
The v5 **framing format was never implemented**.

Consequence: the driver's "v5 beta" can only talk to pre-GA Cassandra 4.0 alphas, which
accepted bare 9-byte envelopes. Against any GA Cassandra 4.0+ node the connection breaks
immediately after `STARTUP`. `USE_BETA` (`0x10`) is also set on v5 frames, which GA servers
reject for a non-beta version.



Changes in Proto-V5

 1  CRC primitives                 
 2  Framing layer                  
 3  Negotiation + version bounds   
 4  Duration decode, now_in_seconds 
 5  Deduplicate v5 conditionals     
 6  Public API + docs
 7  Tests
