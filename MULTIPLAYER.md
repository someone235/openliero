# Multiplayer

Online play uses one game host and one game client per player. Everyone needs
this multiplayer build and must connect to the same updated signaling server.
Rooms have no fixed player limit; local and direct-connect LAN modes remain
two-player. Players join in the lobby, before a round starts.

## 1. Start the signaling server

From the repository root, build and run the server:

```bash
cd server
go build -o openliero-relay .
./openliero-relay
```

Keep it running while people use the room. The server can run on the game host's
machine or on a separate machine with a public IP address.

### Firewall and router ports

**Allow inbound UDP port 19533 on the signaling server.** Allow it in the
machine's firewall and any cloud firewall/security group. If the server is
behind a router, forward UDP 19533 to that machine's LAN IP address. TCP 19533
is not required.

For example, on a machine that already uses UFW:

```bash
sudo ufw allow 19533/udp
```

For Docker, publish the UDP port explicitly:

```bash
docker build -t openliero-signaling ./server
docker run --rm -p 19533:19533/udp openliero-signaling
```

Run those Docker commands from the repository root. The Go signaling server
only needs UDP 19533; the legacy 19600–19699 range mentioned in the Dockerfile
is not used by this server.

| Service | Port | When needed |
| --- | --- | --- |
| Signaling server | **UDP 19533 inbound** | Required for creating and joining online rooms. |
| Game connections | UDP ports selected by ICE | Clients need outbound UDP and return traffic; ICE attempts NAT traversal. |
| Default external STUN service | UDP 19302 outbound from clients | Used to discover public addresses; this is not a port to open on your signaling server. |
| Optional separate TURN service | UDP 3478 inbound plus its configured UDP relay range | Needed for relay connections when direct NAT traversal fails. |

The Go server exchanges connection information; it does not implement a TURN
relay. Opening UDP 19533 alone does not provide TURN. For restrictive networks,
a separate TURN service must run at the signaling server's hostname, with its
own firewall rules and credentials matching the Go server's `TURN_SECRET`.

To change the signaling port, run, for example:

```bash
./openliero-relay --port 20000
```

Then open/forward **UDP 20000** instead and set
`OPENLIERO_SIGNALING_PORT=20000` on every game client.

## 2. Start every player's game

From the repository root:

```bash
OPENLIERO_SIGNALING_SERVER=your-server-address ./LIERO/openliero
```

Replace `your-server-address` with the server's reachable IP address or hostname.
All players must use the same server address and port. A player on the server's
own machine can use `127.0.0.1`; other machines must use its LAN or public address.
The room code alone does not select a server.

For several test clients on one machine:

```bash
OPENLIERO_SIGNALING_SERVER=127.0.0.1 ./LIERO/openliero
```

Run that command in a separate terminal for each client. You can add
`SDL_AUDIODRIVER=dummy` to use silent audio.

The launcher prefers the locally installed build in `install/linux-x64/` and
keeps using the Original ruleset and portable settings under `LIERO/.openliero/`.
If needed, build and install the game first, from the repository root:

```bash
cmake --workflow --preset linux-x64
cmake --install build/linux-x64 --config Release
```

## 3. Create and join a room

1. Each player configures their name, color, and keyboard/gamepad bindings under
   **Network Player (F9)** in the main menu.
2. The host chooses the level, game mode, lives, and enabled weapons in the main
   menu, then selects **Host Online**.
3. The host shares the six-character room code with the other players.
4. Each guest selects **Join Online** and enters that code. The host's ruleset
   and map transfer automatically.
5. Each guest presses **Enter** in the lobby to become ready. Enter again toggles
   readiness off. The host appears ready automatically.
6. Once everyone has joined and is ready, the host presses **Enter** to start.
   At least two players are required.

Each player receives an **independently randomized set of five weapons on every
round**, including rematches. Draws use the host's enabled-weapon list. A player's
five slots contain distinct weapons when at least five are enabled. With fewer
than five enabled, repeats are allowed after every available weapon has been
drawn. Players can have overlapping weapons by chance. Enable at least one weapon
before hosting. Saved profile loadouts are not used for online rounds.

## 4. Play, pause, and rematch

Use your **Network Player** bindings for movement, aiming, firing, changing
weapons, digging, and jumping. Press your bound Fire control when prompted to
spawn or respawn.

| Key | Action |
| --- | --- |
| **Esc** during a round | Toggle your pause request. Everyone who paused must resume before play continues. |
| **Q** while paused | Leave the room and return to the main menu. |
| **E** while paused, on the host | End the round for everyone. |
| Hold **Tab** | Show the player list, lives, and kills. |
| **PgUp / PgDn** | Page through long player lists or scores. |
| **Enter** on the host's results screen | Return everyone to the lobby for a rematch. |
| **Esc** in the lobby or results screen | Leave the room. |

For a rematch, each guest readies again and the host presses Enter. Each player
gets a fresh random loadout. If a guest leaves during a round, the remaining
players return to the lobby; the guest can rejoin using the same room code before
the next round starts. If the host leaves, the room closes.

## Troubleshooting

- **Cannot create/join a room:** check that the updated server is running, all
  players selected the same server and port, and inbound UDP 19533 (or the custom
  port) reaches the server. The older server protocol cannot host these rooms.
- **Room not found:** verify the server address and room code; create a new room
  if its host left.
- **Match already started:** wait for the host to return to the lobby, then join.
- **Connection fails after joining:** check client outbound UDP restrictions;
  some NAT/firewall combinations need the optional TURN service.
- **Game waits or stutters:** online play waits for every player's input. A slow
  or disconnected connection can hold up the whole room.
