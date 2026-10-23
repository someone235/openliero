package main

import (
	"bytes"
	"encoding/binary"
	"testing"
)

func TestMultiplayerRoomRoutesIndependentGuests(t *testing.T) {
	_, address := startTestServer(t)
	host := dial(t, address)
	host.Write([]byte{MsgMultiCreate})
	created := readMsg(t, host)
	if created[0] != MsgMultiCreated {
		t.Fatalf("create: %x", created)
	}
	code := created[1:7]
	// Retrying create must return the same room.
	host.Write([]byte{MsgMultiCreate})
	if !bytes.Equal(readMsg(t, host)[1:7], code) {
		t.Fatal("create retry made another room")
	}
	for i := uint32(1); i <= 20; i++ {
		guest := dial(t, address)
		join := append([]byte{MsgMultiJoin}, code...)
		guest.Write(join)
		joined := readMsg(t, guest)
		if joined[0] != MsgMultiJoined || binary.BigEndian.Uint32(joined[7:11]) != i {
			t.Fatalf("join %d: %x", i, joined)
		}
		if !bytes.Equal(readMsg(t, host), joined) {
			t.Fatal("host got different join notification")
		}
		guest.Write(join)
		if !bytes.Equal(readMsg(t, guest), joined) {
			t.Fatal("join retry assigned another slot")
		}
		readMsg(t, host)
		route := append([]byte{MsgMultiRoute}, code...)
		route = binary.BigEndian.AppendUint32(route, i)
		route = append(route, []byte("credentials")...)
		guest.Write(route)
		forwarded := readMsg(t, host)
		if forwarded[0] != MsgMultiRouted || !bytes.Equal(forwarded[1:], route[1:]) {
			t.Fatalf("guest route: %x", forwarded)
		}
		host.Write(route)
		forwarded = readMsg(t, guest)
		if forwarded[0] != MsgMultiRouted || !bytes.Equal(forwarded[1:], route[1:]) {
			t.Fatalf("host route: %x", forwarded)
		}
	}
	lock := append([]byte{MsgMultiLock}, code...)
	host.Write(append(lock, 1))
	// The host's subsequent create reply acts as a barrier for the lock packet.
	host.Write([]byte{MsgMultiCreate})
	readMsg(t, host)
	late := dial(t, address)
	late.Write(append([]byte{MsgMultiJoin}, code...))
	if response := readMsg(t, late); response[0] != MsgError || response[1] != ErrRoomFull {
		t.Fatalf("late join: %x", response)
	}
	host.Write(append(lock, 0))
	host.Write([]byte{MsgMultiCreate})
	readMsg(t, host)
	late.Write(append([]byte{MsgMultiJoin}, code...))
	if response := readMsg(t, late); response[0] != MsgMultiJoined {
		t.Fatalf("rematch join: %x", response)
	}
}
