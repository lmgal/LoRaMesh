# LoRaMesh API

## Include Library

```arduino
#include <LoRaMesh.h>
```

## Setup

### Begin

Initialize the mesh network with the specified frequency and node address.

```arduino
LoRaMesh mesh;
mesh.begin(frequency, address);
```
 * `frequency` - frequency in Hz (`433E6`, `868E6`, `915E6`)
 * `address` - unique address for this node (0-254, 255 is reserved for broadcast)

Returns `true` on success, `false` on failure.

### Set address

Set or change the node's address after initialization.

```arduino
mesh.setAddress(address);
```
 * `address` - unique address for this node (0-254)

### Get address

Get the current node's address.

```arduino
uint8_t address = mesh.getAddress();
```

Returns the node's address.

### Set pins

Override the default `NSS`, `NRESET`, and `DIO0` pins used by the underlying LoRa library. **Must** be called before `begin()`.

```arduino
mesh.setPins(ss, reset, dio0);
```
 * `ss` - new slave select pin to use, defaults to `10`
 * `reset` - new reset pin to use, defaults to `9`
 * `dio0` - new DIO0 pin to use, defaults to `2`

### Set SPI interface

Override the default SPI interface used by the library. **Must** be called before `begin()`.

```arduino
mesh.setSPI(spi);
```
 * `spi` - new SPI interface to use, defaults to `SPI`

### Set SPI Frequency

Override the default SPI frequency used by the library. **Must** be called before `begin()`.

```arduino
mesh.setSPIFrequency(frequency);
```
 * `frequency` - new SPI frequency to use

## Sending data

### Send to and wait

Send data to a specific node and wait for routing.

```arduino
mesh.sendToWait(destination, data, length);
mesh.sendToWait(destination, data, length, flags);
```
 * `destination` - address of the destination node
 * `data` - data buffer to send
 * `length` - size of data to send (max 251 bytes)
 * `flags` - (optional) additional flags

Returns `true` if the message was sent successfully, `false` on failure.

## Receiving data

### Receive from acknowledge

Check for and receive available messages.

```arduino
uint8_t buffer[251];
uint8_t length = sizeof(buffer);
uint8_t source, dest, id, flags;

if (mesh.recvFromAck(buffer, &length, &source, &dest, &id, &flags)) {
    // Message received
}
```
 * `buffer` - buffer to store received data
 * `length` - pointer to the buffer size, updated with actual message length
 * `source` - (optional) pointer to store the source address
 * `dest` - (optional) pointer to store the destination address
 * `id` - (optional) pointer to store the message ID
 * `flags` - (optional) pointer to store message flags

Returns `true` if a message was received, `false` if no message available.

### Available

Check if a message is available for reading.

```arduino
if (mesh.available()) {
    // Message available
}
```

Returns `true` if a message is available, `false` otherwise.

### Process

Process incoming packets and handle routing. Should be called regularly in the main loop.

```arduino
mesh.process();
```

## Routing

### Get routing table

Get a pointer to the routing table.

```arduino
RoutingEntry* table = mesh.getRoutingTable();
```

Returns pointer to the routing table array.

### Get routing table size

Get the number of entries in the routing table.

```arduino
uint8_t size = mesh.getRoutingTableSize();
```

Returns the maximum number of routing entries (10 by default).

### Print routing table

Print the current routing table to Serial (for debugging).

```arduino
mesh.printRoutingTable();
```

## Configuration

### Set retries

Set the number of retry attempts for sending messages.

```arduino
mesh.setRetries(retries);
```
 * `retries` - number of retry attempts (default is 3)

### Set retry timeout

Set the timeout between retry attempts.

```arduino
mesh.setRetryTimeout(timeout);
```
 * `timeout` - timeout in milliseconds (default is 200)

## Constants

### Maximum message length
```arduino
LORAMESH_MAX_MESSAGE_LEN  // 251 bytes
```

### Broadcast address
```arduino
LORAMESH_BROADCAST_ADDRESS  // 0xFF
```

### Routing constants
```arduino
LORAMESH_ROUTING_TABLE_SIZE       // 10 entries
LORAMESH_MAX_HOPS                 // 10 hops
LORAMESH_ROUTE_TIMEOUT            // 30000 ms
LORAMESH_ROUTE_DISCOVERY_TIMEOUT  // 5000 ms
```

## Message types

The library internally uses these message types for routing:

```arduino
MESSAGE_TYPE_DATA           // 0x00 - Normal data message
MESSAGE_TYPE_ROUTE_REQUEST  // 0x01 - Route discovery request
MESSAGE_TYPE_ROUTE_REPLY    // 0x02 - Route discovery reply
MESSAGE_TYPE_ROUTE_FAILURE  // 0x03 - Route failure notification
```

## Route states

Routes in the routing table can have these states:

```arduino
ROUTE_STATE_INVALID     // 0x00 - No valid route
ROUTE_STATE_DISCOVERING // 0x01 - Route discovery in progress
ROUTE_STATE_VALID       // 0x02 - Valid route available
```

## Routing entry structure

Each entry in the routing table contains:

```arduino
struct RoutingEntry {
    uint8_t destination;    // Destination node address
    uint8_t nextHop;       // Next hop to reach destination
    uint8_t hopCount;      // Number of hops to destination
    RouteState state;      // Current state of the route
    unsigned long lastSeen; // Timestamp of last update
};
```