# Network Routing Simulation Assignment

## Overview

This project implements two classical routing protocols on a synthetic network derived from traceroute data:

1. **Link-State Routing (LSR)**  
   - Simulates flooding of Link-State Advertisements (LSAs) until every router learns the full topology.  
   - Counts total LSA messages sent and synchronous rounds to converge.  
   - Runs Dijkstra’s algorithm at each router to build forwarding tables and compute end-to-end paths.

2. **Distance-Vector Routing (DVR)**  
   - Simulates synchronous Bellman-Ford exchanges of full distance vectors between neighbors.  
   - Counts total distance-vector messages and rounds until no table entry changes.  
   - Builds per-router forwarding tables and path reconstruction based on next hops.


## Input Data

- **`traceroute_ip_cache.txt`**  
  - Must reside in the **same folder** as the Python scripts.  
  - Each line has the form:  
    ```
    <IP_ADDRESS>|(<City>, <CountryCode>, <ASN org>, loc: <lat>,<lon>)
    ```
  - The scripts parse the `loc: <lat>,<lon>` field to extract router coordinates.

## Usage

1. **Install Python 3** (if not already installed).

2. **Place** `traceroute_ip_cache.txt` in the project directory.

3. **Run Link-State simulation**:  
   ```bash
   python code_LSA.py
Outputs:

Total LSAs sent: ...

Rounds until convergence: ...

LSDB entries per router: ...

Sample forwarding table and route prints.

4. **Run Distance-Vector simulation**:

    ```bash
    python code_DV.py
Outputs:

Total DV messages sent: ...

Rounds until convergence: ...

Sample forwarding table and route prints.

## Configuration
1. **Edge-drop fraction (T)**

In both scripts, edit the variable T (default 0.3) to adjust network sparsity:

Reasonable range: 0.2 ≤ T ≤ 0.5.

2. **Final local node**

After parsing the traceroute file, an extra “local” router is appended with ID = n+1 and coordinates (12.99151, 80.23362) (IIT Madras).

## Assumptions & Notes
Node IDs are assumed to be sequential integers starting at 1 in the order they appear in traceroute_ip_cache.txt.

1. **Graph connectivity**

If the random pruning disconnects the graph, the Link-State code will detect “no progress” and exit with a warning; some LSDBs may remain incomplete.

The Distance-Vector code converges regardless; unreachable destinations remain at infinite cost.

2. **Modeling details**

Link costs are straight-line (“great-circle”) distances via the Haversine formula.

Both protocols assume synchronous rounds (all routers act in lock-step).

3. **Output inspection**

Check the printed forwarding tables and routes to verify correctness.

Message counts give a comparative sense of protocol overhead.

