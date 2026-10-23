package main

import (
	"encoding/binary"
	"net"
	"time"
)

func (s *Server) handleMultiplayer(data []byte, from *net.UDPAddr) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if data[0] == MsgMultiCreate {
		// Creation and joining are idempotent: UDP acknowledgements can be lost.
		for code, room := range s.rooms {
			if room.Clients != nil && udpAddrEqual(room.Host.Addr, from) {
				s.sendRoomResponse(MsgMultiCreated, code, from)
				return
			}
		}
		if len(s.rooms) >= maxRooms {
			s.sendError(from, ErrRoomFull, "server full")
			return
		}
		code := s.generateRoomCode()
		if code == "" {
			return
		}
		room := &Room{Host: &Peer{Addr: from}, Clients: make(map[uint32]*Peer), NextID: 1, CreatedAt: time.Now(), LastSeen: time.Now()}
		copy(room.Code[:], code)
		s.rooms[code] = room
		s.sendRoomResponse(MsgMultiCreated, code, from)
		return
	}
	if len(data) < 1+RoomCodeLen {
		return
	}
	code := string(data[1 : 1+RoomCodeLen])
	room := s.rooms[code]
	if room == nil || room.Clients == nil {
		s.sendError(from, ErrRoomNotFound, "no multiplayer room (server upgrade required)")
		return
	}
	isHost := udpAddrEqual(room.Host.Addr, from)
	var senderID uint32
	for id, peer := range room.Clients {
		if udpAddrEqual(peer.Addr, from) {
			senderID = id
			break
		}
	}
	if data[0] == MsgMultiJoin {
		if isHost {
			s.sendError(from, ErrInvalidMsg, "already hosting")
			return
		}
		if senderID == 0 {
			if room.Locked {
				s.sendError(from, ErrRoomFull, "match already started")
				return
			}
			senderID = room.NextID
			if senderID == 0 {
				s.sendError(from, ErrRoomFull, "room full")
				return
			}
			room.NextID++
			room.Clients[senderID] = &Peer{Addr: from}
		}
		room.LastSeen = time.Now()
		msg := make([]byte, 11)
		msg[0] = MsgMultiJoined
		copy(msg[1:], code)
		binary.BigEndian.PutUint32(msg[7:], senderID)
		user, password := s.generateTurnCredentials()
		msg = append(msg, byte(len(user)))
		msg = append(msg, []byte(user)...)
		msg = append(msg, byte(len(password)))
		msg = append(msg, []byte(password)...)
		s.conn.WriteToUDP(msg, from)
		s.conn.WriteToUDP(msg, room.Host.Addr)
		return
	}
	if !isHost && senderID == 0 {
		return
	}
	room.LastSeen = time.Now()
	switch data[0] {
	case MsgMultiLock:
		if isHost && len(data) == 8 {
			room.Locked = data[7] != 0
		}
	case MsgMultiLeave:
		if isHost {
			delete(s.rooms, code)
		} else {
			delete(room.Clients, senderID)
		}
	case MsgMultiRoute:
		if len(data) < 12 {
			return
		}
		id := binary.BigEndian.Uint32(data[7:11])
		var to *net.UDPAddr
		if isHost {
			if peer := room.Clients[id]; peer != nil {
				to = peer.Addr
			}
		} else if id == senderID {
			to = room.Host.Addr
		}
		if to != nil {
			msg := append([]byte(nil), data...)
			msg[0] = MsgMultiRouted
			s.conn.WriteToUDP(msg, to)
		}
	}
}
