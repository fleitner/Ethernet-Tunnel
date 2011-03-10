
People are using more and more Linux ethernet bridges everyday
and to keep the host in the network, they are assigning IP addresses
to the bridge's device. In theory, that address should be only for
bridge's maintenance, and not for host's traffic.

Therefore, ethertund comes to solve this by creating a ethernet
tunnel between two virtual interfaces - one to be inserted in
the bridge and another to act as the host's ethernet.

This allows the host to use the virtual ethernet as any other
ethernet device and also let the bridge handles the other end
as a normal bridge's port.


See the scheme below:

  Host                         Unnamed
Interface                       bridge
 with                         +---------+
IP Address                    |   br0   |      Real
                              +---------+    Ethernet
+-------+         +--------+    | | | |    +------+
| host0 |         | btun0  |<---+ | | +--->| eth0 |-> Real
+--------         +--------+      | |      +------+   World
   |                  |           | |
   +-> [ethertund] <--+           | |
                                  | |
+-------+          +-------+      | |
|VM eth0|          | vnet0 |<-----+ |
+-------+          +-------+        |
   |                    |           |
   +-> virtualization <-+           |
                                    |
+-------+          +-------+        |
|VM eth0|          | vnet1 |<-------+
+-------+          +-------+
   |                    |
   +-> virtualization <-+

ethertund: it is a daemon that forward packets between
           the two TAP devices - host0 and btun0

host0: the host's virtual ethernet

btun0: the ethernet to be included in the bridge.

Use:

1 Run the application:
# ./ethertund

2. setup btun0 as bridge's br0 port
# ifconfig btun0 up
# brctl addif br0 btun0

3. setup host0
# ifconfig host0
# dhclient host0

Flavio Leitner
