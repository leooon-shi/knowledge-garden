LB
P- aocs/system/network.md --›
CNI stands for **Container Network Interface**, It is a specification and a set of libraries for configuring network interfaces in Linux containers. CNI is primarily used in the context of container orchestration platforms 11ke Kubernetes Here's a closer look at what CNI is and how it functions:
1.    **Purpose**: CNI provides a standardized way to configure network interfaces for containerized applications. It allows containers to connect to different network environments, enabling them to communicate with each other and with external systems.
2.    **Architecture**: CNI consists of two main components:
-  **CNI Plugins**: These are executable files that implement network connectivity for containers. They handle tasks like adding and removing network interfaces, IP addresses, routes, etc.
-   **CNI Configuration**: This is a SON configuration file that defines how networks should be set up for containers.
It includes parameters such as network type, IP address allocation, and DNS settings.
*Usage in Kubernetes**: In Kubernetes, CNIis used to manage the network configuration of pods. Kubernetes delegates the responsibility of setting up networking to CNI plugins, allowing for a flexible and extensible networking architecture. This means you can choose from various CNI plugins based on your networking requirements.
4. **Popular CNI Plugins**: There are several popular CNI plugins that provide different networking functionalities for Kubernetes, such as:
-  **Calico**: Provides advanced networking and network policy features.
-   **Flannel**: A simple and easy-to-use overlay network.
-   **Weave Net**: Offers network isolation and encryption features.
-  **Cilium**: Provides networking and security with a focus on using eBPF.
5. "Modularity and Extensibility**: One of the key benefits of CNI is Its modularity. You can easily swap out one CNT plugin for another without disrupting the existing system, allowing for customization based on specific needs.
CNI has become a crucial part of the container ecosystem, especially in Kubernetes, due to its flexibility and the wide range of plugins available to meet various networking requirements.
A VLAN, or **Virtual Local Area Network**, is a technology used in networking to partition and manage a physical network into multiple, distinct broadcast domains. This allows network administrators to segment networks logically without requiring changes to the physical network infrastructure. Here's a more detailed look at VLANs:
### Key Concepts of VLAN:
1.    **Segmentation**: VLANs enable the creation of separate network segments within the same physical network infrastructure. This segmentation can improve network efficiency and security by confining broadcast traffic to a specific VLAN.
2.    **Isolation**: By isolating network traffic, VLANs can enhance security. Devices on different VLANs cannot directly communicate unless explicitly allowed through a router or Layer 3 switch, which can be configured to apply security policies.
3.    **Flexibility and Scalability**: VLANs provide flexibility in network design. Devices can be grouped logically based on function, department, or project, regardless of their physical location. This makes it easier to manage changes to the network, such as moving devices or adding new ones.
4.    **Traffic Management**: VLANs can help manage and reduce network traffic. By Limiting broadcast domains, they prevent broadcasts from being sent to all devices, which can be particularly beneficial in large networks.
### How VLANs Work:
-  **Tagging**; VLANs typically use tagging to identify packets belonging to specific VLANs. The most common protocol for VLAN tagging is IEEE 802.1Q, which inserts a tag into the Ethernet frame header to identify the VLAN.
-  **Switch Ports**; Switch ports can be configured as either access ports or trunk ports:
-  **Access Ports**; Connect to end devices and carry traffic for a single VLAN. These ports are typically used for connecting devices like computers and printers.
-  **Trunk Ports**; Carry traffic for multiple VLANs simultaneously. These ports are used to connect switches to each other or to routers, allowing VLAN information to travel across network segments.
-   **Inter-VLAN Routing**; To enable communication between different VLANs, a router or Layer 3 switch is required to perform inter-VLAN routing, This device can apply access control 11sts (ACLs) to enforce security policies and manage traffic between VLANS,
### Use Cases for VLANs:
-   **Departmental Networks**; VLANs can separate network traffic for different departments within an organization, such as separating HR, Finance, and IT departments,
-   **Enhanced Security*; Sensitive data can be isolated on a dedicated VLAN, with strict access controls to protect against unauthorized access.
-  **Guest Networks**; VLANs can segregate guest network traffic from internal traffic, providing internet access to visitors without compromising internal network security.
-   **Voice and Data Separation**: VLANs can be used to separate voice traffic (11ke VoIP) from data traffic to ensure quality of service (Qos) and reduce latency for voice communications.
VLANS are a versatile and essential tool in modern network design, providing both organizational and technical benefits by allowing for efficient and secure management of network resources.
In networking, a "tunnel" refers to a secure pathway or connection that encapsulates one network protocol inside another network protocol. Tunneling is commonly used to transport data across networks that may not support the original protocol or to enhance security through encryption. Here are some key aspects of tunneling:
### Key Concepts of Tunneling:
1. **Encapsulation**: Tunneling involves encapsulating packets from one protocol inside packets of another protocol. This

