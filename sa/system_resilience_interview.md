- nite/view.md
<1-- docs/opp/SA/system_resilience_interview.md -->
In this context, you mean than traffic Qos.
**service availability and resilience requirements**, rather
For PVTB, clarify the following:
-   **Availability target:** e.g., 99.9%, 99.99%, or another target over a month or year.
-   **Service downtime:** Is any complete outage acceptable? what is the maximum planned and unplanned downtime?
*  *RTO (Recovery Time Objective):** Maximum acceptable time to restore forwarding after a failure.
*  *RPO (Recovery Point objective):** How much tunnel or configuration state may be lost after a restart or failover.
- **Failure detection time:** How quickly should PVTB detect a failed consumer, tunnel, pod, node, or network path?
*  *Failover time:** How quickly should traffic be redirected to another consumer or PVTB instance?
*  *Degraded-mode behavior:** During partial failure, should PVTB:
-   continue forwarding to healthy consumers,
-   stop replicated forwarding,
-   drop packets,
-   buffer packets,
-   or reject new traffic?
**Packet-loss tolerance during recovery:**
Are packets allowed to be lost during tunnel
restart, consumer failover, or PVTB restart?
-**State recovery:** After restart, must VTB reconstruct all tunnels and consumer memberships automatically?
I
- **Configuration availability:** Can the data plane continue forwarding using its last valid configuration if the control plane is unavailable?
*  *Maintenance behavior:** Is zero-downtime rolling upgrade required? Should traffic be drained before pod termination?
*  *Recovery scope:** Must the system survive:
-   a process failure,
-   a pod failure,
-   a node failure,
-   a Kubernetes cluster failure,
-   or a complete consumer-group failure?
-  **MTTR (Mean Time To Repair):** Expected average time for operators or automation to restore normal operation.
-  **Data-plane versus control-plane availability:** For example, control-plane APIs may be temporarily unavailable while existing packet forwarding continues.
A requirement could be stated like this:

*   PVTB shall continue forwarding traffic when a single consumer instance or PVTB pod
*   fails. Failure shall be detected within 3 seconds, and traffic shall be redirected to a
*   healthy instance within 10 seconds. Existing configuration shall remain usable during
*   control-plane unavailability. Packet loss during failover is permitted, but the system
*   shall not require manual intervention.
The exact values must come from the business or operational requirements. Without them,
"high availability" is not sufficiently precise to guide the design or testing.