# LoRaMesh

A multi-hop mesh networking library for LoRa radios built on top of the [arduino-LoRa](https://github.com/sandeepmistry/arduino-LoRa) library. This library provides mesh networking capabilities for platforms like Sony Spresense where RadioHead is not compatible.

## Features

- **Automatic Route Discovery**: Dynamically discovers routes to destination nodes
- **Multi-hop Communication**: Messages automatically forwarded through intermediate nodes
- **Self-healing Network**: Routes updated when nodes join/leave the network
- **Broadcast Support**: Send messages to all nodes in the network
- **Simple API**: Easy-to-use interface similar to arduino-LoRa
- **Spresense Compatible**: Designed for platforms not supported by RadioHead

## Installation

### Prerequisites

This library requires the [arduino-LoRa](https://github.com/sandeepmistry/arduino-LoRa) library to be installed first.

### Using the Arduino IDE Library Manager (Coming Soon)

This library is being prepared for submission to the Arduino Library Manager.

### Install using ZIP Download

1. First install the arduino-LoRa library:
   - Open Arduino IDE
   - Go to `Sketch` → `Include Library` → `Manage Libraries...`
   - Search for "LoRa" by Sandeep Mistry and install it

2. Install LoRaMesh library:
   - Download this repository as a ZIP file:
     - Click the green "Code" button on GitHub
     - Select "Download ZIP"
   - In Arduino IDE: `Sketch` → `Include Library` → `Add .ZIP Library...`
   - Select the downloaded `LoRaMesh-main.zip` or `LoRaMesh-master.zip` file
   - The library will be installed to your Arduino libraries folder

3. Verify installation:
   - Restart Arduino IDE
   - Go to `File` → `Examples` → `LoRaMesh`
   - You should see example sketches: BasicMeshNode, MeshGateway, MultiNodeDemo

### Manual Installation

1. Install arduino-LoRa library (see above)
2. Download and extract this repository
3. Copy the entire LoRaMesh folder to your Arduino libraries directory:
   - Windows: `Documents\Arduino\libraries\`
   - macOS: `~/Documents/Arduino/libraries/`
   - Linux: `~/Arduino/libraries/`
4. Restart Arduino IDE

## API Reference

### Core Methods

#### `begin(frequency, address)`
Initialize the mesh network with specified frequency and node address.
- `frequency`: LoRa frequency (e.g., 433E6, 868E6, 915E6)
- `address`: Unique node address (1-254, 255 is broadcast)
- Returns: `true` if successful

#### `sendToWait(destination, data, length)`
Send data to a destination node, automatically discovering route if needed.
- `destination`: Target node address (1-254, 255 for broadcast)
- `data`: Byte array to send
- `length`: Number of bytes to send
- Returns: `true` if message was successfully routed

#### `recvFromAck(buffer, length, source, dest, id)`
Receive a message if available.
- `buffer`: Buffer to store received data
- `length`: Pointer to buffer size (updated with actual received length)
- `source`: Pointer to store source address (optional)
- `dest`: Pointer to store destination address (optional)
- `id`: Pointer to store message ID (optional)
- Returns: `true` if message received

#### `process()`
Process mesh network tasks. Call regularly in loop().

#### `available()`
Check if a message is available to read.
- Returns: `true` if message available

### Configuration Methods

#### `setPins(ss, reset, dio0)`
Configure LoRa module pins (call before begin).

#### `setSPI(spi)`
Set custom SPI interface (call before begin).

#### `setSPIFrequency(frequency)`
Set SPI clock frequency (call before begin).

#### `setAddress(address)`
Change node address after initialization.

#### `setRetries(count)`
Set number of send retries (default: 3).

#### `setRetryTimeout(ms)`
Set retry timeout in milliseconds (default: 200).

### Diagnostic Methods

#### `printRoutingTable()`
Print current routing table to Serial.

#### `getRoutingTable()`
Get pointer to routing table array.

## How It Works

The mesh network uses a reactive routing protocol:

1. **Route Discovery**: When sending to an unknown destination, broadcasts route request
2. **Route Learning**: Nodes learn routes from passing traffic
3. **Forwarding**: Intermediate nodes forward messages toward destination
4. **Route Maintenance**: Routes timeout after 30 seconds of inactivity
5. **Failure Handling**: Route failure messages trigger new route discovery

## Message Format

Messages include a header with:
- Destination and source addresses
- Message type (DATA, ROUTE_REQUEST, ROUTE_REPLY, ROUTE_FAILURE)
- Hop count and visited nodes list (for loop prevention)
- Message ID for duplicate detection

## Constants

- `LORAMESH_MAX_MESSAGE_LEN`: Maximum message length (251 bytes)
- `LORAMESH_ROUTING_TABLE_SIZE`: Number of routes stored (10)
- `LORAMESH_MAX_HOPS`: Maximum hop count (10)
- `LORAMESH_BROADCAST_ADDRESS`: Broadcast address (0xFF)
- `LORAMESH_ROUTE_TIMEOUT`: Route expiry time (30 seconds)
- `LORAMESH_ROUTE_DISCOVERY_TIMEOUT`: Route discovery timeout (5 seconds)

## Limitations

- No message queueing - one message at a time
- Limited routing table size (10 entries)
- No encryption or authentication
- Basic routing algorithm may not find optimal paths
- Route discovery adds latency to first message

## License

This library is released under the MIT License.

## Contributing

Contributions are welcome! Please submit pull requests or open issues on GitHub.

## Acknowledgments

- Built on top of the excellent [arduino-LoRa](https://github.com/sandeepmistry/arduino-LoRa) library by Sandeep Mistry
- Inspired by RadioHead's [RHMesh documentation](https://www.airspayce.com/mikem/arduino/RadioHead/classRHMesh.html)