allows the original packets to be transmitted across a network that might not natively support the original protocol.
1.    **Protocol Support**: Tunnels can be used to transport data using different protocols, such as IPv6 over an IPV4 network, or private network traffic over the public internet.
2.    *Security**: Tunneling can provide security by encrypting the encapsulated data, ensuring that it remains private an
protected from interception. This is commonly used in Virtual Private Networks (VPNs).
4. **Transparency**: The encapsulation process is transparent to the end users and applications. The date appears to nove seamlessly between the source and destination without needing to interact with the underlying transport network.
#*# Common Tunneling Protocols:
1.    **IPsec (Internet Protocol Security**: Often used for creating secure PN tunnels, IPsec encrypts and authenticates I packets to ensure the confidentiality, integrity, and authenticity of the data.
2.    **GRE (Generic Routing Encapsulation)**: A simple tunneling protocol that encapsulates a wide variety of network layer protocols inside virtual point-to-point links.
3.    **LTP (Layer 2 Tunneling Protocol)**: Often used in conjunction with IPsec to establish VPNS, LTP provides a tunneling mechanism for PPP (Point-to-Point Protocol) frames.
4. **SSH (Secure Shell) Tunneling**: SSH can create encrypted tunnels for securing network connections, often used for securely forwarding ports and transferring files.
5. *#SSL/TLS (Secure Sockets Layer/Transport Layer Security**: These protocols can be used to create secure tunnels for web traffic (HTTPS) and other applications.
## Use Cases for Tunneling:
- **VPNS
(Virtual Private Networks)**: Tunneling is a fundamental component of VPNs, allowing users to connect securely to
a private network over the public internet. VPNs use tunneling protocols to encrypt data and maintain privacy.
-  **Bypassing Network Restrictions**: Tunnels can be used to bypass firewalls and access restricted content by encapsulating traffic in protocols that are allowed by the network.
-   **IPv6 Transition**: Tunneling can facilitate the transition from IPv4 to IPv6 by allowing IPv6 packets to be transmitted over existing IPv4 infrastructure.
-   **Secure Remote Access**: Tunnels enable secure remote access to network resources, allowing employees to connect to corporate networks securely from remote locations.
Overall, tunneling is a versatile and widely used technique in networking for connecting disparate networks, enhancing security, and providing flexible solutions to various networking challenges.
GRE-in-UDP is a tunneling protocol that encapsulates GRE (Generic Routing Encapsulation) packets within UDP (User Datagram Protocol) packets. This method combines the benefits of GRE with the flexibility and features of UDP, offering several advantages for specific networking scenarios. Here's a closer look at GRE-in-UP tunneling:
### GRE (Generic Routing Encapsulation)
GRE is a protocol developed by Cisco that encapsulates a wide variety of network layer protocols within virtual point-to-point Links. It is often used for creating simple, stateless tunnels that can carry multiple types of traffic. including IPv4, IPv6, and even non-IP protocols.
### UDP (User Datagram Protocol)
UDP is a transport layer protocol known for its simplicity and low overhead. Unlike TCP, UDP does not provide error correction or guaranteed delivery, making it suitable for applications where speed is critical and packet loss is acceptable,
### GRE-in-UDP Tunneling
GRE-in-UDP encapsulates GRE packets inside UP packets, which can be advantageous in specific situations:
1.    **NAT Traversal**; One of the primary benefits of encapsulating GRE in UP is Improved NAT (Network Address Translation) traversal, Many NAT devices handle UP traffic more effectively than GRE traffic, allowing GRE-in-UP tunnels to pass through NAT devices more reliably.
2.    **Load Balancing**; UP encapsulation allows GRE traffic to benefit from Equal-Cost Multi-Path (ECMP) routing in networks, ECMP can distribute traffic across multiple paths, enhancing Load balancing and potentially increasing throughput.
*  *Firewall Compatibility"*; Some firewalls may block GRE traffic by default, but allow UP traffic. Encapsulating GRE in UDP can help in traversing such firewall configurations without additional changes.
*  *Port Numbering**; By using UP, each tunnel can be assigned a specific source and destination port. This can help in identifying and managing tunnels more effectively, especially in complex network environments.
### Use Cases
-  **VPNs and Secure Tunnels**; GRE-in-UDP can be used in VPN solutions to combine the simplicity and flexibility of GRE with the NAT traversal benefits of UDP.
-  **Cloud Networking**: In cloud environments, GRE-in-UP can facilitate connectivity between different virtual networks or data centers, especially when NAT devices are involved.
-  **SD-WAN**: Software-Defined Wide Area Networks can use GRE-in-UP to optimize routing and ensure robust connectivity across diverse network paths.
## Configuration and Considerations

-  **Configuration**: Setting up a GRE-in-UDP tunnel typically involves configuring routers or network devices to encapsulate GRE packets in UP and specifying the appropriate UP ports.
-   **Considerations**: While GRE-in-UDP offers many benefits, it is important to consider potential issues such as increased overhead from double encapsulation and the lack of inherent security features. For secure communications, GRE-in-UDP may need to be combined with additional security measures such as IPsec.
Overall, GRE-in-UDP is a useful tunneling technique that leverages the strengths of both GRE and UP to address specifie networking challenges, particularly in environments requiring NAT traversal and enhanced path management.
Certainlyl In a network, a VLAN (Virtual Local Area Network) is used to segment a physical network into separate, logical networks. This segmentation isolates broadcast domains, which means that devices on different VLANs cannot communicate
VLANS works:
directly with each other at the data link layer (Layer 2 of the OST model). Here's how communication between different
### Isolation by Default
1. **Layer 2 Segmentation**: VLANs operate at Layer 2 of the OST model, meaning they separate traffic based on MAC do not reach devices in another VLAN.
addresses within the same physical switch. Each VLAN functions as a distinct broadcast domain, so broadcasts in one VLAN
2. **Default Isolation**: By default, devices on different VLANs are isolated from each other. This means that a device on VLAN 10 cannot communicate with a device on VLAN 20 directly, as their traffic is contained within their respective VLANS.
#** Enabling Inter-VLAN Communication
To allow communication between devices on different VLANs, you need a device that operates at Layer 3 (the Network layer of the OSI model), such as a router or a Layer 3 switch. Here's how it works:
1. **Router or Layer 3 Switch**: These devices are capable of routing traffic between different VLANs. They can examine
the Layer 3 information (such as IP addresses) and determine how to forward packets between VLANS.
2. **VLAN Interfaces**: On a router or Layer 3 switch, you can create sub-interfaces or virtual interfaces for each VLAN.
Each interface is assigned an IP address that acts as the default gateway for devices within that VLAN.
3. **Routing between VLANs**: When a device on one VLAN wants to communicate with a device on another VLAN, it sends its traffic to its default gateway (the router or Layer 3 switch). The router then forwards the traffic to the appropriate VLAN based on its routing table.
### Applying Security Policies
1.    **Access Control Lists (ALs)**: On the router or Layer 3 switch, you can configure ALs to control the flow of traffic between VLANs. ACLallow you to specify which types of traffic are permitted or denied, based on criteria such as IP addresses, protocols, and port numbers.
2.    **Firewall Rules**: In addition to ALs, firewalls can be used to enforce more complex security policies and provide additional protection between VLANS.
3.    **Quality of Service (QoS)**: Network administrators can also apply Qos policies to prioritize certain types of traffic
between VLANs, ensuring that critical applications receive the necessary bandwidth and low latency.
By using routers or Layer 3 switches to manage inter-VLAN communication, network administrators gain fine-grained control over traffic flow and can implement security and performance policies to meet their organization's needs. This design ensures that VLANS remain isolated unless specific rules and configurations allow for interaction, enhancing both security and network efficiency.
To provide an example of a routing table for VLANs on the same physical device, let's assume you have a Layer 3 switch or a router configured to route traffic between different VLANS. Each VLAN is associated with a specific subnet, and the Layer 3 device has virtual interfaces (often called "SVIs" or "Switched Virtual Interfaces") for each VLAN.
### Example Setup
Suppose you have the following VLANs and corresponding subnets:
-   VLAN 10:192,168.10.0/24
-   VLAN 20: 192-168.20.0/24
-   VLAN 30: 192,168,30,0/24
Your Layer 3 switch or router will have virtual interfaces configured for each VLAN. Here is what the routing table might look like:
### Routing Table Example
| Destination Network | Subnet Mask
192.168.10.0
192.168.20.0
192.168.30.0
255.255.255.0
| 255.255.255.0
255.255.255.0
Next Hop / Interface | Metric l .....
Directly connected (VLAN 28 Interface)!
Directly connected (VLAN 20 Interface)
Directly connected (VLAN 30 Interface) i e
### Configuration Explanation
-  **Directly Connected Networks**: Each entry in the routing table indicates that the network is directly connected through one of the VLAN interfaces. This means that the Layer 3 device can send packets directly to devices in these subnets because it has interfaces configured within those VLANS.
-  **VLAN Interfaces**:Each VLAN interface acts as the default gateway for devices within that VLAN. For example, devices in VLAN 10 would use the IP address of the VLAN 10 interface (e.g., 192.168.10.1) as their default gateway.
-   **Routing Between VLANs**: When a device in one VLAN needs to communicate with a device in another VLAN, it sends the packet to its default gateway (the Layer 3 device). The Layer 3 device uses its routing table to determine the appropriate VLAN interface to forward the packet to the destination subnet.

-  **Access Control Lists (ALs)**: You may want to configure ALs on the Layer 3 device to control or restrict traffic between VLANs based on your security policies.
-   **Inter-VLAN Routing**: This example assumes basic inter-VLAN routing is enabled on the Layer 3 device. The device must be configured to handle routing between its VLAN interfaces.
-   **Redundancy and Load Balancing**: Depending on your network design, you might have additional routes for redundancy or load balancing, especially in larger or more complex networks.
This example demonstrates a simple scenario where a Layer 3 device routes traffic between VLANs on the same physical hardware. In practice, the configuration would depend on the specific networking hardware and software in use.
To enable communication between different VLANs on a Layer 3 device (such as a router or a Layer 3 switch), you need to configure inter-VLAN routing. This involves creating virtual interfaces for each VLAN and setting up routing between them.
Here's a step-by-step guide on how to achieve this:
at Step-by-Step Configuration
1. **Configure VLANs on the Switch**:
- First, ensure that the VLANs are properly set up on the Layer 2 switch. Each port that connects to an end device should be configured as an access port associated with the appropriate VLAN.
2. **Set Up Virtual Interfaces (SVIS)**:
-   On the Layer 3 device, create a Switched Virtual Interface (SVI) for each VLAN. An SVI is a logical interface that acts as the default gateway for devices in that VLAN.
-   Assign an IP address to each SVI. This IP address will be used by the devices in the VLAN as their default gateway.
Example configuration for a Cisco switch:
**plaintext interface Vlante
ip address 192.168.10.1 255.255.255.0
interface Vlan20
ip address 192.168.20.1 255.255.255.0
interface Vlan30
ip address 192.168.30.1 255.255.255.0
3.
**Enable Routing on the Layer 3 Device**:
- Ensure that IP routing is enabled on the Layer 3 device. This allows the device to route traffic between the VLANs.
Example for a Cisco device:
•••plaintext ip routing
4. **Configure Routing**:
-Since the Layer 3 device already knows how to reach each VLAN through its directly connected interfaces, you don°t need additional static routes for basic inter-VLAN routing.
5. **Test Connectivity:
- Test the configuration by pinging from a device in one VLAN to a device in another VLAN. Ensure that the devices have their default gateway set to the IP address of their respective VLAN's SVI.
6. **Configure Access Control Lists (Optional) **:
-If you want to control or restrict the traffic between VLANs, configure Access Control Lists (ACLS) on the Layer 3 device. ALs can specify which types of traffic are allowed or denied between VLANS.
Example of a simple ACL to allow all traffic from VLAN 10 to VLAN 28:
•* plaintext
access-list 100 permit ip 192,168.10.0 0.0.0.255 192.168.20.0 0.0.0.255
Apply the ACL to the relevant interfaces or globally, depending on your requirements.
### Considerations
-  **Security**; Be mindful of security when enabling inter-VLAN routing, Without proper controls, devices on different VLANS could communicate freely, which might not be desirable in all scenarios.
-  **Performance**; Ensure that the Layer 3 device has sufficient resources to handle the routing load, espectally in large networks.
-   **Troubleshooting**; If devices cannot communicate between VLANS, check the configuration of SVIs, ensure IP routing is enabled, and verlfy that there are no ACLs inadvertently blocking traffic.
By following these steps, you can enable devices on different VLANs to communicate via the Layer 3 device, allowing for flexible and controlled network segmentation.
To make virtual interfaces reachable among different VLANS, you need to use a Layer 3 device 11ke a router or a Layer 3 switch. This device w111 handle the routing of packets between VLANs based on IP addresses. Here's how it works and how packets are routed:
### Enabling Inter-VLAN Routing
I
1.    **Configure VLANS**: Ensure that VLANs are properly configured on your Layer 2 switches. Each VLAN wi11 have its own subnet.
2.    **Create Virtual Interfaces (SVIs)**:
- On the Layer 3 switch or router, create a Switched Virtual Interface (SVI) for each VLAN. Assign an IP address to
each SVI, which will act as the default gateway for devices in that VLAN.

Example for a Cisco Switch:
***plaintext interface Vlan1e
ip address 192.168.10.1255.255.255.0
interface Vlan2e
...
ip address 192.168.20.1255.255.255.0
3
**Enable IP Routing**:
- Enable IP routing on the Layer 3 device to allow it to route packets between different VLANs.
Example command for a Cisco device:
***plaintext
Ip routing
### Routing Packets with IP Addresses
When a device in one VLAN wants to communicate with a device in another VLAN, the process is as follows:
1*"Default Gateway**: The device sends the packet to its default gateway, which is the SVI on the Layer 3 device for its
1.    **Routing Decision**: The Layer 3 device receives the packet, examines the destination IP address, and consults its routing table to determine the appropriate outgoing interface for the destination VLAN.
2.    **Forwarding the Packet**:
- The Layer 3 device forwards the packet to the SVI of the destination VLAN. At this point, the packet is still encapsulated with the original sender's MAC address and the destination MAC address of the SVI.
4. **ARP Resolution**:
-  The Layer 3 switch or router uses the Address Resolution Protocol (ARP) to find the MAC address associated with the destination IP address if it is not already known. This is done by sending an ARP request within the destination VLAN.
-  The device with the matching IP address responds with its MAC address.
*Final Delivery**: The packet is then forwarded to the correct destination device within the VLAN using the resolved
MAC address.
### MAC Address and IP Address Interaction
-  **MAC Addresses**: While routing decisions are made based on IP addresses, the actual delivery of packets within a VLAN uses MAC addresses. Each time a packet enters a new network segment (such as a VLAN), it is encapsulated with the appropriate MAC headers.
-  **Layer 3 to Layer 2 Transition** The Layer 3 device handles the transition between Layer 3 (IP) and Layer 2 (MAC) by resolving IP addresses to MAC addresses within each VLAN.
### Security and Efficiency
-**Access Control Lists (ACLs)**: You can configure ALs on the Layer 3 device to control or restrict traffic between
VLANS, providing additional security.
-**Broadcast Domains**: VLANS 1imit broadcast domains, reducing unnecessary traffic and increasing network efficiency-By following these steps and understanding the interaction between IP and MAC addresses, you can successfully enable communication between devices on different VLANS In your network. The process ensures that packets are routed correctly based on IP addresses while relying on MAC addresses for final delivery within each VLAN.