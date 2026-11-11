network.md
number_system.mc Um
packets_layout.md
<1-- docs/system/GTPv2-C_packets_layout.md -->
In tepdump, a packet filtering expression like udp[9] refers to examining the 10th byte (index 9, since indexing starts at
e) of the UDP header or payload. However, to understand what this specific byte represents, you need to know the structure of the UDP packet.
A UDP packet consists of:
Source Port (2 bytes)
Destination Port (2 bytes)
Length (2 bytes)
Checksum (2 bytes)
Data (variable length)
The first 8 bytes (indices e to 7) of a UDP packet are taken up by the UDP header flelds 11sted above. Therefore, udpt91 refers to the second byte of the UDP payload (or the 10th byte of the entire UDP packet).
In practice, filters 11ke this are often used to match specifie patterns or values at a particular position within the UDP payload. If you want to filter based on specifie date within the payload, you would use a filter expression that specifies a condition, such as udp[9] 0x81, which would capture packets where the 10th byte of the UDP packet is exe1,
In the context of GTPV2 (GPRS Tunneling Protocol version 2) over UDP, filtering with udpf9] refers to examining the second byte of the GTPV2 header. This is because GPv2 messages are encapsulated within UDP packets, and the GTPV2 header begins Immediately after the UDP header.
A typical GTPv2 header has the following structure:
Flags (1 byte)
Message Type (1 byte)
Length (2 bytes)
TEID (4 bytes, optional in GTPV2-C)
The UDP header Itself occupies the first 8 bytes of the packet. Therefore, when you refer to udp[9], you're looking at the second byte in the GTPv2 header, which is the "Message Type" field.
The "Message Type" field in GPv2 indicates the specific type of GPv2 message being transmitted, such as a Create Session Request, Modify Bearer Request, etc. Each message type is represented by a unique value.
If you want to filter packets by a specific GTPv2 message type, you can use an expression 11ke udp[9] -exxx, where exx is the message type you're interested in.
111
In the context of GTPV2 (GPRS Tunneling Protocol version 2) over UDP, filtering with udp[9] refers to examining the second byte of the GPv2 header. This is because GPv2 messages are encapsulated within UDP packets, and the GTPV2 header begins inmediately after the UDP header.
A typical GTPv2 header has the following structure:
Flags (1 byte)
Message Type (1 byte)
Length (2 bytes)
TEID (4 bytes, optional in GTPv2-C)
The UDP header itself occupies the first 8 bytes of the packet. Therefore, when you refer to udp[9], you're looking at the second byte in the GTPv2 header, which is the "Message Type" field.
The "Message Type"field in GPV2 indicates the specific type of GTv2 message being transmitted, such as a Create Session Request, Modify Bearer Request, etc. Each message type is represented by a unique value.
If you want to filter packets by a specific message message type, you can use an expression like exXX, where exxx is the message type you're interested in.
In GTPV2 (GPRS Tunneling Protocol version 2), the "Message Type" field in the GTPv2 header Identifies the specific type of signaling message being transmitted between network entities. GTPv2 is primarily used for control plane signaling in LTE networks, and it includes a variety of messages for managing sessions, bearers, and mobility.
Here are some common GTPv2 message types and their associated numeric values:
These message types are used to manage different aspects of the user sessions and bearers in mobile networks. When using tepdump or a similar tool, you can filter on the GPv2 message type by matching the approprlate byte in the UDP payload, as discussed earlier.
For example, if you want to capture only "Create Session Request" messages, you can use a filter like udp[9] - 32. This matches packets where the 18th byte of the UDP packet (2nd byte of the GTPv2 header) is 32, which corresponds to the Create Session Request message type.
	Create Session Request (32): Initiates the creation of a session.
	Create Session Response (33): Responds to a Create Session Request.
	Modify Bearer Request (34): Requests modifications to an existing bearer.
	Modify Bearer Response (35): Responds to a Modify Bearer Request.
	Delete Session Request (36); Requests the deletion of a session.
	Delete Session Response (37); Responds to a Delete Session Request.
	Create Bearer Request (95); Requests the creation of a new bearer.
	Create Bearer Response (96); Responds to a Create Bearer Request.
	Delete Bearer Request (99); Requests the deletion of a bearer.
	Delete Bearer Response (108); Responds to a Delete Bearer Request,
	Release Access Bearers Request (170); Requests the release of access bearers.
	Release Access Bearers Response (171); Responds to a Release Access Bearers Request.
	
	
